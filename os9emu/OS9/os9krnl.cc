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
#include <poll.h>
#include <sys/mman.h>
#include <errno.h>
#ifdef __cplusplus
}
#endif /* __cplusplus */
#include "mc6809.h"
#include "devdrvr.h"
#include "devunix.h"
#include "os9krnl.h"
#include "errcodes.h"

/*
 * The working and execution directories a process starts with. /dd is OS9's
 * default device -- what the C compiler and most of the utilities reach for
 * when they do not name a drive -- so that is where a real system puts them,
 * and pwd and pxd now print these back verbatim.
 */
os9config os9cfg = { NULL, "/dd", "/dd/CMDS", 0, EOL_AUTO, 0, 0 };

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


/*
 * The two signal codes we can carry between processes. S$Kill cannot be
 * caught, and S$Wake only ends a sleep. The rest -- S$Abort, S$Intrpt and the
 * codes a program picks for itself, like the $0B shellplus asks a keypress to
 * send -- only ever travel within one process, where SS_SSig raises them.
 */
enum { S_Kill = 0, S_Wake = 1 };

#define debug_syscall (os9cfg.trace)

static size_t MAX_PATHLEN = 1024;

/*
 * Is there input waiting on a host descriptor? End of file counts: a read
 * there returns at once, which is what "ready" means to the caller.
 */
static int fd_ready(int fd)
{
    struct pollfd pfd;

    if(fd < 0)
        return 0;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    return poll(&pfd, 1, 0) > 0 && pfd.revents != 0;
}

/*
 * The system-wide module directory.
 *
 * Real OS9 keeps one for the whole machine, and load, link, unlink, mdir and
 * printerr are written to it: "load echo" is meant to leave echo there for the
 * next command to find. Each OS9 process here is a host process with its own
 * copy of the emulated 64K, so a directory kept in that memory belongs to
 * whoever built it and dies with them -- which is issue #1.
 *
 * What is shared is the directory, not the module memory. An OS9 module is
 * position-independent and re-entrant by rule, so a process that links a
 * module another process loaded can read it in again at an address of its own
 * choosing and be no worse off. That leaves the shared part small enough to
 * be a MAP_SHARED page created before the first fork and inherited by every
 * process after it, and leaves each process's 64K alone: no window carved out
 * of the address space, and no ceiling on anybody's data area.
 *
 * What it does not give is a module whose *contents* are shared, which is
 * what F$DatMod would need. A data module has to be writable by everyone who
 * links it, and that wants real shared pages.
 */
#define SHMODS 32

/*
 * A fake physical memory, in the 8K blocks a CoCo 3 divides its own into.
 * Nothing is mapped through it -- every process still has its own flat 64K --
 * but a Level 2 utility is handed the module directory in the kernel's terms,
 * and the kernel's terms are blocks: an entry says which block its module
 * sits in, and F$CpyMem reads the module by naming that block. So each module
 * in the shared directory gets a block of its own, and the block is how mdir
 * gets from an entry to the header and the name it prints.
 */
#define SHBLKS  64			// 8K blocks: the 512K a CoCo 3 has
#define BLKSIZE 8192
#define SYSBLKS 2			// what the system itself is holding

#define B_InUse 0x01			// RAMinUse, ../nitros9/defs/os9.d
#define B_Mod   0x02			// ModBlock

/*
 * A module image, wherever it is named from. The bytes are not shared -- only
 * the fact of them is -- so what we keep is the file to read them back out
 * of and the blocks of fake memory they answer to.
 */
struct sharedimg {
    char path[512];		// the OS9 pathlist it was loaded from
    long off;			// where in that file the module starts
    int  size;			// M$Size, so we know how much of it there is
    int  blk;			// the first block of fake memory it holds
    int  nblk;			// how many
};

struct sharedmod {
    char name[32];		// as F$Link asks for it
    struct sharedimg img;
    int  links;			// system-wide link count
};

/*
 * And the processes, for the same reason and in the same page: an OS9 process
 * id has to mean the same thing to every process in the machine, or F$Send
 * cannot reach anybody and procs has nothing to list. Each process keeps its
 * own row up to date and reads everybody else's.
 *
 * An id is a slot number plus one, so it is stable, small enough for the byte
 * OS9 keeps it in, and reused after the process holding it has gone -- which
 * is what a real system does with them too. Slot zero is never handed out:
 * id 1 is the system process on a real machine, and procs starts its scan at
 * 2 because of it.
 */
#define SHPROCS PIDMAX

struct sharedproc {
    int   used;
    pid_t host;			// the host process this one is
    int   id, parent;		// its OS9 id, and its parent's
    int   user;
    int   prior, age;
    int   state;
    int   pages;		// data area size, in 256-byte pages
    unsigned sp;		// stack pointer, as at its last system call
    char  module[32];		// the primary module
    struct sharedimg img;	// and where to read that back from
};

struct sharedsys {
    volatile unsigned char lock;
    int count;
    unsigned char blkmap[SHBLKS];	// in the form F$GBlkMp hands over
    struct sharedmod ent[SHMODS];
    struct sharedproc proc[SHPROCS];
};

static struct sharedsys *shmods = NULL;

// Our own row in the process table, as a file static so that the handler
// which gives it back at exit can reach it. A forked child overwrites it with
// its own before it runs anything.
static int myproc = -1;

#ifndef MAP_ANON
#define MAP_ANON MAP_ANONYMOUS
#endif

static void shared_init(void)
{
    void *p = mmap(NULL, sizeof(*shmods), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANON, -1, 0);

    // Without it every process keeps its own directory, which is where we
    // were before and still works for a program that loads its own modules.
    if(p == MAP_FAILED)
        return;
    memset(p, 0, sizeof(*shmods));
    shmods = (struct sharedsys *)p;
    for(int i = 0; i < SYSBLKS; i++)
        shmods->blkmap[i] = B_InUse;
}

/*
 * Hand out a run of blocks long enough to hold a module, or zero if there is
 * no run that long. Block zero is the system's, so zero is free to mean none.
 * Both of these want the directory lock already held.
 */
static int blocks_alloc(int bytes)
{
    int need = (bytes + BLKSIZE - 1) / BLKSIZE, i, run = 0;

    if(!shmods || need <= 0)
        return 0;
    for(i = 0; i < SHBLKS; i++)
    {
        if(shmods->blkmap[i] & B_InUse)
        {
            run = 0;
            continue;
        }
        if(++run < need)
            continue;
        for(run = i + 1 - need; run <= i; run++)
            shmods->blkmap[run] = B_InUse | B_Mod;
        return i + 1 - need;
    }
    return 0;
}

static void blocks_free(int blk, int nblk)
{
    int i;

    if(!shmods || blk < SYSBLKS)
        return;
    for(i = blk; i < blk + nblk && i < SHBLKS; i++)
        shmods->blkmap[i] = 0;
}

/*
 * Hold the directory still. The critical sections are a handful of string
 * compares, so spinning is cheap -- but a process that died inside one would
 * hang every other, so give up after a while and go in anyway. A torn read of
 * a module name is a missed link; a machine that never comes back is worse.
 */
static int shared_lock(void)
{
    long spins;

    if(!shmods)
        return 0;
    for(spins = 0; spins < 10000000L; spins++)
        if(!__atomic_test_and_set(&shmods->lock, __ATOMIC_ACQUIRE))
            return 1;
    return 0;
}

static void shared_unlock(int held)
{
    if(held)
        __atomic_clear(&shmods->lock, __ATOMIC_RELEASE);
}

static struct sharedmod *shared_find(const char *name)
{
    int i;

    for(i = 0; i < SHMODS; i++)
        if(shmods->ent[i].links > 0 &&
           strcasecmp(shmods->ent[i].name, name) == 0)
            return &shmods->ent[i];
    return NULL;
}

/*
 * One more user of a module, recording where it came from if this is the
 * first. A NULL path only counts a module already there. A path too long to
 * keep is not an error: the module is in the loader's own memory either way,
 * and only another process loses by it.
 */
static void shared_add(const char *name, const char *path, long off, int size)
{
    struct sharedmod *e;
    int held = shared_lock(), i;

    if(!shmods)
        return;
    if((e = shared_find(name)) == NULL)
    {
        if(path == NULL)
        {
            shared_unlock(held);
            return;
        }
        for(i = 0; i < SHMODS; i++)
            if(shmods->ent[i].links == 0)
                break;
        if(i == SHMODS || strlen(path) >= sizeof(e->img.path))
        {
            shared_unlock(held);
            return;
        }
        e = &shmods->ent[i];
        snprintf(e->name, sizeof(e->name), "%s", name);
        snprintf(e->img.path, sizeof(e->img.path), "%s", path);
        e->img.off = off;
        e->img.size = size;
        e->img.blk = blocks_alloc(size);
        e->img.nblk = e->img.blk ? (size + BLKSIZE - 1) / BLKSIZE : 0;
        e->links = 0;
        shmods->count++;
    }
    e->links++;
    shared_unlock(held);
}

// Where a module another process loaded came from, so we can read it in too:
// the file, and how far into it the module sits, since a file may hold
// several merged and the one we want may not be the first. A NULL buffer just
// asks whether the directory has it at all.
static int shared_path(const char *name, char *path, size_t pathsize, long *off)
{
    struct sharedmod *e;
    int held, found = 0;

    if(!shmods)
        return 0;
    held = shared_lock();
    if((e = shared_find(name)) != NULL)
    {
        if(path != NULL)
            snprintf(path, pathsize, "%s", e->img.path);
        if(off != NULL)
            *off = e->img.off;
        found = 1;
    }
    shared_unlock(held);
    return found;
}

/*
 * One fewer user, and how many are left. At none the module leaves the
 * directory, as it does on a real system -- and it is up to the caller to
 * unlink as often as it linked. Minus one means there is no shared directory
 * to have counted in, and the caller should fall back to its own tally.
 */
static int shared_release(const char *name)
{
    struct sharedmod *e;
    int held, left = -1;

    if(!shmods)
        return -1;
    held = shared_lock();
    if((e = shared_find(name)) != NULL)
    {
        if((left = --e->links) <= 0)
        {
            left = 0;
            e->links = 0;
            e->name[0] = '\0';
            blocks_free(e->img.blk, e->img.nblk);
            e->img.blk = e->img.nblk = 0;
            shmods->count--;
        }
    }
    shared_unlock(held);
    return left;
}

/*
 * A still picture of the directory, for a caller that wants to walk it --
 * which is what F$GModDr hands over. Returns how many entries were live.
 */
static int shared_copy(struct sharedmod *out, int max)
{
    int held, i, n = 0;

    if(!shmods)
        return 0;
    held = shared_lock();
    for(i = 0; i < SHMODS && n < max; i++)
        if(shmods->ent[i].links > 0)
            out[n++] = shmods->ent[i];
    shared_unlock(held);
    return n;
}

// Which image is holding a block of the fake memory. It is the question
// F$CpyMem is really asking when it is handed a DAT image to read through --
// and the answer may be a module in the directory or the program a process is
// running, since procs asks it the same way mdir does.
static int shared_block(int blk, struct sharedimg *out)
{
    struct sharedimg *g;
    int held, i, found = 0;

    if(!shmods || blk < SYSBLKS)
        return 0;
    held = shared_lock();
    for(i = 0; i < SHMODS + SHPROCS && !found; i++)
    {
        if(i < SHMODS)
        {
            if(shmods->ent[i].links <= 0)
                continue;
            g = &shmods->ent[i].img;
        }
        else
        {
            if(!shmods->proc[i - SHMODS].used)
                continue;
            g = &shmods->proc[i - SHMODS].img;
        }
        if(g->nblk > 0 && blk >= g->blk && blk < g->blk + g->nblk)
        {
            *out = *g;
            found = 1;
        }
    }
    shared_unlock(held);
    return found;
}

/*
 * The processes.
 *
 * A row belongs to the process in it, which keeps it current and gives it
 * back on the way out. One that dies without doing so -- killed, or crashed
 * -- leaves its row behind, so anybody walking the table drops the rows whose
 * host process has gone first. Asking the host is the only way to know: there
 * is nobody here to notice a death but the next process to look.
 */
static void shproc_reap(void)		// with the lock held
{
    struct sharedproc *p;
    int i;

    for(i = 0; i < SHPROCS; i++)
    {
        p = &shmods->proc[i];
        if(!p->used || p->host == 0)
            continue;
        if(kill(p->host, 0) == 0 || errno != ESRCH)
            continue;
        blocks_free(p->img.blk, p->img.nblk);
        memset(p, 0, sizeof(*p));
    }
}

// Take a row, and hand back the OS9 process id that goes with it. The caller
// forks afterwards, so the row is claimed before either half of the fork can
// need it -- and freed again if the fork does not happen.
static int shproc_alloc(int parent, int user, int pages, const char *module)
{
    struct sharedproc *p;
    int held, i, id = -1;

    if(!shmods)
        return -1;
    held = shared_lock();
    shproc_reap();
    for(i = 1; i < SHPROCS; i++)
        if(!shmods->proc[i].used)
        {
            p = &shmods->proc[i];
            memset(p, 0, sizeof(*p));
            p->used = 1;
            p->id = i + 1;
            p->parent = parent;
            p->user = user;
            p->prior = p->age = 128;
            p->pages = pages;
            snprintf(p->module, sizeof(p->module), "%s", module ? module : "");
            id = i + 1;
            break;
        }
    shared_unlock(held);
    return id;
}

static void shproc_claim(int id, pid_t host)
{
    if(!shmods || id < 1 || id > SHPROCS)
        return;
    shmods->proc[id - 1].host = host;
}

static void shproc_free(int id)
{
    struct sharedproc *p;
    int held;

    if(!shmods || id < 1 || id > SHPROCS)
        return;
    held = shared_lock();
    p = &shmods->proc[id - 1];
    blocks_free(p->img.blk, p->img.nblk);
    memset(p, 0, sizeof(*p));
    shared_unlock(held);
}

// What a process is running, once it knows: the name for procs to print and
// the file to read the header and that name back out of.
static void shproc_setimg(int id, const char *module, const char *path,
                          long off, int size, int pages)
{
    struct sharedproc *p;
    int held;

    if(!shmods || id < 1 || id > SHPROCS)
        return;
    held = shared_lock();
    p = &shmods->proc[id - 1];
    blocks_free(p->img.blk, p->img.nblk);
    snprintf(p->module, sizeof(p->module), "%s", module);
    snprintf(p->img.path, sizeof(p->img.path), "%s", path);
    p->img.off = off;
    p->img.size = size;
    p->img.blk = blocks_alloc(size);
    p->img.nblk = p->img.blk ? (size + BLKSIZE - 1) / BLKSIZE : 0;
    p->pages = pages;
    shared_unlock(held);
}

static int shproc_copy(int id, struct sharedproc *out)
{
    int held, found = 0;

    if(!shmods || id < 1 || id > SHPROCS)
        return 0;
    held = shared_lock();
    shproc_reap();
    if(shmods->proc[id - 1].used)
    {
        *out = shmods->proc[id - 1];
        found = 1;
    }
    shared_unlock(held);
    return found;
}

// The host process an OS9 process id belongs to, so that F$Send can reach
// any process in the machine and not only the ones this one forked.
static pid_t shproc_host(int id)
{
    if(!shmods || id < 1 || id > SHPROCS || !shmods->proc[id - 1].used)
        return 0;
    return shmods->proc[id - 1].host;
}

// Where our stack is now. procs prints it, and it is the one field of a row
// that moves under its owner's feet, so it is written on the way into every
// system call rather than kept up to date instruction by instruction.
static void shproc_sp(unsigned sp)
{
    if(shmods && myproc >= 1 && myproc <= SHPROCS)
        shmods->proc[myproc - 1].sp = sp;
}

static void shproc_atexit(void)
{
    if(myproc > 0)
        shproc_free(myproc);
    myproc = -1;
}

/*
 * S$Wake arrives as SIGUSR1 and has nothing to do but interrupt a poll().
 */
static void wake_handler(int)
{
}

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
    for(inx=0; inx < DESMAX; inx++)
        ssig[inx] = 0;
    icpt_pc = icpt_u = 0;
    dev_end = 0;
    memset(pids, 0, sizeof(pids));
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

    // Before anything forks, so that every process after this inherits it.
    shared_init();

    // Set up stdin, stdout and stderr.
    paths[0] = tmpdev->open(stdin);
    paths[1] = tmpdev->open(stdout);
    paths[2] = tmpdev->open(stderr);

    snprintf(cwd, sizeof(cwd), "%s", os9cfg.workdir);
    snprintf(cxd, sizeof(cxd), "%s", os9cfg.execdir);

    loadrcfile();

    /*
     * S$Wake arrives from another process as SIGUSR1, and all it has to do is
     * break the poll() a sleeping process is sitting in. No SA_RESTART, or it
     * would not even do that.
     */
    {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = wake_handler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGUSR1, &sa, NULL);
    }

    // Set up some PIDs This process is hardcoded to PID #1

    /*
     * And a row in the process table, so that this process has an OS9 id the
     * rest of the machine agrees with. Everything it forks takes a row of its
     * own; the module each one is running is filled in by loadmodule, which
     * is where the pathlist is finally resolved.
     */
    atexit(shproc_atexit);
    if((myproc = shproc_alloc(0, getuid() & 0xffff, 0, "")) > 0)
        shproc_claim(myproc, getpid());
    else
        myproc = 2;
    pids[myproc] = getpid();
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
    
    /*
     * Where to look for the program named on our own command line.
     *
     * OS9 looks in the execution directory and nowhere else: F$Load opens with
     * EXEC. set, and ioman starts a relative pathlist from the execution
     * directory whenever that bit is on (L0349 in ioman.asm). A bare name is
     * a module name and gets exactly that -- "os9emu echo" finds the echo in
     * CMDS, and nothing else, however many other files called echo are lying
     * around.
     *
     * A name with a "/" in it is a pathname, though, and "./prog" typed at a
     * host shell means the prog here. We are the thing being typed at, and we
     * have no procedure file to fall back on the way the OS9 shell does, so
     * such a name gets the working directory tried after the execution one.
     * An absolute path comes out the same both times round.
     */
    int trycwd = strchr(filename, '/') != NULL;

    fd = NULL;
    dev = NULL;
    for(int xdir = 1; xdir >= 0 && !fd; xdir--)
    {
        devdrvr *trydev;

        if(!xdir && !trycwd)
            break;
        getpath((Byte*)filename, tmpfn, xdir);
        trydev = find_device(tmpfn);
        if(!trydev)
            continue;
        dev = trydev;
        trydev->errorcode = 0;
        fd = trydev->open((char*)&tmpfn[strlen(trydev->mntpoint)], 1, 0);
    }
    if (!dev)
    {
        sys_error(221);
        return;
    }
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
    // loaded goes with it -- and so do its intercept routine and any signal a
    // path still owed it. The paths stay open across a fork; the process that
    // registered for the signal does not.
    mod_end = 0;
    modtop = MEMTOP;
    icpt_pc = icpt_u = 0;
    for(i = 0; i < DESMAX; i++)
        ssig[i] = 0;

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

    /*
     * What this process is now running, for procs to name, and how much data
     * area it settled on. The pathlist is only resolved here, which is why
     * the row cannot be filled in at the fork that made the process.
     */
    {
        char who[64];

        if(modname(STARTPROG, who, sizeof(who)))
            shproc_setimg(myproc, who, (const char *)tmpfn, 0, modsize,
                          (uppermem - lowermem) >> 8);
    }

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
    int len, i, pages, child;

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

    /*
     * The child's row in the process table is taken before the fork, so that
     * neither half has to wait on the other to know what its id is. Both then
     * write the same host process into it, which is the one thing only the
     * fork can tell them.
     */
    child = shproc_alloc(myproc, getuid() & 0xffff, pages, "");
    if(child < 0)
    {
        // No shared table to hand ids out; any free slot of our own will do.
        for(i = 2; i <= PIDMAX && child < 0; i++)
            if(i != myproc && pids[i] == 0)
                child = i;
    }
    if(child < 0)
    {
        sys_error(E_PrcFul);
        return;
    }

    pid = fork();
    if(pid == 0)
    {
        // In the child: replace this process with the new program. Our whole
        // machine was copied by fork(), so the paths and directories the child
        // inherits are exactly the ones OS9 would have given it.
        myproc = child;
        shproc_claim(child, getpid());
        loadmodule((char*)&memory[x],(char*)parm,pages);
    }
    else if(pid < 0)
    {
        shproc_free(child);
        sys_error(E_PrcFul);
    }
    else
    {
        x += getpath(&memory[x],upath,1);

        shproc_claim(child, pid);

        // Hand back the OS9 process id, and remember which host process it
        // is. The child gives its row in the shared table back on the way
        // out, and F$Wait is asked after that has happened -- so the mapping
        // F$Wait answers from has to be one of our own.
        pids[child] = pid;
        a = (Byte)child;
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

    a = 0;
    for(i = 1; i <= PIDMAX; i++)
        if(pids[i] == pid)
        {
            a = (Byte)i;
            pids[i] = 0;	// the id is free for the next child to take
        }
    b = WIFEXITED(status) ? (Byte)WEXITSTATUS(status) : 0;
}

/*
 * F$Sleep. X = 0 sleeps until a signal arrives -- which is how a shell waits
 * for a keystroke it has asked SS_SSig to tell it about. X = 1 gives up the
 * timeslice, and anything else is a tick count.
 *
 * This used to be wait() for a child process, which returns at once when
 * there are none: shellplus asks for the endless sleep and got a busy loop.
 */
void os9::f_sleep()
{
    if(x == 1)
        return;
    wait_signal(x == 0 ? -1 : (x / 100) * 1000);
}

/*
 * I just ignore any unlinks
 */
/*
 * F$UnLink: U holds the header address of a module the caller is done with.
 * When the last link goes, so does the module -- which is what gives its
 * memory back.
 */
/*
 * Give a module's space back and forget we had it. The link count that says
 * when to do this is the system-wide one; see shared_release.
 */
void os9::release_module(int slot)
{
    if(debug_syscall)
        fprintf(stderr,"'os9::released %s at %04x\n",
                moddir[slot].name,moddir[slot].addr);
    memmove(&moddir[slot], &moddir[slot+1],
            (size_t)(mod_end - slot - 1) * sizeof(moddir[0]));
    mod_end--;
    reclaim_modules();
}

void os9::f_unlk()
{
    int i;

    for(i = 0; i < mod_end; i++)
    {
        int left;

        if(moddir[i].addr != u)
            continue;

        /*
         * The count belongs to the machine, not to us: another process may
         * still be linked to this module, and our copy of it has to stay
         * where it is until nobody is. Without a shared directory to ask,
         * fall back to the tally we keep ourselves.
         */
        left = shared_release(moddir[i].name);
        if(left == 0 || (left < 0 && --moddir[i].links <= 0))
            release_module(i);
        return;
    }
    // Unlinking something we never linked is not worth an error: the caller
    // is only saying it has finished with it.
}

/*
 * F$UnLoad: A is the module type and X names the module. It is F$UnLink by
 * name rather than by address, and it is what the Level 2 shell uses where
 * the Level 1 shell uses F$Link followed by two F$UnLinks.
 *
 * Doing nothing here -- which is what we used to do -- left the module area
 * growing by one command for every command the shell ran, until it came down
 * to meet the shell's own data. The C compiler got three passes in and then
 * stopped with "process memory full".
 */
void os9::f_unload()
{
    char name[64];
    Word i, n, entry = x;
    int slot;

    f_prsnam();
    if(cc.bit.c)
        return;

    n = b;
    for(i = 0; i < n && i + 1 < sizeof(name); i++)
        name[i] = memory[(Word)(x + i)] & 0x7f;
    name[i] = '\0';

    slot = findmodule(name);
    if(slot < 0 && !shared_path(name, NULL, 0, NULL))
    {
        x = entry;
        sys_error(E_MNF);
        return;
    }

    // F$FModul leaves the caller's X past the name it consumed, and F$UnLoad
    // hands that back -- unlike F$Link, which does not. See "X after a name".
    x += n;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_unload: %s\n",name);

    // A module this process never had in its own memory is still one it can
    // hold a link to: the directory is shared even where the memory is not.
    {
        int left = shared_release(name);

        if(slot >= 0 && (left == 0 || (left < 0 && --moddir[slot].links <= 0)))
            release_module(slot);
    }
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
 * Where a real Level 2 kernel keeps the module directory: entries from $0A00
 * upwards, and the DAT image each entry points at from $1000 downwards, with
 * the two growing towards each other in one region (krn.asm sets D.ModDir,
 * D.ModDir+2 and D.ModDAT to exactly these). F$GModDr copies the whole region
 * and mdir reads the images out of its own copy, so the layout has to be one
 * piece and the addresses have to be the ones we claim they are.
 */
#define MD_BASE   0x0a00
#define MD_SIZE   0x0600
#define MD_ESIZE  8			// MD$MPDAT, MD$MBSiz, MD$MPtr, MD$Link
#define MD_IMGSZ  16			// a DAT image covers 8 blocks

/*
 * F$GModDr: hand back a copy of the module directory.
 *
 * Entry: X = a buffer -- 2K on a real system, and mdir gives 4K
 * Exit:  Y = past the last entry of the copy, U = where the directory sits on
 *        the system side, so the caller can translate the pointers inside it
 *
 * This is the half of a shared module directory that a utility can see. The
 * table itself has been shared since modules started outliving the process
 * that loaded them; until something answered this call, nothing could read it
 * back and mdir printed a correct heading over nothing.
 *
 * mdir translates an entry's MD$MPDAT by the distance between the address we
 * report in U and the buffer it gave us, so an entry's DAT image has to
 * travel in the same copy at the offset the pointer claims.
 */
void os9::f_gmoddr()
{
    struct sharedmod ent[SHMODS];
    Byte dir[MD_SIZE];
    int n = shared_copy(ent, SHMODS), i, j;
    Word buf = x;

    memset(dir, 0, sizeof(dir));
    for(i = 0; i < n; i++)
    {
        Word e = (Word)(i * MD_ESIZE);
        Word img = (Word)(MD_SIZE - MD_IMGSZ * (i + 1));
        Word bsize = (Word)(ent[i].img.nblk * BLKSIZE);

        dir[e]     = (Byte)((MD_BASE + img) >> 8);	// MD$MPDAT
        dir[e + 1] = (Byte)(MD_BASE + img);
        dir[e + 2] = (Byte)(bsize >> 8);		// MD$MBSiz
        dir[e + 3] = (Byte)bsize;
        dir[e + 4] = 0;					// MD$MPtr: a module
        dir[e + 5] = 0;					// of ours starts its block
        dir[e + 6] = (Byte)(ent[i].links >> 8);		// MD$Link
        dir[e + 7] = (Byte)ent[i].links;

        for(j = 0; j < ent[i].img.nblk && j < MD_IMGSZ / 2; j++)
            dir[img + j * 2 + 1] = (Byte)(ent[i].img.blk + j);
    }

    for(i = 0; i < MD_SIZE; i++)
        memory[(Word)(buf + i)] = dir[i];

    y = (Word)(buf + n * MD_ESIZE);
    u = MD_BASE;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_gmoddr: %d modules\n",n);
}

/*
 * F$GPrDsc: hand back a copy of a process descriptor.
 *
 * Entry: A = the process id, X = a 512-byte buffer
 *
 * Same shape as F$GModDr and for the same reason: under Level 2 the table is
 * in an address space the caller cannot reach, so procs asks for a row rather
 * than walking to it. The row is built from the shared process table -- which
 * is what makes an OS9 process id mean the same thing to every process here.
 *
 * The primary module is named the way mdir names one: P$DATImg holds the
 * blocks of the program the process is running, P$PModul is where it starts
 * inside them, and procs reads the header and the name through F$CpyMem.
 */
#define PD_SIZE 0x200

void os9::f_gprdsc()
{
    struct sharedproc p;
    Byte pd[PD_SIZE];
    Word buf = x;
    int i;

    if(!shproc_copy(a, &p))
    {
        sys_error(E_IPrcID);
        return;
    }

    memset(pd, 0, sizeof(pd));
    pd[0x00] = (Byte)p.id;			// P$ID
    pd[0x01] = (Byte)p.parent;			// P$PID
    pd[0x04] = (Byte)(p.sp >> 8);		// P$SP
    pd[0x05] = (Byte)p.sp;
    pd[0x06] = (Byte)p.id;			// P$Task
    pd[0x07] = (Byte)p.pages;			// P$PagCnt
    pd[0x08] = (Byte)(p.user >> 8);		// P$User
    pd[0x09] = (Byte)p.user;
    pd[0x0a] = (Byte)p.prior;			// P$Prior
    pd[0x0b] = (Byte)p.age;			// P$Age
    pd[0x0c] = (Byte)p.state;			// P$State
    pd[0x11] = 0;				// P$PModul: a module of ours
    pd[0x12] = 0;				// starts its first block

    for(i = 0; i < p.img.nblk && i < 32; i++)	// P$DATImg
        pd[0x40 + i * 2 + 1] = (Byte)(p.img.blk + i);

    for(i = 0; i < PD_SIZE; i++)
        memory[(Word)(buf + i)] = pd[i];

    if(debug_syscall)
        fprintf(stderr,"'os9::f_gprdsc: %d is %s\n",p.id,p.module);
}

/*
 * F$GBlkMp: hand back a copy of the system's memory block map.
 *
 * Entry: X = a 1K buffer
 * Exit:  D = bytes per block, Y = how long the map is
 *
 * One byte a block, zero meaning free, which is what mfree counts and what
 * procs takes the block size out of. The memory is the fake one the module
 * directory is expressed in -- a module in the directory or a program a
 * process is running holds blocks of it -- so what mfree prints is the room
 * left for those, not the room left in anybody's 64K.
 */
void os9::f_gblkmp()
{
    unsigned char map[SHBLKS];
    int i;

    memset(map, 0, sizeof(map));
    if(shmods)
        for(i = 0; i < SHBLKS; i++)
            map[i] = shmods->blkmap[i];

    for(i = 0; i < SHBLKS; i++)
        memory[(Word)(x + i)] = map[i];

    a = (Byte)(BLKSIZE >> 8);
    b = (Byte)BLKSIZE;
    y = SHBLKS;
}

/*
 * Read a module's image back off the disk it came from. A module is read-only
 * by rule, so the file is as good as the memory somebody else has it in --
 * and it is the only copy we can reach, since what the machine shares is the
 * directory and not the module memory.
 */
int os9::module_bytes(const char *path, long off, Byte *dst, int count)
{
    Byte upath[512];
    devdrvr *dev;
    fdes *fd;
    int got = 0, val;

    snprintf((char *)upath, sizeof(upath), "%s", path);
    if(!(dev = find_device(upath)))
        return 0;
    fd = dev->open((char *)&upath[strlen(dev->mntpoint)], 5, 0);
    if(!fd)
        return 0;
    if(off > 0 && fd->seek((int)off) != 0)
        count = 0;
    while(got < count && (val = fd->read(dst + got, count - got)) > 0)
        got += val;
    fd->close();
    if(fd->usecount == 0) delete fd;
    return got;
}

/*
 * F$CpyMem: copy out of an address space the caller cannot reach.
 *
 * Entry: D = a DAT image -- the address, in the caller's own memory, of the
 *            block list of the space to read from
 *        X = the offset within that space, Y = a byte count, U = where to put
 *            what comes back
 *
 * The images we hand out through F$GModDr name modules in the shared
 * directory, so the block at the head of the list says which module and X
 * says how far into it. Anything else is a caller reading a space of its own
 * describing, and the copy stays inside its own memory -- which is what this
 * call did for every caller before there was a directory to name.
 */
void os9::f_cpymem()
{
    struct sharedimg e;
    Word dat = (Word)((a << 8) | b);
    Word count = y, end = (Word)(u + count);
    int blk = (memory[dat] << 8) | memory[(Word)(dat + 1)];
    long off;

    // What a real kernel refuses: nothing to do, or a copy that would run
    // into the vector and I/O pages. Neither is an error.
    if(count == 0 || (end >> 8) >= 0xfe)
        return;

    if(!shared_block(blk, &e))
    {
        Word i;

        for(i = 0; i < count; i++)
            memory[(Word)(u + i)] = memory[(Word)(x + i)];
        return;
    }

    off = (long)(blk - e.blk) * BLKSIZE + x;
    memset(&memory[u], 0, count);
    module_bytes(e.path, e.off + off, &memory[u], count);

    if(debug_syscall)
        fprintf(stderr,"'os9::f_cpymem: %d bytes of %s at %ld\n",
                count,e.path,off);
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
    pid_t target = shproc_host(a);

    if(debug_syscall)
        fprintf(stderr,"'os9::f_send: pid %d signal %d\n",a,b);

    // The shared process table names every process in the machine, not only
    // the ones this one forked -- which is what an OS9 process id is for.
    if(target == 0 && slot >= 1 && slot <= PIDMAX)
        target = pids[slot];
    if(target == 0)
    {
        sys_error(E_IPrcID);
        return;
    }

    if(b == S_Kill)
    {
        if(kill(target, SIGTERM) == -1)
            sys_error(E_IPrcID);
    }
    else if(b == S_Wake)	// wake a process out of F$Sleep
    {
        if(kill(target, SIGUSR1) == -1)
            sys_error(E_IPrcID);
    }
    // The rest of the signal codes carry meaning we have nowhere to put: a
    // host signal cannot bring the code with it.
}

/*
 * F$Icpt: X names the routine to enter when a signal arrives and U the memory
 * pointer to hand it. X = 0 takes the intercept away again.
 */
void os9::f_icpt()
{
    icpt_pc = x;
    icpt_u  = u;

    if(debug_syscall)
        fprintf(stderr,"'os9::f_icpt: routine %04x u=%04x\n",icpt_pc,icpt_u);
}

/*
 * Deliver a signal the way OS9 does. The process's registers go onto its own
 * stack as an ordinary interrupt frame and we vector to the intercept routine
 * with the signal code in B and its memory pointer in U. The routine ends in
 * RTI, which puts the frame back and carries on from wherever we were -- the
 * instruction after the system call that was interrupted.
 *
 * Nothing to vector to means nothing happens. A real system would kill the
 * process for most codes; here the callers all want a wake-up, and a program
 * that never asked for an intercept still wants to be woken.
 */
int os9::deliver_signal(Byte code)
{
    if(icpt_pc == 0)
        return 0;

    cc.bit.e = 1;			// a whole frame, which is what RTI expects
    s -= 2; write_word(s, pc);
    s -= 2; write_word(s, u);
    s -= 2; write_word(s, y);
    s -= 2; write_word(s, x);
    write(--s, dp);
    write(--s, b);
    write(--s, a);
    write(--s, cc.all);

    b  = code;
    u  = icpt_u;
    pc = icpt_pc;

    if(debug_syscall)
        fprintf(stderr,"'os9::signal %d to %04x\n",code,icpt_pc);
    return 1;
}

/*
 * Wait until a path we were asked to signal on has something to read, and
 * deliver the signal when it does. A negative timeout waits indefinitely.
 *
 * This is the whole of our signalling: SS_SSig is the only thing that raises
 * one from inside the process, and SIGUSR1 from F$Send the only thing that
 * raises one from outside. Neither can arrive while the 6809 is between
 * instructions, so both are collected here, where the program has asked to
 * wait for them.
 */
int os9::wait_signal(int timeout)
{
    struct pollfd pfd[DESMAX];
    int slot[DESMAX];
    int n = 0, i;

    for(i = 0; i < DESMAX; i++)
    {
        int fd;

        if(ssig[i] == 0 || paths[i] == NULL)
            continue;
        if((fd = paths[i]->hostfd()) < 0)
            continue;
        pfd[n].fd = fd;
        pfd[n].events = POLLIN;
        pfd[n].revents = 0;
        slot[n++] = i;
    }

    if(n == 0)
    {
        /*
         * Nothing registered that could wake us. A timed sleep is just slept.
         * An endless one would hang the emulator with no way out, so wait a
         * little and let the caller come round again -- which idles where it
         * used to spin, and still returns if a signal arrives meanwhile.
         */
        poll(NULL, 0, timeout < 0 ? 100 : timeout);
        return 0;
    }

    if(poll(pfd, n, timeout) <= 0)
        return 0;			// timed out, or SIGUSR1 woke us

    for(i = 0; i < n; i++)
    {
        if(pfd[i].revents == 0)
            continue;
        // A driver sends its SS_SSig signal once and forgets the request; the
        // caller sets it up again next time round its loop.
        Byte code = ssig[slot[i]];
        ssig[slot[i]] = 0;
        return deliver_signal(code);
    }
    return 0;
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
 * What a link or a load reports about the module it found.
 *
 * The ordinary calls say where it is: U the module header, Y the execution
 * entry point. The CoCo 3 "non-mapping" pair say what it *needs* instead --
 * Y comes back as M$Mem, the module's memory requirement, and U is left
 * alone. A Level 2 caller cannot read the header for itself, since the module
 * is not in its address space, so the kernel reads it out on the way past
 * (FNMLink in ../nitros9/level2/modules/ioman.asm).
 *
 * The shell turns on the difference. Its Level 2 build leaves out the
 * "ldy M$Mem,y" its Level 1 build does, and hands what came back straight to
 * F$Fork as a page count -- so answering with an address, which is what we
 * used to do, asks F$Fork for the whole address space. Every command the
 * Level 2 shell ran got a data area with nothing above it for F$Mem to add,
 * which is the ground the C runtime's sbrk allocates out of, and the C
 * compiler died in c.prep with "grab overlap".
 */
void os9::modregs(Word base, int nonmapping)
{
    a = memory[(Word)(base + 6)];		// M$Type
    b = memory[(Word)(base + 7)];		// M$Revs

    if(nonmapping)
        y = (Word)((memory[(Word)(base + 0x0b)] << 8) |
                    memory[(Word)(base + 0x0c)]);	// M$Mem
    else
    {
        u = base;
        y = (Word)(base + ((memory[(Word)(base + 9)] << 8) |
                            memory[(Word)(base + 10)]));	// M$Exec
    }
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
void os9::f_link(int nonmapping)
{
    char name[64];
    Word i, n, entry = x;
    int slot;

    // A module name, not a pathlist -- parse it where it stands.
    f_prsnam();
    if(cc.bit.c)
        return;

    n = b;
    for(i = 0; i < n && i + 1 < sizeof(name); i++)
        name[i] = memory[(Word)(x + i)] & 0x7f;
    name[i] = '\0';

    slot = findmodule(name);
    if(slot < 0)
    {
        /*
         * Not in this process's memory. The directory the machine shares says
         * whether another process has it and where it came from -- and an OS9
         * module is position-independent and re-entrant, so reading it in
         * again here is as good as having been the one who loaded it.
         */
        Byte upath[512];
        Word base;
        long off = 0;

        if(shared_path(name, (char *)upath, sizeof(upath), &off))
        {
            if((base = load_image(upath, off)) != 0 &&
               (slot = register_module(base, name)) >= 0)
                moddir[slot].links = 0;		// the bump below makes it one
            else
            {
                x = entry;
                if(!cc.bit.c)
                    sys_error(E_MemFul);
                return;
            }
        }
    }

    if(slot < 0)
    {
        /*
         * Hand the caller back the X it gave us. Level 1's FLink only writes
         * R$X on the way out with a module; on E$MNF the caller's registers
         * are its own. The shell relies on that: when a link fails it opens
         * the name from wherever X now points, so anything we consumed here
         * is lost to it -- and F$PrsNam consumes the leading "/", which is
         * how "/dd/BIN/prog" arrived at I$Open as a relative "dd/BIN/prog"
         * and got the execution directory pasted in front of it.
         */
        x = entry;
        if(debug_syscall)
            fprintf(stderr,"'os9::f_link: %s not loaded\n",name);
        sys_error(E_MNF);
        return;
    }

    /*
     * A link that found something hands X back past the name it consumed --
     * "the address of the last byte of the module name, plus 1". A link that
     * did not hands back the X it was given, which is the branch above. The
     * two are not the same call from the caller's side: "link a b c" walks
     * its parameter line by the X that comes back, and with X left where it
     * started it linked the first name for ever.
     */
    x += n;

    moddir[slot].links++;
    shared_add(name, NULL, 0, 0);
    modregs(moddir[slot].addr, nonmapping);

    if(debug_syscall)
        fprintf(stderr,"'os9::f_link%s: %s at %04x\n",
                nonmapping ? " (nm)" : "",name,moddir[slot].addr);
}

/*
 * Read a module in from an OS9 pathlist and place it at the top of this
 * process's memory, below anything already there. Returns where it went, or
 * zero with the error already reported.
 */
Word os9::load_image(Byte *upath, long off)
{
    devdrvr *dev;
    fdes *fd;
    Word base;

    dev = find_device(upath);
    if(!dev)
    {
        sys_error(E_MNF);
        return 0;
    }
    fd = dev->open((char*)&upath[strlen(dev->mntpoint)],5,0);
    if(!fd)
    {
        sys_error(dev->errorcode ? dev->errorcode : E_PNNF);
        return 0;
    }
    if(off > 0 && fd->seek((int)off) != 0)
    {
        fd->close();
        if(fd->usecount == 0) delete fd;
        sys_error(E_BMID);
        return 0;
    }

    base = read_module(fd, off == 0);

    fd->close();
    if(fd->usecount == 0) delete fd;
    return base;
}

/*
 * Read the module the path is positioned at into memory, placing it at the
 * top and below anything already there. Returns where it went, or zero -- and
 * an error only if the caller says a module was expected here at all, since
 * walking a file of them ends by finding that there is not another.
 *
 * Reporting the header without actually loading it -- which this used to do
 * -- left the caller reading whatever was at address 0, which is its own
 * image: runb concluded that every packed procedure it was handed had a
 * compiler error in it.
 */
Word os9::read_module(fdes *fd, int required)
{
    unsigned char modhead[14];
    Word base, addr, modsize;
    int val;

    if(fd->read(modhead,14) < 14 || modhead[0] != 0x87 || modhead[1] != 0xcd)
    {
        if(required)
            sys_error(E_BMID);
        return 0;
    }

    modsize = (modhead[2] << 8) | modhead[3];

    base = (Word)((modtop - modsize) & 0xff00);
    if(modsize == 0 || base < uppermem || base > modtop)
    {
        sys_error(E_MemFul);
        return 0;
    }

    memcpy(&memory[base], modhead, 14);
    addr = base + 14;
    while(addr < (Word)(base + modsize) &&
          (val = fd->read(&memory[addr], (base + modsize) - addr)) > 0)
        addr += val;

    modtop = base;
    return base;
}

/*
 * F$Load's real work: every module in the file, not just the first.
 *
 * A module file may hold several merged, and the Level 2 CMDS/shell is nine
 * of them -- shellplus and the date, deiniz, echo, iniz, link, load, save and
 * unlink it expects to find resident afterwards. Loading only the first left
 * the other eight where they were, so the shell's own "load" could never make
 * good on what the file was merged for.
 *
 * Each module goes in the directory the machine shares, with the offset it
 * sits at in the file, so that another process linking it later reads back
 * the right one. Returns the first, which is what F$Load reports on.
 */
Word os9::load_file(Byte *upath)
{
    devdrvr *dev;
    fdes *fd;
    Word first = 0, base;
    long off = 0;

    dev = find_device(upath);
    if(!dev)
    {
        sys_error(E_MNF);
        return 0;
    }
    fd = dev->open((char*)&upath[strlen(dev->mntpoint)],5,0);
    if(!fd)
    {
        sys_error(dev->errorcode ? dev->errorcode : E_PNNF);
        return 0;
    }

    while((base = read_module(fd, first == 0)) != 0)
    {
        char name[64];
        Word modsize = (Word)((memory[(Word)(base + 2)] << 8) |
                               memory[(Word)(base + 3)]);

        if(first == 0)
            first = base;
        if(modname(base, name, sizeof(name)))
        {
            register_module(base, name);
            shared_add(name, (const char *)upath, off, modsize);
        }
        off += modsize;
        if(fd->seek((int)off) != 0)
            break;
    }

    fd->close();
    if(fd->usecount == 0) delete fd;

    // A module we could not place is not a reason to lose the ones we did.
    if(first != 0)
        cc.bit.c = 0;
    return first;
}

/*
 * Remember a module this process now holds, and hand back its slot.
 */
int os9::register_module(Word base, const char *name)
{
    Word modsize = (Word)((memory[(Word)(base + 2)] << 8) |
                           memory[(Word)(base + 3)]);

    if(mod_end >= (int)(sizeof(moddir)/sizeof(moddir[0])))
        return -1;
    snprintf(moddir[mod_end].name, sizeof(moddir[mod_end].name), "%s", name);
    moddir[mod_end].addr = base;
    moddir[mod_end].end = (Word)(base + modsize);
    moddir[mod_end].links = 1;
    return mod_end++;
}

void os9::f_load(int nonmapping)
{
    Byte upath[512];
    Word base;

    x += getpath(&memory[x],upath,1);

    if((base = load_file(upath)) == 0)
        return;

    modregs(base, nonmapping);

    if(debug_syscall)
        fprintf(stderr,"'os9::f_load%s: %s type=%02X at %04x\n",
                nonmapping ? " (nm)" : "",(char*)upath,a,base);
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
    a = (Byte)(myproc > 0 ? myproc : 1);
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
    if(b == SS_FDInf)
    {
        // Which descriptor is wanted: the top byte of the number in Y's MSB,
        // the other two in U. Y's LSB is how much of it the caller wants.
        statbuf.lsn = ((unsigned long)(y >> 8) << 16) | u;
    }
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
        case SS_FDInf:
            // Here only the low byte of Y is the count; the high byte was
            // part of the sector number on the way in.
            for(inx = 0; inx < (y & 0xff) &&
                         inx < (int)sizeof(statbuf.filler); inx++)
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

    Byte path = a, code = (Byte)(x & 0xff);
    int opcode = b;

    paths[a]->errorcode = 0;
    paths[a]->setstatus(opcode,&statbuf);
    if(paths[a]->errorcode)
    {
        sys_error(paths[a]->errorcode);
        return;
    }

    /*
     * Which process to signal is not something a driver here can know, so the
     * two calls that ask for one are answered by the kernel. SS_SSig asks for
     * a signal when input turns up on this path -- X carries the code to send
     * -- and SS_Relea takes the request away again.
     */
    if(opcode == SS_SSig)
    {
        ssig[path] = code;
        // A driver whose input is already waiting sends the signal there and
        // then rather than holding on to the request (RSendSig in mc6850.asm).
        if(fd_ready(paths[path]->hostfd()))
        {
            ssig[path] = 0;
            deliver_signal(code);
        }
    }
    else if(opcode == SS_Relea)
        ssig[path] = 0;
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

    /*
     * Resolve the dots now, before the name is remembered. Storing "/h0/T1/.."
     * as it stands left "chd .." sitting where it was and every later relative
     * path built on a directory that grew a component each time.
     */
    canonicalizePath((char*)upath,(char*)upath,strlen(dev->mntpoint),
                     sizeof(upath));

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
    shproc_sp(s);		// the one field of our row that keeps moving
    switch(memory[pc++])
    {
        case 0x00:
            f_link(0);
            break;
        case 0x01:
            f_load(0);
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
        case 0x09:		// F$Icpt
            f_icpt();
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

        case 0x18:		// F$GPrDsc
            f_gprdsc();
            break;
        case 0x19:		// F$GBlkMp
            f_gblkmp();
            break;
        case 0x1a:		// F$GModDr
            f_gmoddr();
            break;
        case 0x1b:		// F$CpyMem
            f_cpymem();
            break;
        case 0x1c:		// F$SUser -- everything here runs as the super user
            break;
        case 0x1d:		// F$UnLoad
            f_unload();
            break;
        case 0x1e:		// F$Alarm -- no clock to hang an alarm off
            break;

        /*
         * The CoCo 3 "non-mapping" pair, which cc1 and the shell reach for by
         * preference. With one address space the work is the same as the
         * ordinary calls do; what they report is not. See modregs().
         */
        case 0x21:		// F$NMLink
            f_link(1);
            break;
        case 0x22:		// F$NMLoad
            f_load(1);
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
