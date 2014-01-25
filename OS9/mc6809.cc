//
//	mc6809.cc
//
//	(C) R.P.Bellis
//

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "machdep.h"
#include "usim.h"
#include "mc6809.h"

static const char *const irmodes[] = {
	"#",
	"",
	"",
	"",
	""
	};
static const char *const mnemonics[] = {
//    0       1       2       3        4       5       6       7 
    "NEG",  "",     "",     "COM",  "LSR",  "",     "ROR",  "ASR",
    "ASL",  "ROL",  "DEC",  "",     "INC",  "TST",  "JMP",  "CLR",
    /*10*/
    "",     "",     "NOP",  "SYNC",  "",    "",     "LBRA", "LBSR",
    "",     "DAN",  "",     "",     "",     "SEX",  "EXG",  "TFR",
    /*20*/
    "BRA",  "BRN",  "BHI",  "BLS",  "BCC",  "BCS",  "BNE",  "BEQ",
    "BVC",  "BVS",  "BPL",  "BMI",  "BGE",  "BLT",  "BGT",  "BLE",
    /*30*/
    "LEAX", "LEAY", "LEAS", "LEAU", "PSHS", "PULS", "PSHU", "PULU",
    "",     "RTS",  "ABX",  "RTI",  "CWAI", "MUL",  "",     "SWI1",
    /*40*/
    "NEGA", "",     "",     "COMA", "LSRA", "",     "RORA", "ASRA",
    "ASLA", "ROLA", "DECA", "",     "INCA", "TSTA", "",     "CLRA",
    /*50*/
    "NEGB", "",     "",     "COMB", "LSRB", "",     "RORB", "ASRB",
    "ASLB", "ROLB", "DECB", "",     "INCB", "TSTB", "",     "CLRB",
    /*60*/
    "NEG",  "",     "",     "COM",  "LSR",  "",     "ROR",  "ASR",
    "ASL",  "ROL",  "DEC",  "",     "INC",  "TST",  "JMP",  "CLR",
    /*70*/
    "NEG",  "",     "",     "COM",  "LSR",  "",     "ROR",  "ASR",
    "ASL",  "ROL",  "DEC",  "",     "INC",  "TST",  "JMP",  "CLR",
    /*80*/
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA",  "",
    "EORA", "ADCA", "ORA",  "ADDA", "CMPX", "BSR",  "LDX",  "",
    /*90*/
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA",  "STA",
    "EORA", "ADCA", "ORA",  "ADDA", "CMPX", "JSR",  "LDX",  "STX",
    /*A0*/
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA",  "STA",
    "EORA", "ADCA", "ORA",  "ADDA", "CMPX", "JSR",  "LDX",  "STX",
    /*B0*/
    "SUBA", "CMPA", "SBCA", "SUBD", "ANDA", "BITA", "LDA",  "STA",
    "EORA", "ADCA", "ORA",  "ADDA", "CMPX", "JSR",  "LDX",  "STX",
    /*C0*/
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB",  "",
    "EORB", "ADCB", "ORB",  "ADDB", "LDD",  "",     "LDU",  "",
    /*D0*/
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB",  "STB",
    "EORB", "ADCB", "ORB",  "ADDB", "LDD"  ,"STD",  "LDU",  "STU",
    /*E0*/
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB",  "STB",
    "EORB", "ADCB", "ORB",  "ADDB", "LDD"  ,"STD",  "LDU",  "STU",
    /*F0*/
    "SUBB", "CMPB", "SBCB", "ADDD", "ANDB", "BITB", "LDB",  "STB",
    "EORB", "ADCB", "ORB",  "ADDB", "LDD"  ,"STD",  "LDU",  "STU",
    ""
    };
static const char *const mnemonics10[] = {
//    0       1       2       3        4       5       6       7 
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*10*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*20*/
    "", "LBRN", "LBHI", "LBLS", "LBCC", "LBCS", "LBNE", "LBEQ",
    "LBVC", "LBVS", "LBPL", "LBMI", "LBGE", "LBLT", "LBGT", "LBLE",
    /*30*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "SWI2",
    /*40*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*50*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*60*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*70*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "",  "",
    /*80*/
    "",  "",  "",  "CMPD",  "",  "",  "",  "",
    "",  "",  "",  "",  "CMPY",  "",  "LDY",  "",
    /*90*/
    "",  "",  "",  "CMPD",  "",  "",  "",  "",
    "",  "",  "",  "",  "CMPY",  "",  "LDY",  "STY",
    /*A0*/
    "",  "",  "",  "CMPD",  "",  "",  "",  "",
    "",  "",  "",  "",  "CMPY",  "",  "LDY",  "STY",
    /*B0*/
    "",  "",  "",  "CMPD",  "",  "",  "",  "",
    "",  "",  "",  "",  "CMPY",  "",  "LDY",  "STY",
    /*C0*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "LDS",  "",
    /*D0*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "LDS",  "STS",
    /*E0*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "LDS",  "STS",
    /*F0*/
    "",  "",  "",  "",  "",  "",  "",  "",
    "",  "",  "",  "",  "",  "",  "LDS",  "STS",
    ""
    };

mc6809::mc6809()
    : a(acc.byte.a), b(acc.byte.b), d(acc.d), debug_6809(0)
{
	memory = new Byte[0x10000L];
	reset();
}

mc6809::~mc6809()
{
	delete[] memory;
}

void mc6809::reset(void)
{
	pc = read_word(0xfffe);
	dp = 0x00;		/* Direct page register = 0x00 */
	cc.all = 0x00;		/* Clear all flags */
	cc.bit.i = 1;		/* IRQ disabled */
	cc.bit.f = 1;		/* FIRQ disabled */
}

void mc6809::status(void)
{
}

void mc6809::setdebug(int newval)
{
    debug_6809 = newval;
}

void mc6809::execute(void)
{
	ir = fetch();

	/* Select addressing mode */
	switch (ir & 0xf0) {
		case 0x00: case 0x90: case 0xd0:
			mode = direct; break;
		case 0x20:
			mode = relative; break;
		case 0x30: case 0x40: case 0x50:
			if (ir < 0x34) {
				mode = indexed;
			} else if (ir < 0x38) {
				mode = immediate;
			} else {
				mode = inherent;
			}
			break;
		case 0x60: case 0xa0: case 0xe0:
			mode = indexed; break;
		case 0x70: case 0xb0: case 0xf0:
			mode = extended; break;
		case 0x80: case 0xc0:
			if (ir == 0x8d) {
				mode = relative;
			} else {
				mode = immediate;
			}
			break;
		case 0x10:
			switch (ir & 0x0f) {
				case 0x02: case 0x03: case 0x09:
				case 0x0d: case 0x0e: case 0x0f:
					mode = inherent; break;
				case 0x06: case 0x07:
					mode = relative; break;
				case 0x0a: case 0x0c:
					mode = immediate; break;
				case 0x00: case 0x01:
					ir <<= 8;
					ir |= fetch();
					switch (ir & 0xf0) {
						case 0x20:
							mode = relative; break;
						case 0x30:
							mode = inherent; break;
						case 0x80: case 0xc0:
							mode = immediate; break;
						case 0x90: case 0xd0:
							mode = direct; break;
						case 0xa0: case 0xe0:
							mode = indexed; break;
						case 0xb0: case 0xf0:
							mode = extended; break;
					}
					break;
			}
			break;
	}

	if(debug_6809)
	{
	    if (ir < 256)
	       fprintf(stderr,"%04X:   %02X %5s  ",pc-1,ir,mnemonics[ir]);
	    else if (ir == 0x103F)
	       fprintf(stderr,"%04X: %04X OS9 %02x ",pc-2,ir,memory[pc]);
	    else if (ir < 0x1100)
	       fprintf(stderr,"%04X: %04X %5s  ",pc-2,ir,mnemonics10[ir&0xff]);
	    else
	       fprintf(stderr,"%04X: %04X ----  ",pc-2,ir);
	    switch(mode)
	    {
	    case extended:
	       fprintf(stderr,"Ex ");
	       break;
	    case relative:
	       fprintf(stderr,"Re ");
	       break;
	    case direct:
	       fprintf(stderr,"Di ");
	       break;
	    case indexed:
	       fprintf(stderr,"In ");
	       break;
	    }
	}


	/* Select instruction */
	switch (ir) {
		case 0x3a:
			abx(); break;
		case 0x89: case 0x99: case 0xa9: case 0xb9:
			adca(); break;
		case 0xc9: case 0xd9: case 0xe9: case 0xf9:
			adcb(); break;
		case 0x8b: case 0x9b: case 0xab: case 0xbb:
			adda(); break;
		case 0xcb: case 0xdb: case 0xeb: case 0xfb:
			addb(); break;
		case 0xc3: case 0xd3: case 0xe3: case 0xf3:
			addd(); break;
		case 0x84: case 0x94: case 0xa4: case 0xb4:
			anda(); break;
		case 0xc4: case 0xd4: case 0xe4: case 0xf4:
			andb(); break;
		case 0x1c:
			andcc(); break;
		case 0x47:
			asra(); break;
		case 0x57:
			asrb(); break;
		case 0x07: case 0x67: case 0x77:
			asr(); break;
		case 0x24:
			bcc(); break;
		case 0x25:
			bcs(); break;
		case 0x27:
			beq(); break;
		case 0x2c:
			bge(); break;
		case 0x2e:
			bgt(); break;
		case 0x22:
			bhi(); break;
		case 0x85: case 0x95: case 0xa5: case 0xb5:
			bita(); break;
		case 0xc5: case 0xd5: case 0xe5: case 0xf5:
			bitb(); break;
		case 0x2f:
			ble(); break;
		case 0x23:
			bls(); break;
		case 0x2d:
			blt(); break;
		case 0x2b:
			bmi(); break;
		case 0x26:
			bne(); break;
		case 0x2a:
			bpl(); break;
		case 0x20:
			bra(); break;
		case 0x16:
			lbra(); break;
		case 0x21:
			brn(); break;
		case 0x8d:
			bsr(); break;
		case 0x17:
			lbsr(); break;
		case 0x28:
			bvc(); break;
		case 0x29:
			bvs(); break;
		case 0x4f:
			clra(); break;
		case 0x5f:
			clrb(); break;
		case 0x0f: case 0x6f: case 0x7f:
			clr(); break;
		case 0x81: case 0x91: case 0xa1: case 0xb1:
			cmpa(); break;
		case 0xc1: case 0xd1: case 0xe1: case 0xf1:
			cmpb(); break;
		case 0x1083: case 0x1093: case 0x10a3: case 0x10b3:
			cmpd(); break;
		case 0x118c: case 0x119c: case 0x11ac: case 0x11bc:
			cmps(); break;
		case 0x8c: case 0x9c: case 0xac: case 0xbc:
			cmpx(); break;
		case 0x1183: case 0x1193: case 0x11a3: case 0x11b3:
			cmpu(); break;
		case 0x108c: case 0x109c: case 0x10ac: case 0x10bc:
			cmpy(); break;
		case 0x43:
			coma(); break;
		case 0x53:
			comb(); break;
		case 0x03: case 0x63: case 0x73:
			com(); break;
		case 0x19:
			daa(); break;
		case 0x4a:
			deca(); break;
		case 0x5a:
			decb(); break;
		case 0x0a: case 0x6a: case 0x7a:
			dec(); break;
		case 0x88: case 0x98: case 0xa8: case 0xb8:
			eora(); break;
		case 0xc8: case 0xd8: case 0xe8: case 0xf8:
			eorb(); break;
		case 0x1e:
			exg(); break;
		case 0x4c:
			inca(); break;
		case 0x5c:
			incb(); break;
		case 0x0c: case 0x6c: case 0x7c:
			inc(); break;
		case 0x0e: case 0x6e: case 0x7e:
			jmp(); break;
		case 0x9d: case 0xad: case 0xbd:
			jsr(); break;
		case 0x86: case 0x96: case 0xa6: case 0xb6:
			lda(); break;
		case 0xc6: case 0xd6: case 0xe6: case 0xf6:
			ldb(); break;
		case 0xcc: case 0xdc: case 0xec: case 0xfc:
			ldd(); break;
		case 0x10ce: case 0x10de: case 0x10ee: case 0x10fe:
			lds(); break;
		case 0xce: case 0xde: case 0xee: case 0xfe:
			ldu(); break;
		case 0x8e: case 0x9e: case 0xae: case 0xbe:
			ldx(); break;
		case 0x108e: case 0x109e: case 0x10ae: case 0x10be:
			ldy(); break;
		case 0x32:
			leas(); break;
		case 0x33:
			leau(); break;
		case 0x30:
			leax(); break;
		case 0x31:
			leay(); break;
		case 0x48:
			lsla(); break;
		case 0x58:
			lslb(); break;
		case 0x08: case 0x68: case 0x78:
			lsl(); break;
		case 0x44:
			lsra(); break;
		case 0x54:
			lsrb(); break;
		case 0x04: case 0x64: case 0x74:
			lsr(); break;
		case 0x3d:
			mul(); break;
		case 0x40:
			nega(); break;
		case 0x50:
			negb(); break;
		case 0x00: case 0x60: case 0x70:
			neg(); break;
		case 0x12:
			nop(); break;
		case 0x8a: case 0x9a: case 0xaa: case 0xba:
			ora(); break;
		case 0xca: case 0xda: case 0xea: case 0xfa:
			orb(); break;
		case 0x1a:
			orcc(); break;
		case 0x34:
			pshs(); break;
		case 0x36:
			pshu(); break;
		case 0x35:
			puls(); break;
		case 0x37:
			pulu(); break;
		case 0x49:
			rola(); break;	
		case 0x59:
			rolb(); break;
		case 0x09: case 0x69: case 0x79:
			rol(); break;
		case 0x46:
			rora(); break;	
		case 0x56:
			rorb(); break;
		case 0x06: case 0x66: case 0x76:
			ror(); break;
		case 0x3b:
			rti(); break;
		case 0x39:
			rts(); break;
		case 0x82: case 0x92: case 0xa2: case 0xb2: 
			sbca(); break;
		case 0xc2: case 0xd2: case 0xe2: case 0xf2: 
			sbcb(); break;
		case 0x1d:
			sex(); break;
		case 0x97: case 0xa7: case 0xb7:
			sta(); break;
		case 0xd7: case 0xe7: case 0xf7:
			stb(); break;
		case 0xdd: case 0xed: case 0xfd:
			std(); break;
		case 0x10df: case 0x10ef: case 0x10ff:
			sts(); break;
		case 0xdf: case 0xef: case 0xff:
			stu(); break;
		case 0x9f: case 0xaf: case 0xbf:
			stx(); break;
		case 0x109f: case 0x10af: case 0x10bf:
			sty(); break;
		case 0x80: case 0x90: case 0xa0: case 0xb0:
			suba(); break;
		case 0xc0: case 0xd0: case 0xe0: case 0xf0:
			subb(); break;
		case 0x83: case 0x93: case 0xa3: case 0xb3:
			subd(); break;
		case 0x3f:
			swi(); break;
		case 0x103f:
			swi2(); break;
		case 0x113f:
			swi3(); break;
		case 0x1f:
			tfr(); break;
		case 0x4d:
			tsta(); break;
		case 0x5d:
			tstb(); break;
		case 0x0d: case 0x6d: case 0x7d:
			tst(); break;
		case 0x1024:
			lbcc(); break;
		case 0x1025:
			lbcs(); break;
		case 0x1027:
			lbeq(); break;
		case 0x102c:
			lbge(); break;
		case 0x102e:
			lbgt(); break;
		case 0x1022:
			lbhi(); break;
		case 0x102f:
			lble(); break;
		case 0x1023:
			lbls(); break;
		case 0x102d:
			lblt(); break;
		case 0x102b:
			lbmi(); break;
		case 0x1026:
			lbne(); break;
		case 0x102a:
			lbpl(); break;
		case 0x1021:
			lbrn(); break;
		case 0x1028:
			lbvc(); break;
		case 0x1029:
			lbvs(); break;
		default:
			invalid("instruction"); break;
	}


	if(debug_6809)
	{
	      fprintf(stderr,"\t0x%04x 0x%04x 0x%04x 0x%02x 0x%02x ",
	     u, x, y, a, b);
      if (cc.bit.h) {
        fprintf(stderr,"H");
      } else {
        fprintf(stderr,"-");
      }
      if (cc.bit.n) {
        fprintf(stderr,"N");
      } else {
        fprintf(stderr,"-");
      }
      if (cc.bit.z) {
        fprintf(stderr,"Z");
      } else {
        fprintf(stderr,"-");
      }
      if (cc.bit.v) {
        fprintf(stderr,"V");
      } else {
        fprintf(stderr,"-");
      }
      if (cc.bit.c) {
        fprintf(stderr,"C");
      } else {
        fprintf(stderr,"-");
      }
        fprintf(stderr,"\n");
 
	}

}

Word& mc6809::refreg(Byte post)
{
	post &= 0x60;
	post >>= 5;

	if (post == 0) {
        if (debug_6809)
            fprintf(stderr, "x");
		return x;
	} else if (post == 1) {
        if (debug_6809)
            fprintf(stderr, "y");
		return y;
	} else if (post == 2) {
        if (debug_6809)
            fprintf(stderr, "u");
		return u;
	} else {
        if (debug_6809)
            fprintf(stderr, "s");
		return s;
	}
}

Byte& mc6809::byterefreg(int r)
{
	if (r == 0x08) {
		return a;
	} else if (r == 0x09) {
		return b;
	} else if (r == 0x0a) {
		return cc.all;
	} else {
		return dp;
	}
}

Word& mc6809::wordrefreg(int r)
{
	if (r == 0x00) {
		return d;
	} else if (r == 0x01) {
		return x;
	} else if (r == 0x02) {
		return y;
	} else if (r == 0x03) {
		return u;
	} else if (r == 0x04) {
		return s;
	} else {
		return pc;
	}
}

Byte mc6809::fetch_operand(void)
{
	Byte		ret = 0;
	Word		addr;

	if (mode == immediate) {
		ret = fetch();
        if (debug_6809)
            fprintf(stderr, "#0x%04x", ret);
	} else if (mode == relative) {
		ret = fetch();
	} else if (mode == extended) {
		addr = fetch_word();
		ret = read(addr);
	} else if (mode == direct) {
		addr = ((Word)dp << 8) | fetch();
		ret = read(addr);
	} else if (mode == indexed) {
		Byte		post = fetch();
		do_predecrement(post);
		addr = do_effective_address(post);
		ret = read(addr);
		do_postincrement(post);
	} else {
		invalid("addressing mode");
	}

	return ret;
}

Word mc6809::fetch_word_operand(void)
{
	Word		addr, ret = 0;

	if (mode == immediate) {
		ret = fetch_word();
	} else if (mode == relative) {
		ret = fetch_word();
	} else if (mode == extended) {
		addr = fetch_word();
		ret = read_word(addr);
	} else if (mode == direct) {
		addr = (Word)dp << 8 | fetch();
		ret = read_word(addr);
	} else if (mode == indexed) {
		Byte	post = fetch();
		do_predecrement(post);
		addr = do_effective_address(post);
		do_postincrement(post);
		ret = read_word(addr);
	} else {
		invalid("addressing mode");
	}

	return ret;
}

Word mc6809::fetch_effective_address(void)
{
	Word		addr = 0;

	if (mode == extended) {
		addr = fetch_word();
        if (debug_6809)
            fprintf(stderr, "0x%04x    ", addr);
	} else if (mode == direct) {
		addr = (Word)dp << 8 | fetch();
        if (debug_6809)
            fprintf(stderr, "0x%04x    ", addr);
	} else if (mode == indexed) {
		Byte		post = fetch();
		do_predecrement(post);
		addr = do_effective_address(post);
		do_postincrement(post);
	} else {
		invalid("addressing mode");
	}

	return addr;
}

Word mc6809::do_effective_address(Byte post)
{
	Word		addr = 0;

	if ((post & 0x80) == 0x00) {
		addr = refreg(post) + extend5(post & 0x1f);
	} else {
        Word tmp;
		switch (post & 0x1f) {
			case 0x00: case 0x02:
				addr = refreg(post);
				break;
			case 0x01: case 0x03: case 0x11: case 0x13:
				addr = refreg(post);
				break;
			case 0x04: case 0x14:
				addr = refreg(post);
				break;
			case 0x05: case 0x15:
				addr = (SByte)b + refreg(post);
				break;
			case 0x06: case 0x16:
				addr = (SByte)a + refreg(post);
				break;
			case 0x08: case 0x18:
                tmp = extend8(fetch());
                if (debug_6809)
                    fprintf(stderr, "0x%04x,", tmp);
				addr = refreg(post) + tmp;
				break;
			case 0x09: case 0x19:
				addr = refreg(post) + (SWord)fetch_word();
				break;
			case 0x0b: case 0x1b:
				addr = (SWord)d + refreg(post);
				break;
			case 0x0c: case 0x1c:
                addr = extend8(fetch());
				addr = addr + pc;
				break;
			case 0x0d: case 0x1d:
                addr = (SWord)fetch_word();
				addr = addr + pc;
				break;
			case 0x1f:
				addr = fetch_word();
				break;
			default:
				invalid("indirect addressing postbyte");
				break;
		}

		/* Do extra indirection */
		if (post & 0x10) {
			addr = read_word(addr);
		}
	}

	return addr;
}

void mc6809::do_postincrement(Byte post)
{
	switch (post & 0x9f) {
		case 0x80:
            if (debug_6809)
                fprintf(stderr, ",");
			refreg(post) += 1;
            if (debug_6809)
                fprintf(stderr, "+");
			break;
		case 0x90:
			invalid("postincrement");
			break;
		case 0x81: case 0x91:
            if (debug_6809)
                fprintf(stderr, ",");
			refreg(post) += 2;
            if (debug_6809)
            fprintf(stderr, "+");
                break;
	}
}

void mc6809::do_predecrement(Byte post)
{
	switch (post & 0x9f) {
		case 0x82:
            if (debug_6809)
                fprintf(stderr, ",");
			refreg(post) -= 1;
            if (debug_6809)
                fprintf(stderr, "-");
			break;
		case 0x92:
			invalid("predecrement");
			break;
		case 0x83: case 0x93:
            if (debug_6809)
                fprintf(stderr, ",");
			refreg(post) -= 2;
            if (debug_6809)
                fprintf(stderr, "-");
			break;
	}
}
