#!/usr/bin/env bash
#
# Run a session on a real NitrOS-9 machine under MAME and capture the screen.
# This is the ground truth os9emu is measured against: when the emulator and
# the real system disagree about a utility, this settles which is wrong.
#
#     scripts/mame-console.sh session.mame       # run a session file
#     echo 'type dir' | scripts/mame-console.sh -
#
# A session is a list of directives (see tests/mame/console.lua):
#
#     idle 3            wait for the screen to settle
#     type dir          type a line and press return
#     snap after-dir    screenshot, saved under that name
#     dump after-dir    decode the text screen out of memory
#
# Booting is done for you: the CoCo comes up in Disk BASIC, "DOS" is typed for
# it, and the session starts once NitrOS-9 has settled.
#
# Screenshots land in an output directory named after the snap that took them.
# There is no automatic comparison: "dump", the only capture that yields text,
# needs the screen to live in the CPU's own address space. That holds on a
# CoCo 1 or 2, but not on a CoCo 3, whose GIME keeps video RAM in physical
# memory MAME's Lua cannot reach -- and most MAME builds ship only CoCo 3
# support. So in practice you look at the pictures.
#
# Options:
#   -d, --disk PATH   disk for drive 0 (default: the NitrOS-9 L1 coco1 disk)
#   -2, --disk2 PATH  disk for drive 1
#   -m, --machine M   MAME machine (default coco3)
#   -o, --out DIR     where to put the results (default: a temp directory)
#   -t, --time SECS   emulated seconds to allow the whole session (default 180)
#   -s, --screen ADDR text screen base for "dump" (default 0x0400)
#   -n, --no-boot     do not type DOS first; the disk boots itself
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NITROS9DIR="${NITROS9DIR:-$(cd "$PROJECT_DIR/../nitros9" && pwd)}"
MAME_DIR="${MAME_DIR:-$HOME/Applications/mame}"
MAME="${MAME:-$MAME_DIR/mame}"
MAME_ROMPATH="${MAME_ROMPATH:-$MAME_DIR/roms}"

disk="$NITROS9DIR/level1/coco1/NOS9_6809_L1_coco1_40d_1.dsk"
disk2=""
machine=coco3
outdir=""
maxtime=180
screen=0x0400
boot=1
session=""

while [ $# -gt 0 ]; do
  case "$1" in
    -d|--disk)    disk="$2"; shift 2 ;;
    -2|--disk2)   disk2="$2"; shift 2 ;;
    -m|--machine) machine="$2"; shift 2 ;;
    -o|--out)     outdir="$2"; shift 2 ;;
    -t|--time)    maxtime="$2"; shift 2 ;;
    -s|--screen)  screen="$2"; shift 2 ;;
    -n|--no-boot) boot=0; shift ;;
    -h|--help)    sed -n '2,34p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *)            session="$1"; shift ;;
  esac
done

[ -x "$MAME" ] || { echo "no mame at $MAME" >&2; exit 1; }
[ -f "$disk" ] || { echo "no disk at $disk" >&2; exit 1; }
[ -n "$session" ] || { echo "usage: $0 <session-file>|-" >&2; exit 2; }

if [ -z "$outdir" ]; then
  outdir="$(mktemp -d "${TMPDIR:-/tmp}/os9mame.XXXXXX")"
fi
mkdir -p "$outdir"

# A NitrOS-9 disk is not self-booting on a CoCo: the machine comes up in Disk
# BASIC and "DOS" is what hands control to OS-9.
{
  # Explicit waits, not "idle": a CoCo blinks its cursor, so the screen is
  # never still and idle has nothing to detect. NitrOS-9 Level 1 takes about
  # 40 emulated seconds from DOS to its first prompt.
  if [ "$boot" = 1 ]; then
    echo "wait 4"
    echo "type DOS"
    echo "wait 45"
  else
    echo "wait 45"
  fi
  if [ "$session" = "-" ]; then cat; else cat "$session"; fi
} > "$outdir/session"

# Snapshots need a video target, so this cannot run with -video none. The
# window is transient and the run is unthrottled.
flags=(
  "$machine"
  -rompath "$MAME_ROMPATH"
  -flop1 "$disk"
  -window -nomaximize -sound none
  -skip_gameinfo -nothrottle
  -seconds_to_run "$maxtime"
  -snapshot_directory "$outdir/snaps"
  -autoboot_script "$PROJECT_DIR/tests/mame/console.lua"
  -autoboot_delay 0
)
[ -n "$disk2" ] && flags+=(-flop2 "$disk2")

# MAME drops cfg and nvram next to its working directory; keep that out of the
# project by running from the output directory.
(
  cd "$outdir"
  OS9SCRIPT="$outdir/session" OS9OUT="$outdir/out.txt" OS9SCREEN="$screen" \
    "$MAME" "${flags[@]}" >"$outdir/mame.log" 2>&1
) || true

if [ ! -f "$outdir/out.txt" ]; then
  echo "MAME produced nothing; its log said:" >&2
  tail -20 "$outdir/mame.log" >&2
  exit 1
fi

# MAME numbers its snapshots in the order they were taken; the Lua side
# recorded which name went with which, so give them their names back.
while IFS=$'\t' read -r kind index name; do
  [ "$kind" = "snapshot" ] || continue
  src="$(find "$outdir/snaps" -name '*.png' | sort | sed -n "$((10#$index + 1))p")"
  [ -n "$src" ] && cp "$src" "$outdir/$name.png"
done < "$outdir/out.txt"

# Anything the session dumped as text goes straight to standard output.
grep -v $'^snapshot\t' "$outdir/out.txt" || true

echo "results in $outdir" >&2
ls "$outdir"/*.png 2>/dev/null >&2 || true
