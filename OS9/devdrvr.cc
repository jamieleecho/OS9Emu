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

/*
 * The file descriptor class contains the resources necesary for
 * the open file. It also has a pointer to the driver.
 */
fdes::fdes()
{
    usecount=0;
}

int fdes::close()
{
    usecount--;
    return 0;
}

int fdes::getstatus(int opcode,statusbuf *buf)
{
    return -1;
}

int fdes::setstatus(int opcode,statusbuf *buf)
{
    return -1;
}

int fdes::seek(int offset)
{
    return -1;
}

/*
 * The devdrvr class is a base class for virtual devices
 * The methods expect pathnames that are relative to the mount pount
 * and with out leading slash.
 */
devdrvr::devdrvr(char *mntpnt)
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
