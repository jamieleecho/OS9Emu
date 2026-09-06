# Basic09 "pack" writes a procedure out as an I-code module, and runb runs it
# without Basic09 in the way. This is the path that needs F$Load to actually
# load the module -- reporting its header without loading it left runb reading
# its own image and calling every procedure a compiler error.
printf 'e counter\r PRINT "counting to three"\r FOR i=1 TO 3\r PRINT i\r NEXT i\rq\rpack counter\rbye\r' > pk.in
$OS9 --mem 32k basic09 < pk.in >/dev/null 2>&1
echo "--- packed module ---"
os9 ident -s CMDS/counter | sed 's/\$[0-9A-F]*/$CRC/2'
echo "--- runb ---"
$OS9 --eol crlf runb counter
