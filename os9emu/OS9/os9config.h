/*
 * Emulator-wide settings, filled in from the command line before the kernel
 * starts. Kept apart from os9krnl.h so a device driver can read them without
 * pulling in the CPU and kernel classes.
 */
#ifndef __os9config_h__
#define __os9config_h__

/*
 * How a line ending reaches our standard output. OS9 programs write a bare CR
 * and let the terminal driver expand it; a file gets the CR on its own. AUTO
 * makes that same distinction from whether our output is a terminal, which is
 * what keeps redirected output usable as an OS9 text file.
 */
enum { EOL_AUTO, EOL_CR, EOL_CRLF };

struct os9config {
    const char *root;		// host directory /d0, /h0 and /dd map onto
    const char *workdir;	// initial OS9 working directory
    const char *execdir;	// initial OS9 execution directory
    int         trace;		// trace system calls to stderr
    int         eol;		// one of EOL_*
    int         cols;		// screen width to report, or 0 to ask the terminal
    int         rows;		// screen height, likewise
};

extern os9config os9cfg;

#endif // __os9config_h__
