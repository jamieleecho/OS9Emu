# Basic09's floating point, at the values that caught an 8-bit ADD setting V
# to the carry into bit 7 instead of the carry in xor the carry out. V is
# half of every signed branch, not just BVS, so the damage lands anywhere the
# float routines compare -- and it does not reduce to a rule about the value:
# .1*1.0 was always right where .1*4.0 was zero.
#
# All three shapes the bad V produced are here: a product of zero (mul1..4),
# a loss of precision (prec, which gave .00 for .06), and a multiply that
# never returned at all (hang, which looped forever). A regression in the
# last one shows up as this case timing out rather than diffing.
#
# The answers are stock OS-9's.
cat > math.in <<'INPUT'
e math
 PRINT "sin.1:  "; SIN(0.1)
 PRINT "cos.1:  "; COS(0.1)
 PRINT "sin3:   "; SIN(3.0)
 PRINT "atn.1:  "; ATN(0.1)
 PRINT "exp.1:  "; EXP(0.1)
 PRINT "log2:   "; LOG(2.0)
 PRINT "sqr2:   "; SQR(2.0)
 PRINT "mul1:   "; 0.4*1.0
 PRINT "mul2:   "; 0.25*4.0
 PRINT "mul3:   "; 0.4*2.0
 PRINT "mul4:   "; 0.1*4.0
 PRINT "hang:   "; 0.1*0.1
 PRINT "prec:   "; 0.1*0.6
 PRINT "pow:    "; 0.1^2.0
 PRINT "int:    "; INT(0.1*10.0)
q
run math
bye
INPUT
$OS9 --mem 32k --eol crlf basic09 < math.in 2>&1 | sed -n '/^B:sin.1/,$p'
