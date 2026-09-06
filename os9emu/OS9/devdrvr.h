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
#include <stddef.h>

#include "typedefs.h"

class devdrvr;  // Forward declaration

/*
 * I$GetStt / I$SetStt function codes, from NitrOS-9 defs/os9.d. Only the ones
 * a TTY system actually sees are named; anything else reaches the driver as a
 * number and comes back as E_UnkSvc, which is what a real driver does.
 */
enum {
    SS_Opt    = 0x00,	// read/write the 32-byte path descriptor options
    SS_Ready   = 0x01,	// is there input waiting?
    SS_Size    = 0x02,	// file size
    SS_Reset   = 0x03,
    SS_Pos     = 0x05,	// current file position
    SS_EOF     = 0x06,	// at end of file?
    SS_Feed    = 0x09,
    SS_Frz     = 0x0a,
    SS_DevNm   = 0x0e,	// device name, 32 bytes at X
    SS_FD      = 0x0f,	// file descriptor sector, Y bytes at X
    SS_Ticks   = 0x10,
    SS_Lock    = 0x11,
    SS_DStat   = 0x12,
    SS_SSig    = 0x1a,	// send a signal when input arrives
    SS_Relea   = 0x1b,	// release the device from SS_SSig
    SS_Attr    = 0x1c,	// file attributes
    SS_Break   = 0x1d,
    SS_FDInf   = 0x20,	// file descriptor sector of any file on the device,
			// named by its number: Y = high byte of the number
			// and a byte count, U = the low two bytes, X = buffer
    SS_DirEnt  = 0x21,
    SS_Cursr   = 0x25,
    SS_ScSiz   = 0x26,	// screen size: X = columns, Y = rows
    SS_KySns   = 0x27,
    SS_ComSt   = 0x28,	// baud/parity
    SS_Open    = 0x29,	// a path was opened
    SS_Close   = 0x2a,	// a path was closed
    SS_HngUp   = 0x2b,
};

/*
 * Whatever a status call needs to hand back. This was a union, which meant
 * SS_Opt and SS_Size wrote over each other; the fields a caller does not set
 * now simply stay zero. `filler` is a full sector because SS_FD returns up to
 * one, not the 32 bytes SS_Opt and SS_DevNm use.
 */
typedef struct {
    size_t filesize;			// SS_Size, SS_Pos
    int status;				// SS_EOF, SS_Ready
    int cols, rows;			// SS_ScSiz
    unsigned long lsn;			// SS_FDInf, on the way in
    unsigned char filler[256];		// SS_Opt, SS_DevNm, SS_FD, SS_FDInf
} statusbuf;

/*
 * File descriptor
 * The actual data are in the derived classes
 */
class fdes {
public:
    devdrvr *driver;
    int usecount;
    int errorcode;
    fdes();
    virtual ~fdes();
    virtual int close();
    virtual int read(Byte *,int) = 0;
    virtual int readln(Byte *,int) = 0;
    virtual int getstatus(int,statusbuf *);
    virtual int setstatus(int,statusbuf *);
    void devname(statusbuf *);		// SS_DevNm, the same for every device
    virtual int seek(int);
    virtual int isdir() { return 0; }	// only a directory path says yes
    virtual int write(Byte *,int) = 0;
    virtual int writeln(Byte *,int) = 0;
};

// The devdrvr class is a base class for virtual devices
// The methods expect pathnames that are relative to the mount pount
// and with out leading slash.

class devdrvr {
public:
    static char *type;
    char *mntpoint;
    int errorcode;
    
    devdrvr(const char *);                 // Constructor
    virtual fdes *open(const char *,int,int);
    virtual int makdir(char *,int);
    virtual int chdir(char *);
    virtual int close(fdes *);
    virtual int delfile(char *) { return 203; };
};

class fdnull : public fdes  {
public:
    virtual int read(Byte *buf,int len) { return 0; }
    virtual int readln(Byte * buf,int len)  { return 0; }
    virtual int write(Byte *buf,int len) { return len; }
    virtual int writeln(Byte *buf,int len) { return len; }
};


// Coresponds to /dev/null in UNIX
class devnull : public devdrvr {
public:
    devnull(char *);                 // Constructor
    fdes *open(const char *,int,int);
};
