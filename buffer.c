#include <sys/types.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "pdpcompat.h"
#include "buffer.h"

#ifndef O_BINARY
#define O_BINARY 0
#endif

static int
scratch()
{
	char path[32];
	int fd;

	strcpy(path, "/tmp/p11XXXX");
	fd = mkstemp(path);
	if (fd >= 0)
		(void)unlink(path);
	return fd;
}

static void
uncache(b)
struct buffer* b;
{
	b->cachefd = -1;
	b->cachelen = 0;
}

static int
putone(fd, ch)
int fd;
int ch;
{
	char c;

	c = ch;
	return write(fd, (char *)&c, 1) == 1 ? 0 : -1;
}

static int
popone(fd, len)
int fd;
off_t* len;
{
	char c;

	if (*len == 0)
		return -1;
	if (lseek(fd, *len - 1, L_SET) < 0)
		return -1;
	if (read(fd, (char *)&c, 1) != 1)
		return -1;
	--*len;
	if (ftruncate(fd, *len) < 0)
		return -1;
	return (int)c;
}

int
b_open(b, name)
struct buffer* b;
char* name;
{
	int in;
	char block[BUF_CACHE];
	char swap;
	int n;
	int i;
	off_t left;
	off_t end;

	b->left = scratch();
	b->right = scratch();
	b->nleft = 0;
	b->nright = 0;
	b->changed = 0;
	b->version = 0;
	uncache(b);
	if (b->left < 0 || b->right < 0)
		return -1;
	if (name == (char *)0 || *name == '\0')
		return 0;
	in = open(name, O_RDONLY | O_BINARY, 0);
	if (in < 0) {
		if (errno == ENOENT)
			return 0;
		return -1;
	}
	end = lseek(in, (off_t)0, L_XTND);
	if (end < 0) {
		close(in);
		return -1;
	}
	while (end > 0) {
		n = end > BUF_CACHE ? BUF_CACHE : (int)end;
		left = end - n;
		if (lseek(in, left, L_SET) < 0 || read(in, block, n) != n) {
			close(in);
			return -1;
		}
		for (i = 0; i < n / 2; ++i) {
			swap = block[i];
			block[i] = block[n - 1 - i];
			block[n - 1 - i] = swap;
		}
		if (write(b->right, block, n) != n) {
			close(in);
			return -1;
		}
		b->nright += n;
		end = left;
	}
	close(in);
	return 0;
}

b_close(b)
struct buffer* b;
{
	if (b->left >= 0)
		(void)close(b->left);
	if (b->right >= 0)
		(void)close(b->right);
	b->left = b->right = -1;
}

off_t
b_size(b)
struct buffer* b;
{
	return b->nleft + b->nright;
}

off_t
b_pos(b)
struct buffer* b;
{
	return b->nleft;
}

int
b_get(b, pos)
struct buffer* b;
off_t pos;
{
	int fd;
	off_t physical;
	off_t base;
	int n;

	if (pos < 0 || pos >= b_size(b))
		return -1;
	if (pos < b->nleft) {
		fd = b->left;
		physical = pos;
	} else {
		fd = b->right;
		physical = b->nright - 1 - (pos - b->nleft);
	}
	base = physical - physical % BUF_CACHE;
	if (b->cachefd != fd || physical < b->cachebase ||
	    physical >= b->cachebase + b->cachelen) {
		if (lseek(fd, base, L_SET) < 0)
			return -1;
		n = read(fd, (char *)b->cache, BUF_CACHE);
		if (n <= 0)
			return -1;
		b->cachefd = fd;
		b->cachebase = base;
		b->cachelen = n;
	}
	return b->cache[(int)(physical - b->cachebase)] & 0377;
}

int
b_insert(b, ch)
struct buffer* b;
int ch;
{
	if (lseek(b->left, b->nleft, L_SET) < 0 || putone(b->left, ch) < 0)
		return -1;
	++b->nleft;
	++b->version;
	b->changed = 1;
	uncache(b);
	return 0;
}

int
b_backspace(b)
struct buffer *b;
{
	int ch;

	ch = popone(b->left, &b->nleft);
	if (ch < 0)
		return -1;
	++b->version;
	b->changed = 1;
	uncache(b);
	return ch;
}

int
b_delete(b)
struct buffer* b;
{
	int ch;

	ch = popone(b->right, &b->nright);
	if (ch < 0)
		return -1;
	++b->version;
	b->changed = 1;
	uncache(b);
	return ch;
}

int
b_left(b)
struct buffer* b;
{
	int ch;

	ch = popone(b->left, &b->nleft);
	if (ch < 0)
		return -1;
	if (lseek(b->right, b->nright, L_SET) < 0 || putone(b->right, ch) < 0)
		return -1;
	++b->nright;
	uncache(b);
	return 0;
}

int
b_right(b)
struct buffer* b;
{
	int ch;

	ch = popone(b->right, &b->nright);
	if (ch < 0)
		return -1;
	if (lseek(b->left, b->nleft, L_SET) < 0 || putone(b->left, ch) < 0)
		return -1;
	++b->nleft;
	uncache(b);
	return 0;
}

int
b_seek(b, pos)
struct buffer* b;
off_t pos;
{
	if (pos < 0 || pos > b_size(b))
		return -1;
	while (b->nleft > pos) {
		if (b_left(b) < 0)
			return -1;
	}
	while (b->nleft < pos) {
		if (b_right(b) < 0)
			return -1;
	}
	return 0;
}

static int
copy_forward(out, in, len)
int out, in;
off_t len;
{
	char block[BUF_CACHE];
	int want;
	int n;

	if (lseek(in, (off_t)0, L_SET) < 0)
		return -1;
	while (len > 0) {
		want = len > BUF_CACHE ? BUF_CACHE : (int)len;
		n = read(in, block, want);
		if (n != want || write(out, block, n) != n)
			return -1;
		len -= n;
	}
	return 0;
}

static int
copy_reverse(out, in, len)
int out, in;
off_t len;
{
	char block[BUF_CACHE];
	char rev[BUF_CACHE];
	off_t start;
	int want;
	int n;
	int i;

	while (len > 0) {
		want = len > BUF_CACHE ? BUF_CACHE : (int)len;
		start = len - want;
		if (lseek(in, start, L_SET) < 0)
			return -1;
		n = read(in, block, want);
		if (n != want)
			return -1;
		for (i = 0; i < n; ++i)
			rev[i] = block[n - 1 - i];
		if (write(out, rev, n) != n)
			return -1;
		len -= n;
	}
	return 0;
}

/* @vak: no rename, and link/unlink don't work if file exists
then, simplify it */

int
b_save(b, name)
struct buffer* b;
char* name;
{
	int out;
	int ok;

	out = creat(name, 0666);
	if (out < 0)
		return -1;
	ok = copy_forward(out, b->left, b->nleft);
	if (ok == 0)
		ok = copy_reverse(out, b->right, b->nright);
	if (close(out) < 0)
		ok = -1;
	if (ok == 0)
		b->changed = 0;
	return ok;
}
