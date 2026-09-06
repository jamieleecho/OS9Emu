# The headline case: compile a C program with the Microware compiler built
# from NitrOS-9 sources, then run what came out.
printf '#include <stdio.h>\r\rmain()\r{\r  int i;\r  for (i = 1; i <= 3; i++)\r    printf("%%d squared is %%d\\n", i, i*i);\r}\r' > squares.c
$OS9 cc1 squares.c
echo "--- ident ---"
os9 ident -s CMDS/squares | sed 's/\$[0-9A-F]*/\$CRC/2'
echo "--- run ---"
$OS9 --eol crlf squares
