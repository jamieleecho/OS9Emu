# What a Level 2 utility is handed instead of the module directory itself.
#
# The directory belongs to the machine and has done since modules started
# outliving the process that loaded them -- but on a real Level 2 system it
# sits in an address space the caller cannot reach, so mdir asks the kernel
# for a copy (F$GModDr) and reads the module headers out through F$CpyMem
# rather than walking the table where it lies. Answering those two is what
# lets it name what "load" left behind.
#
# Level 1's mdir reads the kernel's own direct page instead, and the modules
# would have to be resident in the asking process for it to find any -- so
# there the same commands print a heading over nothing, which is NAME.out.
#
# The shell and the mdir it forked are in the listing before anything is
# loaded at all, because a running program is a resident module: the kernel
# puts it in the directory when it loads it and takes the link back when the
# process ends. Two shells running would show Shell with a use count of two.
#
# The block column is a real address in the fake memory the directory is
# expressed in, so it counts from whatever is already resident -- the shell's
# own program holds the first blocks going.
#
# The banner carries the clock and its first half goes to standard output, so
# it lands in front of the first command's output; the header carries the
# clock too. Cut both away. The prompt goes to standard error, which the pipe
# below drops.
run() {
  printf "$1" | $OS9 shellplus 2>/dev/null \
    | sed -e 's/^Shell+ v[0-9.]*[a-z]* //' \
          -e 's/\(irectory at \)[0-9:]*/\1HH:MM:SS/'
}

echo "--- nothing has been loaded"
run 'mdir -e\n'

echo "--- two modules, loaded by the commands before it"
run 'load echo\nload copy\nmdir -e\n'

echo "--- the names on their own"
run 'load echo\nload copy\nmdir\n'

echo "--- and unlinked again"
run 'load echo\nunlink echo\nmdir -e\n'
