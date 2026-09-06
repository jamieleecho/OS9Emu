# The shell: running commands, redirecting output, reading it back.
# stderr is folded in before the pipe so the prompts stay interleaved with the
# output rather than arriving in a lump at the end.
#
# Which shell is installed depends on the root -- shell_21 on a Level 1 one,
# shellplus on a Level 2 one -- and the two announce themselves differently:
# "Shell" against "Shell+ v2.2a" and the clock, "OS9:" against "{term|01}/dd:".
# This case is about what the shell does, so both are folded to Level 1's.
printf 'echo one two three\recho redirected >out.txt\rlist out.txt\rdir\r' > cmds.sh
$OS9 --eol crlf shell < cmds.sh 2>&1 \
  | sed -e 's/^Shell+ v[0-9.]*[a-z]* [0-9][0-9]\/[0-9][0-9]\/[0-9][0-9] [0-9:]*/Shell/' \
        -e 's/{term|[0-9a-z]*}[^:]*:/OS9:/g' \
        -e 's/[0-9]\{4\}\/[0-9]\{2\}\/[0-9]\{2\}  *[0-9]\{2\}:[0-9]\{2\}/<date>/'
