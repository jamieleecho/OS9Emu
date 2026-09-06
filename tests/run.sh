#!/usr/bin/env bash
#
# Golden-output tests for os9emu.
#
#     tests/run.sh                 # run everything
#     tests/run.sh dir echo        # run tests/cases/dir.t and echo.t
#     tests/run.sh --update        # rewrite the .out files from what we got
#     tests/run.sh -v dir          # show the output even when it passes
#
# A case is tests/cases/NAME.t, a bash fragment that prints to stdout, paired
# with NAME.out holding what it should print. The fragment gets:
#
#     $OS9 <module> [args]   run a module under the emulator
#     $WORK                  a scratch directory, already the OS-9 data area
#
# Each case runs against a freshly built OS-9 root under $WORK, containing the
# CMDS from $OS9ROOT plus the fixtures in tests/fixtures/. Tests therefore do
# not see -- or disturb -- whatever else is in the real ~/OS9.
#
# Output is normalised before comparing: OS-9 ends its lines with CR, and
# trailing blanks on a line are not something a test should care about.
set -uo pipefail

TESTDIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$TESTDIR")"
OS9EMU="${OS9EMU:-$PROJECT_DIR/build/os9emu}"
OS9ROOT="${OS9ROOT:-$HOME/OS9}"
TIMEOUT="${TIMEOUT:-20}"

update=0
verbose=0
names=()
for arg in "$@"; do
  case "$arg" in
    --update|-u) update=1 ;;
    -v)          verbose=1 ;;
    -h|--help)   sed -n '2,20p' "$0" | sed 's/^# \?//'; exit 0 ;;
    *)           names+=("$arg") ;;
  esac
done

[ -x "$OS9EMU" ] || { echo "no emulator at $OS9EMU (run make)" >&2; exit 1; }
[ -d "$OS9ROOT/CMDS" ] || {
  echo "no OS-9 commands in $OS9ROOT (run scripts/os9root.sh)" >&2; exit 1; }

if [ "${#names[@]}" -eq 0 ]; then
  while IFS= read -r f; do names+=("$(basename "$f" .t)"); done \
    < <(ls -1 "$TESTDIR"/cases/*.t 2>/dev/null | sort)
fi
[ "${#names[@]}" -gt 0 ] || { echo "no test cases found" >&2; exit 1; }

# A terminal under OS-9 gets CR LF at the end of each line; a plain file gets a
# bare CR. Fold both to a newline -- taking CR LF first, so it does not turn
# into a blank line -- and drop trailing blanks so a diff shows real changes.
normalise() { perl -pe 's/\r\n/\n/g; s/\r/\n/g' | sed -e 's/[[:space:]]*$//'; }

pass=0; fail=0; failed=()

for name in "${names[@]}"; do
  case_file="$TESTDIR/cases/$name.t"
  want_file="$TESTDIR/cases/$name.out"
  if [ ! -f "$case_file" ]; then
    echo "no such test: $name" >&2; fail=$((fail+1)); failed+=("$name"); continue
  fi

  WORK="$(mktemp -d "${TMPDIR:-/tmp}/os9test.XXXXXX")"
  mkdir -p "$WORK/CMDS"
  # Link rather than copy: the command set is large and never written to.
  for m in "$OS9ROOT"/CMDS/*; do ln -s "$m" "$WORK/CMDS/" 2>/dev/null; done
  for d in SYS DEFS LIB; do
    [ -d "$OS9ROOT/$d" ] && cp -R "$OS9ROOT/$d" "$WORK/$d"
  done
  [ -d "$TESTDIR/fixtures" ] && cp -R "$TESTDIR"/fixtures/. "$WORK/"

  # $OS9 is what a case calls: a fresh emulator rooted at this case's $WORK,
  # with a wall-clock limit so a hung program fails loudly instead of stalling
  # the suite. It is a script rather than a variable holding a command line, so
  # that a case can quote its arguments however it likes.
  #
  # The screen size is stated rather than asked for. A case's standard input is
  # whatever ran the suite, so a program that reports on it -- tmode prints the
  # page length out of the path options -- otherwise answers with the size of
  # the window "make test" was typed in, and the golden file records that. A
  # case that cares passes its own --cols or --rows after these, which wins.
  # Kept beside the OS-9 root rather than inside it: a file here would show
  # up in every directory listing a test takes.
  mkdir -p "$WORK.bin"
  cat > "$WORK.bin/os9run" <<RUNNER
#!/usr/bin/env bash
exec perl -e 'alarm shift; exec @ARGV' "$TIMEOUT" "$OS9EMU" \\
  --root "$WORK" --cols 80 --rows 24 "\$@"
RUNNER
  chmod +x "$WORK.bin/os9run"
  OS9="$WORK.bin/os9run"
  export OS9 WORK OS9EMU TIMEOUT

  got="$(cd "$WORK" && bash "$case_file" 2>&1 | normalise)"

  if [ "$update" = 1 ]; then
    printf '%s\n' "$got" > "$want_file"
    echo "updated $name"
    rm -rf "$WORK" "$WORK.bin"
    continue
  fi

  if [ ! -f "$want_file" ]; then
    echo "FAIL $name (no $name.out -- run with --update to create it)"
    fail=$((fail+1)); failed+=("$name"); rm -rf "$WORK" "$WORK.bin"; continue
  fi

  if [ "$got" = "$(normalise < "$want_file")" ]; then
    echo "ok   $name"
    [ "$verbose" = 1 ] && printf '%s\n' "$got" | sed 's/^/       /'
    pass=$((pass+1))
  else
    echo "FAIL $name"
    # Plain files rather than process substitution: /dev/fd is not always
    # readable by a sandboxed diff.
    normalise < "$want_file" > "$WORK/.want"
    printf '%s\n' "$got"     > "$WORK/.got"
    diff -u "$WORK/.want" "$WORK/.got" \
      | sed -e '1,2d' -e 's/^/       /' | head -40
    fail=$((fail+1)); failed+=("$name")
  fi
  rm -rf "$WORK" "$WORK.bin"
done

[ "$update" = 1 ] && exit 0

echo
echo "$pass passed, $fail failed"
if [ "$fail" -gt 0 ]; then
  echo "failed: ${failed[*]}"
  exit 1
fi
