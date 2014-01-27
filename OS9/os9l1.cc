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
#ifdef __cplusplus
}
#endif /* __cplusplus */
#include "mc6809.h"
#include "devdrvr.h"
#include "os9krnl.h"

#ifdef __unix
#include <unistd.h>
#endif

#ifdef __osf__
extern "C" unsigned int alarm(unsigned int);
#endif

//#ifndef sun
//typedef void SIG_FUNC_TYP(int);
//typedef SIG_FUNC_TYPE *SIG_FP;
//#endif


os9 sys;

#ifdef SIGALRM
void update(int s)
{
	sys.status();
	(void)signal(SIGALRM, update);
	alarm(1);
}
#endif // SIGALRM

void interupt(int sig)
{
   exit(0);
}

void wakeitup(int sig)
{
}

int main(int argc, char *argv[])
{
    char parm[256],*p;
    int i,j,size;

    (void)signal(SIGINT, interupt);
#if 0
    (void)signal(SIGCHLD, wakeitup);
#endif
#ifdef SIGALRM
    (void)signal(SIGALRM, update);
    alarm(1);
#endif

    /* figure out how large the parameter area is*/
    size = 0;
    p = parm;
 
    for (i = 2; i < argc; i++) {
	j = (int)strlen(argv[i]);
	strcpy(p, argv[i]);
	size += j;
	p += j;
	*(p++) = ' ';               /* add a space between parameters */
	size++;
    }
 
    if (size != 0) {
	*(p - 1) = 0x0d;
    } else {
	size++;
	*p = 0x0d;
    }

    sys.loadmodule((argc==1)?"shell":argv[1],parm);
    sys.setdebug(0);
    sys.run();

    return EXIT_SUCCESS;
}
