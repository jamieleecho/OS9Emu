# A wider pass over Basic09: strings, reals, integer arrays, the maths and
# string builtins, and both loop forms. Lines going into the editor need their
# leading space; the here-document keeps it.
cat > feat.in <<'INPUT'
e feature
 DIM a(5):INTEGER
 DIM s:STRING[20]
 DIM r:REAL
 s="Basic09"
 r=3.14159
 FOR i=1 TO 5
 a(i)=i*i
 NEXT i
 PRINT "string: "; s
 PRINT "real:   "; r
 PRINT "sqrt:   "; SQRT(16.0)
 PRINT "array:  "; a(1); a(2); a(3); a(4); a(5)
 PRINT "substr: "; MID$(s,1,5)
 PRINT "len:    "; LEN(s)
 IF r > 3 THEN
 PRINT "compare ok"
 ENDIF
 j=0
 WHILE j < 3 DO
 j=j+1
 ENDWHILE
 PRINT "while:  "; j
q
run feature
bye
INPUT
$OS9 --mem 32k --eol crlf basic09 < feat.in 2>&1 | sed -n '/^B:string/,$p'
