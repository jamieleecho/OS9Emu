# Basic09: enter a procedure in the editor, list it, run it.
# In the Basic09 editor a source line is introduced by a leading space -- that
# is what the editor's command table maps to "insert".
#
# Basic09 writes its prompts and listings to standard error and the running
# program's output to standard output, so both are folded together here.
printf 'e demo\r PRINT "counting"\r FOR i=1 TO 3\r PRINT i, i*i\r NEXT i\rq\rlist demo\rrun demo\rbye\r' > demo.in
$OS9 --eol crlf basic09 < demo.in 2>&1 | sed -n '/^Basic09/,$p'
