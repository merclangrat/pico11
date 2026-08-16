#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "pdpcompat.h"
#include "buffer.h"
#include "terminal.h"

extern char *sys_errlist[];

#define MAXNAME 255
#define MAXCOLS 160
#define CTRL(c) ((c) & 037)

struct editor {
	struct buffer text;
	char name[MAXNAME + 1];
	off_t top;
	int hscroll;
	int rows;
	int cols;
	int wanted;
	int cutfd;
	int cutappend;
	/* @vak: what the body was drawn from.
	refresh() skips the body repaint when none
	of these has changed */
	long lstver; /* AS: shorten names */
	off_t lsttop;
	int lsthscr;
	char message[MAXCOLS + 1];
};

static struct editor E;

static off_t line_start();
static off_t line_end();
static off_t next_line();
static int visual_col();
static refresh();

/* @vak: force a full body repaint */
static void repaint()
{
	E.lstver = -1;
}

static int on_line(cur,start,next)
off_t cur, start, next;
{
	off_t n;

	n = b_size(&E.text);
	return cur >= start && (cur < next ||
	    (cur == n && line_end(&E.text, start) == n));
}

static void
die(sig)
int sig;
{
	t_close();
	b_close(&E.text);
	if (E.cutfd >= 0)
		close(E.cutfd);
	if (sig)
		_exit(1);
}

static void
message(s)
char* s;
{
	strncpy(E.message, s, MAXCOLS);
	E.message[MAXCOLS] = '\0';
}

static off_t
line_start(b,p)
struct buffer *b;
off_t p;
{
	while (p > 0 && b_get(b, p - 1) != '\n')
		--p;
	return p;
}

static off_t
line_end(b,p)
struct buffer *b;
off_t p;
{
	off_t n;

	n = b_size(b);
	while (p < n && b_get(b, p) != '\n')
		++p;
	return p;
}

static off_t
next_line(b,p)
struct buffer *b;
off_t p;
{
	off_t q;

	q = line_end(b, p);
	if (q < b_size(b))
		++q;
	return q;
}

static int
visual_col(b,p)
struct buffer *b;
off_t p;
{
	off_t q;
	int col;
	int ch;

	q = line_start(b, p);
	col = 0;
	while (q < p) {
		ch = b_get(b, q++);
		if (ch == '\t')
			col = (col + 8) & ~7;
		else if (ch != '\n')
			++col;
	}
	return col;
}

static off_t
at_column(b, start, want)
struct buffer *b;
off_t start;
int want;
{
	off_t p;
	off_t n;
	int col;
	int next;
	int ch;

	p = start;
	n = b_size(b);
	col = 0;
	while (p < n && (ch = b_get(b, p)) != '\n') {
		next = ch == '\t' ? (col + 8) & ~7 : col + 1;
		if (next > want)
			break;
		col = next;
		++p;
	}
	return p;
}

static void
put_padded(s, inverse)
char* s;
int inverse;
{
	char out[MAXCOLS];
	int width;
	int n;

	width = E.cols > MAXCOLS ? MAXCOLS : E.cols;
	n = strlen(s);
	if (n > width)
		n = width;
	(void)memcpy(out, s, n);
	while (n < width)
		out[n++] = ' ';
	/* DEMOS/VT52 doesn't have inverse -- AS */
	/*if (inverse)
		t_put("\033[7m");*/
	t_write(out, width);
	/*if (inverse)
		t_put("\033[0m");*/
}

static int
body_rows()
{
	return E.rows > 6 ? E.rows - 4 : E.rows - 2;
}

static void
ensure_visible()
{
	off_t cur;
	off_t p;
	off_t q;
	int body;
	int i;
	int col;

	cur = b_pos(&E.text);
	body = body_rows();
	if (E.top > cur)
		E.top = line_start(&E.text, cur);
	p = E.top;
	for (i = 0; i < body && p <= b_size(&E.text); ++i) {
		q = next_line(&E.text, p);
		if (on_line(cur, p, q))
			break;
		if (q == p)
			break;
		p = q;
	}
	while (i >= body) {
		E.top = next_line(&E.text, E.top);
		p = E.top;
		for (i = 0; i < body && p <= b_size(&E.text); ++i) {
			q = next_line(&E.text, p);
			if (on_line(cur, p, q))
				break;
			p = q;
		}
	}
	col = visual_col(&E.text, cur);
	if (col < E.hscroll)
		E.hscroll = col;
	if (col >= E.hscroll + E.cols)
		E.hscroll = col - E.cols + 1;
}

static void
draw_line(start)
off_t start;
{
	char out[MAXCOLS];
	off_t p;
	off_t n;
	int width;
	int col;
	int used;
	int ch;
	int count;

	width = E.cols > MAXCOLS ? MAXCOLS : E.cols;
	for (used = 0; used < width; ++used)
		out[used] = ' ';
	p = start;
	n = b_size(&E.text);
	col = 0;
	while (p < n && (ch = b_get(&E.text, p++)) != '\n') {
		count = ch == '\t' ? 8 - (col & 7) : 1;
		while (count-- > 0) {
			if (col >= E.hscroll && col - E.hscroll < width) {
				if (ch == '\t')
					out[col - E.hscroll] = ' ';
				/* cyrillic -- AS */
				else if ((unsigned)(ch & 0377) < 32 
					|| ch == 127)
					out[col - E.hscroll] = '?';
				else
					out[col - E.hscroll] = ch;
			}
			++col;
		}
		if (col >= E.hscroll + width)
			break;
	}
	t_write(out, width);
}

static void
refresh()
{
	char title[MAXCOLS + 1];
	off_t p;
	off_t cur;
	off_t q;
	int body;
	int row;
	int crow;
	int ccol;
	int cfound;
	int full;

	t_size(&E.rows, &E.cols);
	if (E.cols > MAXCOLS)
		E.cols = MAXCOLS;
	ensure_visible();
	/* @vak: repaint the body only when the doc
		or the viewport changed */
	full = (E.text.version != E.lstver) || (E.top != E.lsttop) ||
		(E.hscroll != E.lsthscr);
	E.lstver = E.text.version;
	E.lsttop = E.top;
	E.lsthscr = E.hscroll;
	/* t_put("\033[?25l"); */
	t_move(0, 0);
	strcpy(title, "  pico11       ");
	strncat(title, E.name[0] ? E.name : "New Buffer",
	    MAXCOLS - strlen(title));
	if (E.text.changed)
		strncat(title, "  (modified)", MAXCOLS - strlen(title));
	put_padded(title, 1);
	body = body_rows();
	p = E.top;
	cur = b_pos(&E.text);
	crow = 0;
	cfound = 0;
	for (row = 0; row < body; ++row) {
		if (full) {
			t_move(row + 1, 0);
			draw_line(p);
		}
		q = next_line(&E.text, p);
		if (!cfound && on_line(cur, p, q)) {
			crow = row;
			cfound = 1;
		}
		if (q != p)
			p = q;
	}
	t_move(body + 1, 0);
	put_padded(E.message, 1);
	if (full && E.rows > 6) {
	 t_move(body + 2, 0);
	 put_padded("^G Help  ^W Write Out  ^F Where Is  ^K Cut  ^U Uncut", 0);
	 t_move(body + 3, 0);
	 put_padded("^X Exit  ^R Read File  ^C Position  ^A Home  ^E End", 0);
	}
	ccol = visual_col(&E.text, cur) - E.hscroll;
	if (ccol < 0)
		ccol = 0;
	if (ccol >= E.cols)
		ccol = E.cols - 1;
	t_move(crow + 1, ccol);
	/* t_put("\033[?25h"); */
}

static int
prompt(label, answer, size)
char* label;
char* answer;
int size;
{
	int n;
	int key;
	char shown[MAXCOLS + 1];

	n = strlen(answer);
	for (;;) {
		strncpy(shown, label, MAXCOLS);
		shown[MAXCOLS] = '\0';
		strncat(shown, answer, MAXCOLS - strlen(shown));
		message(shown);
		refresh();
		key = t_key();
		if (key == '\r' || key == '\n')
			return 1;
		if (key == CTRL('C') || key == 27) {
			message("Cancelled");
			return 0;
		}
		if (key == 127 || key == CTRL('H')) {
			if (n > 0)
				answer[--n] = '\0';
		} else if (key >= 32 && key < 127 && n < size - 1) {
			answer[n++] = key;
			answer[n] = '\0';
		}
	}
}

static void
move_vertical(down)
int down;
{
	off_t cur;
	off_t start;
	off_t target;

	cur = b_pos(&E.text);
	start = line_start(&E.text, cur);
	if (E.wanted < 0)
		E.wanted = visual_col(&E.text, cur);
	if (down) {
		target = line_end(&E.text, start);
		if (target == b_size(&E.text))
			return;
		++target;
	} else {
		if (start == 0)
			return;
		target = line_start(&E.text, start - 1);
	}
	(void)b_seek(&E.text, at_column(&E.text, target, E.wanted));
}

static void
page_move(down)
int down;
{
	int i;
	int count;

	count = body_rows() - 1;
	for (i = 0; i < count; ++i)
		move_vertical(down);
	E.top = line_start(&E.text, b_pos(&E.text));
}

static void
do_write()
{
	char name[MAXNAME + 1];

	strcpy(name, E.name);
	if (!prompt("File Name to Write: ", name, sizeof name))
		return;
	if (!name[0]) {
		message("No file name");
		return;
	}
	if (b_save(&E.text, name) < 0) {
		message("Error writing file");
		return;
	}
	strcpy(E.name, name);
	message("Wrote file");
}

static void
do_read()
{
	char name[MAXNAME + 1];
	char block[BUF_CACHE];
	int fd;
	int n;
	int i;

	name[0] = '\0';
	if (!prompt("File to Insert: ", name, sizeof name))
		return;
	fd = open(name, O_RDONLY, 0);
	if (fd < 0) {
		message("Cannot read file");
		return;
	}
	while ((n = read(fd, block, sizeof block)) > 0) {
		for (i = 0; i < n; ++i) {
			if (b_insert(&E.text, block[i]) < 0)
				break;
		}
		if (i != n)
			break;
	}
	close(fd);
	message(n < 0 || (n > 0 && i != n) ? "Read error" : "Inserted file");
}

static void
do_search()
{
	char pat[80];
	off_t start;
	off_t p;
	off_t n;
	off_t q;
	int len;
	int i;
	int pass;

	pat[0] = '\0';
	if (!prompt("Search: ", pat, sizeof pat) || !pat[0])
		return;
	len = strlen(pat);
	n = b_size(&E.text);
	start = b_pos(&E.text);
	p = start < n ? start + 1 : 0;
	for (pass = 0; pass < 2; ++pass) {
		while (p + len <= n && (pass == 0 || p <= start)) {
			for (i = 0, q = p; i < len; ++i, ++q) {
				if (b_get(&E.text, q) != pat[i])
					break;
			}
			if (i == len) {
				b_seek(&E.text, p);
				message("Found");
				return;
			}
			++p;
		}
		p = 0;
	}
	message("Not found");
}

static void
do_position()
{
	off_t p;
	off_t n;
	off_t i;
	long line;
	int col;

	p = b_pos(&E.text);
	n = b_size(&E.text);
	line = 1;
	for (i = 0; i < p; ++i) {
		if (b_get(&E.text, i) == '\n')
			++line;
	}
	col = visual_col(&E.text, p) + 1;
	sprintf(E.message, "line %ld, column %d, character %ld of %ld",
	    line, col, (long)p, (long)n);
}

static void
do_cut()
{
	int ch;
	int any;
	char c;

	if (!E.cutappend) {
		ftruncate(E.cutfd, (off_t)0);
		lseek(E.cutfd, (off_t)0, L_SET);
	} else
		lseek(E.cutfd, (off_t)0, L_XTND);
	any = 0;
	while ((ch = b_delete(&E.text)) >= 0) {
		c = ch;
		write(E.cutfd, (char *)&c, 1);
		any = 1;
		if (ch == '\n')
			break;
	}
	E.cutappend = any;
	message(any ? "Cut text" : "Nothing to cut");
}

static void
do_uncut()
{
	char block[BUF_CACHE];
	int n;
	int i;

	lseek(E.cutfd, (off_t)0, L_SET);
	while ((n = read(E.cutfd, block, sizeof block)) > 0) {
		for (i = 0; i < n; ++i)
			b_insert(&E.text, block[i]);
	}
	message("Uncut text");
}

static void
help()
{
	int body;
	int row;
	char *lines[12];

	lines[0] = "pico11 help";
	lines[1] = "Arrow keys move; Home/End and Page Up/Down work.";
	lines[2] = "^A/^E home/end     ^P/^N up/down     ^B/^F left/right";
	lines[3] = "^O write file      ^R insert file   ^W search";
	lines[4] = "^K cut line        ^U uncut         ^D delete";
	lines[5] = "^C position        ^X exit";
	lines[6] = "";
	lines[7] = "Files are edited through disk scratch space, not held in RAM.";
	lines[8] = "Press any key to return.";
	lines[9] = "";
	lines[10] = "";
	lines[11] = "";
	body = E.rows < 12 ? E.rows : 12;
	t_clear();
	for (row = 0; row < body; ++row) {
		t_move(row, 0);
		put_padded(lines[row], row == 0);
	}
	t_key();
	message("");
	repaint();
}

static int
confirm_exit()
{
	int key;

	if (!E.text.changed)
		return 1;
	message("Save modified buffer?  Y Yes  N No  ^C Cancel");
	refresh();
	key = t_key();
	if (key == 'y' || key == 'Y') {
		do_write();
		return !E.text.changed;
	}
	if (key == 'n' || key == 'N')
		return 1;
	message("Cancelled");
	return 0;
}

static void
edit_loop()
{
	int key;

	for (;;) {
		refresh();
		key = t_key();
		if (key < 0)
			return;
		if (key != CTRL('K'))
			E.cutappend = 0;
		if (key == CTRL('X')) {
			if (confirm_exit())
				return;
		} else if (key == CTRL('G')) {
			help();
		} else if (key == CTRL('W')) {
			do_write();
		} else if (key == CTRL('R')) {
			do_read();
		} else if (key == CTRL('F')) {
			do_search();
		} else if (key == CTRL('C')) {
			do_position();
		} else if (key == CTRL('K')) {
			do_cut();
		} else if (key == CTRL('U')) {
			do_uncut();
		} else if (key == KEY_LEFT || key == CTRL('B')) {
			b_left(&E.text);
			E.wanted = -1;
		} else if (key == KEY_RIGHT || key == CTRL('V')) {
			b_right(&E.text);
			E.wanted = -1;
		} else if (key == KEY_UP || key == CTRL('P')) {
			move_vertical(0);
		} else if (key == KEY_DOWN || key == CTRL('T')) {
			move_vertical(1);
		} else if (key == KEY_HOME || key == CTRL('A')) {
			b_seek(&E.text,line_start(&E.text,b_pos(&E.text)));
			E.wanted = -1;
		} else if (key == KEY_END || key == CTRL('E')) {
			b_seek(&E.text,line_end(&E.text,b_pos(&E.text)));
			E.wanted = -1;
		} else if (key == KEY_PGUP) {
			page_move(0);
		} else if (key == KEY_PGDN) {
			page_move(1);
		} else if (key == KEY_DELETE || key == CTRL('D')) {
			b_delete(&E.text);
			E.wanted = -1;
		} else if (key == 127 || key == CTRL('H')) {
			b_backspace(&E.text);
			E.wanted = -1;
		} else if (key == '\r' || key == '\n') {
			b_insert(&E.text, '\n');
			E.wanted = -1;
		} else if (key == '\t' || (key >= 32 && key < 256)) {
			b_insert(&E.text, key);
			E.wanted = -1;
		}
	}
}

static int
make_cutfile()
{
	char path[32];
	int fd;

	strcpy(path, "/tmp/p11cutXXXX");
	fd = mkstemp(path);
	if (fd >= 0)
		unlink(path);
	return fd;
}

int
main(argc, argv)
int argc;
char** argv;
{
	char *name;

	name = argc > 1 ? argv[1] : "";
	memset((char *)&E, 0, sizeof E);
	E.text.left = E.text.right = -1;
	E.cutfd = -1;
	E.wanted = -1;
	repaint(); /* @vak: ensure first refresh draws the full screen */
	if (strlen(name) > MAXNAME) {
		fprintf(stderr, "pico11: file name too long\n");
		return 1;
	}
	strcpy(E.name, name);
	if (b_open(&E.text, name) < 0) {
		fprintf(stderr, "pico11: cannot open %s: %s\n", name,
		    sys_errlist[errno]);
		b_close(&E.text);
		return 1;
	}
	E.cutfd = make_cutfile();
	if (E.cutfd < 0 || t_open() < 0) {
		(void)fprintf(stderr, "pico11: cannot initialize terminal or scratch file\n");
		die(0);
		return 1;
	}
	signal(SIGHUP, die);
	signal(SIGTERM, die);
	signal(SIGINT, die);
	message("Use ^G for help");
	edit_loop();
	die(0);
	return 0;
}
