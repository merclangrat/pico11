#ifndef PDP11_COMPAT_H
#define PDP11_COMPAT_H

#include <sys/types.h>

#if defined(pdp11) || defined(__pdp11__)
/*
 * Some 2.11BSD installations have a newer unistd.h but no stdint.h.
 * Keep the declarations whose return types matter on a 16-bit target here.
 */
int close();
int ftruncate();
off_t lseek();
int read();
int unlink();
int write();
/* compat routines in compat.c -- AS */
char* memcpy();
char* memset();
char* mktemp();

#else
#include <unistd.h>
#endif

#endif
