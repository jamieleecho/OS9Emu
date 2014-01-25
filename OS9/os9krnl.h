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
	pid_t pids[32]; // Mapping of Proces identifiers
	int pid_end;

public:
                void     loadmodule(const char *,const char *);
 
// Public constructor and destructor
 
                         os9();
                        ~os9();

protected:
		void swi2();

private:
		int sys_error(Byte);
		void f_chain();
		void i_chgdir();
		void f_crc();
		void f_fork();
		void f_id();
		void f_link();
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
		char *findpathseg(char *, char *);
		char *findpath(char *,bool );
		Word getpath(Byte*,Byte *,int);
		devdrvr *find_device(Byte *);
};

