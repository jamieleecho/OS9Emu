# os9emu

An OS-9 Level One emulator for the command line.

It is a Motorola 6809 interpreter that runs real OS-9 program modules and
services their system calls against the host filesystem. There is no disk
image and no display emulation: `/dd`, `/d0`, `/d1` and `/h0` all map onto one
host directory, an OS-9 file is a host file, and `/term` is your terminal.

The aim is to be as complete as Level One allows for text work — the standard
command set, Basic09, and the Microware C compiler — with no graphics or
sound.

```console
$ os9emu echo hello world
hello world

$ os9emu cc1 hello.c
CC1 VERSION RS 01.00.00
COPYRIGHT 1983 MICROWARE
...
c.prep:
c.pass1:
c.pass2:
c.opt:
c.asm:
c.link:

$ os9emu hello
Hello, world!
```

That `hello` is a real OS-9 program module, compiled by the real Microware C
compiler, running on the emulated 6809.

## Credits

The emulator is the work of **Søren Roug**, who wrote it as *OSNine in C++*
(OS9L1) and released versions from 2001 to 2011. His page is the origin of
this code and the place to look for the original releases and his Java
successor:

> **<https://www.roug.org/soren/6809>**

Søren's emulator is in turn built on the **Usim** 6809 simulator by
**Ray Bellis**, which supplies the CPU core (`usim.cc`, `mc6809.cc`,
`mc6809in.cc`).

Everything the emulator runs — the OS-9 command set, Basic09, and the
Microware C compiler package — is built from the
[NitrOS-9](https://github.com/nitros9project/nitros9) sources, the work of the
NitrOS-9 Project and of Microware and Tandy before them.

This repository is a continuation of Søren's C++ emulator: a 64-bit macOS and
Linux build, and work towards running the Level One utilities, Basic09 and the
C compiler unmodified.

## Building

The emulator itself needs only a C++ compiler:

```sh
make                 # builds build/os9emu
```

Assembling the OS-9 root it runs against needs `lwasm`, `lwlink` and
ToolShed's `os9`, and a checkout of NitrOS-9 beside this one:

```sh
make os9root         # builds ../nitros9 into ~/OS9
make check           # the above, then the tests
```

The Level 2 command set builds into a root of its own, so the two can be
compared without either disturbing the other:

```sh
make os9root-l2      # builds the Level 2 "coco3" port into ~/OS9L2
make test-l2         # the golden-output tests against it
make survey-l2       # runs every command in it and reports how each fared
```

Most of Level 2's modules are the same sources as Level 1's. The ones that
differ are the more interesting for an emulator, not less: `mdir`, `procs` and
`mfree` there ask the kernel for a copy of its tables, where their Level 1
counterparts read them straight out of the direct page. `CLAUDE.md` has the
detail.

`./coco-dev` runs a command inside the `jamieleecho/coco-dev` container if you
would rather not install the toolchain on the host.

## Using it

```sh
os9emu                            # the OS-9 shell
os9emu dir /dd/CMDS               # one command
os9emu --mem 32k basic09          # Basic09 wants a real workspace
os9emu cc1 hello.c                # compile; the result lands in CMDS/
os9emu -d dir                     # trace system calls; -dd adds reads/writes
os9emu --eol crlf list file.txt   # readable line endings when piping
os9emu --cols 40 --rows 16 dir    # a screen of your choosing, not the host's
os9emu --root /path/to/os9        # a different OS-9 root ($OS9ROOT, else ~/OS9)
```

Arguments after the module name become an OS-9 parameter line, so quote
anything your shell would otherwise eat.

Two things surprise people, and neither is a bug:

- In the **Basic09 editor a source line is introduced by a leading space**.
  Without it every line comes back `What?`.
- Text files that OS-9 reads must have **CR line endings**, not newlines.
  `scripts/os9root.sh` converts the headers and help files it installs.

## Layout

```
os9emu/OS9/           the emulator sources (and an Xcode project over them)
scripts/os9root.sh    build the OS-9 root from ../nitros9 (--level 1 or 2)
scripts/survey.sh     run every installed command and report how each fared
scripts/mame-console.sh   drive a real NitrOS-9 under MAME, for comparison
tests/run.sh          golden-output tests
```

`CLAUDE.md` records the details of how OS-9 expects a process to be set up —
memory layout, name termination, line endings, what `F$Load` and `F$Mem` must
actually do — which is where most of the work went.

## License

GPL-2.0-or-later. Every source file carries Søren Roug's original notice,
which offers the GNU General Public License "either version 2 of the License,
or (at your option) any later version"; `COPYING` holds the version 2 text.

The OS-9 modules the emulator runs are not covered by this license. NitrOS-9
has its own terms, and the Microware C compiler and Basic09 are Microware
software distributed as part of the NitrOS-9 project.
