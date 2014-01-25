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

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
#ifdef __cplusplus
}
#endif /* __cplusplus */
#include "mc6809.h"
#include "devdrvr.h"
#include "devunix.h"
#include "os9krnl.h"
#include "errcodes.h"

static int debug_syscall = 1;

/*
 * An os9 string is terminated with highorder bit set
 * This helpful functions prints it out.
 */
static void
print_os9string(FILE *out,Byte *str)
{
    while(*str < 128)
        putc(*str++,out);
    putc(*str & 127,out);
}

/*
 * Find the device driver that handles a file with that pathname
 */
devdrvr *os9::find_device(Byte *path)
{
    int i;

    for(i = 0; i < 32; i++)
       if(devices[i] && strncasecmp(devices[i]->mntpoint,(char*)path,
                strlen(devices[i]->mntpoint)) == 0)
           return devices[i];
    if(debug_syscall)
	fprintf(stderr,"No driver for %s\n",path);
    return 0;
}

/*
 * This function should read a configuration file in the user's home
 * directory. It could be called directly from the constructor.
 * The idea is to specify where /d0, /h0 is in the UNIX hierarchy.
 */
void os9::loadrcfile(void)
{
// Build a list of devices that map to a point in the UNIX filesystem
    char *homedir = (char*)malloc(strlen(getenv("HOME")) + 5);

    sprintf(homedir,"%s/OS9",getenv("HOME"));
    dev_end = 0;
    devices[dev_end++] = new devterm("/term","/dev/tty");
    devices[dev_end++] = new devunix("/h0",homedir);
    devices[dev_end++] = new devunix("/d0",homedir);
    devices[dev_end++] = new devunix("/d1",homedir);
    devices[dev_end++] = new devpipe("/pipe","");
// fixme: Make a new dir (/wd) That mounts at UNIX working directory.
}


// Constructor
os9::os9()
{
    int inx;
    devterm *tmpdev = new devterm("/term","/dev/tty");

    for(inx=0; inx < DESMAX; inx++)
    {
	paths[inx] = NULL;
    }
    // Set up stdin, stdout and stderr.
    paths[0] = tmpdev->open(stdin);
    paths[1] = tmpdev->open(stdout);
    paths[2] = tmpdev->open(stderr);

    strcpy(cwd,"/h0");
    strcpy(cxd,"/h0/CMDS");

    loadrcfile();

// Set up some PIDs This process is hardcoded to PID #1
    pids[0] = getppid();
    pids[1] = getpid();
    pid_end = 2;

// Setting the memory allocation bitmap SYSMAN 3.3
//	memset(&memory[0x100],0xff,0x1f);
}
 
os9::~os9()
{
}

int os9::sys_error(Byte errcode)
{
    if(errcode == 0)
        return 0;
    cc.bit.c = 1;
    b = errcode;
    return errcode;
}

/*
 * Get the size of the argument transferred from the shell
 * We do not count the \r as part of the string.
 */
static int
parmsize(const char *s)
{
    int i = 0;
    while(*s++ != '\r')
        i++;
    return i;
}

/*
 * Copy the argument vector
 * Not \0 terminated
 */
static void
parmcopy(Byte *to,Byte *from)
{
    while((*to++ = *from++) != '\r')
        ;
}

/*
 * getpath: get the path into a UNIX form, take into account the
 * execution directory.
 * Caller must provide adequate space in pathname.
 * Return value is the end of the path. You usually set register x to that.
 */
Word os9::getpath(Byte *mem,Byte *pathname,int xdir)
{
    Byte *mp;

    /*
     * When you do a "load filename" in basic09, getpath gets
     * called with leading spaces in filename
     */
    for(mp=mem;*mp == ' '; mp++)
       ;

    // If the path is absolute, prepend the offset into the UNIX fs
    if(*mp == '/')
    {
	*pathname = '\0';
    }
    else
    {
	if(xdir)
	    sprintf((char*)pathname,"%s/",cxd);
	else
	    sprintf((char*)pathname,"%s/",cwd);
	pathname += strlen((const char*)pathname);
    }

    for(; *mp; mp++)
    {
	if(*mp <= '-' || *mp == '<' || *mp == '>')
	    break;
	*pathname++ = *mp & 0x7f;
	if(*mp & 0x80)
	    break;
    }
    *pathname++ = '\0';

    // Skip past spaces
    for(;*mp == ' '; mp++)
       ;
    return mp - mem;
}

#define STARTPROG 0x00
#define TOPMEM   0xf800

void os9::loadmodule(const char *filename,const char *parm)
{
    fdes	*fd;
    Word	addr;
    devdrvr *dev;
    int		val,i;
    Byte tmpfn[1024];

    getpath((Byte*)filename,tmpfn,1);
    dev = find_device(tmpfn);
    if(!dev)
    {
	sys_error(221);
	return;
    }
    filename = (char*)&tmpfn[strlen(dev->mntpoint)];
    fd = dev->open(filename,1,0);
    if (!fd) {
	b = 216;
	f_perr();
	exit(EXIT_FAILURE);
    }

    addr = STARTPROG;
    while((val = fd->read(&memory[addr],256)) >0 )
    {
	addr+= val;
    }
    fd->close();
    if(fd->usecount == 0) delete fd;

    pc = STARTPROG + ( memory[STARTPROG + 0x09] << 8 ) + 
		      memory[STARTPROG + 0x0a] ;
    y = uppermem = STARTPROG + ((memory[STARTPROG + 0x0b]+1) << 8)
		 + ((memory[STARTPROG + 0x02]+1) << 8);
    y = uppermem = TOPMEM;

// Load the argument vector
// parm is already terminated with \r
    d = parmsize(parm) + 1;
    s = y - d;
    for(i = s; i < y ; i++)
	memory[i] = *parm++;
    u = lowermem = STARTPROG + ((memory[STARTPROG + 0x02]+1) << 8);
    x = s;
    dp = u >> 8;
    cc.bit.f = 0;
    cc.bit.i = 0;
    if(debug_syscall)
	printf("Start pc=%04x u=%04x dp=%02x x=%04x y=%04x s=%04x\r\n",
	 pc,u,dp,x,y,s);
}

static char *errmsg[] = {
#include "errmsg.i"
};

void os9::f_perr(void)
{
    Byte buf[128];
    // According to sysman, a holds the path number to write to,
    // but the shell never sets a.
    sprintf((char*)buf,"ERROR #%d %s\r",b,errmsg[b]);
    paths[2]->writeln(buf,strlen((char*)buf));
}

/*
 * f_chain: We will only support OS9 programs
 * Because the parameter area can be overwritten
 * when we load a new program, we make a copy
 * outside of the emulator's memory.
 */
void os9::f_chain()
{
    Byte parm[256];

    parmcopy(parm,&memory[u]);
    if(debug_syscall)
    {
	Byte prog[256];
	parmcopy(prog,&memory[x]);
	fprintf(stderr,"'os9::f_chain: %s %s\n",
	 (char*)prog,(char*)parm);
    }
    loadmodule((char*)&memory[x],(char*)parm);
}

/*
 * f_fork: We will only support OS9 programs
 */
void os9::f_fork()
{
    int pid;
    Byte upath[512];
    Byte parm[256];

    if(debug_syscall)
	fprintf(stderr,"'os9::f_fork\n");

    if((pid = fork()) == 0)
    {
	parmcopy(parm,&memory[u]);
	loadmodule((char*)&memory[x],(char*)parm);
    }
    else
    {
	x += getpath(&memory[x],upath,1);
        a = 2; // Return child's process id.
    }

}
/*
 * f_wait:
 */
void os9::f_wait()
{
    int ret;
    pid_t pid;

    pid = wait(&ret);
    a = 2;
    b = ret & 0xff;
}

/*
 * f_sleep:
 */
void os9::f_sleep()
{
    if(x == 1)
       return; // Same as giving up the timeslice.
    if(x == 0)
        wait((int*)0);
    else
	sleep(x / 100);
}

/*
 * I just ignore any unlinks
 */
void os9::f_unlk()
{
}

void os9::f_link()
{
    sys_error(221);
}

void os9::f_load()
{
    Byte upath[512];
    unsigned char modhead[14];
    devdrvr *dev;
    fdes *fd;

    x += getpath(&memory[x],upath,1);

    dev = find_device(upath);
    if(!dev)
    {
	sys_error(221);
	return;
    }
    fd = dev->open((char*)&upath[strlen(dev->mntpoint)],5,0);
    if(!fd)
    {
	sys_error(216);
	return;
    }
    fd->read(modhead,14);
    fd->close();
    if(fd->usecount == 0) delete fd;

    a = modhead[6];
    b = modhead[7];
    u = STARTPROG;
    y = STARTPROG + ( modhead[0x09] << 8 ) + 
		      modhead[0x0a] ;
    if(debug_syscall)
	fprintf(stderr,"'os9::f_load: a=%02X %s\n",a,(char*)upath);
    
}

void os9::f_mem()
{
   if(d == 0)
   {
       y = uppermem;
       d = uppermem - lowermem;
   }
   else
   { // fixme: check that the program requests less than what we have
       y = uppermem;
       d = uppermem - lowermem;
   }
}

void os9::f_prsnam()
{
    Byte *p;

    if(debug_syscall)
	fprintf(stderr,"'os9::f_prsnam:");

    if(memory[x] == '/' || isalnum(memory[x]) || memory[x] == '_'|| memory[x] == '.')
    {
	p = &memory[x];

	while(*p == '/')   // Skip slash(es)
	   p++;

	x = p - memory;

	while(*p == '_' || *p == '.' || isalnum(*p) )
	    p++;
	y = p - memory;
	b = y - x;
	if(debug_syscall)
	{
	    int i;
	    for(i=0;i < b; i++)
		fputc(memory[x+i],stderr);
	    fputc('\n',stderr);
	}
    }
    else // We are not pointing to a pathname
    {
	while(memory[x] == ' ' || memory[x] == '\t')
	{
	    x++;
        }
	sys_error(235);
	if(debug_syscall)
	    fprintf(stderr,"(whitespace)\n");
    }
}

#define CRC24_POLY 0x800063L

typedef long crc24;

static crc24
compute_crc(unsigned long crc, unsigned char *octets, int len)
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

void os9::f_crc()
{
    unsigned long tmpcrc;

    tmpcrc = (memory[u] << 16) + (memory[u+1] << 8) + memory[u+2];

    if(debug_syscall)
      fprintf(stderr,"'os9::f_crc: X=%04x Y=%04x DP=%02x\nU=%04x start=%lx\n",
             x,y,dp,u,tmpcrc);
    tmpcrc = compute_crc(tmpcrc,&memory[x],(int)y);
    memory[u+0] = (tmpcrc >> 16) & 0xff;
    memory[u+1] = (tmpcrc >> 8) & 0xff;
    memory[u+2] = tmpcrc & 0xff;
}

/*
 * Get userid
 */
void os9::f_id()
{
    a = 1;
    y = getuid() & 0xffff;
}

/*
 * Get date and time
 */
void os9::f_time()
{
    struct tm *local_time;
    time_t now;

    now = time(NULL);		// F$Time
    local_time = localtime(&now);
    memory[x+0] = (Byte)local_time->tm_year % 100; // Two char year.
    memory[x+1] = (Byte)local_time->tm_mon + 1;
    memory[x+2] = (Byte)local_time->tm_mday;
    memory[x+3] = (Byte)local_time->tm_hour;
    memory[x+4] = (Byte)local_time->tm_min;
    memory[x+5] = (Byte)local_time->tm_sec;
}

/*
 * After a cursory inspection of the disassembled shell and having some
 * trouble with basic09, I've come to the conclusion that System Manager's
 * Manual is probably wrong. The new path number is returned in register a.
 */
void os9::i_dup()
{
    Byte t;

    if(debug_syscall)
	fprintf(stderr,"'os9::i_dup: %d ",a);

    for(t = 0; t < DESMAX; t++)
	if(paths[t] == NULL)
	{
	    paths[t] = paths[a];
	    paths[a]->usecount++;
	    break;
	}
    if(t == DESMAX)
    {
	sys_error(200);
	return;
    }
    a = t;
    if(debug_syscall)
	fprintf(stderr,"=> %d\n",a);
}

void os9::i_getstt()
{
    statusbuf statbuf;
    int inx;

    if(debug_syscall)
	fprintf(stderr,"'os9::i_getstt: FD=%d opcode %d\n",a,b);

    paths[a]->errorcode = 0;
    paths[a]->getstatus((int)b,&statbuf);
    if(paths[a]->errorcode)
    {
	sys_error(paths[a]->errorcode);
	return;
    }
    switch (b)
    {
    case 0:
    case 14:
	for(inx = 0; inx < 32; inx++)
	    memory[inx + x] = statbuf.filler[inx];
        break;
    case 2:
	u = (Word)(statbuf.filesize & 0xffff);
	x = (Word)(statbuf.filesize >> 16);
    case 5:
	u = (Word)(statbuf.filesize & 0xffff);
	x = (Word)(statbuf.filesize >> 16);
        break;
    case 6:
	b = 0;
	if(statbuf.status)
	    sys_error(0xD3);
        break;
    default:
	fprintf(stderr,"Getstat code %d not implemented\n",b);
	exit(1);
        break;
    }
}

void os9::i_setstt()
{
    statusbuf statbuf;

    if(debug_syscall)
	fprintf(stderr,"'os9::i_setstt: FD=%d opcode %d\n",a,b);
    paths[a]->errorcode = 0;
    paths[a]->setstatus((int)b,&statbuf);
    if(paths[a]->errorcode)
    {
	sys_error(paths[a]->errorcode);
	return;
    }
    switch (b)
    {
    case 2:
        return;
    case 15:
    case 28:
	break;
    default:
	fprintf(stderr,"Setstat code %d not implemented\n",b);
	exit(1);
        break;

    }
}

/*
 * Make directory
 */
void os9::i_mdir()
{
    Byte upath[512];
    devdrvr *dev;

    x += getpath(&memory[x],upath,0);

    if(debug_syscall)
	fprintf(stderr,"'os9::i_mdir: %s\n",(char*)upath);

    dev = find_device(upath);
    if(!dev)
    {
	sys_error(221);
	return;
    }
    sys_error(dev->makdir((char*)&upath[strlen(dev->mntpoint)],0777));
    /* fixme: mode bits */
}

void os9::i_deletex(int xdir)
{
    Byte upath[512];
    devdrvr *dev;

    x += getpath(&memory[x],upath,(xdir)?(a&4):0);

    if(debug_syscall)
	fprintf(stderr,"'os9::i_deletex: %s\n",(char*)upath);

    dev = find_device(upath);
    if(!dev)
    {
	sys_error(221);
	return;
    }
    sys_error(dev->delfile((char*)&upath[strlen(dev->mntpoint)]));
}

/*
 * input  (X) = Address of pathlist
 * input  (A) = Access mode (D S PE PW PR E W R)
 * output (X) = Updated past pathlist (trailing spaces skipped)
 * outpu  (A) = Path number
 */
void os9::i_open(int create)
{
    Byte upath[512];
    devdrvr *dev;
    int mode = a;

    x += getpath(&memory[x],upath,(a & 4));

    if(debug_syscall)
	fprintf(stderr,"'os9::i_open: %s (%s) mode %03o",
	   (char*)upath,create?"create":"open",mode);

    dev = find_device(upath);
    if(!dev)
    {
	sys_error(221);
	return;
    }

    for(a = 0; a < DESMAX; a++)
    {
	if(paths[a] == NULL)
	{
	    paths[a] =dev->open((char*)&upath[strlen(dev->mntpoint)]
	          ,mode,create);

	    if(paths[a] == NULL)
	    {
		sys_error(216);
		return;
	    }
	    break;
	}
    }
    if(a == DESMAX)
	sys_error(200);

    if(debug_syscall)
	fprintf(stderr,"= %d\n",a);
}

void os9::i_rdln()
{
    int c;

    if(debug_syscall > 1)
	fprintf(stderr,"'os9::i_rdln: FD=%d pos=%x len=%d ",a,x ,y);

    c = paths[a]->readln(&memory[x],y);
    if(c == -1)
    {
        sys_error(paths[a]->errorcode);
	if(debug_syscall > 1)
	    fprintf(stderr,"error = %d\n",b);
	return;
    }
    y = (Word)c;
    if(debug_syscall > 1)
	fprintf(stderr,"ret = %d\n",y);
}

void os9::i_read()
{
    int c;

    if(debug_syscall > 1)
	fprintf(stderr,"'os9::i_read: FD=%d pos=0x%x len=#%d ",a,x ,y);

    c = paths[a]->read(&memory[x],y);
    if(c == -1)
    {
        sys_error(paths[a]->errorcode);
	if(debug_syscall > 1)
	    fprintf(stderr,"error = %d\n",b);
	return;
    }
    y = (Word)c;
    if(debug_syscall > 1)
	fprintf(stderr,"ret = %d\n",y);
}

void os9::i_seek()
{
    if(debug_syscall)
	fprintf(stderr,"'os9::i_seek: FD=%d pos=%04X%04X\n",a,x,u);
    paths[a]->seek((x << 16) + u);
}

void os9::i_wrln()
{
    if(debug_syscall > 1)
	fprintf(stderr,"'os9::i_wrln: FD=%d y=%d x=%04x %c%c%c...\n",a,y,x,
	  memory[x],memory[x+1],memory[x+2]);

    paths[a]->errorcode = 0;
    y = paths[a]->writeln(&memory[x],y); // Return number of bytes written
    sys_error(paths[a]->errorcode);
}

void os9::i_write()
{
    if(debug_syscall > 1)
	fprintf(stderr,"'os9::i_write: FD=%d y=%d x=%04x %c%c%c...\n",a,y,x,
	  memory[x],memory[x+1],memory[x+2]);

    paths[a]->errorcode = 0;
    y = paths[a]->write(&memory[x],y); // Return number of bytes written
    sys_error(paths[a]->errorcode);
}

void os9::i_close()
{
    if(debug_syscall)
        fprintf(stderr,"'os9::i_close: FD=%d\n",a);
    if(paths[a] == NULL)
    {
        sys_error(E_BPNum);
	return;
    }
    paths[a]->close();

    if(paths[a]->usecount == 0)
        delete paths[a];
    paths[a] = NULL;
}

/*
 * change directory.
 * Contrary to what SYSMAN says, the output is that register x is updated past
 * the path.
 */
/*
 * fixme: If the a&4 == 4 then set the exec dir bye changing the cxd
 * string.
 */
void os9::i_chgdir()
{
    Byte upath[512];
    Byte newcwd[256];
    devdrvr *dev;

    strcpy((char*)newcwd,cwd);
    x += getpath(&memory[x],upath,(a & 4));
    dev = find_device(newcwd);
    if(!dev)
    {
	sys_error(221);
	return;
    }
    if(sys_error(dev->chdir((char*)newcwd)) == 0)
        strcpy(cwd,(const char*)upath);
    fprintf(stderr,"Changing dir to %s\n",upath);
}

void os9::swi2(void)
{
	cc.bit.c = 0;
	switch(memory[pc++])
	{
	case 0x00:
	    f_link();
	    break;
	case 0x01:
	    f_load();
	    break;
	case 0x02:
	    f_unlk();
	    break;
	case 0x03:
	    f_fork();
	    break;
	case 0x04:
	    f_wait();
	    break;
	
	case 0x05:
	    f_chain();
	    break;

	case 0x06:		// F$Exit
	    if(b != 0)
		fprintf(stderr,"Exit code %d\n",b);
	    exit(b);
	    break;
        case 0x07:
	    f_mem();
	    break;
        case 0x09:
	    if(debug_syscall)
		fprintf(stderr,"'os9::Set intercept trap\n");
	    break;
        case 0x0a:
	    f_sleep();
	    break;
	case 0x0c:
	    f_id();
	    break;
	case 0x0d:		// F$SPri
	    /* Ignore */
	    break;

	case 0x0f:		// F$Perr
	    f_perr();
	    break;
	case 0x10:		// F$Pnam
	    f_prsnam();
	    break;
	case 0x15:		// F$Time
	    f_time();
	    break;

	case 0x16:		// F$STim
	    /* Ignore */
	    break;

	case 0x17:
	    f_crc();
	    break;

        case 0x82:
	    i_dup();
	    break;

	case 0x83:
	    i_open(1);		// I$Crea
	    break;

	case 0x84:		// I$Open
	    i_open(0);
	    break;

	case 0x85:
	    i_mdir();
	    break;

	case 0x86:
	    i_chgdir();
	    break;

	case 0x87:
            i_deletex(0);
	    break;

        case 0x88:
	    i_seek();
	    break;

        case 0x89:
	    i_read();
	    break;

	case 0x8a:
	    i_write();
	    break;

        case 0x8b:
	    i_rdln();
	    break;

	case 0x8c:
	    i_wrln();
	    break;

	case 0x8d:
	    i_getstt();
	    break;

	case 0x8e:
	    i_setstt();
	    break;

	case 0x8f:
            i_close();
	    break;

	case 0x90:
            i_deletex(1);
	    break;

        default:
	    printf("Uncaught SWI2 call request %x\r\n", memory[--pc]);
	    exit(0);
	}
}
