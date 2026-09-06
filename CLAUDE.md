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
scripts/os9root.sh    build ../nitros9 into ~/OS9
scripts/survey.sh     run every installed command, report how each fared
scripts/mame-console.sh   drive a real NitrOS-9 in MAME for ground truth
tests/run.sh          golden-output tests
tests/cases/*.t,*.out the cases and what they should print
tests/mame/           the MAME side of the comparison harness
```

`make check` builds the OS-9 root and runs the tests. `make survey` is the
broad view: it runs all ~93 installed commands and buckets them.

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

### fork() and stdio

`F$Fork` is a real `fork()`; the whole machine is copied and the child loads
its program over the copy. Two consequences:

- **Flush before forking.** Anything in a stdio buffer is duplicated and
  written twice. A shell script piped in ran each of its commands' output two
  and three times over.
- **Standard input is unbuffered.** Otherwise the parent reads ahead and the
  bytes it swallowed reach the child as a private copy, so a piped script runs
  some lines twice.

### Unknown service calls must not be fatal

Real OS-9 answers `E$UnkSvc` and lets the caller decide. Bailing out of the
whole machine killed programs that would have carried on. The same goes for
status codes: `dir` asks every device for its screen width and keeps its
default when the answer is an error.

`F$NMLink`/`F$NMLoad` ($21/$22) are the CoCo 3 "non-mapping" calls. With one
address space they mean what the ordinary calls mean, and both `cc1` and the
shell reach for them by preference.

### The module directory is per process

`F$Load` really loads: it places the module at the top of memory, growing
downwards, and records it so `F$Link` can find it again. Reporting the header
without loading it left the caller reading whatever sat at address 0 -- its
own image -- and `runb` concluded that every packed procedure it was handed
had a compiler error in it.

`F$UnLink` gives the space back. Without that a shell that loads command after
command runs the module area down into the program's data, and the C compiler
dies partway through with "process memory full".

The directory belongs to the process that built it, though. Each OS-9 process
here is a host process with its own copy of memory, so a module `load`ed by
one is invisible to the next: `load`, `link` and `mdir` work within a program
but not across the shell's commands. A system-wide directory would need the
module area in shared memory, which the single 64K array the CPU runs on does
not lend itself to. See issue #1.

### A new process gets a cleared data area

OS-9 zeroes it. Our memory is one array reused by every program, and after a
fork it still holds the parent's variables — the shell read an uninitialised
`modstk` through it and asked `F$Fork` for 255 pages that nothing needed.

### X after a name

`getpath` used to stop *on* the high-bit terminator rather than after it, so
every caller that advanced X by the amount consumed was left one character
short — `runb counter` parsed `counte` and complained about the `r`.

`F$Link` is the other way round: it leaves X where `F$PrsNam` put it, at the
start of the name. The shell reads X back to open the command it just failed
to link, so advancing past the name loses the name.

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

## Known gaps

- No signals between processes. `F$Send` delivers a kill; the keyboard signals
  have nothing to wake, since a sleeping process is inside `nanosleep`.
- `load`, `link`, `mdir` and `printerr` only see modules the running program
  loaded itself — see "the module directory is per process" above, and #1.
- The execution directory is not handled properly: `pwd` and `pxd` print only
  the device name, `pxd` fails after a `chx`, and a relative pathname with a
  `/` in it is looked up under `cxd` rather than the working directory. #2.
- `format`, `dcheck` and `os9gen` want a disk image to work on, and `httpd`,
  `inetd`, `telnet` and `dw` want a network. Neither exists here.
- Interactive programs that drive the terminal directly — `ded`, `minted`,
  `tsmon`, `edit` — sit waiting for input the test harness never sends.
