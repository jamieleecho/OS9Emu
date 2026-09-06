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

#define OS9EMU_VERSION "0.2"

os9 sys;

void interupt(int sig)
{
    exit(0);
}

static void usage(FILE *out, int status)
{
    fprintf(out,
"Usage: os9emu [options] [module [arguments...]]\n"
"\n"
"Run an OS-9 Level 1 program under a 6809 emulator, servicing its system\n"
"calls against the host filesystem. With no module, runs the shell.\n"
"\n"
"Options:\n"
"  -r, --root DIR      host directory /dd, /d0, /d1 and /h0 map onto\n"
"                      (default: $OS9ROOT, else ~/OS9)\n"
"  -w, --workdir DIR   initial OS-9 working directory   (default /h0)\n"
"  -x, --execdir DIR   initial OS-9 execution directory (default /h0/CMDS)\n"
"  -d, --debug         trace system calls to stderr; -dd (or -d -d) also\n"
"                      traces reads, writes and seeks\n"
"  -m, --mem SIZE      data area for the program, e.g. 32k or 8192 bytes.\n"
"                      The default is what the module itself asks for --\n"
"                      the same thing the shell's \"prog #32k\" overrides\n"
"  -e, --eol MODE      line endings on standard output: auto (default; CR LF\n"
"                      to a terminal, bare CR otherwise), crlf, or cr\n"
"  -V, --version       print the version and exit\n"
"  -h, --help          print this message and exit\n"
"\n"
"Arguments after the module name are passed to it as an OS-9 parameter\n"
"line, so they need quoting if they contain characters the shell eats:\n"
"\n"
"  os9emu dir -e\n"
"  os9emu shell 'echo hello >/h0/greeting'\n");
    exit(status);
}

int main(int argc, char *argv[])
{
    char parm[256], *p;
    const char *module = NULL;
    int i, j, argi, mempages = 0;

    os9cfg.root = getenv("OS9ROOT");

    for (argi = 1; argi < argc; argi++) {
        const char *a = argv[argi];
        const char *val = NULL;

        if (a[0] != '-' || a[1] == '\0')
            break;			// the module name, or a bare "-"

        // An option that takes a value consumes the next argument.
        if (!strcmp(a, "-r") || !strcmp(a, "--root") ||
            !strcmp(a, "-w") || !strcmp(a, "--workdir") ||
            !strcmp(a, "-x") || !strcmp(a, "--execdir") ||
            !strcmp(a, "-e") || !strcmp(a, "--eol") ||
            !strcmp(a, "-m") || !strcmp(a, "--mem")) {
            if (argi + 1 >= argc) {
                fprintf(stderr, "os9emu: %s needs a value\n", a);
                return EXIT_FAILURE;
            }
            val = argv[++argi];
        }

        if (!strcmp(a, "-r") || !strcmp(a, "--root"))
            os9cfg.root = val;
        else if (!strcmp(a, "-w") || !strcmp(a, "--workdir"))
            os9cfg.workdir = val;
        else if (!strcmp(a, "-x") || !strcmp(a, "--execdir"))
            os9cfg.execdir = val;
        else if (!strcmp(a, "-m") || !strcmp(a, "--mem")) {
            char *end;
            long bytes = strtol(val, &end, 0);

            if (end == val || bytes <= 0) {
                fprintf(stderr, "os9emu: --mem wants a size, e.g. 32k\n");
                return EXIT_FAILURE;
            }
            if (*end == 'k' || *end == 'K') bytes *= 1024;
            else if (*end != '\0') {
                fprintf(stderr, "os9emu: --mem: unknown suffix \"%s\"\n", end);
                return EXIT_FAILURE;
            }
            mempages = (int)((bytes + 255) / 256);
        }
        else if (!strcmp(a, "-e") || !strcmp(a, "--eol")) {
            if (!strcmp(val, "auto"))      os9cfg.eol = EOL_AUTO;
            else if (!strcmp(val, "cr"))   os9cfg.eol = EOL_CR;
            else if (!strcmp(val, "crlf")) os9cfg.eol = EOL_CRLF;
            else {
                fprintf(stderr, "os9emu: --eol wants auto, cr or crlf\n");
                return EXIT_FAILURE;
            }
        }
        else if (!strcmp(a, "--debug"))
            os9cfg.trace++;
        else if (a[1] == 'd' && strspn(a + 1, "d") == strlen(a + 1))
            // -d, -dd, -ddd: each d raises the level. -dd also traces the
            // reads, writes and seeks, which are far too noisy by default.
            os9cfg.trace += (int)strlen(a + 1);
        else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
            printf("os9emu %s\n", OS9EMU_VERSION);
            return EXIT_SUCCESS;
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help"))
            usage(stdout, EXIT_SUCCESS);
        else {
            fprintf(stderr, "os9emu: unknown option %s\n", a);
            usage(stderr, EXIT_FAILURE);
        }
    }

    if (os9cfg.root == NULL) {
        static char defroot[1024];
        const char *home = getenv("HOME");

        if (home == NULL) {
            fprintf(stderr, "os9emu: neither OS9ROOT nor HOME is set;"
                            " use --root\n");
            return EXIT_FAILURE;
        }
        snprintf(defroot, sizeof(defroot), "%s/OS9", home);
        os9cfg.root = defroot;
    }

    module = (argi < argc) ? argv[argi++] : "shell";

    (void)signal(SIGINT, interupt);

    /* Build the OS-9 parameter line: arguments separated by spaces and
     * terminated by a carriage return, which is what a program sees at X. */
    p = parm;
    for (i = argi; i < argc; i++) {
        j = (int)strlen(argv[i]);
        if ((p - parm) + j + 2 > (int)sizeof(parm)) {
            fprintf(stderr, "os9emu: parameter line too long\n");
            return EXIT_FAILURE;
        }
        memcpy(p, argv[i], j);
        p += j;
        *(p++) = ' ';		/* add a space between parameters */
    }

    if (p != parm) {
        *(p - 1) = 0x0d;
        *p = 0x0;
    } else {
        *p++ = 0x0d;
        *p = 0x0;
    }

    /*
     * Line buffering, even when our output is a pipe. A program's output and
     * the shell's prompts go to different streams, and with the default block
     * buffering on a pipe they come out in the wrong order -- and are lost
     * entirely if the emulator is killed.
     */
    setvbuf(stdout, NULL, _IOLBF, 0);

    /*
     * Standard input is read unbuffered so that a forked child picks up
     * exactly where its parent left off. With a buffer in the way the parent
     * reads ahead, and the bytes it swallowed reach the child as a private
     * copy -- so a script piped into the shell runs some of its lines twice.
     */
    setvbuf(stdin, NULL, _IONBF, 0);

    sys.init();
    sys.loadmodule(module, parm, mempages);
    sys.setdebug(0);
    sys.run();

    return EXIT_SUCCESS;
}
