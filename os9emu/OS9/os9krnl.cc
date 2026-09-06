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

os9config os9cfg = { NULL, "/h0", "/h0/CMDS", 0, EOL_AUTO, 0 };

#define STARTPROG 0x00
#define TOPMEM   0xf800
/*
 * The highest address a process data area may grow to. Everything below it is
 * the program's; nothing of ours lives up there, so this is simply the top of
 * the address space rounded down to a page.
 */
#define MEMTOP   0xff00
/*
 * Slack added to a module's declared storage when the process starts. Modules
 * fold their stack requirement into M$Mem, but a few of the older utilities
 * under-declare it and would otherwise run their stack into their data before
 * they ever get the chance to ask F$Mem for more.
 */
#define MEMSLACK 0x0400


#define debug_syscall (os9cfg.trace)

static size_t MAX_PATHLEN = 1024;

/*
 * An os9 string is terminated with highorder bit set
 * This helpful functions prints it out.
 */
void
print_os9string(FILE *out, Byte *str)
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
    
    for(i = 0; i < 32; i++) {
        if (devices[i]) {
            if (strncasecmp(devices[i]->mntpoint,(char*)path,
                            strlen(devices[i]->mntpoint)) == 0) {
                return devices[i];
            }
        }
    }
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
    // Build a list of devices that map onto a point in the UNIX filesystem.
    // /dd is OS9's "default device": the C compiler and most of the utilities
    // reach for /dd/LIB and /dd/DEFS rather than naming a drive.
    const char *root = os9cfg.root;

    dev_end = 0;
    devices[dev_end++] = new devterm("/term", "/dev/tty");
    devices[dev_end++] = new devunix("/dd", root);
    devices[dev_end++] = new devunix("/h0", root);
    devices[dev_end++] = new devunix("/d0", root);
    devices[dev_end++] = new devunix("/d1", root);
    devices[dev_end++] = new devpipe("/pipe", "");
}


// Constructor
os9::os9()
{
    int inx;

    for(inx=0; inx < DESMAX; inx++)
    {
        paths[inx] = NULL;
    }
    dev_end = 0;
    pid_end = 0;
    mod_end = 0;
    modtop = MEMTOP;
    cwd[0] = cxd[0] = '\0';
}

/*
 * Bring the kernel up. Split out of the constructor because os9cfg is not
 * filled in until the command line has been parsed, and the emulator is a
 * global.
 */
void os9::init()
{
    devterm *tmpdev = new devterm("/term","/dev/tty");

    // Set up stdin, stdout and stderr.
    paths[0] = tmpdev->open(stdin);
    paths[1] = tmpdev->open(stdout);
    paths[2] = tmpdev->open(stderr);

    snprintf(cwd, sizeof(cwd), "%s", os9cfg.workdir);
    snprintf(cxd, sizeof(cxd), "%s", os9cfg.execdir);

    loadrcfile();

    // Set up some PIDs This process is hardcoded to PID #1
    pids[0] = getppid();
    pids[1] = getpid();
    pid_end = 2;
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
 * Characters OS9 allows in a file or device name. The high bit is the name
 * terminator rather than part of the character, so it is masked off first.
 */
static int namechar(Byte c)
{
    c &= 0x7f;
    return isalnum(c) || c == '_' || c == '.' || c == '$';
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
Word os9::getpath(Byte *mem, Byte *pathname, int xdir)
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
            snprintf((char*)pathname, MAX_PATHLEN, "%s/", cxd);
        else
            snprintf((char*)pathname, MAX_PATHLEN, "%s/", cwd);
        pathname += strlen((const char*)pathname);
    }
    
    for(; *mp; mp++)
    {
        if(*mp <= '-' || *mp == '<' || *mp == '>')
            break;
        *pathname++ = *mp & 0x7f;
        if(*mp & 0x80)
        {
            // The high bit marks the last character of the name, not a
            // delimiter -- step over it, or the caller carries on parsing
            // from a character it has already given us.
            mp++;
            break;
        }
    }
    *pathname++ = '\0';
    
    // Skip past spaces
    for(;*mp == ' '; mp++)
        ;
    return mp - mem;
}


void os9::loadmodule(const char *filename,const char *parm,int pages)
{
    fdes	*fd;
    Word	addr;
    devdrvr *dev;
    int		val,i;
    Byte tmpfn[1024];
    
    getpath((Byte*)filename, tmpfn, 1);
    dev = find_device(tmpfn);
    if (!dev)
    {
        sys_error(221);
        return;
    }
    filename = (char*)&tmpfn[strlen(dev->mntpoint)];
    fd = dev->open(filename, 1, 0);
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
    
    /*
     * Lay out the process the way OS9 does.
     *
     *   STARTPROG        the module image
     *   lowermem         start of the data area, on the next page boundary
     *   ...              static data, then the heap growing up
     *   ...              the stack growing down
     *   uppermem         end of the data area; parameters sit just below it
     *
     * The size of the data area comes from the module header's storage field
     * (M$Mem) plus the parameter area, and F$Mem grows it from there. Handing
     * the program the whole address space up front looks generous but breaks
     * the C runtime: its sbrk allocates the memory that F$Mem *adds*, so when
     * the area cannot grow it hands out the region the stack is already in.
     */
    Word modsize  = (memory[STARTPROG + 0x02] << 8) | memory[STARTPROG + 0x03];
    Word datasize = (memory[STARTPROG + 0x0b] << 8) | memory[STARTPROG + 0x0c];
    int  parmlen  = parmsize(parm) + 1;
    long top;

    pc = STARTPROG + ( memory[STARTPROG + 0x09] << 8 ) +
    memory[STARTPROG + 0x0a] ;

    lowermem = (STARTPROG + modsize + 0xff) & 0xff00;

    // A new program owns the address space, so whatever the last one had
    // loaded goes with it.
    mod_end = 0;
    modtop = MEMTOP;

    /*
     * F$Fork's B register carries the data area size the caller wants, in
     * pages -- this is how the shell's "prog #32k" reaches us, and how a
     * program like Basic09 gets a workspace bigger than its own declaration.
     * Zero means "whatever the module asks for".
     */
    top = (long)datasize + MEMSLACK;
    if((long)pages * 256 > top)
        top = (long)pages * 256;
    top += (long)lowermem + parmlen;
    top = (top + 0xff) & ~0xffL;
    if(top > modtop)
        top = modtop;
    uppermem = (int)top;

    y = uppermem;

    /*
     * OS9 hands a new process a cleared data area. We must too: our memory is
     * one array reused by every program, and after a fork it still holds the
     * parent's variables. The shell reads an uninitialised "modstk" through
     * it and asks F$Fork for 255 pages of memory that nothing needs.
     */
    memset(&memory[lowermem], 0, (size_t)(uppermem - lowermem));

    // Load the argument vector
    // parm is already terminated with \r
    d = parmlen;
    s = y - d;
    for(i = s; i < y ; i++)
        memory[i] = *parm++;
    u = lowermem;
    x = s;
    dp = u >> 8;
    cc.bit.f = 0;
    cc.bit.i = 0;
    if(debug_syscall)
        printf("Start pc=%04x u=%04x dp=%02x x=%04x y=%04x s=%04x\r\n",
               pc,u,dp,x,y,s);
}

static const char *errmsg[] = {
#include "errmsg.i"
};

void os9::f_perr(void)
{
    Byte buf[128];
    // According to sysman, a holds the path number to write to,
    // but the shell never sets a.
    snprintf((char*)buf, sizeof(buf), "ERROR #%d %s\r", b, errmsg[b]);
    paths[2]->writeln(buf, (int)strlen((char*)buf));
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
    
    int pages = b;		// as for F$Fork, B is the data area size in pages

    parmcopy(parm,&memory[u]);
    if(debug_syscall)
    {
        Byte prog[256];
        parmcopy(prog,&memory[x]);
        fprintf(stderr,"'os9::f_chain: %s %s\n",
                (char*)prog,(char*)parm);
    }
    loadmodule((char*)&memory[x],(char*)parm,pages);
}

/*
 * f_fork: We will only support OS9 programs
 */
void os9::f_fork()
{
    pid_t pid;
    Byte upath[512];
    Byte parm[256];
    int len, i, pages;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_fork: %d pages requested\n",b);

    /*
     * The parameter area is at U and Y bytes long. Take Y as the bound rather
     * than scanning for the terminating CR, since a caller that leaves it out
     * would otherwise walk us off the end of the buffer.
     */
    len = 0;
    while(len < y && len < (int)sizeof(parm) - 1)
    {
        parm[len] = memory[(Word)(u + len)];
        if(parm[len++] == '\r')
            break;
    }
    if(len == 0 || parm[len-1] != '\r')
        parm[len++] = '\r';
    parm[len] = '\0';

    pages = b;			// data area size the caller asked for

    /*
     * Anything still sitting in a stdio buffer would be duplicated into the
     * child and written twice -- once by each process. Empty them first.
     */
    fflush(NULL);

    pid = fork();
    if(pid == 0)
    {
        // In the child: replace this process with the new program. Our whole
        // machine was copied by fork(), so the paths and directories the child
        // inherits are exactly the ones OS9 would have given it.
        loadmodule((char*)&memory[x],(char*)parm,pages);
    }
    else if(pid < 0)
    {
        sys_error(E_PrcFul);
    }
    else
    {
        x += getpath(&memory[x],upath,1);

        // Hand back an OS9 process id, and remember which host process it is
        // so F$Wait and F$Send can find it again.
        a = 2;
        for(i = 0; i < pid_end; i++)
            if(pids[i] == pid)
                a = i;
        if(pids[a] != pid && pid_end < 32)
        {
            pids[pid_end] = pid;
            a = pid_end++;
        }
    }
}
/*
 * f_wait:
 */
/*
 * F$Wait: block until a child dies. A comes back with its process id and B
 * with the status it exited on.
 */
void os9::f_wait()
{
    int status = 0, i;
    pid_t pid;

    pid = wait(&status);
    if(pid < 0)
    {
        sys_error(E_NoChld);
        return;
    }

    a = 2;
    for(i = 0; i < pid_end; i++)
        if(pids[i] == pid)
            a = i;
    b = WIFEXITED(status) ? (Byte)WEXITSTATUS(status) : 0;
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
/*
 * F$UnLink: U holds the header address of a module the caller is done with.
 * When the last link goes, so does the module -- which is what gives its
 * memory back.
 */
void os9::f_unlk()
{
    int i;

    for(i = 0; i < mod_end; i++)
    {
        if(moddir[i].addr != u)
            continue;
        if(--moddir[i].links > 0)
            return;

        if(debug_syscall)
            fprintf(stderr,"'os9::f_unlk: %s at %04x\n",moddir[i].name,u);
        memmove(&moddir[i], &moddir[i+1],
                (size_t)(mod_end - i - 1) * sizeof(moddir[0]));
        mod_end--;
        reclaim_modules();
        return;
    }
    // Unlinking something we never linked is not worth an error: the caller
    // is only saying it has finished with it.
}

/*
 * F$CmpNam: do two names match? X points at the pattern, Y at the candidate,
 * B is the pattern's length. Both are OS9 names, so either may end at a high
 * bit rather than at its counted length, and the comparison ignores case.
 *
 * Carry clear means they match; E$BNam means they do not.
 */
void os9::f_cmpnam()
{
    Word p = x, q = y;
    int len = b, i;

    for(i = 0; i < len; i++)
    {
        Byte pc_ = memory[(Word)(p + i)];
        Byte qc = memory[(Word)(q + i)];

        if(tolower(pc_ & 0x7f) != tolower(qc & 0x7f))
        {
            sys_error(E_BNam);
            return;
        }
        // The pattern ran out here; the candidate has to end here too.
        if((pc_ & 0x80) || (qc & 0x80))
        {
            if((pc_ & 0x80) && (qc & 0x80) && i == len - 1)
                return;
            sys_error(E_BNam);
            return;
        }
    }
    // Pattern exhausted by count: the candidate must not continue past it.
    if(namechar(memory[(Word)(q + len)]) && !(memory[(Word)(q + len - 1)] & 0x80))
        sys_error(E_BNam);
}

/*
 * F$CpyMem: copy from another process's address space into ours. There is only
 * one address space here, so this is a memcpy that wraps at 64K.
 *
 * Entry: X = source, Y = byte count, U = destination, D = the process to copy
 *        from, which we ignore.
 */
void os9::f_cpymem()
{
    Word i, count = y;

    for(i = 0; i < count; i++)
        memory[(Word)(u + i)] = memory[(Word)(x + i)];
}

/*
 * F$Send: send a signal to another process. A holds the process id, B the
 * signal. Each OS9 process is a host process here, so a signal becomes a host
 * signal -- but only the fatal one has a faithful equivalent, since we have no
 * way to make another emulator instance run its intercept routine.
 */
void os9::f_send()
{
    int slot = a;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_send: pid %d signal %d\n",a,b);

    if(slot < 0 || slot >= pid_end)
    {
        sys_error(E_IPrcID);
        return;
    }
    if(b == 0)			// S$Kill
    {
        if(kill(pids[slot], SIGTERM) == -1)
            sys_error(E_IPrcID);
    }
    // S$Wake and the keyboard signals have nothing to wake here: a sleeping
    // process is inside nanosleep(), and returns on its own.
}

/*
 * A module's own name, from the offset in its header. Names are stored high
 * bit terminated, so the last character carries the terminator.
 */
int os9::modname(Word base, char *out, size_t outsz)
{
    Word off = (memory[(Word)(base + 4)] << 8) | memory[(Word)(base + 5)];
    Word p = base + off;
    size_t i;

    for(i = 0; i + 1 < outsz; i++)
    {
        Byte c = memory[(Word)(p + i)];
        if(!namechar(c))
            return 0;
        out[i] = c & 0x7f;
        if(c & 0x80)
        {
            i++;
            break;
        }
    }
    out[i] = '\0';
    return i > 0;
}

int os9::findmodule(const char *name)
{
    int i;

    for(i = 0; i < mod_end; i++)
        if(strcasecmp(moddir[i].name, name) == 0)
            return i;
    return -1;
}

/*
 * Recompute where loaded modules start, after one has gone. Without this the
 * space a module occupied is never handed back and a shell that loads command
 * after command runs the module area down into the program's data.
 */
void os9::reclaim_modules()
{
    int i;

    modtop = MEMTOP;
    for(i = 0; i < mod_end; i++)
        if(moddir[i].addr < modtop)
            modtop = moddir[i].addr;
}

/*
 * F$Link and F$NMLink: find a module that is already in memory.
 *
 * Entry: X = the module name
 * Exit:  X past the name, U = module header, Y = entry point,
 *        A = type/language, B = attributes/revision
 *
 * Only modules this process brought in with F$Load are here, since nothing is
 * resident when a program starts. That is why the shell's first link of a
 * command fails and it forks the command instead.
 */
void os9::f_link()
{
    char name[64];
    Word i, n;
    int slot;

    // A module name, not a pathlist -- parse it where it stands.
    f_prsnam();
    if(cc.bit.c)
        return;

    n = b;
    for(i = 0; i < n && i + 1 < sizeof(name); i++)
        name[i] = memory[(Word)(x + i)] & 0x7f;
    name[i] = '\0';

    // X is left where F$PrsNam put it -- at the start of the name, past any
    // leading slash. The shell reads it back to open the command it just
    // failed to link, so moving it past the name loses the name.

    slot = findmodule(name);
    if(slot < 0)
    {
        if(debug_syscall)
            fprintf(stderr,"'os9::f_link: %s not loaded\n",name);
        sys_error(E_MNF);
        return;
    }

    moddir[slot].links++;
    {
        Word base = moddir[slot].addr;
        u = base;
        y = base + ((memory[(Word)(base + 9)] << 8) | memory[(Word)(base + 10)]);
        a = memory[(Word)(base + 6)];
        b = memory[(Word)(base + 7)];
    }
    if(debug_syscall)
        fprintf(stderr,"'os9::f_link: %s at %04x\n",name,u);
}

void os9::f_load()
{
    Byte upath[512];
    unsigned char modhead[14];
    devdrvr *dev;
    fdes *fd;
    char name[64];
    Word base, addr, modsize;
    int val;

    x += getpath(&memory[x],upath,1);

    dev = find_device(upath);
    if(!dev)
    {
        sys_error(E_MNF);
        return;
    }
    fd = dev->open((char*)&upath[strlen(dev->mntpoint)],5,0);
    if(!fd)
    {
        sys_error(dev->errorcode ? dev->errorcode : E_PNNF);
        return;
    }

    /*
     * Read the header first so we know how much room the module needs, then
     * place it at the top of memory, below anything already loaded.
     *
     * Reporting the header without actually loading it -- which is what this
     * used to do -- left the caller reading whatever was at address 0, which
     * is its own image: runb concluded that every packed procedure it was
     * handed had a compiler error in it.
     */
    if(fd->read(modhead,14) < 14 || modhead[0] != 0x87 || modhead[1] != 0xcd)
    {
        fd->close();
        if(fd->usecount == 0) delete fd;
        sys_error(E_BMID);
        return;
    }

    modsize = (modhead[2] << 8) | modhead[3];

    base = (Word)((modtop - modsize) & 0xff00);
    if(modsize == 0 || base < uppermem || base > modtop)
    {
        fd->close();
        if(fd->usecount == 0) delete fd;
        sys_error(E_MemFul);
        return;
    }

    memcpy(&memory[base], modhead, 14);
    addr = base + 14;
    while(addr < (Word)(base + modsize) &&
          (val = fd->read(&memory[addr], (base + modsize) - addr)) > 0)
        addr += val;
    fd->close();
    if(fd->usecount == 0) delete fd;

    modtop = base;

    if(mod_end < (int)(sizeof(moddir)/sizeof(moddir[0])) &&
       modname(base, name, sizeof(name)))
    {
        snprintf(moddir[mod_end].name, sizeof(moddir[mod_end].name), "%s", name);
        moddir[mod_end].addr = base;
        moddir[mod_end].end = (Word)(base + modsize);
        moddir[mod_end].links = 1;
        mod_end++;
    }

    a = memory[(Word)(base + 6)];
    b = memory[(Word)(base + 7)];
    u = base;
    y = base + ((memory[(Word)(base + 9)] << 8) | memory[(Word)(base + 10)]);
    if(debug_syscall)
        fprintf(stderr,"'os9::f_load: %s type=%02X at %04x size %d\n",
                (char*)upath,a,base,modsize);
}

/*
 * F$Mem: resize the process data area.
 *
 * Entry: D = the total size wanted, or 0 to just ask what we have.
 * Exit:  Y = the new upper bound, D = the size we settled on.
 *
 * OS9 grows the area upwards, so the memory a caller gets back sits above
 * everything that was already there -- which is exactly what the C runtime's
 * sbrk relies on when it carves the heap out of the space F$Mem just added.
 */
void os9::f_mem()
{
    long want;

    if(d != 0)
    {
        want = ((long)lowermem + d + 0xff) & ~0xffL;

        // Never hand back less than the process already holds: its stack is
        // living at the top of that area.
        if(want < uppermem)
            want = uppermem;
        if(want > modtop)
        {
            // Anything above is taken by modules F$Load brought in.
            sys_error(E_MemFul);
            return;
        }
        uppermem = (int)want;
    }

    y = uppermem;
    d = uppermem - lowermem;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_mem: %04x-%04x (%d bytes)\n",
                lowermem,uppermem,uppermem-lowermem);
}

void os9::f_prsnam()
{
    Byte *p;
    
    if(debug_syscall)
        fprintf(stderr,"'os9::f_prsnam:");
    
    if(memory[x] == '/' || namechar(memory[x]))
    {
        p = &memory[x];

        while(*p == '/')   // Skip slash(es)
            p++;

        x = p - memory;	   // X is left past the optional leading slash

        // A name runs to the first character that cannot be part of one. It
        // may also end at a character with the high bit set, which is how OS9
        // terminates a name in memory -- that character is the last one of the
        // name, not a delimiter, so it counts towards the length.
        while(namechar(*p))
        {
            int last = (*p & 0x80);
            p++;
            if(last)
                break;
        }
        y = p - memory;	   // Y is the address just past the name
        b = y - x;
        if(debug_syscall)
        {
            int i;
            for(i=0;i < b; i++)
                fputc(memory[x+i] & 0x7f,stderr);
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

typedef int crc24;

static crc24
compute_crc(unsigned int crc, unsigned char *octets, int len)
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
    unsigned int tmpcrc;
    
    tmpcrc = (memory[u] << 16) + (memory[u+1] << 8) + memory[u+2];
    
    if(debug_syscall)
        fprintf(stderr,"'os9::f_crc: X=%04x Y=%04x DP=%02x\nU=%04x start=%x\n",
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
    // OS9 stores the year as year-1900 and works out the century from it, so
    // tm_year goes across as it stands. Truncating it to two digits is what
    // made date(1) report 1926.
    memory[x+0] = (Byte)local_time->tm_year;
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
    
    if(a >= DESMAX || paths[a] == NULL)
    {
        sys_error(E_BPNum);
        return;
    }

    memset(&statbuf, 0, sizeof(statbuf));
    paths[a]->errorcode = 0;
    paths[a]->getstatus((int)b,&statbuf);
    if(paths[a]->errorcode)
    {
        sys_error(paths[a]->errorcode);
        return;
    }
    switch (b)
    {
        case SS_Opt:
        case SS_DevNm:
            for(inx = 0; inx < 32; inx++)
                memory[inx + x] = statbuf.filler[inx];
            break;
        case SS_FD:
            // Y says how much of the descriptor the caller wants.
            for(inx = 0; inx < y && inx < (int)sizeof(statbuf.filler); inx++)
                memory[(Word)(x + inx)] = statbuf.filler[inx];
            break;
        case SS_Size:
        case SS_Pos:
            u = (Word)(statbuf.filesize & 0xffff);
            x = (Word)(statbuf.filesize >> 16);
            break;
        case SS_Ready:
            b = (Byte)statbuf.status;
            break;
        case SS_EOF:
            b = 0;
            if(statbuf.status)
                sys_error(E_EOF);
            break;
        case SS_ScSiz:
            x = (Word)statbuf.cols;
            y = (Word)statbuf.rows;
            break;
        default:
            // The driver said it handled the call but we have no register
            // convention for it, so there is nothing to hand back.
            break;
    }
}

void os9::i_setstt()
{
    statusbuf statbuf;
    
    if(debug_syscall)
        fprintf(stderr,"'os9::i_setstt: FD=%d opcode %d\n",a,b);

    if(a >= DESMAX || paths[a] == NULL)
    {
        sys_error(E_BPNum);
        return;
    }

    // Collect whatever the call carries in registers before handing it down.
    memset(&statbuf, 0, sizeof(statbuf));
    switch (b)
    {
        case SS_Size:
            statbuf.filesize = ((size_t)x << 16) | u;
            break;
        case SS_Opt:
        {
            int inx;
            for(inx = 0; inx < 32; inx++)
                statbuf.filler[inx] = memory[(Word)(x + inx)];
            break;
        }
        case SS_FD:
        {
            int inx;
            for(inx = 0; inx < y && inx < (int)sizeof(statbuf.filler); inx++)
                statbuf.filler[inx] = memory[(Word)(x + inx)];
            break;
        }
        case SS_Attr:
            statbuf.status = x & 0xff;
            break;
        default:
            break;
    }

    paths[a]->errorcode = 0;
    paths[a]->setstatus((int)b,&statbuf);
    if(paths[a]->errorcode)
        sys_error(paths[a]->errorcode);
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
/*
 * I$ChgDir. A carries the access mode: with EXEC. set it moves the execution
 * directory, otherwise the working one -- chx and chd are the same call.
 *
 * The directory has to exist and has to be a directory; this used to accept
 * anything and store it, so a mistyped chd left every later path broken with
 * no hint of where it went wrong.
 */
void os9::i_chgdir()
{
    Byte upath[512];
    devdrvr *dev;
    fdes *fd;
    int isexec = (a & 4) != 0;

    x += getpath(&memory[x],upath,isexec);

    if(debug_syscall)
        fprintf(stderr,"'os9::i_chgdir: %s (%s)\n",upath,isexec?"exec":"data");

    dev = find_device(upath);
    if(!dev)
    {
        sys_error(E_MNF);
        return;
    }

    dev->errorcode = 0;
    fd = dev->open((const char*)&upath[strlen(dev->mntpoint)], 0x80 | 1, 0);
    if(!fd)
    {
        sys_error(dev->errorcode ? dev->errorcode : E_PNNF);
        return;
    }
    if(!fd->isdir())
    {
        fd->close();
        if(fd->usecount == 0) delete fd;
        sys_error(E_BPNam);
        return;
    }
    fd->close();
    if(fd->usecount == 0) delete fd;

    if(isexec)
        snprintf(cxd,sizeof(cxd),"%s",(const char*)upath);
    else
        snprintf(cwd,sizeof(cwd),"%s",(const char*)upath);
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
        case 0x08:		// F$Send
            f_send();
            break;
        case 0x09:
            if(debug_syscall)
                fprintf(stderr,"'os9::Set intercept trap\n");
            break;
        case 0x0a:
            f_sleep();
            break;
        case 0x0b:		// F$SSpd -- suspend, which we never need to do
            break;
        case 0x0c:
            f_id();
            break;
        case 0x0d:		// F$SPri
            /* Ignore */
            break;
        case 0x0e:		// F$SSWI -- install a SWI vector, which nothing
            break;		// in a hosted program can usefully reach

        case 0x0f:		// F$Perr
            f_perr();
            break;
        case 0x10:		// F$Pnam
            f_prsnam();
            break;
        case 0x11:		// F$CmpNam
            f_cmpnam();
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

        case 0x1b:		// F$CpyMem
            f_cpymem();
            break;
        case 0x1c:		// F$SUser -- everything here runs as the super user
            break;
        case 0x1d:		// F$UnLoad -- see F$UnLink
            break;
        case 0x1e:		// F$Alarm -- no clock to hang an alarm off
            break;

        // The CoCo 3 "non-mapping" pair. With one address space they mean
        // exactly what the ordinary calls mean, and cc1 and the shell reach
        // for them by preference.
        case 0x21:		// F$NMLink
            f_link();
            break;
        case 0x22:		// F$NMLoad
            f_load();
            break;

        case 0x80:		// I$Attach -- a device is attached the moment it
        case 0x81:		// I$Detach    exists, and never goes away
            u = 0;
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
            /*
             * A service call we do not provide. Real OS-9 answers E$UnkSvc
             * and lets the caller decide what to do; bailing out of the whole
             * machine here used to kill programs that would have carried on
             * perfectly well without the call.
             */
            if(debug_syscall)
                fprintf(stderr,"'os9::unimplemented service call $%02x\n",
                        memory[pc-1]);
            sys_error(E_UnkSvc);
            break;
    }
}
