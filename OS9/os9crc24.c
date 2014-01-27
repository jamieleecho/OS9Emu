/*
    (c) 2001 Soren Roug
 
    This file is part of os9l1emu.
 
    Os9l1emu is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.
 
    Foobar is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
 
    You should have received a copy of the GNU General Public License
    along with Foobar; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/
#define CRC24_INIT 0xffffffL
#define CRC24_POLY 0x800063L

typedef long crc24;

crc24 compute_crc(unsigned long crc, unsigned char *octets, int len)
{
    int i;

    while (len--) {
        crc ^= (*octets++) << 16;
        for (i = 0; i < 8; i++) {
            crc <<= 1;
            if (crc & 0x1000000)
                crc ^= CRC24_POLY;
        }
    }
    return crc & 0xffffffL;
}

void
crc(unsigned char *start,int count,unsigned char *accum)
{
    unsigned long crc;

    crc = (accum[0] << 16) + (accum[1] << 8) + accum[2];
    crc = compute_crc(crc,start,count);
    accum[0] = (crc >> 16) & 0xff;
    accum[1] = (crc >> 8) & 0xff;
    accum[3] = crc & 0xff;
}
