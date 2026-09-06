# CLAUDE.md — os9emu

A TTY OS-9 Level 1 emulator: a 6809 interpreter that runs real OS-9 program
modules and services their system calls against the host filesystem. No
graphics, no sound, no disk images — `/dd`, `/d0`, `/d1` and `/h0` are all one
host directory, and an OS-9 file is a host file.

The goal is to be as complete as Level 1 allows for text work: the standard
command set, Basic09, and the Microware C compiler, all built from the
NitrOS-9 sources next door in `../nitros9`.

## Layout

```
Makefile              build, test, survey, build the OS-9 root
coco-dev              docker wrapper for the NitrOS-9 toolchain
os9emu/OS9/           the emulator (and an Xcode project over the same sources)
scripts/os9root.sh    build ../nitros9 into ~/OS9 (--level 2 into ~/OS9L2)
scripts/survey.sh     run every installed command, report how each fared
scripts/mame-console.sh   drive a real NitrOS-9 in MAME for ground truth
tests/run.sh          golden-output tests
tests/cases/*.t,*.out the cases and what they should print (*.2.out at Level 2)
tests/mame/           the MAME side of the comparison harness
```

`make check` builds the OS-9 root and runs the tests. `make survey` is the
broad view: it runs all ~93 installed commands and buckets them. `make
os9root-l2`, `make test-l2` and `make survey-l2` do the same against a Level 2
root — see "Level 2" below.

## The pieces that had to be right

Everything below cost real time to find.

### Memory layout is not a detail

A process gets `[lowermem, uppermem)` for data, with the parameter area at the
top and the stack growing down into it. `uppermem` starts at the module's
declared storage (`M$Mem`) plus the parameters, and **F$Mem grows it upwards**.

Handing the program the whole address space up front looks generous and breaks
everything: the C runtime's `sbrk` allocates out of the region F$Mem *adds*,
so when the area cannot grow it hands the heap back the memory the stack is
already using. That is what "grab overlap" from `c.prep` means, and it is why
the C compiler could not preprocess a single `#include`.

`F$Fork`'s B register carries the data area size the caller wants, in pages —
this is how the shell's `prog #32k` arrives. Basic09 without it gets only its
declared 8K.

### I$Open must not truncate

Only `I$Create` truncates. Opening for write has to be `"rb+"`. Getting this
wrong empties every file a program opens to rewrite in place — `attr` on a
command emptied the command.

### Names are high-bit terminated

OS-9 ends a name with a byte that has bit 7 set, and that byte is the last
*character* of the name, not a delimiter. `F$PrsNam` has to count it, and
`SS_DevNm` has to produce it. Without that, `pwd` prints `/h` for `/h0`.

### A directory starts with `..`, then `.`

RBF's `MakDir` writes the parent's entry first and the directory's own second
(`ldd #$2EAE` in `../nitros9/level1/modules/rbf.asm`), and `pwd` and `pxd`
count on that order. They read the two entries, take them being equal to mean
"this is the root", and otherwise `chd ..` and hunt through the parent for the
entry whose LSN matches the *second* one — the directory they just came from.
With `.` first the hunt looks for the parent's own number, never finds it,
reads to the end and prints `read error`.

We had them the other way round, and the mistake hid itself: our `..` also
resolved to the directory itself, so the two LSNs matched, every directory
looked like the root and `pwd` stopped there and printed just `/h0`. That is
what "pwd and pxd print only the device name" used to mean.

`dir` seeks straight past both entries, so it never noticed either way.

### Dots are components, and a run of them counts

A component of nothing but dots goes up one level for every dot after the
first: `.` stays put, `..` is the parent, `...` the grandparent, `....` its
parent, for as long as the run goes on. It is how a shell walks a path back
up without a call for it — shellplus carries a forty-character string of dots
and points further back into it at each step (`L1732`, used by `CmdPWD` in
`../nitros9/level1/cmds/shellplus.asm`), and RBF counts the run and rewrites
it (`GtDvcNam` in `../nitros9/level1/modules/rbf.asm`). Reading three dots as
a name, which we used to, let one level of shellplus's prompt work and not
two, so its prompt stopped dead after a `chd` two directories down. A file
actually named `...` is unreachable, here as on a real system.

The rule is about a component that is *nothing but* dots, not about what one
starts with: `.profile`, `a.b` and `..hidden` are names.

`canonicalizePath` used to work by counting dots as it went, which got all of
it wrong — `/dd/T1/..` came back as `/dd/T1`, so `chd ..` stayed where it was
and `dir T1/..` listed `T1`; `/dd/./T1` kept its dot; and a `.` inside a name
after any earlier hidden name ate a whole directory component.

The run stops at the mount point, so no path walks out of the OS-9 disk and
into the rest of the host filesystem, and the root comes out as its own parent
— which is what OS-9 does and what `pwd` needs to know when to stop.

`I$ChgDir` resolves the dots before it stores the name. Keeping `/h0/T1/..`
as it stood left the working directory a component longer after every `chd`.

### Line endings

OS-9 ends a line with a bare CR. The terminal driver expands it to CR LF on
the way to a screen; a file gets the CR alone. `fdterm::writeln` therefore
looks at `isatty()`, and `--eol` overrides it. Adding the newline
unconditionally put a stray byte into everything the emulator redirected —
the compiler passes read it back as a syntax error on every line.

Text files copied out of the NitrOS-9 git checkout have newlines, and a header
copied across unchanged reaches the compiler as one enormous line.
`scripts/os9root.sh` converts them, the same way NitrOS-9's own makefiles do
with `os9 copy -l`.

### The screen is never wider than 80 columns

`dir`, `procs` and `mdir` ask the terminal for its width before they format,
and we answer from `TIOCGWINSZ` — but never with more than 80, whatever the
host window says. No OS-9 terminal was wider, and the utilities take that as
given. `dir` builds its line in a buffer and then writes a fixed 80 bytes of
it, so a wider answer makes it **drop every name past the eightieth column**,
silently, because those names are never written at all. Its own check for the
buffer filling up cannot save it: `cmpx #$0090` compares an absolute address,
which only lands on the end of the buffer for a process whose data area is at
$0000, and ours are at $0400.

The symptom is a listing that is simply missing files, which looks exactly
like a directory bug and is not. `--cols N` reports a width of your choosing —
still capped — so the layout can be tested without a terminal.

The height is not capped, because nothing depends on it the way `dir` depends
on the width: it is the page length, `PD.PAG`, and a program that pages its
output simply pauses less often. It is asked for the same way, though, and
`--rows N` states it.

That size has to be *stated* in a test. A case's standard input is whatever
ran the suite, so a program that reports on it — `tmode` with no argument
prints the page length out of standard input's path options — otherwise
answers with the size of the window `make test` was typed in, and the golden
file records that. `tests/run.sh` therefore hands every case `--cols 80
--rows 24`, and a case that wants something else passes its own afterwards,
which wins. The `utils` case failed for exactly this reason on a 48-line
terminal: `pag=30` where the golden said `pag=18`.

### dir -e asks for a descriptor by its number

`dir -e` does not open a file to describe it. It has the sector number out of
the directory entry already, and asks the *directory's* path for the file
descriptor that number names — `SS_FDInf`. Our sector numbers are indices
into the table that hands them out, so answering means looking the number back
up as a host path and stating it.

The register convention is not the one `../nitros9/level1/modules/rbf.asm`
writes at the head of its own `Gst627`: the comment there has Y's halves the
wrong way round. The code, and `dir`, put the top byte of the sector number in
Y's MSB and the byte count in its LSB, with the other two bytes of the number
in U.

The listing ends at the first line without it. `dir` takes a failed
`SS_FDInf` as fatal and exits carrying the error, so E$UnkSvc came out as a
header, no files and error 208.

A directory's `FD.SIZ` has to be the length of the listing we would serve —
entries times 32 — for the same reason `SS_FD` does: the host's idea of a
directory's size is about host records.

### fork() and stdio

`F$Fork` is a real `fork()`; the whole machine is copied and the child loads
its program over the copy. Two consequences:

- **Flush before forking.** Anything in a stdio buffer is duplicated and
  written twice. A shell script piped in ran each of its commands' output two
  and three times over.
- **Standard input is unbuffered.** Otherwise the parent reads ahead and the
  bytes it swallowed reach the child as a private copy, so a piped script runs
  some lines twice.

### A shell waits for a signal, not for a read

`shellplus` — the shell a Level 2 system runs, and one Level 1 builds without
installing — never blocks in `I$ReadLn` waiting for a command. It asks the
terminal driver to signal it when a key arrives (`SS.SSig` on standard input),
names a routine to enter when a signal turns up (`F$Icpt`), and then sleeps
until it is signalled (`F$Sleep` with X = 0). Its intercept routine is two
instructions, `stb <u000E` and `rti`; the main loop reads that byte back and,
if it is the $0B it asked for, goes and reads the line.

Three calls, and none of them works unless all three do. `F$Sleep(0)` used to
be `wait()` for a child process, which returns at once when there are none, so
shellplus printed its banner and its prompt and then spun at 100% CPU having
never issued a read. That looks exactly like a hung shell and is not.

The kernel therefore keeps the intercept routine (`icpt_pc`, `icpt_u`) and the
signal each path owes us (`ssig[]`), and `F$Sleep` waits by `poll`ing the host
descriptors behind those paths. Delivering a signal is what OS-9 does with
one: push the registers onto the process's own stack as an ordinary interrupt
frame, set B to the signal code and U to the intercept's memory pointer, and
vector to the routine. Its `RTI` puts the frame back and carries on from the
instruction after the system call that was interrupted — so the sleep simply
returns, which is what the caller is waiting for.

A driver sends its `SS.SSig` signal once and forgets the request; the caller
sets it up again each time round its loop. One whose input is *already*
waiting sends the signal there and then rather than storing the request
(`RSendSig` in `../nitros9/level1/modules/mc6850.asm`), and `SS.Relea` takes
the request back.

The register convention is not where you would guess it: `I$SetStt` holds the
function code in B, so `SS.SSig` carries the signal to send in the **low byte
of X**.

`F$Send` delivers `S$Wake` as well as a kill, as a `SIGUSR1` whose only job is
to break the `poll` — installed without `SA_RESTART`, or it would not even do
that. A signal *code* cannot travel with it, since each OS-9 process here is a
host process, so the rest of the codes still have nowhere to go.

### Unknown service calls must not be fatal

Real OS-9 answers `E$UnkSvc` and lets the caller decide. Bailing out of the
whole machine killed programs that would have carried on. The same goes for
status codes: `dir` asks every device for its screen width and keeps its
default when the answer is an error.

### The non-mapping calls report a size, not an address

`F$NMLink`/`F$NMLoad` ($21/$22) are the CoCo 3 "non-mapping" pair, and both
`cc1` and the shell reach for them by preference. With one address space the
*work* is what the ordinary calls do. What they report is not.

`F$Link` says where the module is: U the header, Y the execution entry point.
`F$NMLink` says what it **needs** — Y comes back as `M$Mem` — and leaves U
alone. Its caller has no way to read the header for itself, because under
Level 2 the module is not in its address space, so the kernel reads it out on
the way past (`FNMLink` in `../nitros9/level2/modules/ioman.asm`).

The shell turns on exactly that. Its Level 1 build does `ldy M$Mem,y` after
linking; its Level 2 build leaves the line out and hands what came back
straight to `F$Fork` as a page count. Answering with an address therefore
asked `F$Fork` for **255 pages** for every command the Level 2 shell ran — the
whole address space, with nothing above the data area for `F$Mem` to add,
which is the ground the C runtime's `sbrk` allocates out of. The C compiler
died in `c.prep` with "grab overlap", which looks like a memory-layout bug and
is a register convention.

### The module directory is shared; the module memory is not

`F$Load` really loads: it places the module at the top of memory, growing
downwards, and records it so `F$Link` can find it again. Reporting the header
without loading it left the caller reading whatever sat at address 0 -- its
own image -- and `runb` concluded that every packed procedure it was handed
had a compiler error in it.

`F$UnLink` gives the space back, and so must `F$UnLoad` ($1D), which is the
same thing named by module rather than by address — `A` the type, `X` the
name, `X` handed back past it. The Level 1 shell releases a command with
`F$Link` and two `F$UnLink`s and the Level 2 shell with one `F$UnLoad`, so
treating $1D as a no-op cost nothing at Level 1 and everything at Level 2:
the module area grew by one command for every command run, until it came down
to meet the shell's own data and the C compiler stopped three passes in with
"process memory full".

Real OS-9 keeps one directory for the whole machine, and `load`, `link`,
`unlink` and `mdir` are written to it: `load echo` is meant to leave `echo`
there for the next command to find, and it is a different process that goes
looking. Each OS-9 process here is a host process with its own copy of the
emulated 64K, so a directory kept in that memory used to die with whoever
built it — issue #1.

What is shared is **the directory, not the module memory**. An OS-9 module is
position-independent and re-entrant by rule, so a process that links a module
another process loaded can read it in again at an address of its own choosing
and be no worse off; only the name, the pathlist it came from and the link
count have to be held in common. That fits in a `MAP_SHARED` page created
before the first fork and inherited by every process after it, and it leaves
each process's 64K alone — no window carved out of the address space, and no
ceiling on anybody's data area, which is what mapping the module *area* would
have cost.

The link count is the machine's, so `F$UnLink` and `F$UnLoad` decrement that
one and give the local copy back when it reaches zero — not when this
process's own tally does. `unlink` links the module to find it and then
unlinks twice, so the second `F$UnLink` has to be the one that counts; with a
per-process tally the first had already taken the entry away and the second
found nothing to do, and a module could never be unloaded at all.

It is one emulator's directory, not the host's. The page is made when
`os9emu` starts and inherited by everything it forks, so `os9emu load echo`
and `os9emu link echo` typed separately at a host shell are two machines and
share nothing — which is what they are. Inside one `os9emu shell` they are
the same machine, and that is the case #1 is about.

What this does not give is a module whose *contents* are shared, which is what
`F$DatMod` would need: a data module has to be writable by everyone linked to
it, and that wants real shared pages.

### The directory a Level 2 utility is handed is a copy

Sharing the table is half of it. The other half is that nothing could read it
back: `mdir` printed a correct heading over nothing, because the copy it asks
the kernel for — `F$GModDr` — was not a call we answered.

The copy is not a list of entries. It is the whole region a real kernel keeps
the directory in: entries from `D.ModDir` ($0A00) upwards, and the DAT image
each entry's `MD$MPDAT` points at from `D.ModDir+2` ($1000) *downwards*, the
two growing towards each other (`krn.asm` sets all three pointers to exactly
these). `F$GModDr` copies the region whole and reports `Y` past the last entry
and `U` = the address the region has on the system side; `mdir` then reads an
entry's DAT image out of *its own copy*, at the offset it gets by subtracting
that `U`. So the images have to travel in the same copy, at the offsets the
pointers claim, or the block numbers come out of the middle of nothing.

An entry names its module by block, not by address, and `F$CpyMem` is what
turns a block back into bytes: `D` is not a process but the address, in the
caller's own memory, of the block list to read through. So the emulator keeps
a fake physical memory alongside the shared directory — 64 blocks of 8K, a
512K CoCo 3 — and gives every module in the directory a block of its own. The
bytes then come from the file the module was loaded from rather than from
anyone's memory, which is as good: a module is read-only by rule, and what the
machine shares is the directory and not the module memory.

`F$CpyMem` handed a DAT image that names no block of ours copies within the
caller's own memory, which is what it did for every caller before there was a
directory to name.

Level 1's `mdir` is a different program with the same name: it reads
`D.ModDir` out of the kernel's direct page and follows `MD$MPtr` straight to
the module header, both of which mean the modules have to be resident in the
process that is asking. Ours are not — a forked `mdir` has loaded nothing —
so on a Level 1 root it still prints a heading over nothing, and that is not
something `F$GModDr` can fix.

### A module file may hold more than one module

`F$Load` loads every module in the file, not the one whose header comes
first. The Level 2 `CMDS/shell` is nine merged — `shellplus` and the `date`,
`deiniz`, `echo`, `iniz`, `link`, `load`, `save` and `unlink` it expects to
find resident afterwards — so stopping at the first left the other eight
where they were and the shell's own `load` could never make good on what the
file was merged for.

The directory therefore remembers **how far into its file each module sits**,
because a process linking one later has to read back the right one. `Save` is
the eighth module of `CMDS/shell`, and without the offset a link to it reads
`shellplus` and calls it `Save`.

The walk ends where the file does, so a file that ends after its last module
is not an error — `read_module` only complains about a header that is not
there when the caller was expecting one, which is the first time round.

### A process id belongs to the machine

The same argument as the module directory, one step on. An OS-9 process id
has to mean the same thing to every process or `F$Send` cannot reach anybody
and `procs` has nothing to list — and each OS-9 process here is a host
process, so the table has to live in the shared page beside the directory.
`F$ID` used to answer 1 for everybody.

An id is a slot number, so it is small enough for the byte OS-9 keeps it in
and is reused once the process holding it has gone. **Slot zero is never
handed out**: id 1 is the system process on a real machine, and `procs` opens
its scan with `inc <u0001` from 1, so the first id it ever asks about is 2.
Numbering our first process 1 hid it from every listing.

A row is given back at exit, and one whose process died without doing so —
killed, or crashed — is dropped by the next process to walk the table, which
asks the host with `kill(pid, 0)`. There is nobody here to notice a death but
the next process to look.

**`F$Wait` has to answer with the id `F$Fork` gave out**, and by the time it
is asked the child has exited and its shared row is gone. So the mapping
`F$Wait` answers from is the one the parent kept for itself, not the shared
table. Getting this wrong is quiet and expensive: `shellplus` waits again for
a child it has already been told about, the second wait finds no children at
all, and every command after the first ends in "ERROR #226 - No Children".

`F$GPrDsc` builds a 512-byte descriptor out of a row; `F$GBlkMp` hands over
the block map the fake memory is kept in, one byte a block and zero meaning
free. What `mfree` reports is therefore the room left for modules and for the
programs processes are running, not the room left in anybody's 64K — those
are separate things here in a way they are not on a real machine.

`printerr` is not waiting on this call at all. It links itself an extra time
to stay resident and then installs its own `F$PErr` into the system service
table with `F$SSvc`, so that error numbers come out of `/DD/SYS/ERRMSG` — a
6809 routine running kernel-side, which is a different thing to want.

### A new process gets a cleared data area

OS-9 zeroes it. Our memory is one array reused by every program, and after a
fork it still holds the parent's variables — the shell read an uninitialised
`modstk` through it and asked `F$Fork` for 255 pages that nothing needed. (The
Level 2 shell asked for 255 for a different reason entirely; see "the
non-mapping calls report a size, not an address".)

### X after a name

`getpath` used to stop *on* the high-bit terminator rather than after it, so
every caller that advanced X by the amount consumed was left one character
short — `runb counter` parsed `counte` and complained about the `r`.

`F$Link` cuts both ways. When it *finds* the module it hands X back past the
name — "the address of the last byte of the module name, plus 1" — and `link
a b c` walks its parameter line by the X that comes back, so with X left where
it started it linked the first name for ever. When it cannot find the module
it hands back the X it was given, untouched: Level 1's `FLink` only writes
`R$X` on the way out *with* a module, and the shell relies on that — when a
link fails it opens the name from wherever X now points, so anything the call
consumed is lost to it.

Nothing showed the first half until the module directory was shared, because
until then a link from the shell's own children never succeeded.

`F$PrsNam` consumes the leading `/`. Leaving X where it put it therefore made
`/dd/BIN/prog` arrive at the shell's `I$Open` as a relative `dd/BIN/prog`,
which got the execution directory pasted in front of it — so the shell could
not run a program by an absolute pathname at all. It looked instead as though
`cxd` were being consulted for something that was plainly not relative.

### Where a program is looked for

OS-9 looks in the execution directory and nowhere else: `F$Load` opens with
`EXEC.` set, and ioman starts a relative pathlist from the execution directory
whenever that bit is on (`L0349` in `../nitros9/level1/modules/ioman.asm`).
So `BIN/prog` typed at an OS-9 shell really does mean `<cxd>/BIN/prog`, and
`i_open` passing `(a & 4)` to `getpath` is faithful. When the open misses, the
shell falls back to opening the name as a shell procedure — which is why
naming a module by a path it cannot reach ends in the binary being read as a
script rather than in a clean error.

A bare name is a module name and gets exactly that treatment: `os9emu echo`
finds the `echo` in `CMDS` and nothing else, however many other files of that
name are lying about. A name with a `/` in it is a pathname, though, and
`./prog` typed at a host shell means the prog here — we are the thing being
typed at, and we have no procedure file to fall back on, so `loadmodule` tries
the working directory after the execution one for those.

### Install the shell NitrOS-9 builds

`~/OS9/CMDS/shell` has to be the one from `../nitros9` — `scripts/os9root.sh`
follows `WHICHSHELL` in the port makefile. An older shell left lying in the
root stays in charge and misbehaves in ways that look like emulator bugs.

### rename writes to the directory

`rename` opens the file, reads `PD.DCP` out of the path options — the byte
offset of the file's entry in its parent directory — then opens `.` as a
directory in update mode, seeks there and writes the new name. So directory
paths have to be writable, and a write that changes an entry's name becomes a
host `rename()`. The path option offsets are in `../nitros9/defs/rbf.d`; ours
were three bytes out.

### A directory listing is live, and its slots do not move

A directory path is served out of an array of entries built from `readdir`.
That array used to be filled in when the path was opened and never again, so
the listing was frozen: a program that opened a directory and kept reading it
never saw a file created afterwards, and went on seeing one that had gone.
`fdirunix::rescan` brings it back in step whenever the host directory's stat
has moved.

It brings it back *by name*, though, because RBF never moves an entry. A slot
belongs to its file until the file goes, a deleted entry leaves its slot with
a zero first byte, and a new file takes the first slot going spare. `rename`
depends on exactly that — `PD.DCP` is a byte offset into the directory, read
before the write and used after it — and so does anything that deletes files
while walking a listing.

## Running things

```sh
os9emu echo hello world           # run one command
os9emu shell                      # interactive shell
os9emu --mem 32k basic09          # Basic09 wants a real workspace
os9emu cc1 hello.c                # compile; the result lands in CMDS/
os9emu -d dir                     # trace system calls; -dd adds reads/writes
os9emu --eol crlf list file.txt   # readable output when piping
```

Arguments after the module name become an OS-9 parameter line, so quote
anything the host shell would eat.

## Basic09

In the Basic09 editor **a source line is introduced by a leading space**. That
is what the editor's command table maps to "insert"; without it every line
comes back "What?", which looks exactly like a broken emulator and is not.

```
e demo
 PRINT "counting"
 FOR i=1 TO 3
 PRINT i, i*i
 NEXT i
q
run demo
```

## The C compiler

`cc1 hello.c` writes a shell procedure `c.com` and runs it. The passes are
`c.prep`, `c.pass1`, `c.pass2`, `c.opt`, `c.asm`, `c.link`, each forked by the
shell with redirections. The result is a real OS-9 module — `os9 ident` will
identify it — and `c.link` puts it in the **execution directory**, so it lands
in `CMDS/`, not next to the source.

Headers come from `/dd/DEFS`, libraries from `/dd/LIB`.

## Comparing against a real system

`scripts/mame-console.sh` boots a NitrOS-9 disk in MAME, types a session and
takes screenshots. Use it whenever it is not obvious whether the emulator or
your expectation is wrong — twice now the emulator was right.

Two limits worth knowing before you spend time on it:

- **Text can only be read out of memory on a CoCo 1 or 2.** On a CoCo 3 the
  GIME keeps video RAM in physical memory MAME's Lua cannot reach, so the
  `dump` directive finds nothing there and you use `snap` and read the picture.
  Most MAME builds ship only CoCo 3 support, so that is the usual case.
- **Use explicit `wait`, not `idle`.** The CoCo blinks its cursor, so the
  screen never settles and `idle` just burns its timeout.

Booting stops at a `TIME ?` prompt; an empty line gets past it.

### free reads a disk that is not there

`free` opens `/d0@`, the whole-device path, reads the identification sector
and counts zero bits in the allocation bitmap. There is no disk, so
`devunix::open` serves a synthetic one built from `statvfs` of the host
directory: capacity, a cluster size chosen to keep the bitmap under 8K, and a
map with the used clusters at the front so the free space reads as one run.
An OS-9 sector number is 24 bits, so a large host filesystem is reported
scaled down — the proportions are right, the absolute numbers cannot be.

## Level 2

`scripts/os9root.sh --level 2` builds the Level 2 "coco3" port into a root of
its own, `~/OS9L2`, so it can be built and surveyed without disturbing the
Level 1 one. Both ports are plain 6809; the 6309 ports are not.

### The memory model here is already Level 2's

Level 1 puts every process in one 64K address space. Here `F$Fork` is a host
`fork()`, so **every OS-9 process already has a private 64K** — which is Level
2's model, not Level 1's. What is missing is the other half of Level 2: the
parts a real system deliberately shares. The module directory, data modules,
the process table. That is issue #1 seen from the other end, and it is the
gate on most of the rest.

### Level 2's utilities ask; Level 1's read the kernel

This is what makes Level 2 the easier of the two to host.

| | Level 1 | Level 2 |
|---|---|---|
| `mdir`  | `ldx >D.ModDir` | `F$GModDr` |
| `procs` | `ldx >D.Proc`   | `F$GBlkMp`, `F$ID`, `F$GPrDsc`, `F$CpyMem` |
| `mfree` | —               | `F$GBlkMp` |

Under Level 2 those tables live in another address space, so the utilities
cannot walk them and have to ask the kernel for a copy instead. We have no
direct page worth reading, but we can answer a question, and all three do
their job now: `mdir` lists what `load` left behind, `procs` lists the
processes with the program each is running, and `mfree` reports what is left
of the memory the modules live in. Level 1's versions of the same three read
the kernel's direct page and still print a heading over nothing.

`level2/coco3/cmds` assembles most of its modules straight out of
`level1/cmds`, and only fourteen sources differ, so a Level 2 root is a small
delta from a Level 1 one. 101 modules against 93.

### The Level 2 shell, and what it took

`CMDS/shell` in the Level 2 port is not one module but nine merged:
`shellplus` and the `date`, `deiniz`, `echo`, `iniz`, `link`, `load`, `save`
and `unlink` it expects to find resident afterwards. It ran nothing at all
until `SS.SSig`, `F$Icpt` and `F$Sleep(0)` added up to a delivered signal —
see "a shell waits for a signal, not for a read" above. It now runs commands,
redirects, and exits at end of file, on either root; `tests/cases/shellplus.t`
is the case that says so.

Something else follows from `shell` being a merged file: `F$Load` has to load
every module in one, and the directory has to remember where in the file each
came from. See "a module file may hold more than one module".

With the non-mapping calls reporting `M$Mem` and `F$UnLoad` releasing what it
is given, the Level 2 shell forks exactly the page counts the Level 1 shell
does, and the C compiler runs on a Level 2 root.

With runs of dots reading as the pathlist components they are — the last
thing shellplus needed, for the directory in its prompt — it is 19 of 19, the
same as Level 1. See "dots are components, and a run of them counts".

`tests/cases/shell.t`, `dots.t` and `progpath.t` fold the shell's identity
away, so the same golden files serve either root: shellplus writes the first
half of its banner to standard *output* and prompts `{term|01}/dd:` where
`shell_21` prompts `OS9:`, and the case is about what the shell does.

### What to leave alone

The ill-behaved end of Level 2 is a tidy set to ignore: `dmem`, `pmap`,
`smap`, `mmap`, `modpatch` and `proc` want `F$Move`, `F$LDABX`, `F$STABX` and
a real block map — the calls that reach into *another* process's address
space, which is exactly what a private-64K-per-host-process model cannot
serve. They are also the least interesting commands in the set.

## Known gaps

- Signals reach a process from its own paths, through `SS.SSig`, and `F$Send`
  carries `S$Kill` and `S$Wake` between processes. No other code travels: each
  OS-9 process here is a host process, and a host signal cannot bring the code
  with it.
- `F$DatMod` wants a module whose contents are shared, which the shared
  directory deliberately does not give: what is shared is the fact of a
  module, not its bytes.
- `format`, `dcheck` and `os9gen` want a disk image to work on, and `httpd`,
  `inetd`, `telnet` and `dw` want a network. Neither exists here.
- Interactive programs that drive the terminal directly — `ded`, `minted`,
  `tsmon`, `edit` — sit waiting for input the test harness never sends.
