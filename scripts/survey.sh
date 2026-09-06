#!/usr/bin/env bash
#
# Run every command in the OS-9 root and report how it fared. A quick map of
# what the emulator can and cannot do yet -- not a pass/fail test suite, since
# many of these legitimately need arguments or a real disk.
#
#     scripts/survey.sh            # every module in CMDS
#     scripts/survey.sh dir free   # just these
#
# Each command gets a fresh emulator, no stdin, and a wall-clock limit. The
# verdict comes from what it printed:
#
#   ok         produced output, no emulator complaint
#   silent     exited cleanly with nothing to say
#   unimpl     hit a system call or status code we do not implement
#   error      OS-9 error, or the emulator bailed out
#   timeout    still running when the clock ran out (usually waiting on input)
set -uo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OS9EMU="${OS9EMU:-$PROJECT_DIR/build/os9emu}"
OS9ROOT="${OS9ROOT:-$HOME/OS9}"
TIMEOUT="${TIMEOUT:-8}"

[ -x "$OS9EMU" ] || { echo "no emulator at $OS9EMU (run make)" >&2; exit 1; }

# Survey against a throwaway copy of the root. Several of these commands write
# -- attr rewrites a file's attributes, del removes things -- and a survey that
# damages the command set it is surveying is worse than no survey.
SCRATCH="$(mktemp -d "${TMPDIR:-/tmp}/os9survey.XXXXXX")"
trap 'rm -rf "$SCRATCH"' EXIT
mkdir -p "$SCRATCH/CMDS" "$SCRATCH/SURVEYDIR2"
# Files the commands below are pointed at, so they have something real to do.
printf 'survey fixture\r' > "$SCRATCH/survey.tmp"
printf 'survey fixture\r' > "$SCRATCH/survey.del"
cp -R "$OS9ROOT"/CMDS/. "$SCRATCH/CMDS/"
for d in SYS DEFS LIB; do
  [ -d "$OS9ROOT/$d" ] && cp -R "$OS9ROOT/$d" "$SCRATCH/$d"
done

# Arguments that give a command something harmless to chew on. Anything not
# listed runs bare.
args_for() {
  case "$1" in
    attr)    echo "/dd/CMDS/echo" ;;
    cmp)     echo "/dd/SYS/errmsg /dd/SYS/errmsg" ;;
    dump)    echo "/dd/CMDS/echo" ;;
    echo)    echo "hello" ;;
    ident)   echo "-s /dd/CMDS/echo" ;;
    list)    echo "/dd/SYS/password" ;;
    sleep)   echo "1" ;;
    error)   echo "216" ;;
    printerr) echo "216" ;;
    makdir)  echo "SURVEYDIR" ;;
    rename)  echo "survey.tmp survey2.tmp" ;;
    copy)    echo "/dd/SYS/password survey.copy" ;;
    del)     echo "survey.del" ;;
    deldir)  echo "SURVEYDIR2" ;;
    touch)   echo "survey.touch" ;;
    binex|exbin) echo "" ;;
    *)       echo "" ;;
  esac
}

# bash 3.2 (what macOS ships) has no mapfile.
cmds=()
if [ "$#" -gt 0 ]; then
  cmds=("$@")
else
  while IFS= read -r line; do cmds+=("$line"); done < <(ls -1 "$OS9ROOT/CMDS" | sort)
fi

pass=0; silent=0; unimpl=0; err=0; slow=0

for cmd in "${cmds[@]}"; do
  extra=()
  for word in $(args_for "$cmd"); do extra+=("$word"); done
  out=$(perl -e 'alarm shift; exec @ARGV' "$TIMEOUT" \
          "$OS9EMU" --root "$SCRATCH" "$cmd" ${extra+"${extra[@]}"} </dev/null 2>&1)
  rc=$?

  if [ "$rc" -eq 142 ] || [ "$rc" -eq 14 ]; then
    verdict="timeout"; slow=$((slow+1))
  elif grep -qE "not implemented|Uncaught SWI|No driver for" <<< "$out"; then
    verdict="unimpl"; unimpl=$((unimpl+1))
  elif grep -qE "^ERROR #|Exit code" <<< "$out"; then
    # F$Exit passes the OS-9 error code straight out as our exit status, so a
    # status above 128 here is an OS-9 error, not a signal.
    verdict="error"; err=$((err+1))
  elif [ "$rc" -gt 128 ] && [ "$rc" -lt 160 ]; then
    verdict="crash(sig$((rc-128)))"; err=$((err+1))
  elif [ -z "$out" ]; then
    verdict="silent"; silent=$((silent+1))
  else
    verdict="ok"; pass=$((pass+1))
  fi

  printf '%-12s %-14s %s\n' "$cmd" "$verdict" \
    "$(head -1 <<< "$out" | cut -c1-58)"
done

echo
printf 'ok %d   silent %d   unimpl %d   error %d   timeout %d   (of %d)\n' \
  "$pass" "$silent" "$unimpl" "$err" "$slow" "${#cmds[@]}"
