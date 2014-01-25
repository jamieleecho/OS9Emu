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
// The devdrvr class is a base class for virtual devices
// The methods expect pathnames that are relative to the mount pount
// and with out leading slash.

#include <string.h>

class os9dentry {
 public:
  unsigned char name[28];
  unsigned char res[1];
  unsigned char lsn[3];

  void set(char *the_name, int n) {
    int len = strlen(the_name);
    len = ((size_t)len <= sizeof(name)) ? len : sizeof(name);
    strncpy((char *)name, the_name, sizeof(name));

    name[len-1] = 0x80 | name[len-1];
    res[0] = 0x0;
    lsn[0] = (n & 0xff0000) >> 16;
    lsn[1] = (n & 0xff00) >> 8;
    lsn[2] = n & 0xff;
  }
};

class devunix: public devdrvr {
public:
    char *unixdir;

    devunix(char *,char *);
    fdes *open(const char *,int,int);
    fdes *open(FILE*);
    int makdir(char *,int);
    int chdir (char *);
    int delfile(char *);
};

class fdunix: public fdes {
public:
    FILE *fp;

    virtual ~fdunix();
    fdunix();
    int close();
    int read(Byte *,int);
    int readln(Byte *,int);
    int write(Byte *,int);
    int writeln(Byte *,int);
    int seek(int);
    int getstatus (int, statusbuf *);
    int setstatus (int, statusbuf *);
};

class fdirunix: public fdunix {
public:
    os9dentry *dentries;
    int offset;
    int length;

    virtual ~fdirunix();
    fdirunix();
    int close();
    int read(Byte *,int);
    int getstatus (int, statusbuf *);
};

class devterm: public devdrvr {
public:
    char *device; /* The UNIX device it coresponds to -- like /dev/tty */

    devterm(char *,char *);
    fdes *open(const char *,int,int);
    fdes *open(FILE*);
};

class fdterm: public fdes {
public:
    FILE *fp;
    virtual ~fdterm();
    fdterm();
    fdterm(FILE *);
    int close();
    int read(Byte *,int);
    int readln(Byte *,int);
    int write(Byte *,int);
    int writeln(Byte *,int);
    int seek(int);
    int getstatus (int, statusbuf *);
    int setstatus (int, statusbuf *);
};

/*
 * Implementation of OS9 pipes
 */
class devpipe: public devdrvr {
public:
    devpipe(char *,char *);
    fdes *open(const char *,int,int);
    fdes *open(FILE*);
};

class fdpipe: public fdes {
public:
    int filedes[2];
    FILE *ifp;
    virtual ~fdpipe();
    fdpipe();
    int close();
    int read(Byte *,int);
    int readln(Byte *,int);
    int write(Byte *,int);
    int writeln(Byte *,int);
    int getstatus (int, statusbuf *);
    int setstatus (int, statusbuf *);
};
