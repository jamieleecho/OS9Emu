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
extern "C" {
#include <string.h>
#include <stdio.h>
  //#include <malloc.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
}
#include "devdrvr.h"
#include "devunix.h"

static int debug_syscall = 0;

static int
u2o_attr(int umode)
{
    int omode = 0;
 
    if(umode & S_IRUSR) omode |= 1;
    if(umode & S_IWUSR) omode |= 2;
    if(umode & S_IXUSR) omode |= 4;
    if(umode & S_IRGRP) omode |= 8;
    if(umode & S_IWGRP) omode |= 16;
    if(umode & S_IXGRP) omode |= 32;
    if(umode & S_IROTH) omode |= 8;
    if(umode & S_IWOTH) omode |= 16;
    if(umode & S_IXOTH) omode |= 32;
    if(S_ISDIR(umode))  omode |=128;
    return omode;
}
 
static int
o2u_attr(int omode)
{
    int umode = 0;
    if(omode & 1)  umode |= S_IRUSR;
    if(omode & 2)  umode |= S_IWUSR;
    if(omode & 4)  umode |= S_IXUSR;
    if(omode & 8)  umode |= (S_IRGRP|S_IROTH);
    if(omode & 16) umode |= (S_IWGRP|S_IWOTH);
    if(omode & 32) umode |= (S_IXGRP|S_IXOTH);
    return umode;
}

/*
 * Return the real file name of the segment or NULL
 * You can then append the segment to dir and try again
 */
static char *findpathseg(char *dir,char *segment)
{
    DIR *dirp;
    struct dirent *dp;
 
    dirp = opendir(dir);
    while((dp = readdir(dirp)))
    {
        if(strcasecmp(dp->d_name,segment) == 0)
            break;
    }
    closedir(dirp);
    if(dp)
        return dp->d_name;
    else
        return NULL;
}

static char *findpath(char *path,bool mustexist)
{
    char *endp,*endseg,*begseg;
    char *dirp,*nseg;
 
    endp = path + strlen(path);
 
    if(*path == '/')
    {
        dirp = "/";
        begseg = path + 1;
    }
    else
    {
        dirp = ".";
        begseg = path;
    }
 
    do {
        endseg=strchr(begseg,'/');
        if(endseg == NULL)
            endseg = endp;
        *endseg = '\0';
        nseg = findpathseg(dirp,begseg);
        if(endseg != endp && !nseg)
            return NULL;
        if(nseg)
            strcpy(begseg,nseg);
        if(dirp == path)
           begseg[-1] = '/';
        dirp = path;
        begseg = endseg +1;
    } while(endseg != endp);
    if(mustexist && !nseg)
        return NULL;
    return path;
}

/* Canonicalizes a filename. */
void canonicalizePath(char *dst, char *path) {
  int dotCount = 0, slashCount = 0;
  int jj=0;
  bool foundEnd = false;
  int partSize = 0;
  bool dotStarted = false;
  for (int ii=0; !foundEnd; ii++) {
    char c = path[ii];
    foundEnd = (c == '\0');
    
    // Dots are special. Handle multiple dots here
    if (c != '.' && (dotCount > 0)) {
      // We encountered two dots, so remove the previous filename component
      if ((dotCount <= 2) && dotStarted) {
	jj--;
	int slashesLeft = (dotCount == 2) ? 2 : 1;
	for (; (jj>1) && (slashesLeft>0); jj--)
	  if (dst[jj] == '/') slashesLeft--;
	dst[++jj] = '\0';
      } else {
	// Simply append all the dots
	while(dotCount-- > 0)
	  dst[jj++] = '.';
      }
      dotCount = 0;
    }
    if (c == '.') {
      if (partSize == 0) {
	dotCount = 1;
	dotStarted = true;
      } else
	dotCount++;
      partSize++;
    }
      
    // Slashes are special. Handle them here
    if (c != '/' && (slashCount > 0)) {
      dst[jj++] = '/';
      slashCount = 0;
      partSize = 0;
    }
    if (c == '/') slashCount++;

    // Add the last character
    if ((c != '.') && (c != '/')) {
      dst[jj++] = c;
      partSize++;
    }
  }

  if ((jj >= 3) && (dst[jj-2] == '/'))
    dst[jj-2] = '\0';
}

/*********************************************************************
 * devunix methods
 *********************************************************************/
// The devdrvr class is a base class for virtual devices
// The methods expect pathnames that are relative to the mount pount
// and without leading slash.

devunix::devunix(char *mntpnt,char *args) : devdrvr(mntpnt)
{
    unixdir = args;
}

typedef struct {
  char name[1024]; 
} FileEntry;

struct {
  FileEntry files[100];
  int size;

  int getID(const char *buf, const char *path) {
    char buf2[1024];

    sprintf(buf2,"%s/%s",buf,path);
    canonicalizePath(buf2, buf2);
    return getID(buf2);
  }

  int getID(char *buf) {
    int fid;
    for (fid=0; fid<size; fid++) {
      if (strcmp(buf, files[fid].name) == 0)
	break;
    }
    if (fid == size) {
      strcpy(files[fid].name, buf);
      size++;
    }

    return fid;
  }
} fileTable;


static int rootID = fileTable.getID("/Users/jcho/OS9");


/* Open a file
 * fixme: Go through the path to see if the path actually exists
 * fixme: Open in other modes than read
 */
fdes *devunix::open(const char *path,int mode,int create)
{
    char buf[1024];
    const char *umode;

    sprintf(buf,"%s%s",unixdir,path);
    printf("%s", buf);
    canonicalizePath(buf, buf);
    printf(":%s\n", buf);

    if (!findpath(buf,!create))
    {
        errorcode = 216;
        return 0;
    }
    switch(mode & 3)
    {
    case 0:
    case 1: umode="rb";
        break;
    case 2: umode="wb";
        break;
    case 3: umode=(create)?"wb+":"rb+";
        break;
    }

    // First open the file/or directory
    FILE *fp = fopen(buf,umode);

    // Are we actually trying to open a directory?
    fdunix *fd;
    DIR *dir = opendir(buf);
    struct dirent *entry;
    if (dir != NULL) {
      int size = 16;
      os9dentry *dentries = new os9dentry[size];
      int numEntries = 0;
      dentries[numEntries++].set(".", fileTable.getID(buf));
      dentries[numEntries++].set("..", fileTable.getID(buf, ".."));
      while((entry = readdir(dir)) != NULL) {
	if (strcmp(".", entry->d_name) == 0) continue;
	if (strcmp("..", entry->d_name) == 0) continue;
	if (numEntries >= size) {
	  size = size * 2;
	  os9dentry *dentries2 = new os9dentry[size * 2];
	  memcpy(dentries2, dentries, numEntries * sizeof(os9dentry));
	  delete [] dentries;
	  dentries = dentries2;
	}
	dentries[numEntries++].set(entry->d_name,
				   fileTable.getID(buf, entry->d_name));
      }
      closedir(dir);
      
      // Set up the fdirunix entry
      fdirunix *fdir = new fdirunix;
      fdir->dentries = dentries;      
      fdir->length = numEntries * sizeof(os9dentry);
      fd = fdir;
    } else {
      fd =  new fdunix;
    }

    // Common initialization
    fd->fp = fp;
    fd->usecount=1;
    fd->driver = this;
    return fd;
}

fdes *devunix::open(FILE *unixfp)
{
    fdunix *fd = new fdunix;
    
    fd->fp = unixfp;
    fd->usecount=1;
    fd->driver = this;
    return fd;
}

int devunix::makdir(char *path,int mode)
{
    char buf[1024];

    sprintf(buf,"%s/%s",unixdir,path);
    if(mkdir(buf,o2u_attr(mode)) == -1)
	return(errorcode = 218);
    return 0;
}

/*
 * fixme: return more meaningful error code
 */
int devunix::delfile(char *path)
{
    char buf[1024];

    sprintf(buf,"%s/%s",unixdir,path);
    if(unlink(buf) == -1)
	return(errorcode = 218);
    return 0;
}

/* Change directory
 * fixme: Go through the path to see if the path actually exists
 */
int devunix::chdir(char *path)
{
    return 0;
}

/*********************************************************************
 * fdunix methods
 *********************************************************************/

fdunix::fdunix()
{
}


fdunix::~fdunix()
{
    if(usecount)
	fclose(fp);
    usecount--;
}

int fdunix::close()
{
    if(usecount == 1)
	fclose(fp);
    usecount--;
    return 0;
}

int fdunix::read(Byte *buf, int size)
{
    int c;
    printf("read(%d)\n", size);
    c = fread((char*)buf,1,size,fp);
    if(c == 0)
    {
        errorcode = 211;
        return -1;
    }
    return c;
}

/*
 * fixme
 */
int fdunix::readln(Byte *buf, int size)
{
    Byte *p,*maxp;
    int y,c;
 
    if(feof(fp))
    {
        errorcode = 211;
        return -1;
    }
    p = buf;
    maxp = buf + size;
 
    while(p < maxp && (c = fgetc(fp)) != EOF )
    {
        *p++ = (Byte)c;
        if(c == '\r')
            break;
    }
    y = p - buf;
    if(y == 0)
    {
        errorcode = 211;
        return -1;
    }
    return y;
}

int fdunix::write(Byte *buf, int size)
{
    int inx;
 
    for(inx = 0; inx < size; inx++)
    {
        if(fputc(buf[inx],fp) == -1)
        {
            errorcode = 211;
            break;
        }
    }
    return inx;
/*
 * Can I do this instead?
    return fwrite((char*)buf,1,size,fp);
 */
}

/*
 * Write buffer until CR is seen
 * Only regular files here
 */
int fdunix::writeln(Byte *buf, int size)
{
    int inx;
 
    for(inx = 0; inx < size;)
    {
        fputc(((char*)buf)[inx],fp);
        if(((char*)buf)[inx++] == '\r')
            break;
    }
    return inx;
}

int fdunix::seek(int offset)
{
    fflush(fp);
    fseek(fp, offset, SEEK_SET);
    return 0;
}

int fdunix::getstatus(int opcode,statusbuf *status)
{
    struct stat statbuf;
    printf("getstatus(%d)\n", opcode);
    switch (opcode)
    {
    case 0:  /* Read/Write PD Options */
	memset(status,'\0',sizeof(*status));
        status->filler[0x00] = 0x1;  /* RBF */
        status->filler[0x03] = 0x80;  /* Winchester disk */
        if(fstat(fileno(fp), &statbuf) != -1)
	{
	    status->filler[0x10] = u2o_attr(statbuf.st_mode); /* Attributes */
	    status->filler[0x11] = statbuf.st_ino >> 16 & 0xff;
	    status->filler[0x12] = statbuf.st_ino >> 8 & 0xff;
	    status->filler[0x13] = statbuf.st_ino & 0xff;
	}
        break;
    case 2: /* Read/Write File Size */
        if(fstat(fileno(fp), &statbuf) != -1)
	    status->filesize = statbuf.st_size;
        else
	    return(errorcode = 203);
        break;
    case 5: /* Get File Current Position */
        {
	    status->filesize=ftell(fp);
        }
        break;
    case 6: /* Test for End of File */
        {
	    status->status=feof(fp);
        }
        break;
    case 14: /* Return Device name (32-bytes at [X]) */
	strcpy((char*) status->filler, driver->mntpoint);
        break;
    default:
        fprintf(stderr,"Getstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return(0);
}

int fdunix::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
    case 2:
	ftruncate(fileno(fp),status->filesize);
        return 0;
        break;
    case 15:
    case 28:
        // Codes 15 and 28 are used by the ar-program.
        // Probably to set file access mode.
        return 0;
        break;
    default:
        fprintf(stderr,"Setstat code %d not implemented\n",opcode);
        exit(1);
        break;
 
    }
    return 0;
}

/*********************************************************************
 * fdirunix methods
 *********************************************************************/

fdirunix::fdirunix()
{
  offset = 0;
}


fdirunix::~fdirunix()
{
    if(usecount)
      delete [] dentries;
}

int fdirunix::close()
{
    if(usecount == 1)
	delete [] dentries;
    return fdunix::close();
}

int fdirunix::read(Byte *buf, int size)
{
  if (offset >= length) {
    errorcode = 211;
    return -1;
  }

  size = (size + offset < length) ? size : length - offset;
  memcpy((void *)buf, (Byte *)dentries + offset, size);

  offset += size;
  return size;
}

int fdirunix::getstatus(int opcode,statusbuf *status)
{
    struct stat statbuf;
    switch (opcode)
    {
    case 0:  /* Read/Write PD Options */
	memset(status,'\0',sizeof(*status));
        status->filler[0x00] = 0x1;  /* RBF */
        status->filler[0x03] = 0x80;  /* Winchester disk */
        if(fstat(fileno(fp), &statbuf) != -1)
	{
	    status->filler[0x10] = u2o_attr(statbuf.st_mode); /* Attributes */
	    status->filler[0x11] = statbuf.st_ino >> 16 & 0xff;
	    status->filler[0x12] = statbuf.st_ino >> 8 & 0xff;
	    status->filler[0x13] = statbuf.st_ino & 0xff;
	}
        break;
    case 2: /* Read/Write File Size */
        status->filesize = length;
        break;
    case 5: /* Get File Current Position */
        {
	    status->filesize=ftell(fp);
        }
        break;
    case 6: /* Test for End of File */
        {
	  return (offset >= length);
        }
        break;

    case 14: /* Return Device name (32-bytes at [X]) */
        strcpy((char*) status->filler, driver->mntpoint);
        break;
    default:
        fprintf(stderr,"Getstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return(0);
}

/*********************************************************************
 * devterm methods
 *********************************************************************/
/*
 * Methods for serial device such as tty and printer
 */
devterm::devterm(char *mntpnt,char *args) : devdrvr(mntpnt)
{
    device = args;
}

fdes *devterm::open(const char *path,int mode,int create)
{
    fdterm *fd = new fdterm;

    fd->fp = fopen(device,"r");
    fd->usecount=1;
    fd->driver = this;
    return fd;
}

fdes *devterm::open(FILE *unixfp)
{
    fdterm *fd = new fdterm;
    
    fd->fp = unixfp;
    fd->usecount=1;
    fd->driver = this;
    return fd;
}

/*********************************************************************
 * fdterm methods
 *********************************************************************/
fdterm::fdterm()
{
}

fdterm::fdterm(FILE *orgfp) : fdes()
{
    usecount++;
    fp = orgfp;
}

fdterm::~fdterm()
{
    fclose(fp);
    usecount--;
}

int fdterm::close()
{
    if(usecount == 1)
	fclose(fp);
    usecount--;
    return 0;
}

int fdterm::read(Byte *buf, int size)
{
    int c;
    c = fread((char*)buf,1,size,fp);
    if(c == 0)
    {
        errorcode = 211;
        return -1;
    }
    return c;
}

/*
 * Returns the number of bytes read or -1 on error
 */
int fdterm::readln(Byte *buf, int size)
{
    Byte *p,*maxp;
    int y,c;
 
    if(feof(fp))
    {
        errorcode = 211;
        return -1;
    }
    p = buf;
    maxp = buf + size;
 
    while(p < maxp && (c = fgetc(fp)) != EOF )
    {
        if(c == '\n') // Do conversion
                c= '\r';
        *p++ = (Byte)c;
        if(c == '\r')
            break;
    }
    y = p - buf;
    if(y == 0)
    {
        errorcode = 211;
        return -1;
    }
    return y;
}

/* fixme: convert to \n here?
 */
int fdterm::write(Byte *buf, int size)
{
    return fwrite((char*)buf,1,size,fp);
}

/*
 * Write buffer until CR is seen
 * Only ttys files here
 */
int fdterm::writeln(Byte *buf, int size)
{
    int inx;
 
    for(inx = 0; inx < size;)
    {
        fputc(((char*)buf)[inx],fp);
        if(((char*)buf)[inx++] == '\r')
	{
	    fputc('\n',fp);
            break;
        }
    }
    return inx;
}

int fdterm::seek(int offset)
{
    fflush(fp);
    fseek(fp, offset, SEEK_SET);
    return 0;
}

int fdterm::getstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
    case 0:
	memset(status,'\0',sizeof(*status));
	status->filler[0x00] = 0x00;
	status->filler[0x08] = 24; /* Lines per page */
	status->filler[0x09] = 8;  /* BS char */
	status->filler[0x0a] = 0x7f; /* DEL char */
	status->filler[0x0b] = 13; /* EOR char */
	status->filler[0x0c] = 4; /* EOF char ctrl-d */

        break;
    default:
        fprintf(stderr,"Getstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return(203);
}

int fdterm::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
    case 2:
        return 0;
        break;
        return 0;
        break;
    default:
        fprintf(stderr,"Setstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return 203;
}

/*********************************************************************
 * devpipe methods
 *********************************************************************/
/*
 * Methods for serial device such as tty and printer
 */
devpipe::devpipe(char *mntpnt,char *args) : devdrvr(mntpnt)
{
}

fdes *devpipe::open(const char *path,int mode,int create)
{
    fdpipe *fd = new fdpipe;

    ::pipe(fd->filedes);
    fd->ifp = fdopen(fd->filedes[0],"r");
    fd->usecount=1;
    fd->driver = this;
    return fd;
}

/*********************************************************************
 * fdpipe methods
 *********************************************************************/
fdpipe::fdpipe()
{
}

fdpipe::~fdpipe()
{
    fclose(ifp);
    ::close(filedes[1]);
    usecount--;
}

int fdpipe::close()
{
    if(usecount == 1)
    {
	fclose(ifp);
        ::close(filedes[1]);
    }
    usecount--;
    return 0;
}

int fdpipe::read(Byte *buf, int size)
{
    int c;
    c = fread((char*)buf,1,size,ifp);
    if(c == 0)
    {
        errorcode = 211;
        return -1;
    }
    return c;
}

/*
 * Returns the number of bytes read or -1 on error
 */
int fdpipe::readln(Byte *buf, int size)
{
    Byte *p,*maxp;
    int y,c;
 
    if(feof(ifp))
    {
        errorcode = 211;
        return -1;
    }
    p = buf;
    maxp = buf + size;
 
    while(p < maxp && (c = fgetc(ifp)) != EOF )
    {
        *p++ = (Byte)c;
        if(c == '\r')
            break;
    }
    y = p - buf;
    if(y == 0)
    {
        errorcode = 211;
        return -1;
    }
    return y;
}

int fdpipe::write(Byte *buf, int size)
{
    return ::write(filedes[1],(char*)buf,size);
}

/*
 * Write buffer until CR is seen
 */
int fdpipe::writeln(Byte *buf, int size)
{
    int nl;
 
    for(nl = 0; nl < size;)
    {
        if(((char*)buf)[nl++] == '\r')
	   break;
    }
    return ::write(filedes[1],(char*)buf,nl);
}

int fdpipe::getstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
    case 0:
	memset(status,'\0',sizeof(*status));
	status->filler[0x00] = 0x02;
        break;
    default:
        fprintf(stderr,"Getstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return(203);
}

int fdpipe::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
    case 2:
        return 0;
        break;
        return 0;
        break;
    default:
        fprintf(stderr,"Setstat code %d not implemented\n",opcode);
        exit(1);
        break;
    }
    return 203;
}
