# What a Level 2 utility is handed instead of the process table.
#
# An OS-9 process here is a host process, so a process id has to mean the same
# thing to every one of them or F$Send cannot reach anybody and procs has
# nothing to list. The table is shared the way the module directory is, and
# procs asks the kernel for a row at a time (F$GPrDsc) rather than walking to
# one, reading the primary module's name through F$CpyMem exactly as mdir
# reads a module's.
#
# mfree is the same call procs takes its block size from -- F$GBlkMp -- and
# the memory it reports is the fake one the module directory is expressed in:
# the room left for modules and for the programs processes are running, not
# the room left in anybody's 64K.
#
# Level 1's procs reads the kernel's own direct page instead, so there the
# same commands print a heading over nothing; that is NAME.out.
#
# The user number is the host's, which is whoever ran the suite, so the column
# it fills is folded away: seven characters wide, starting at the ninth. Only
# a process row has one, and a process row is the one with a status byte on
# it -- mfree's rows start with numbers in the same shape and must be left
# alone.
#
# The banner carries the clock and its first half goes to standard output, so
# it lands in front of the first command's output. Cut it away. The prompt
# goes to standard error, which the pipe below drops.
run() {
  printf "$1" | $OS9 shellplus 2>/dev/null | tr '\r' '\n' \
    | sed -E -e 's/^Shell\+ v[0-9.]*[a-z]* //' \
             -e '/^ *[0-9]+ +[0-9]+ .*\$[0-9A-F][0-9A-F] /s/^(.{8}).{7}/\1  UID  /'
}

echo "--- the shell, and the command it forked to ask"
run 'procs\n'

echo "--- how much of the memory the modules live in is left"
run 'mfree\n'
