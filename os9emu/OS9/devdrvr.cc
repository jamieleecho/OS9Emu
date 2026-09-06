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
//#include <malloc.h>
}
#include "devdrvr.h"
#include "errcodes.h"

/*
 * The file descriptor class contains the resources necesary for
 * the open file. It also has a pointer to the driver.
 */
fdes::fdes()
{
    usecount=0;
}

fdes::~fdes()
{
}

int fdes::close()
{
    usecount--;
    return 0;
}

/*
 * A driver that does not recognise a status code reports E_UnkSvc and carries
 * on. Callers are written for that -- dir asks every device for its screen
 * width and just keeps its default when the answer is an error.
 */
int fdes::getstatus(int opcode,statusbuf *buf)
{
    errorcode = E_UnkSvc;
    return -1;
}

int fdes::setstatus(int opcode,statusbuf *buf)
{
    errorcode = E_UnkSvc;
    return -1;
}

int fdes::seek(int offset)
{
    return -1;
}

/*
 * SS_DevNm: the name of the device behind this path, as OS-9 spells it --
 * no leading slash, and high-bit terminated the way every other name in the
 * system is.
 */
void fdes::devname(statusbuf *status)
{
    const char *nm = driver ? driver->mntpoint : "";
    size_t i, len;

    memset(status, '\0', sizeof(*status));
    if(*nm == '/')
        nm++;
    len = strlen(nm);
    if(len > sizeof(status->filler) - 1)
        len = sizeof(status->filler) - 1;
    for(i = 0; i < len; i++)
        status->filler[i] = nm[i];
    if(len)
        status->filler[len - 1] |= 0x80;
}

/*
 * The devdrvr class is a base class for virtual devices
 * The methods expect pathnames that are relative to the mount pount
 * and with out leading slash.
 */
devdrvr::devdrvr(const char *mntpnt)
{
    mntpoint = new char[strlen(mntpnt)+1];
    strcpy(mntpoint,mntpnt);
}

/*
 * Not possible
 */
fdes *devdrvr::open(const char *path,int mode,int create)
{
    return (fdes*)0;
}

/*
 * Not possible
 */
int devdrvr::close(fdes *)
{
    return 0;
}

/*
 * Not possible
 * 0 = OK, Not 0 means error code
 */
int devdrvr::makdir(char *path,int mode)
{
    return 203;
}

/*
 * Not possible
 * 0 = OK, Not 0 means error code
 */
int devdrvr::chdir(char *path)
{
    return 203;
}
//
// The NULL device driver.
// Is expected to be invoked as /null and will do the same as UNIX /dev/null

devnull::devnull(char *mntpnt) : devdrvr(mntpnt)
{
}

fdes *devnull::open(const char *path,int mode,int create)
{
    return new fdnull;
}
