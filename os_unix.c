/*
 * gptsync/os_unix.c
 * Unix OS glue for gptsync
 *
 * Copyright (c) 2006 Christoph Pfisterer
 * All rights reserved.
 *
 * Enhanced version by JrCs 2009-2013
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *  * Neither the name of Christoph Pfisterer nor the names of the
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "gptsync.h"

#include <sys/disk.h>
#include <stdarg.h>
#include <getopt.h>

#define STRINGIFY(s) #s
#define STRINGIFY2(s) STRINGIFY(s)
#define PROGNAME_S STRINGIFY2(PROGNAME)

// variables

static int      fd;
char* progname = 0;
BOOLEAN fill_mbr;
BOOLEAN create_empty_mbr;

//
// error functions
//

void error(const char *msg, ...)
{
    va_list par;
    char buf[4096];
    
    va_start(par, msg);
    vsnprintf(buf, 4096, msg, par);
    va_end(par);
    
    fprintf(stderr, "ERROR: %s\n", buf);
}

void errore(const char *msg, ...)
{
    va_list par;
    char buf[4096];
    
    va_start(par, msg);
    vsnprintf(buf, 4096, msg, par);
    va_end(par);
    
    fprintf(stderr, "ERROR: %s: %s\n", buf, strerror(errno));
}

//
// get the size of device (in blockcount)
//
UINT64 get_disk_size(void) {
	UINT64        block_count;
	if (ioctl (fd, DKIOCGETBLOCKCOUNT, &block_count))
		return 0;
	else
		return block_count;
}

//
// sector I/O functions
//

UINTN read_sector(UINT64 lba, UINT8 *buffer)
{
    off_t   offset;
    off_t   result_seek;
    ssize_t result_read;
    
    offset = lba * 512;
    result_seek = lseek(fd, offset, SEEK_SET);
    if (result_seek != offset) {
        errore("Seek to %llu failed", offset);
        return 1;
    }
    
    result_read = read(fd, buffer, 512);
    if (result_read < 0) {
        errore("Data read failed at position %llu", offset);
        return 1;
    }
    if (result_read != 512) {
        errore("Data read fell short at position %llu", offset);
        return 1;
    }
    return 0;
}

UINTN write_sector(UINT64 lba, UINT8 *buffer)
{
    off_t   offset;
    off_t   result_seek;
    ssize_t result_write;
    
    offset = lba * 512;
    result_seek = lseek(fd, offset, SEEK_SET);
    if (result_seek != offset) {
        errore("Seek to %llu failed", offset);
        return 1;
    }
    
    result_write = write(fd, buffer, 512);
    if (result_write < 0) {
        errore("Data write failed at position %llu", offset);
        return 1;
    }
    if (result_write != 512) {
        errore("Data write fell short at position %llu", offset);
        return 1;
    }
    return 0;
}

//
// keyboard input
//

UINTN input_boolean(CHARN *prompt, BOOLEAN *bool_out)
{
    int c;
    
    printf("%s", prompt);
    fflush(NULL);
    
    c = getchar();
    if (c == EOF)
        return 1;
    
    if (c == 'y' || c == 'Y') {
        printf("Yes\n");
        *bool_out = TRUE;
    } else {
        printf("No\n");
        *bool_out = FALSE;
    }
    
    return 0;
}

//
// EFI-style print function
//

void Print(wchar_t *format, ...)
{
    va_list par;
    char formatbuf[256];
    char buf[4096];
    int i;
    
    for (i = 0; format[i]; i++)
        formatbuf[i] = (format[i] > 255) ? '?' : (char)(format[i] & 0xff);
    formatbuf[i] = 0;
    
    va_start(par, format);
    vsnprintf(buf, 4096, formatbuf, par);
    va_end(par);
    
    printf("%s", buf);
}

//
// list recognized types
//
void list_types(void) {
	MBR_PARTTYPE *p = mbr_types;
	Print(L"\n%s MBR recognized types:\n\n",progname);
	while (p->type != 0) {
		Print(L"    %02x  %s\n", p->type, p->name);
		p++;
	}
}

//
// display version string
//
void print_version(void) {
    Print (L"%s 0.3\n", progname);
}

//
// Usage
//
static void
usage (int status)
{
	if (status)
		fprintf (stderr, "Try ``" PROGNAME_S " --help'' for more information.\n");
	else {
		print_version();
	    printf ("\n\
Usage: %s [OPTION]... DEVICE [PARTITION[+/-[TYPE]]] ...\n\
\n\
%s fill hybrid MBR of GPT drive DEVICE.\n\
\n\
Specified partitions will be a part of hybrid MBR. Up to 3 partitions are allowed.\n\
+ means that partition is active (only one partition can be active).\n\
TYPE is an MBR hexadecimal type (use -t option to list recognized types).\n\
\n\
Valid options:\n\
  -e, --empty             create an MBR containing only the EFI Protective partition\n\
  -n, --nofill            don't try to protect unused partition\n\
  -t, --types             list the MBR recognized type codes\n\
  -h, --help              display this message and exit\n\
  -V, --version           print version information and exit\n\
\n\
example: sudo ./%s /dev/disk1 1 2+af\n",
				progname,
				progname,
				progname);
	}
	exit (status);
}

// Options
static struct option options[] =
{
{"nofill",  no_argument, 0, 'n'},
{"empty",   no_argument, 0, 'e'},
{"types",   no_argument, 0, 't'},
{"help",    no_argument, 0, 'h'},
{"version", no_argument, 0, 'V'},
{0, 0, 0, 0}
};

//
// main entry point
//

int main(int argc, char *argv[])
{
    char   *filename;
    struct stat sb;
    int    filekind;
    UINT64 filesize;
    char   *reason;
    int    status;
    
    progname         = PROGNAME_S;
	fill_mbr         = TRUE;
	create_empty_mbr = FALSE;

	/* Check for options.  */
	while (1) {
		int c = getopt_long (argc, argv, "nethV", options, 0);
		if (c == -1)
			break;
		else
			switch (c) {
				case 'n':
					fill_mbr = FALSE;
					break;

				case 'e':
					create_empty_mbr = TRUE;
					break;
					
				case 't':
					list_types();
					return 0;

				case 'h':
					usage (0);
					break;
					
				case 'V':
					print_version();
					return 0;
					
				default:
					usage (1);
					break;
			}
	}
	
	/* 1 parameters minimum needed.  */
	if (optind >= argc) {
		fprintf (stderr, "No enough parameters.\n");
		usage (1);
    }

	if (argc - optind > 4) {
		error("only 3 partitions can be in hybrid MBR.");
		return 1;
	}
		
    filename = argv[optind];
    
    // set input to unbuffered
    fflush(NULL);
    setvbuf(stdin, NULL, _IONBF, 0);
    
    // stat check
    if (stat(filename, &sb) < 0) {
        errore("Can't stat %.300s", filename);
        return 1;
    }
    
    filekind = 0;
    filesize = 0;
    reason = NULL;
    if (S_ISREG(sb.st_mode))
        filesize = sb.st_size;
    else if (S_ISBLK(sb.st_mode))
        filekind = 1;
    else if (S_ISCHR(sb.st_mode))
        filekind = 2;
    else if (S_ISDIR(sb.st_mode))
        reason = "Is a directory";
    else if (S_ISFIFO(sb.st_mode))
        reason = "Is a FIFO";
#ifdef S_ISSOCK
    else if (S_ISSOCK(sb.st_mode))
        reason = "Is a socket";
#endif
    else
        reason = "Is an unknown kind of special file";
    
    if (reason != NULL) {
        error("%.300s: %s", filename, reason);
        return 1;
    }
    
    // open file
    fd = open(filename, O_RDWR|O_SHLOCK);
    if (fd < 0 && errno == EBUSY) {
        fd = open(filename, O_RDONLY);
#ifndef NOREADONLYWARN
        if (fd >= 0)
            printf("Warning: %.300s opened read-only\n", filename);
#endif
    }
    if (fd < 0) {
        errore("Can't open %.300s", filename);
        return 1;
    }
    
    // (try to) guard against TTY character devices
    if (filekind == 2) {
        if (isatty(fd)) {
            error("%.300s: Is a TTY device", filename);
            return 1;
        }
    }
    
    // run sync algorithm
    status = PROGNAME(optind+1, argc, argv);
    printf("\n");
    
    // close file
    if (close(fd) != 0) {
        errore("Error while closing %.300s", filename);
        return 1;
    }
    
    return status;
}
