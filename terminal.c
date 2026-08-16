#include <sys/types.h>
#include <sys/ioctl.h>
#include <string.h>
#include <stdio.h>
#include "pdpcompat.h"
#include "terminal.h"

#if defined(pdp11) || defined(__pdp11__)
#define PICO_SGTTY 1
#endif

#ifdef PICO_SGTTY
#include <sgtty.h>
static struct sgttyb saved_tty;
#else
#include <termios.h>
static struct termios saved_tty;
#endif

static int opened;

int
t_open()
{
#ifdef PICO_SGTTY
	struct sgttyb t;

	if (gtty(0, &saved_tty) < 0)
		return -1;
	t = saved_tty;
	t.sg_flags &= ~(ECHO | CRMOD);
	t.sg_flags |= RAW;
	if (stty(0, &t) < 0)
		return -1;
#else
	struct termios t;

	if (tcgetattr(0, &saved_tty) < 0)
		return -1;
	t = saved_tty;
	t.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	t.c_oflag &= ~(OPOST);
	t.c_cflag |= CS8;
	t.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
	t.c_cc[VMIN] = 1;
	t.c_cc[VTIME] = 0;
	if (tcsetattr(0, TCSAFLUSH, &t) < 0)
		return -1;
#endif
	opened = 1;
/*	t_put("\033[?25l"); */
	return 0;
}

t_close()
{
	if (!opened)
		return;
	/* doesn't work on DEMOS/VT52 */
	/* t_put("\033[?25h\033[0m\033[H\033[J"); */
	t_put("\033H\033J");
#ifdef PICO_SGTTY
	stty(0, &saved_tty);
#else
	tcsetattr(0, TCSAFLUSH, &saved_tty);
#endif
	opened = 0;
}

t_size(rows, cols)
int* rows;
int* cols;
{
#ifdef TIOCGWINSZ
	struct winsize w;

	if (ioctl(1, TIOCGWINSZ, (char *)&w) == 0 && w.ws_row && w.ws_col) {
		*rows = w.ws_row;
		*cols = w.ws_col;
		return;
	}
#endif
	*rows = 24;
	*cols = 80;
}

t_write(s, n)
char* s;
int n;
{
	/* This function looks strange because
	just write can be used, if we don't
	check the result.
	Maybe, I couldn't catch the idea. -- AS */
	/*
	int done;
	int k;

	done = 0;
	while (done < n) {
		k = write(1, s + done, n - done);
		if (k <= 0)
			return;
		done += k;
	}
	*/
	write (1, s, n);
}

t_put(s)
char* s;
{
	t_write(s, strlen(s));
}

t_move(row, col)
int row, col;
{
	char seq[32];

	/* sprintf(seq, "\033[%d;%dH", row + 1, col + 1); */
	/* DEMOS terminal -- AS */
	sprintf(seq, "\033Y%c%c", row + 32, col + 32);
	t_put(seq);
}

t_clear()
{
	t_put("\033H\033J");
}

static int
readone()
{
	char c;

	if (read(0, (char *)&c, 1) != 1)
		return -1;
	return c;
}

int
t_key()
{
	int a;
	/*int b;*/
	int c;
	char s[3];

	a = readone();
	if (a != 27)
		return a;
	/* DEMOS/VT52 doesn't need [ after Esc -- AS */
	/*b = readone();
	if (b != '[' && b != 'O')
		return 27;*/
	c = readone();
	if (c == 'A') return KEY_UP;
	if (c == 'B') return KEY_DOWN;
	if (c == 'C') return KEY_RIGHT;
	if (c == 'D') return KEY_LEFT;
	if (c == 'H') return KEY_HOME;
	if (c == 'F') return KEY_END;
	if (c >= '0' && c <= '9') {
		a = c - '0';
		c = readone();
		if (c >= '0' && c <= '9') {
			a = a * 10 + c - '0';
			c = readone();
		}
		if (c == '~') {
			if (a == 1 || a == 7) return KEY_HOME;
			if (a == 3) return KEY_DELETE;
			if (a == 4 || a == 8) return KEY_END;
			if (a == 5) return KEY_PGUP;
			if (a == 6) return KEY_PGDN;
		}
	}
	return 27;
}
