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
#include <sys/stat.h>

/*
 * Resolve "." and ".." out of a path, in place when dst == path. The first
 * rootlen characters are kept verbatim and ".." never climbs above them --
 * the mount point of a host path, the device name of an OS9 one.
 */
void canonicalizePath(char *dst, const char *path, size_t rootlen,
                      size_t dstsize);

class os9dentry {
public:
    unsigned char name[28];
    unsigned char res[1];
    unsigned char lsn[3];
    
    void set(const char *the_name, int n) {
        size_t len = strlen(the_name);
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
    const char *unixdir;
    
    devunix(const char *, const char *);
    fdes *open(const char *, int, int);
    fdes *open(FILE*);
    int makdir(char *,int);
    int chdir (char *);
    int delfile(char *);
};

class fdunix: public fdes {
public:
    FILE *fp;
    int dcp;		// byte offset of our entry in the parent directory,
			// or -1 if we could not work it out (PD.DCP)
    
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
    int capacity;		// entries the array has room for
    int offset;
    int length;			// entries we serve, in bytes
    char hostdir[1024];		// the host directory these entries came from
    size_t hostroot;		// how much of it is the mount point
    struct stat dirstat;	// what it looked like when we last read it
    time_t scantime;		// and when that was, by the host clock
    int havestat;

    virtual ~fdirunix();
    fdirunix();
    int isdir() { return 1; }
    int close();
    int read(Byte *,int);
    int write(Byte *,int);
    int writeln(Byte *,int);
    int seek(int);
    int getstatus (int, statusbuf *);

    // Bring the entry array back in step with the host directory. Zero if we
    // could not look, in which case the entries are left as they were.
    int rescan();

    // Read an entry's name back out of OS9 form
    static void entryname(const os9dentry *, char *, size_t);

private:
    int stale(const struct stat *);
    void reserve(int);
};

/*
 * The "entire device" path -- /d0@ -- which OS9 uses to read a disk's raw
 * sectors. There is no disk here, only a host directory, so what this serves
 * is a plausible OS9 disk built out of what the host filesystem reports:
 * an identification sector at LSN 0 and an allocation bitmap from LSN 1.
 *
 * free(1) is what wants it, and what it wants is the capacity, the cluster
 * size and enough of a bitmap to count the free space and the largest run.
 */
class fdwhole: public fdes {
public:
    unsigned char *image;
    int length;
    int offset;

    fdwhole(const char *hostdir, const char *volname);
    virtual ~fdwhole();
    int close();
    int read(Byte *,int);
    int readln(Byte *,int);
    int write(Byte *,int);
    int writeln(Byte *,int);
    int seek(int);
    int getstatus(int, statusbuf *);
    int setstatus(int, statusbuf *);
};

class devterm: public devdrvr {
public:
    const char *device; /* The UNIX device it coresponds to -- like /dev/tty */
    
    devterm(const char *, const char *);
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
    devpipe(const char *, const char *);
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
