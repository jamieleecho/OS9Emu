# The module directory belongs to the machine, not to the process that built
# it -- issue #1.
#
# "load echo" is meant to leave echo there for the next command to find, and
# it is a different process that goes looking. What is shared is the directory
# rather than the module memory: a module is position-independent and
# re-entrant, so a process that links one another process loaded reads it in
# again at an address of its own and is none the worse for it.
#
# load, link and unlink are silent when they work, so what this case shows is
# the complaints -- the ones that should no longer be there, and the one that
# still should be once the last link has been given back.
# The prompt shares a line with whatever follows it, and which shell is
# installed depends on the root; fold it away as the other shell cases do.
errs() {
  printf "$1" > cmds.sh
  $OS9 shell < cmds.sh 2>&1 | tr '\r' '\n' \
    | sed -e 's/{term|[0-9a-z]*}[^:]*:/OS9:/g' -e 's/^OS9://' \
    | grep -E 'ERROR|Exit code'
  echo "(nothing further)"
}

echo "--- link before anything has loaded it"
errs 'link echo\n'

echo "--- load, then link from the command after it"
errs 'load echo\nlink echo\n'

echo "--- unlink as often as you linked, and no more"
errs 'load echo\nlink echo\nunlink echo\nlink echo\nunlink echo\nunlink echo\nlink echo\n'

echo "--- a module loaded by one command, unlinked by another"
errs 'load echo\nunlink echo\nlink echo\n'

# A module file may hold several merged -- the Level 2 CMDS/shell is nine --
# and F$Load loads all of them, not just the one whose header comes first.
# copy is the second here, so linking it says whether the load walked the
# file, and the offset it was found at says whether the directory remembered
# where in the file to read it back from.
echo "--- every module of a merged file, not just the first"
$OS9 merge /dd/CMDS/echo /dd/CMDS/copy > CMDS/pair
errs 'load pair\nlink echo\nlink copy\nunlink copy\nunlink copy\nlink copy\n'
