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
#include "typedefs.h"

class devdrvr;  // Forward declaration

typedef union {
    size_t filesize;
    int status;
    unsigned char filler[32];
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
    virtual int seek(int);
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
