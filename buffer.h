#ifndef BUFFER_H
#define BUFFER_H

#include <sys/types.h>

#define BUF_CACHE 256

extern int errno;

struct buffer {
	int left;
	int right;
	off_t nleft;
	off_t nright;
	int changed;
	/* @vak: let's refresh distinguish a cursor move
	(repaint nothing) from an edit (repaint the body) */
	int version;
	int cachefd;
	off_t cachebase;
	int cachelen;
	char cache[BUF_CACHE];
};

int b_open();
b_close();
off_t b_size();
off_t b_pos();
int b_get();
int b_insert();
int b_backspace();
int b_delete();
int b_left();
int b_right();
int b_seek();
int b_save();

#endif
