# shellplus, the shell a Level 2 system runs, does not block in a read.
#
# It asks the terminal driver to signal it when a key arrives (SS.SSig on
# standard input), names an intercept routine to enter when one does (F$Icpt),
# and then sleeps until it is signalled (F$Sleep with X=0). All three have to
# work together or it prints its prompt, never issues a read, and spins.
#
# The banner carries the clock, and its first half goes to standard output --
# so it lands in front of whatever the first command printed. Cut it away.
# The prompt goes to standard error, which the pipe below drops.
run() {
  printf "$1" | $OS9 shellplus 2>/dev/null \
    | sed -e 's/^Shell+ v[0-9.]*[a-z]* //'
}

echo "--- one command"
run 'echo hello\n'

echo "--- several, in order"
run 'echo one\necho two\necho three\n'

echo "--- output redirected, then read back"
run 'echo redirected >out.txt\n'
$OS9 list out.txt

# The error itself goes to standard error, which the pipe drops; what this
# case is after is that the shell is still reading afterwards.
echo "--- a command that is not there, then one that is"
run 'nosuchcommand\necho still here\n'
