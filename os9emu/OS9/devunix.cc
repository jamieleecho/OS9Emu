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
#include <sys/stat.h>
#include <stdlib.h>
#include <dirent.h>
#include <unistd.h>
#include <time.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <sys/statvfs.h>
}
#include "devdrvr.h"
#include "devunix.h"
#include "errcodes.h"
#include "os9config.h"


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
static const char *findpathseg(const char *dir, const char *segment)
{
    DIR *dirp;
    struct dirent *dp = NULL;
    static char dirname[1024];

    dirp = opendir(dir);
    if(dirp == NULL)		// not a directory, or we may not read it
        return NULL;
    while((dp = readdir(dirp)))
    {
        if(strcasecmp(dp->d_name,segment) == 0)
            break;
    }
    if(dp) {
        strncpy(dirname, dp->d_name, sizeof(dirname) - 1);
        dirname[sizeof(dirname) - 1] = '\0';
    }
    closedir(dirp);
    if(dp)
        return dirname;
    else
        return NULL;
}

/*
 * OS9 names are case insensitive, so the name a program asks for may differ
 * in case from the one on disk. Rewrite each segment to its real spelling.
 *
 * Only the part of the path below rootlen is searched: everything above it is
 * the mount point we were configured with, which is a host path and is used
 * verbatim. Scanning it would mean listing directories we have no business
 * reading -- and failing to open any one of them used to crash us.
 *
 * A match differs from the requested segment only in case, so it always has
 * the same length and can be written back in place.
 */
static char *findpath(char *path, size_t rootlen, bool mustexist)
{
    char *seg = path + rootlen;

    while(*seg == '/')
        seg++;

    while(*seg)
    {
        char *end = strchr(seg, '/');
        char endsave = '\0';
        char sepsave;
        const char *real;

        if(end) {
            endsave = *end;
            *end = '\0';
        }

        // Terminate the parent prefix so it can be opened as a directory
        sepsave = seg[-1];
        seg[-1] = '\0';
        real = findpathseg(path[0] ? path : "/", seg);
        seg[-1] = sepsave;

        if(real)
            memcpy(seg, real, strlen(seg));
        else if(end || mustexist)
            return NULL;		// a parent is missing, or we needed a hit

        if(!end)
            break;
        *end = endsave;
        seg = end + 1;
    }
    return path;
}

/*
 * Resolve a path's dot components, in place when dst == path.
 *
 * A component of nothing but dots goes up one level for every dot after the
 * first: "." stays put, ".." is the parent, "..." the grandparent, and so on
 * for as long as the run continues. Everything else is a name, dots and all:
 * ".profile", "a.b" and "..hidden" are copied across untouched. Repeated
 * slashes collapse and a trailing slash is dropped.
 *
 * The first rootlen characters are kept verbatim and no run of dots ever
 * climbs above them. For a host path that is the mount point -- so no amount of ".." walks
 * out of the OS9 disk and into the rest of the filesystem -- and for an OS9
 * path it is the device name. The root of an OS9 disk is its own parent,
 * which is what OS9 itself does.
 *
 * The result is no longer than the path it came from, save for the separator
 * this has to put in when rootlen does not end at one, so dstsize is here to
 * bound that rather than because a real path ever needs it.
 *
 * Doing this by counting dots as it went, as this used to, mistakes every one
 * of those cases: "/dd/T1/.." came back as "/dd/T1", so "chd .." stayed where
 * it was and "dir T1/.." listed T1; "/dd/./T1" was left with the dot still in
 * it; and a name with a dot in it after any earlier hidden name lost a whole
 * directory component.
 */
void canonicalizePath(char *dst, const char *path, size_t rootlen,
                      size_t dstsize)
{
    size_t len = strlen(path);
    size_t out, floor;
    const char *p;
    bool rooted;

    if(dstsize == 0)
        return;
    if(rootlen > len)
        rootlen = len;
    if(rootlen > dstsize - 1)
        rootlen = dstsize - 1;
    if(dst != path)
        memcpy(dst, path, rootlen);
    out = floor = rootlen;
    p = path + rootlen;
    rooted = rootlen > 0;

    // A leading slash is the root itself, not a separator
    if(out == 0 && *p == '/')
    {
        dst[out++] = '/';
        floor = out;
        rooted = true;
        while(*p == '/')
            p++;
    }

    while(*p)
    {
        const char *seg = p;
        size_t seglen, dots;

        while(*p && *p != '/')
            p++;
        seglen = (size_t)(p - seg);
        while(*p == '/')
            p++;

        if(seglen == 0)			// a run of slashes, or a trailing one
            continue;

        /*
         * A component of nothing but dots is not a name. One dot is this
         * directory and every dot after the first goes up another level, so
         * ".." is the parent, "..." the grandparent, and so on -- which is
         * how a shell walks a path back up: shellplus carries a string of
         * forty of them and points further into it at each step. Anything
         * with a character among the dots is a name: ".profile", "a.b",
         * "..hidden".
         */
        dots = 0;
        while(dots < seglen && seg[dots] == '.')
            dots++;

        if(dots == seglen)
        {
            size_t up;

            for(up = seglen - 1; up > 0; up--)
            {
                if(out > floor)
                {
                    // Back over the last component and the slash before it
                    while(out > floor && dst[out-1] != '/')
                        out--;
                    if(out > floor)
                        out--;
                    continue;
                }
                if(rooted)
                    break;	// at the root: it is its own parent

                /*
                 * A relative path may genuinely start above where it stands.
                 * Keep one ".." for each level we cannot resolve, and put the
                 * floor above it so nothing later backs over it.
                 */
                if(out > 0 && dst[out-1] != '/')
                {
                    if(out + 1 > dstsize - 1)
                        break;
                    dst[out++] = '/';
                }
                if(out + 2 > dstsize - 1)
                    break;
                dst[out++] = '.';
                dst[out++] = '.';
                floor = out;
            }
            continue;
        }

        if(out > 0 && dst[out-1] != '/')
        {
            if(out + 1 > dstsize - 1)
                break;
            dst[out++] = '/';
        }
        if(out + seglen > dstsize - 1)
            break;
        memmove(dst + out, seg, seglen);
        out += seglen;
    }

    if(out == 0 && len > 0 && dstsize > 1)
        dst[out++] = '.';	// everything cancelled out: that is here
    dst[out] = '\0';
}

/*********************************************************************
 * devunix methods
 *********************************************************************/
// The devdrvr class is a base class for virtual devices
// The methods expect pathnames that are relative to the mount pount
// and without leading slash.

devunix::devunix(const char *mntpnt, const char *args) : devdrvr(mntpnt)
{
    unixdir = args;
}

/*
 * What stands in for OS9 sector numbers.
 *
 * A directory entry names the sector its file descriptor lives in, and that
 * number is how callers tell entries apart -- the walk up to the root
 * compares the number recorded for ".." against the entries of the parent.
 * So every host path we hand out an entry for needs its own number, and the
 * table grows to hold as many as the process asks about: it used to hold a
 * hundred, and the root plus /dd/CMDS alone comes to more than that.
 */
static struct {
    char **names;
    int size;
    int capacity;

    int getID(const char *buf, const char *path, size_t rootlen) {
        char buf2[1024];
        
        snprintf(buf2, sizeof(buf2), "%s/%s", buf, path);
        canonicalizePath(buf2, buf2, rootlen, sizeof(buf2));
        return getID(buf2);
    }
    
    int getID(const char *buf) {
        int fid;
        for (fid=0; fid<size; fid++) {
            if (strcmp(buf, names[fid]) == 0)
                return fid;
        }
        // A sector number is 24 bits wide, so that is as many names as can be
        // told apart at all. Nothing comes close to it in practice.
        if (size >= 0xffffff)
            return size - 1;
        if (size == capacity) {
            int grown = capacity ? capacity * 2 : 64;
            char **files = new char *[grown];
            memcpy(files, names, size * sizeof(*files));
            delete [] names;
            names = files;
            capacity = grown;
        }
        names[size] = strdup(buf);
        return size++;
    }

    // The host path a number stands for, or NULL if we never handed it out.
    const char *getName(int fid) {
        return (fid >= 0 && fid < size) ? names[fid] : NULL;
    }
} fileTable;

/*
 * Where a file's entry sits in its parent directory, as a byte offset.
 *
 * OS9 reports this as PD.DCP in the path options, and rename(1) uses it to
 * seek straight to the entry and write a new name over it. The enumeration
 * has to match the one fdirunix::rescan builds for a directory path -- ".."
 * and "." first, then the directory's own order -- or the seek lands on the
 * wrong file.
 */
static int direntry_offset(const char *hostpath)
{
    char dirpart[1024];
    const char *base;
    DIR *dir;
    struct dirent *entry;
    int index = 2;		// ".." and "." occupy the first two slots

    base = strrchr(hostpath, '/');
    if(base == NULL)
        return -1;
    if((size_t)(base - hostpath) >= sizeof(dirpart))
        return -1;
    memcpy(dirpart, hostpath, base - hostpath);
    dirpart[base - hostpath] = '\0';
    base++;

    dir = opendir(dirpart[0] ? dirpart : "/");
    if(dir == NULL)
        return -1;

    while((entry = readdir(dir)) != NULL) {
        if(strcmp(".", entry->d_name) == 0) continue;
        if(strcmp("..", entry->d_name) == 0) continue;
        if(strcmp(base, entry->d_name) == 0) {
            closedir(dir);
            return index * (int)sizeof(os9dentry);
        }
        index++;
    }
    closedir(dir);
    return -1;
}


/* Open a file
 * fixme: Go through the path to see if the path actually exists
 * fixme: Open in other modes than read
 */
fdes *devunix::open(const char *path,int mode,int create)
{
    char buf[1024];
    const char *umode;
    
    /*
     * "@" on the end of a device name asks for the whole device -- the raw
     * sectors. There is no disk behind us, so serve a plausible one built
     * from what the host filesystem reports; free(1) is what asks.
     */
    if (!create && (strcmp(path, "@") == 0 || strcmp(path, "/@") == 0)) {
        const char *vol = strrchr(unixdir, '/');
        fdwhole *fd = new fdwhole(unixdir, vol ? vol + 1 : unixdir);
        fd->usecount = 1;
        fd->driver = this;
        return fd;
    }

    snprintf(buf, sizeof(buf), "%s%s", unixdir, path);
    canonicalizePath(buf, buf, strlen(unixdir), sizeof(buf));

    if (!findpath(buf,strlen(unixdir),!create))
    {
        errorcode = E_PNNF;
        return 0;
    }

    /*
     * OS9 access modes: 1 = read, 2 = write, 3 = update.
     *
     * I$Open never truncates -- that is what I$Create is for -- so opening
     * for write alone still has to be "rb+". Getting this wrong emptied every
     * file a program opened to rewrite in place, attr(1) among them.
     */
    if (create)
        umode = "wb+";
    else if ((mode & 3) == 1 || (mode & 3) == 0)
        umode = "rb";
    else
        umode = "rb+";

    // First open the file/or directory
    FILE *fp = fopen(buf,umode);
    if (fp == NULL && !create && (mode & 3) != 1) {
        // Read-only on disk, or a directory, which cannot be opened "rb+".
        // OS9 reports the failure when the write is attempted, not here.
        fp = fopen(buf, "rb");
    }

    // Are we actually trying to open a directory?
    fdunix *fd;
    struct stat dirst;
    if (stat(buf, &dirst) != -1 && S_ISDIR(dirst.st_mode)) {
        fdirunix *fdir = new fdirunix;
        // Remembered so the listing can be brought back in step with the host,
        // and so a rewritten entry can be turned into a host rename.
        snprintf(fdir->hostdir, sizeof(fdir->hostdir), "%s", buf);
        // ".." of the mount point is the mount point: an OS9 disk's root is
        // its own parent, and pwd stops when it sees the two agree.
        fdir->hostroot = strlen(unixdir);
        if (!fdir->rescan()) {
            // A directory we cannot read is an error, the same as a file we
            // cannot open. Serving it as an empty one would have a caller
            // conclude there is nothing in it.
            delete fdir;
            if (fp != NULL)
                fclose(fp);
            errorcode = (errno == EACCES) ? E_FNA : E_PNNF;
            return 0;
        }
        fd = fdir;
    } else {
        if (fp == NULL) {
            // Not a directory and we could not open it. Say why, since the
            // caller shows the error to the user.
            switch (errno) {
                case EACCES: errorcode = E_FNA;   break;
                case EISDIR: errorcode = E_BMode; break;
                case ENOENT: errorcode = E_PNNF;  break;
                default:     errorcode = E_PNNF;  break;
            }
            return 0;
        }
        fdunix *plain = new fdunix;
        plain->dcp = direntry_offset(buf);
        fd = plain;
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
    
    snprintf(buf, sizeof(buf), "%s/%s", unixdir, path);
    canonicalizePath(buf, buf, strlen(unixdir), sizeof(buf));
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
    
    snprintf(buf, sizeof(buf), "%s/%s", unixdir, path);
    canonicalizePath(buf, buf, strlen(unixdir), sizeof(buf));
    if(unlink(buf) == -1)
        return(errorcode = 216);
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
    fp = NULL;
    dcp = -1;
}


fdunix::~fdunix()
{
    if(usecount && fp)
        fclose(fp);
    usecount--;
}

int fdunix::close()
{
    if(usecount == 1 && fp)
        fclose(fp);
    usecount--;
    return 0;
}

int fdunix::read(Byte *buf, int size)
{
    int c;

    if(!fp)
        return (errorcode = E_BMode), -1;
    c = (int)fread((char*)buf, 1, size, fp);
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

    if(!fp)
        return (errorcode = E_BMode), -1;
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
    y = (int)(p - buf);
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

    if(!fp)
        return (errorcode = E_BMode), -1;
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

    if(!fp)
        return (errorcode = E_BMode), -1;
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
    if(!fp)
        return (errorcode = E_BMode);
    fflush(fp);
    if(fseek(fp, offset, SEEK_SET) == -1)
        return (errorcode = E_Sect);
    return 0;
}

/*
 * Fill in an RBF file descriptor sector, which is what SS_FD hands back:
 *
 *   $00 FD.ATT   attributes: d s pe pw pr e w r
 *   $01 FD.OWN   owner id                     (2 bytes)
 *   $03 FD.DAT   last modified: Y M D H M     (5 bytes, year is -1900)
 *   $08 FD.LNK   link count
 *   $09 FD.SIZ   file size                    (4 bytes)
 *   $0d FD.Creat created: Y M D               (3 bytes)
 *   $10 FD.SEG   segment list
 *
 * We have no sectors to describe, so the segment list stays zero -- which is
 * how a reader is told there are no more segments.
 */
static void fill_fd_sector(statusbuf *status, const struct stat *st)
{
    struct tm *tm;
    time_t mtime = st->st_mtime;
    time_t ctime = st->st_ctime;
    unsigned char *fd = status->filler;

    memset(status, '\0', sizeof(*status));
    fd[0x00] = u2o_attr(st->st_mode);
    fd[0x01] = 0;		// Owner 0, the super user: attr(1) refuses to
    fd[0x02] = 0;		// touch a file owned by somebody else, and
				// F$ID reports us as 0 too.
    if((tm = localtime(&mtime)) != NULL) {
        fd[0x03] = tm->tm_year;
        fd[0x04] = tm->tm_mon + 1;
        fd[0x05] = tm->tm_mday;
        fd[0x06] = tm->tm_hour;
        fd[0x07] = tm->tm_min;
    }
    fd[0x08] = st->st_nlink ? st->st_nlink : 1;
    fd[0x09] = (st->st_size >> 24) & 0xff;
    fd[0x0a] = (st->st_size >> 16) & 0xff;
    fd[0x0b] = (st->st_size >> 8) & 0xff;
    fd[0x0c] = st->st_size & 0xff;
    if((tm = localtime(&ctime)) != NULL) {
        fd[0x0d] = tm->tm_year;
        fd[0x0e] = tm->tm_mon + 1;
        fd[0x0f] = tm->tm_mday;
    }
}

/*
 * How long a directory reads, in bytes: an entry for every host file, plus
 * the ".." and "." that fdirunix::rescan puts in front of them. The host's
 * own idea of a directory's size describes host records, not OS9 ones.
 */
static long dir_listing_size(const char *hostpath)
{
    DIR *dir;
    struct dirent *entry;
    long slots = 2;

    if((dir = opendir(hostpath)) == NULL)
        return 0;
    while((entry = readdir(dir)) != NULL)
        if(strcmp(".", entry->d_name) != 0 && strcmp("..", entry->d_name) != 0)
            slots++;
    closedir(dir);
    return slots * (long)sizeof(os9dentry);
}

/*
 * SS_FDInf: the descriptor of any file on this device, named by the number
 * its directory entry carries rather than by a path. `dir -e` asks for one
 * per entry -- it has the numbers already and opening every file to ask
 * SS_FD would cost a path apiece -- and takes an error as fatal, so without
 * this the whole listing ended at the first line with E$UnkSvc.
 *
 * Our numbers are indices into the table that hands them out, so this is a
 * lookup back to the host path and a stat of it. A number we never issued is
 * a sector that is not a descriptor, which is E$Sect on a real disk.
 */
static int fd_info(unsigned long lsn, statusbuf *status)
{
    struct stat st;
    const char *host = fileTable.getName((int)lsn);

    if(host == NULL || stat(host, &st) == -1)
        return E_Sect;
    fill_fd_sector(status, &st);
    if(S_ISDIR(st.st_mode))
    {
        long size = dir_listing_size(host);
        status->filler[0x09] = (size >> 24) & 0xff;
        status->filler[0x0a] = (size >> 16) & 0xff;
        status->filler[0x0b] = (size >> 8) & 0xff;
        status->filler[0x0c] = size & 0xff;
    }
    return 0;
}

int fdunix::getstatus(int opcode,statusbuf *status)
{
    struct stat statbuf;
    switch (opcode)
    {
        case SS_Opt:  /* Read/Write PD Options */
            /*
             * The RBF path options, laid out as defs/rbf.d has them. Only the
             * fields a hosted program can act on are filled in; the geometry
             * ones describe a disk we do not have.
             *
             *   $00 PD.DTP  device type      $13 PD.ATT  attributes
             *   $14 PD.FD   file descriptor  $17 PD.DFD  directory descriptor
             *   $1a PD.DCP  our entry's offset in the parent directory
             */
            memset(status,'\0',sizeof(*status));
            status->filler[0x00] = 0x1;  /* RBF */
            status->filler[0x03] = 0x80;  /* Winchester disk */
            if(fp && fstat(fileno(fp), &statbuf) != -1)
            {
                status->filler[0x13] = u2o_attr(statbuf.st_mode);
                status->filler[0x14] = statbuf.st_ino >> 16 & 0xff;
                status->filler[0x15] = statbuf.st_ino >> 8 & 0xff;
                status->filler[0x16] = statbuf.st_ino & 0xff;
            }
            if(dcp >= 0)
            {
                status->filler[0x1a] = (dcp >> 24) & 0xff;
                status->filler[0x1b] = (dcp >> 16) & 0xff;
                status->filler[0x1c] = (dcp >> 8) & 0xff;
                status->filler[0x1d] = dcp & 0xff;
            }
            break;
        case SS_Size: /* Read/Write File Size */
            if(fp && fstat(fileno(fp), &statbuf) != -1)
                status->filesize = statbuf.st_size;
            else
                return(errorcode = E_BMode);
            break;
        case SS_Pos: /* Get File Current Position */
        {
            status->filesize=ftell(fp);
        }
            break;
        case SS_EOF: /* Test for End of File */
        {
            // feof() only goes true once a read has already run off the end,
            // but the caller is asking before it reads. Compare the position
            // against the size instead.
            long here = ftell(fp);
            status->status = 1;
            if(here >= 0 && fstat(fileno(fp), &statbuf) != -1)
                status->status = (here >= statbuf.st_size);
        }
            break;
        case SS_DevNm: /* Return Device name (32-bytes at [X]) */
            devname(status);
            break;
        case SS_FD: /* Return the file descriptor sector */
            if(!fp || fstat(fileno(fp), &statbuf) == -1)
                return(errorcode = E_BMode);
            fill_fd_sector(status, &statbuf);
            break;
        case SS_FDInf: /* Somebody else's file descriptor sector */
        {
            int err = fd_info(status->lsn, status);
            if(err)
                return(errorcode = err);
            break;
        }

        default:
            return(errorcode = E_UnkSvc);
    }
    return(0);
}

int fdunix::hostfd()
{
    return fp ? fileno(fp) : -1;
}

int fdunix::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
        case SS_Size:
            if(ftruncate(fileno(fp),status->filesize) == -1)
                return(errorcode = E_Write);
            return 0;
        case SS_FD:
            // attr(1) writes back a single byte, FD.ATT. The rest of the
            // descriptor is derived from the host file, so there is nothing
            // else here for us to store.
            if(fchmod(fileno(fp), o2u_attr(status->filler[0])) == -1)
                return(errorcode = E_Write);
            return 0;
        case SS_Attr:
            if(fchmod(fileno(fp), o2u_attr(status->status)) == -1)
                return(errorcode = E_Write);
            return 0;
        case SS_Opt:
            // Path options belong to the path, not to the file behind it.
            return 0;
        case SS_Lock:
        case SS_Ticks:
            // Record locking: nothing else is contending for the file.
            return 0;
        default:
            return(errorcode = E_UnkSvc);
    }
}

/*********************************************************************
 * fdirunix methods
 *********************************************************************/

fdirunix::fdirunix()
{
    dentries = NULL;
    capacity = 0;
    offset = 0;
    length = 0;
    hostdir[0] = '\0';
    hostroot = 0;
    scantime = 0;
    havestat = 0;
    memset(&dirstat, '\0', sizeof(dirstat));
}

/*
 * The nanoseconds of a stat's modification time. POSIX spells it st_mtim and
 * macOS st_mtimespec; both #define st_mtime onto the seconds of their own, so
 * the Apple case has to be asked about first.
 */
#if defined(__APPLE__)
#define ST_MTIM_NSEC(st) ((long)(st).st_mtimespec.tv_nsec)
#elif defined(st_mtime)
#define ST_MTIM_NSEC(st) ((long)(st).st_mtim.tv_nsec)
#else
#define ST_MTIM_NSEC(st) 0L
#endif

/*
 * Has the host directory moved since we last read it?
 *
 * A stat that differs settles it. A stat that matches only proves nothing
 * happened if the host times its writes finer than a second -- otherwise a
 * change made in second N, after we looked in second N, leaves the timestamp
 * exactly as we recorded it. So an answer with no nanoseconds in it is not
 * taken as proof, and one that has them is still only trusted once the second
 * our scan began in has passed.
 */
int fdirunix::stale(const struct stat *st)
{
    if(!havestat)
        return 1;
    if(st->st_dev != dirstat.st_dev || st->st_ino != dirstat.st_ino ||
       st->st_size != dirstat.st_size || st->st_mtime != dirstat.st_mtime ||
       ST_MTIM_NSEC(*st) != ST_MTIM_NSEC(dirstat))
        return 1;
    if(ST_MTIM_NSEC(*st) == 0)
        return 1;
    return st->st_mtime >= scantime;
}

// Make room for at least this many entries, with the new ones empty.
void fdirunix::reserve(int entries)
{
    if(entries <= capacity)
        return;

    int grown = capacity ? capacity : 16;
    while(grown < entries)
        grown *= 2;

    os9dentry *bigger = new os9dentry[grown];
    memset(bigger, '\0', grown * sizeof(os9dentry));
    if(dentries != NULL)
        memcpy(bigger, dentries, capacity * sizeof(os9dentry));
    delete [] dentries;
    dentries = bigger;
    capacity = grown;
}

/*
 * Is this slot the host file of that name?
 *
 * Asked by encoding the host name the way an entry stores it and comparing
 * the two, rather than by decoding the entry: a name of 29 characters or more
 * -- or one with a byte of its own above 0x7f, which UTF-8 is full of -- does
 * not survive the trip back, and an entry that fails to recognise itself is
 * taken for deleted and re-appended somewhere else, which is exactly what the
 * slots are not allowed to do.
 */
static int sameentry(const os9dentry *e, const char *hostname)
{
    os9dentry probe;

    if(hostname[0] == '\0')
        return 0;
    probe.set(hostname, 0);
    return memcmp(e->name, probe.name, sizeof(e->name)) == 0;
}

/*
 * Read the host directory into the entry array, keeping the slots we have.
 *
 * RBF serves a directory out of its sectors as the caller asks for them, so a
 * listing follows the disk: a file created while the path is open turns up in
 * it. Ours is an array, and it used to be filled in once when the path was
 * opened and never again, which froze the listing for as long as anything
 * held the directory -- new files were invisible and deleted ones were still
 * there.
 *
 * What RBF does not do is move an entry. A slot belongs to its file until the
 * file goes, a deleted entry leaves its slot with a zero first byte, and a new
 * file takes the first slot going spare. That matters here beyond looking
 * right: rename(1) reads an entry, then seeks back to where it was and writes
 * the new name over it, and PD.DCP is that offset. So this brings the array
 * back in step by name rather than rebuilding it, and an entry that is still
 * there stays where it was.
 */
int fdirunix::rescan()
{
    struct stat st;
    DIR *dir;
    struct dirent *entry;
    time_t began = time(NULL);
    int i, j;

    // Gone or unreadable: serve what we have, and say we could not look. The
    // first scan has nothing to fall back on, so open() turns this into the
    // error the caller would have got before it had a path at all.
    if(hostdir[0] == '\0')
        return 0;
    if(stat(hostdir, &st) == -1)
        return 0;
    if(!stale(&st))
        return 1;
    if((dir = opendir(hostdir)) == NULL)
        return 0;

    /*
     * What the host has, less "." and ".." -- those are ours to place, and
     * OS9 wants them first.
     */
    int nnames = 0, namecap = 32;
    char **names = new char *[namecap];
    while((entry = readdir(dir)) != NULL)
    {
        if(strcmp(".", entry->d_name) == 0) continue;
        if(strcmp("..", entry->d_name) == 0) continue;
        if(nnames == namecap)
        {
            char **more = new char *[namecap * 2];
            memcpy(more, names, nnames * sizeof(*more));
            delete [] names;
            names = more;
            namecap *= 2;
        }
        names[nnames++] = strdup(entry->d_name);
    }
    closedir(dir);

    int slots = length / (int)sizeof(os9dentry);
    if(slots < 2)
        slots = 2;
    reserve(slots);
    /*
     * ".." comes first and "." second -- that is the order RBF's MakDir
     * writes them in, and pwd/pxd depend on it: they read the two entries,
     * take them being equal to mean "this is the root", and otherwise walk up
     * looking for the entry in the parent whose number matches the second
     * one. With the two the other way round the walk searches for the
     * parent's own number, finds nothing and reports a read error.
     */
    dentries[0].set("..", fileTable.getID(hostdir, "..", hostroot));
    dentries[1].set(".", fileTable.getID(hostdir));

    // An entry the host still has keeps its slot; the rest are freed.
    for(i = 2; i < slots; i++)
    {
        int found = -1;

        if(dentries[i].name[0] != '\0')
            for(j = 0; j < nnames; j++)
                if(names[j] != NULL && sameentry(&dentries[i], names[j]))
                {
                    found = j;
                    break;
                }
        if(found >= 0)
        {
            free(names[found]);
            names[found] = NULL;		// placed
        }
        else
            dentries[i].name[0] = '\0';		// how OS9 marks a dead entry
    }

    // Whatever the host has that we do not is new. First slot going spare.
    int spare = 2;
    for(j = 0; j < nnames; j++)
    {
        if(names[j] == NULL)
            continue;
        while(spare < slots && dentries[spare].name[0] != '\0')
            spare++;
        if(spare == slots)
        {
            reserve(slots + 1);
            slots++;
        }
        dentries[spare].set(names[j],
                            fileTable.getID(hostdir, names[j], hostroot));
        free(names[j]);
    }
    delete [] names;

    // Dead entries at the end are not worth serving.
    while(slots > 2 && dentries[slots-1].name[0] == '\0')
        slots--;

    length = slots * (int)sizeof(os9dentry);
    dirstat = st;
    scantime = began;
    havestat = 1;
    return 1;
}


fdirunix::~fdirunix()
{
    if(usecount)
        delete [] dentries;
}

int fdirunix::close()
{
    if(usecount == 1)
    {
        delete [] dentries;
        dentries = NULL;
        capacity = 0;
        length = 0;
    }
    return fdunix::close();
}

int fdirunix::read(Byte *buf, int size)
{
    rescan();
    if (offset >= length) {
        errorcode = E_EOF;
        return -1;
    }

    size = (size + offset < length) ? size : length - offset;
    memcpy((void *)buf, (Byte *)dentries + offset, size);

    offset += size;
    return size;
}

/*
 * An entry's name, back in host form: OS9 stores it high-bit terminated and
 * pads the rest with whatever was there before.
 */
void fdirunix::entryname(const os9dentry *e, char *out, size_t outsz)
{
    size_t i;

    for(i = 0; i + 1 < outsz && i < sizeof(e->name); i++)
    {
        unsigned char c = e->name[i];
        out[i] = c & 0x7f;
        if(c & 0x80)
        {
            i++;
            break;
        }
        if(c == 0)
            break;
    }
    out[i] = '\0';
}

/*
 * Writing to a directory is how OS9 renames a file: the program reads the
 * entry, changes the name in it and writes it back. Turn that into a host
 * rename. An entry whose first byte is zero has been deleted, which is how
 * OS9 removes a directory entry.
 */
int fdirunix::write(Byte *buf, int size)
{
    int written = 0;

    while(written < size)
    {
        int slot = offset / (int)sizeof(os9dentry);
        int within = offset % (int)sizeof(os9dentry);
        int chunk = (int)sizeof(os9dentry) - within;

        if(chunk > size - written)
            chunk = size - written;
        if(offset + chunk > length)
        {
            // We cannot grow a directory this way: entries appear when a file
            // is created, not when somebody writes past the end.
            errorcode = E_Full;
            return written ? written : -1;
        }

        if(slot < 2)
        {
            // Slots 0 and 1 are ".." and ".", which we make up rather than
            // read off a disk. Renaming or clearing one would ask the host to
            // rename a directory out from under itself.
            errorcode = E_FNA;
            return written ? written : -1;
        }

        char oldname[64], newname[64];
        entryname(&dentries[slot], oldname, sizeof(oldname));

        memcpy((Byte *)&dentries[slot] + within, buf + written, chunk);
        entryname(&dentries[slot], newname, sizeof(newname));

        if(os9cfg.trace)
            fprintf(stderr,"'os9::dirwrite: slot %d <%s> -> <%s> in <%s>\n",
                    slot, oldname, newname, hostdir);
        if(strcmp(oldname, newname) != 0 && oldname[0] && *hostdir)
        {
            char from[2048], to[2048];

            snprintf(from, sizeof(from), "%s/%s", hostdir, oldname);
            if(newname[0] == '\0')
            {
                if(unlink(from) == -1)
                {
                    errorcode = E_FNA;
                    return written ? written : -1;
                }
            }
            else
            {
                snprintf(to, sizeof(to), "%s/%s", hostdir, newname);
                if(rename(from, to) == -1)
                {
                    errorcode = (errno == EACCES) ? E_FNA : E_BPNam;
                    return written ? written : -1;
                }
            }
        }

        offset += chunk;
        written += chunk;
    }
    return written;
}

// Writing a line to a directory means the same thing as writing bytes to it.
int fdirunix::writeln(Byte *buf, int size)
{
    return write(buf, size);
}

/*
 * A directory path is served out of the entry array we built when it was
 * opened, so seeking moves our own cursor rather than the underlying file.
 */
int fdirunix::seek(int newoffset)
{
    if(newoffset < 0)
        return (errorcode = E_BPNam);
    offset = newoffset;
    return 0;
}

int fdirunix::getstatus(int opcode,statusbuf *status)
{
    struct stat statbuf;
    switch (opcode)
    {
        case SS_Opt:  /* Read/Write PD Options */
            memset(status,'\0',sizeof(*status));
            status->filler[0x00] = 0x1;  /* RBF */
            status->filler[0x03] = 0x80;  /* Winchester disk */
            if(fp && fstat(fileno(fp), &statbuf) != -1)
            {
                status->filler[0x10] = u2o_attr(statbuf.st_mode); /* Attributes */
                status->filler[0x11] = statbuf.st_ino >> 16 & 0xff;
                status->filler[0x12] = statbuf.st_ino >> 8 & 0xff;
                status->filler[0x13] = statbuf.st_ino & 0xff;
            }
            break;
        case SS_Size: /* Read/Write File Size */
            rescan();
            status->filesize = length;
            break;
        case SS_Pos: /* Get File Current Position */
            // A directory is served out of our own entry array, not out of
            // fp, so the position is ours to report.
            status->filesize = offset;
            break;
        case SS_EOF: /* Test for End of File */
            rescan();
            status->status = (offset >= length);
            break;
        case SS_DevNm: /* Return Device name (32-bytes at [X]) */
            devname(status);
            break;
        case SS_FD: /* Return the file descriptor sector */
            if(!fp || fstat(fileno(fp), &statbuf) == -1)
                return(errorcode = E_BMode);
            rescan();
            fill_fd_sector(status, &statbuf);
            /*
             * FD.SIZ has to be the length of the listing we serve, not what
             * the host makes of a directory -- macOS happens to report 32
             * bytes an entry, the same as an OS9 directory entry, and any
             * other host would have a caller that sizes a directory this way
             * reading a truncated or an over-long one.
             */
            status->filler[0x09] = (length >> 24) & 0xff;
            status->filler[0x0a] = (length >> 16) & 0xff;
            status->filler[0x0b] = (length >> 8) & 0xff;
            status->filler[0x0c] = length & 0xff;
            break;
        case SS_FDInf: /* The descriptor of a file this directory lists */
        {
            int err = fd_info(status->lsn, status);
            if(err)
                return(errorcode = err);
            break;
        }
        default:
            return(errorcode = E_UnkSvc);
    }
    return(0);
}


/*********************************************************************
 * fdwhole methods -- the "entire device" path
 *********************************************************************/

/*
 * Build the disk OS9 would have seen, from what the host filesystem reports.
 *
 * Layout, from defs/rbf.d:
 *   $00 DD.TOT  3  total sectors        $0e DD.DSK  2  disk id
 *   $03 DD.TKS  1  sectors per track    $10 DD.FMT  1  format
 *   $04 DD.MAP  2  bytes of bitmap      $11 DD.SPT  2  sectors per track
 *   $06 DD.BIT  2  sectors per cluster  $15 DD.BT   3  bootstrap sector
 *   $08 DD.DIR  3  root directory FD    $18 DD.BSZ  2  bootstrap size
 *   $0b DD.OWN  2  owner                $1a DD.DAT  5  creation date
 *   $0d DD.ATT  1  attributes           $1f DD.NAM 32  volume name
 * and the allocation bitmap starts at $100, one bit per cluster, used
 * clusters set, most significant bit first.
 */
fdwhole::fdwhole(const char *hostdir, const char *volname)
{
    struct statvfs vfs;
    struct stat st;
    unsigned long total_sectors, free_sectors, clusters, free_clusters;
    unsigned long used_clusters, bitmap_bytes, sectors_per_cluster;
    unsigned long i;
    unsigned char *dd;

    image = NULL;
    length = 0;
    offset = 0;

    total_sectors = 0;
    free_sectors  = 0;
    if(statvfs(hostdir, &vfs) == 0) {
        unsigned long unit = vfs.f_frsize ? vfs.f_frsize : vfs.f_bsize;
        total_sectors = (unsigned long)((double)vfs.f_blocks * unit / 256.0);
        free_sectors  = (unsigned long)((double)vfs.f_bavail * unit / 256.0);
    }
    if(total_sectors == 0)
        total_sectors = 1440;			// a floppy, if we cannot ask
    if(free_sectors > total_sectors)
        free_sectors = total_sectors;

    // An OS9 sector number is 24 bits.
    if(total_sectors > 0xffffffUL) {
        free_sectors = (unsigned long)((double)free_sectors *
                                       0xffffffUL / total_sectors);
        total_sectors = 0xffffffUL;
    }

    /*
     * Pick a cluster size that keeps the bitmap to at most 8K. A modern host
     * filesystem holds far more 256-byte sectors than a bit each would fit
     * in, and free(1) reads the whole map.
     */
    sectors_per_cluster = 1;
    while(total_sectors / sectors_per_cluster > 65536UL)
        sectors_per_cluster *= 2;

    clusters      = (total_sectors + sectors_per_cluster - 1) / sectors_per_cluster;
    free_clusters = free_sectors / sectors_per_cluster;
    if(free_clusters > clusters)
        free_clusters = clusters;
    used_clusters = clusters - free_clusters;
    bitmap_bytes  = (clusters + 7) / 8;

    length = 256 + (int)bitmap_bytes;
    image = new unsigned char[length];
    memset(image, 0, length);

    dd = image;
    dd[0x00] = (total_sectors >> 16) & 0xff;
    dd[0x01] = (total_sectors >> 8) & 0xff;
    dd[0x02] = total_sectors & 0xff;
    dd[0x03] = 18;				// sectors per track
    dd[0x04] = (bitmap_bytes >> 8) & 0xff;
    dd[0x05] = bitmap_bytes & 0xff;
    dd[0x06] = (sectors_per_cluster >> 8) & 0xff;
    dd[0x07] = sectors_per_cluster & 0xff;
    dd[0x08] = 0; dd[0x09] = 0; dd[0x0a] = 2;	// root directory FD
    dd[0x0d] = 0xbf;				// d s pe pw pr e w r
    dd[0x0e] = 0x4f; dd[0x0f] = 0x39;		// disk id, "O9"
    dd[0x10] = 0x03;				// double sided, double density
    dd[0x11] = 0; dd[0x12] = 18;
    if(stat(hostdir, &st) == 0) {
        time_t ct = st.st_ctime;
        struct tm *tm = localtime(&ct);
        if(tm) {
            dd[0x1a] = tm->tm_year;
            dd[0x1b] = tm->tm_mon + 1;
            dd[0x1c] = tm->tm_mday;
            dd[0x1d] = tm->tm_hour;
            dd[0x1e] = tm->tm_min;
        }
    }

    {
        // The volume name, high bit terminated the way OS9 stores names.
        size_t n = strlen(volname);
        if(n > 31) n = 31;
        if(n == 0) { volname = "OS9"; n = 3; }
        memcpy(&dd[0x1f], volname, n);
        dd[0x1f + n - 1] |= 0x80;
    }

    // The bitmap: used clusters first, so the free space is one run and
    // free(1) reports a sensible largest block.
    for(i = 0; i < used_clusters; i++)
        image[256 + i / 8] |= (unsigned char)(0x80 >> (i % 8));
    // Clusters past the end of the disk are marked in use, as on a real one.
    for(i = clusters; i < bitmap_bytes * 8; i++)
        image[256 + i / 8] |= (unsigned char)(0x80 >> (i % 8));
}

fdwhole::~fdwhole()
{
    delete [] image;
}

int fdwhole::close()
{
    usecount--;
    return 0;
}

int fdwhole::read(Byte *buf, int size)
{
    if(offset >= length)
        return (errorcode = E_EOF), -1;
    if(size > length - offset)
        size = length - offset;
    memcpy(buf, image + offset, size);
    offset += size;
    return size;
}

int fdwhole::readln(Byte *buf, int size)
{
    return read(buf, size);
}

int fdwhole::write(Byte *buf, int size)
{
    // The image is ours, not the host's: there is nothing here to write to.
    return (errorcode = E_WP), -1;
}

int fdwhole::writeln(Byte *buf, int size)
{
    return write(buf, size);
}

int fdwhole::seek(int newoffset)
{
    if(newoffset < 0)
        return (errorcode = E_Sect);
    offset = newoffset;
    return 0;
}

int fdwhole::getstatus(int opcode, statusbuf *status)
{
    switch(opcode)
    {
        case SS_Opt:
            memset(status, '\0', sizeof(*status));
            status->filler[0x00] = 0x1;		// RBF
            status->filler[0x03] = 0x80;
            break;
        case SS_Size:
            status->filesize = length;
            break;
        case SS_Pos:
            status->filesize = offset;
            break;
        case SS_EOF:
            status->status = (offset >= length);
            break;
        case SS_DevNm:
            devname(status);
            break;
        default:
            return (errorcode = E_UnkSvc);
    }
    return 0;
}

int fdwhole::setstatus(int opcode, statusbuf *status)
{
    switch(opcode)
    {
        case SS_Opt:
        case SS_Lock:
        case SS_Ticks:
            return 0;
        default:
            return (errorcode = E_UnkSvc);
    }
}

/*********************************************************************
 * devterm methods
 *********************************************************************/
/*
 * Methods for serial device such as tty and printer
 */
devterm::devterm(const char *mntpnt, const char *args) : devdrvr(mntpnt)
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
    int c = (int)fread((char*)buf, 1, size, fp);
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
    int c;
    
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
    int y = (int)(p - buf);
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
    int val = (int)fwrite((char*)buf, 1, size, fp);
    fflush(fp);
    return val;
}

/*
 * Write buffer until CR is seen
 * Only ttys files here
 */
int fdterm::writeln(Byte *buf, int size)
{
    int inx;
    /*
     * OS9 lines end with a bare CR; it is the terminal driver that turns that
     * into CR LF on the way to a screen. A file gets the CR alone -- so when
     * our standard output is not a terminal, neither do we. Adding the newline
     * regardless put a stray byte into every file produced by redirecting the
     * emulator's own output, which the compiler passes read back as an error.
     */
    int istty = (os9cfg.eol == EOL_CRLF) ||
                (os9cfg.eol == EOL_AUTO && fp && isatty(fileno(fp)));

    for(inx = 0; inx < size;)
    {
        fputc(((char*)buf)[inx],fp);
        if(((char*)buf)[inx++] == '\r')
        {
            if(istty)
                fputc('\n',fp);
            break;
        }
    }
    fflush(fp);
    return inx;
}

int fdterm::seek(int offset)
{
    fflush(fp);
    fseek(fp, offset, SEEK_SET);
    return 0;
}

/*
 * The widest screen an OS-9 utility is built for. No OS-9 terminal was wider
 * than this, and the utilities take it as given: dir(1) lays a line out in a
 * buffer and then writes a fixed 80 bytes of it, so told the truth about a
 * 120-column host window it drops every name that falls past the eightieth
 * column -- silently, because those names are simply never written. Its own
 * check for the buffer filling up cannot save it: `cmpx #$0090` compares an
 * absolute address, so it only fires for a process whose data area sits at
 * $0000, and ours are at $0400.
 */
#define MAXCOLS 80

/*
 * How wide and tall the terminal is. A program that formats in columns -- dir,
 * procs, mdir -- asks before it prints. Ask the real terminal if there is one,
 * otherwise answer with the size OS-9 assumed.
 */
static void term_size(FILE *fp, int *cols, int *rows)
{
    struct winsize ws;

    *cols = MAXCOLS;
    *rows = 24;
    if(os9cfg.cols > 0)
        *cols = os9cfg.cols;
    else if(fp && ioctl(fileno(fp), TIOCGWINSZ, &ws) == 0) {
        if(ws.ws_col) *cols = ws.ws_col;
        if(ws.ws_row) *rows = ws.ws_row;
    }
    if(*cols > MAXCOLS)
        *cols = MAXCOLS;
}

int fdterm::getstatus(int opcode, statusbuf *status)
{
    switch (opcode)
    {
        case SS_Opt:
            memset(status,'\0',sizeof(*status));
            status->filler[0x00] = 0x00;   /* SCF */
            status->filler[0x08] = 24; /* Lines per page */
            status->filler[0x09] = 8;  /* BS char */
            status->filler[0x0a] = 0x7f; /* DEL char */
            status->filler[0x0b] = 13; /* EOR char */
            status->filler[0x0c] = 4; /* EOF char ctrl-d */
            {
                int cols, rows;
                term_size(fp, &cols, &rows);
                status->filler[0x08] = rows;
            }
            break;

        case SS_Ready:  /* how many characters are waiting to be read */
        {
            struct pollfd pfd;
            pfd.fd = fp ? fileno(fp) : -1;
            pfd.events = POLLIN;
            pfd.revents = 0;
            status->status = 0;
            if(pfd.fd >= 0 && poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
                int n = 0;
                // The count is what the caller gets back in B, so an honest
                // number matters; fall back to "at least one" if we cannot ask.
                if(ioctl(pfd.fd, FIONREAD, &n) != 0 || n <= 0)
                    n = 1;
                status->status = (n > 255) ? 255 : n;
            }
            if(status->status == 0)
                return(errorcode = E_NotRdy);
            break;
        }

        case SS_EOF:
            status->status = 0;   /* a terminal is never at end of file */
            break;

        case SS_DevNm: /* Return Device name (32-bytes at [X]) */
            devname(status);
            break;

        case SS_ScSiz: /* Screen size: X = columns, Y = rows */
            term_size(fp, &status->cols, &status->rows);
            break;

        default:
            return(errorcode = E_UnkSvc);
    }
    return(0);
}

/*
 * What to wait on for input. A terminal, a pipe and an ordinary file all sit
 * behind a stdio stream, and it is that stream's descriptor SS_SSig watches.
 * Standard input is read unbuffered, so nothing hides from poll() there.
 */
int fdterm::hostfd()
{
    return fp ? fileno(fp) : -1;
}

int fdterm::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
        // Terminal settings we have no equivalent for. Accepting them is what
        // a driver without the feature does; refusing makes shells and editors
        // give up before they start.
        case SS_Opt:     /* tmode(1) writing back the path options */
        case SS_Size:
        case SS_Reset:
        case SS_Feed:
        case SS_Frz:
        case SS_SSig:    /* signal on data ready -- we never send it */
        case SS_Relea:
        case SS_Attr:
        case SS_Break:
        case SS_Cursr:
        case SS_KySns:
        case SS_ComSt:   /* baud and parity mean nothing to a pipe of bytes */
        case SS_Open:
        case SS_Close:
        case SS_HngUp:
        case SS_Ticks:
        case SS_Lock:
            return 0;
        default:
            return(errorcode = E_UnkSvc);
    }
}

/*********************************************************************
 * devpipe methods
 *********************************************************************/
/*
 * Methods for serial device such as tty and printer
 */
devpipe::devpipe(const char *mntpnt, const char *args) : devdrvr(mntpnt)
{
}

fdes *devpipe::open(const char *path, int mode, int create)
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
    int c = (int)fread((char*)buf, 1, size, ifp);
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
    int c;
    
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
    int y = (int)(p - buf);
    if(y == 0)
    {
        errorcode = 211;
        return -1;
    }
    return y;
}

int fdpipe::write(Byte *buf, int size)
{
    return (int)::write(filedes[1], (char*)buf, size);
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
    return (int)::write(filedes[1], (char*)buf, nl);
}

int fdpipe::getstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
        case SS_Opt:
            memset(status,'\0',sizeof(*status));
            status->filler[0x00] = 0x02;   /* PIPEMAN */
            break;
        case SS_Ready:
        {
            struct pollfd pfd;
            pfd.fd = filedes[0];
            pfd.events = POLLIN;
            pfd.revents = 0;
            status->status = 0;
            if(pfd.fd >= 0 && poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN))
                status->status = 1;
            if(status->status == 0)
                return(errorcode = E_NotRdy);
            break;
        }
        case SS_EOF:
            status->status = 0;
            break;
        case SS_DevNm:
            devname(status);
            break;
        default:
            return(errorcode = E_UnkSvc);
    }
    return(0);
}

int fdpipe::hostfd()
{
    return ifp ? fileno(ifp) : -1;
}

int fdpipe::setstatus(int opcode,statusbuf *status)
{
    switch (opcode)
    {
        case SS_Opt:
        case SS_Size:
        case SS_SSig:
        case SS_Relea:
        case SS_Open:
        case SS_Close:
            return 0;
        default:
            return(errorcode = E_UnkSvc);
    }
}
