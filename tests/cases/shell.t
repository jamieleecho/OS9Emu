# The shell: running commands, redirecting output, reading it back.
# stderr is folded in before the pipe so the prompts stay interleaved with the
# output rather than arriving in a lump at the end.
printf 'echo one two three\recho redirected >out.txt\rlist out.txt\rdir\r' > cmds.sh
$OS9 --eol crlf shell < cmds.sh 2>&1 | sed -e 's/[0-9]\{4\}\/[0-9]\{2\}\/[0-9]\{2\}  *[0-9]\{2\}:[0-9]\{2\}/<date>/'
