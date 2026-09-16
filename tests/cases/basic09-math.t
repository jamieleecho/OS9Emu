# Basic09's floating point, at the values that show an exponent sum going
# wrong. A Microware real is a mantissa in [0.5, 1) and an excess-128
# exponent byte, and a multiply adds the two exponents -- so .4 (exponent
# $7F) times 1.0 ($81) is the case where that addition carries out of bit 7
# while nothing carries into it, and a mis-set overflow flag turns the
# product into zero. SIN, COS and ATN reach the same addition for a small
# argument and used to flush to zero with it; the answers below are stock
# OS-9's.
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
 PRINT "pow:    "; 0.1^2.0
 PRINT "int:    "; INT(0.1*10.0)
q
run math
bye
INPUT
$OS9 --mem 32k --eol crlf basic09 < math.in 2>&1 | sed -n '/^B:sin.1/,$p'
