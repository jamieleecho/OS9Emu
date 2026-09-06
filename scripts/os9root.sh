#!/usr/bin/env bash
#
# Assemble the OS-9 root that os9emu runs against ($OS9ROOT).
#
# Everything installed here is built from the NitrOS-9 sources in $NITROS9DIR --
# a command set, Basic09, and the Microware C compiler package. The emulator
# maps /d0, /h0 and /dd onto this directory, so the layout mirrors an OS-9
# system disk:
#
#     CMDS/   executables            (the execution directory, /dd/CMDS)
#     SYS/    errmsg, help, password
#     DEFS/   C headers              (#include <stdio.h>)
#     LIB/    clib.l, cstart.r       (what c.link pulls in)
#
#     scripts/os9root.sh              # build what is missing, then install
#     scripts/os9root.sh --level 2    # the Level 2 command set instead
#     scripts/os9root.sh --rebuild    # force a rebuild of every module first
#     scripts/os9root.sh --clean      # empty the root and start over
#
# Two levels, two roots. Level 1 is the "coco1" port and Level 2 the "coco3"
# one; both are plain 6809, which is what the emulated CPU is, while the 6309
# ports assemble to instructions os9emu cannot execute. With $OS9ROOT unset the
# two install side by side, into ~/OS9 and ~/OS9L2, so a Level 2 root can be
# built and surveyed without disturbing the Level 1 one.
#
# The command sets overlap almost entirely -- level2/coco3/cmds assembles most
# of its modules straight out of level1/cmds -- and the interesting difference
# is in the handful that do not. Level 1's mdir and procs read the kernel's own
# tables out of the direct page; Level 2's ask for a copy, through F$GModDr and
# F$GPrDsc. An emulator can answer a question. It has no direct page to be read.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NITROS9DIR="${NITROS9DIR:-$(cd "$PROJECT_DIR/../nitros9" && pwd)}"

LEVEL=1
rebuild=0
clean=0
while [ $# -gt 0 ]; do
  case "$1" in
    --level)   LEVEL="${2:-}"; shift 2 ;;
    --level=*) LEVEL="${1#--level=}"; shift ;;
    --rebuild) rebuild=1; shift ;;
    --clean)   clean=1; shift ;;
    -h|--help) sed -e '1d' -e '/^[^#]/,$d' "$0" | sed -E 's/^#[[:space:]]?//'; exit 0 ;;
    *) echo "$0: unknown option $1" >&2; exit 2 ;;
  esac
done

case "$LEVEL" in
  1) PORT=coco1; OS9ROOT="${OS9ROOT:-$HOME/OS9}"   ;;
  2) PORT=coco3; OS9ROOT="${OS9ROOT:-$HOME/OS9L2}" ;;
  *) echo "$0: --level wants 1 or 2, not \"$LEVEL\"" >&2; exit 2 ;;
esac

CMDSRC="$NITROS9DIR/level$LEVEL/$PORT/cmds"
CCSRC="$NITROS9DIR/3rdparty/packages/ccompiler"

# Where SYS/ text comes from, in increasing order of priority. The Level 1
# pages are the ones most of the command set still uses whichever level it was
# built for; a level's own directory adds the pages for its own commands; the
# port's directory has the last word on errmsg, helpmsg and password.
SYSDIRS=("$NITROS9DIR/level1/sys")
[ "$LEVEL" = 1 ] || SYSDIRS+=("$NITROS9DIR/level$LEVEL/sys")
SYSDIRS+=("$NITROS9DIR/level$LEVEL/$PORT/sys")

say() { printf '\033[1m==>\033[0m %s\n' "$*"; }

for tool in lwasm lwlink os9; do
  command -v "$tool" >/dev/null || {
    echo "$0: $tool not found on PATH." >&2
    echo "    Install lwtools + ToolShed, or run this under ./coco-dev." >&2
    exit 1
  }
done

[ -d "$CMDSRC" ] || { echo "$0: no NitrOS-9 sources at $CMDSRC" >&2; exit 1; }

if [ "$clean" = 1 ]; then
  say "emptying $OS9ROOT"
  rm -rf "$OS9ROOT/CMDS" "$OS9ROOT/SYS" "$OS9ROOT/DEFS" "$OS9ROOT/LIB"
fi

# ---------------------------------------------------------------- build
# NitrOS-9's own makefiles do the work; we only decide what to ask for.

if [ "$rebuild" = 1 ]; then
  say "cleaning NitrOS-9 build products"
  make -C "$CMDSRC" clean >/dev/null 2>&1 || true
  make -C "$CCSRC"  clean >/dev/null 2>&1 || true
fi

say "building Level $LEVEL commands ($PORT)"
make -C "$CMDSRC" all >/dev/null

say "building the C compiler"
make -C "$CCSRC" all >/dev/null

# ---------------------------------------------------------------- install
say "installing into $OS9ROOT"
mkdir -p "$OS9ROOT"/{CMDS,SYS,DEFS,LIB}

# Only install real OS-9 modules. The command directories also hold sources,
# makefiles and .list files, and a stray text file in CMDS would be offered to
# F$Load as if it were executable.
install_modules() {
  local dest="$1"; shift
  local src count=0
  for src in "$@"; do
    [ -f "$src" ] || continue
    case "$src" in *.asm|*.as|*.list|*.map|makefile|*.mak|*.dsk) continue ;; esac
    # An OS-9 module starts with the sync bytes $87CD.
    if [ "$(head -c2 "$src" | od -An -tx1 | tr -d ' \n')" = "87cd" ]; then
      cp -p "$src" "$dest/$(basename "$src")"
      count=$((count + 1))
    fi
  done
  echo "$count"
}

say "installing commands into $OS9ROOT/CMDS"
n=$(install_modules "$OS9ROOT/CMDS" "$CMDSRC"/*)
echo "    $n modules"

say "installing the C compiler"
m=$(install_modules "$OS9ROOT/CMDS" "$CCSRC"/cc1 "$CCSRC"/c.* "$CCSRC"/c_asm "$CCSRC"/make)
echo "    $m modules"
# cc1 runs the pass it needs by name; c.asm is the assembler under its C name.
[ -f "$OS9ROOT/CMDS/c.asm" ] || cp -p "$CCSRC/c_asm" "$OS9ROOT/CMDS/c.asm"

# NitrOS-9 builds the shell under a versioned name and installs it as "shell";
# WHICHSHELL in the port makefile says which one a disk gets. Follow it, so we
# run the shell the real system runs -- and so an older shell left lying in the
# root does not quietly stay in charge.
#
# The Level 2 port names no shell, because it builds one called "shell"
# already: a merged package of shellplus and the eight modules it expects to
# find resident afterwards. Leave that one where install_modules put it.
WHICHSHELL="$(sed -n 's/^WHICHSHELL[[:space:]]*=[[:space:]]*//p' \
                 "$NITROS9DIR/level$LEVEL/$PORT/makefile" | tail -1)"
if [ -n "$WHICHSHELL" ] && [ -f "$CMDSRC/$WHICHSHELL" ]; then
  cp -p "$CMDSRC/$WHICHSHELL" "$OS9ROOT/CMDS/shell"
  echo "    shell <- $WHICHSHELL"
elif [ ! -f "$OS9ROOT/CMDS/shell" ] && [ -f "$CMDSRC/shell_21" ]; then
  cp -p "$CMDSRC/shell_21" "$OS9ROOT/CMDS/shell"
  echo "    shell <- shell_21"
fi

# OS-9 ends a line of text with a bare CR. These files live in a git checkout
# with newlines, and a header copied across unchanged reads to the compiler as
# one enormous line. NitrOS-9's own makefiles convert them the same way, with
# "os9 copy -l".
install_text() {
  local dest="$1"; shift
  local src
  for src in "$@"; do
    [ -f "$src" ] || continue
    perl -pe 's/\r\n/\r/g; s/\n/\r/g' < "$src" > "$dest/$(basename "$src")"
  done
}

install_text "$OS9ROOT/DEFS" "$CCSRC"/defs/*.h
cp -p "$CCSRC"/lib/*    "$OS9ROOT/LIB/"

say "installing SYS files"
# errmsg and the help pages are text; the password file is too.
for sysdir in "${SYSDIRS[@]}"; do
  [ -d "$sysdir" ] || continue
  install_text "$OS9ROOT/SYS" "$sysdir"/errmsg "$sysdir"/password \
               "$sysdir"/motd "$sysdir"/helpmsg "$sysdir"/*.hp
done

# Which level this root was built from. Not inside CMDS, SYS, DEFS or LIB, so
# it never shows up in a directory listing a test takes -- but the test runner
# reads it, since a Level 2 command set does not always print what a Level 1
# one prints.
echo "$LEVEL" > "$OS9ROOT/.level"

say "done"
printf '    CMDS  %4d files\n' "$(ls -1 "$OS9ROOT/CMDS" | wc -l | tr -d ' ')"
printf '    SYS   %4d files\n' "$(ls -1 "$OS9ROOT/SYS"  | wc -l | tr -d ' ')"
printf '    DEFS  %4d files\n' "$(ls -1 "$OS9ROOT/DEFS" | wc -l | tr -d ' ')"
printf '    LIB   %4d files\n' "$(ls -1 "$OS9ROOT/LIB"  | wc -l | tr -d ' ')"
