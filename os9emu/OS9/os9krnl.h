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
#define DESMAX 16 // Whatever _NFILE is set to in os9's stdio.h

#include "os9config.h"

class os9 : virtual public mc6809 {
private:
    fdes *paths[DESMAX];
    char cxd[256]; // Execution directory, typically /d0/CMDS
    char cwd[256]; // Working directory
    char *sys_dev;	// System device - as known from init module
    int uppermem; // Absolute values
    int lowermem;
    devdrvr *devices[32]; // devices, typically /d0,/h0 etc.
    int dev_end;

    /*
     * Modules brought in by F$Load, so F$Link can find them again. Real OS9
     * keeps one of these system-wide; ours belongs to the running process,
     * which is as far as a one-program-per-emulator model reaches.
     *
     * They are placed at the top of memory and grow downwards, with modtop
     * marking the lowest one. The process data area is not allowed past it.
     */
    struct modent {
        char name[32];
        Word addr;		// module header
        Word end;		// first byte past it
        int  links;
    };
    modent moddir[32];
    int mod_end;
    int modtop;
    pid_t pids[32]; // Mapping of Proces identifiers
    int pid_end;

    /*
     * Signals. F$Icpt names a routine to vector to when one arrives, and
     * SS_SSig asks a path to send one when input turns up there -- which is
     * how a shell waits for a keystroke without blocking in a read. The
     * driver in a real system does the sending; here the kernel does, since
     * it is the only part of us that knows what process it is talking to.
     */
    Word icpt_pc;		// intercept routine, or 0 for none
    Word icpt_u;		// the memory pointer to hand it in U
    Byte ssig[DESMAX];		// signal a path owes us, 0 for none
    
public:
    void     init();
    void     loadmodule(const char *,const char *,int pages = 0);

    // Public constructor and destructor

    os9();
    ~os9();
    
protected:
    void swi2();
    
private:
    int sys_error(Byte);
    void f_chain();
    void i_chgdir();
    void f_cmpnam();
    void f_cpymem();
    void f_crc();
    void f_fork();
    void f_send();
    void f_icpt();
    int  deliver_signal(Byte);
    int  wait_signal(int);
    void f_id();
    void f_link();
    int  modname(Word base, char *out, size_t outsz);
    int  findmodule(const char *name);
    void reclaim_modules();
    void f_load();
    void f_mem();
    void f_perr();
    void f_prsnam();
    void f_sleep();
    void f_time();
    void f_unlk();
    void f_wait();
    void i_close();
    void i_dup();
    void i_getstt();
    void i_mdir();
    void i_open(int);
    void i_rdln();
    void i_read();
    void i_seek();
    void i_setstt();
    void i_wrln();
    void i_write();
    void i_deletex(int);
    void loadrcfile();
    const char *findpathseg(const char *, char *);
    const char *findpath(const char *, bool );
    Word getpath(Byte*,Byte *,int);
    devdrvr *find_device(Byte *);
};

