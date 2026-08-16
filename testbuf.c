#include <sys/types.h>
#include <sys/file.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include "pdp11_compat.h"
#include "buffer.h"

#define TEST_SIZE 50000L

static int
value_at(i)
	long i;
{
	return (int)(i % 251) + 1;
}

static void
fail(s)
	char* s;
{
	fprintf(stderr, "test_buffer: %s\n", s);
	exit(1);
}

int
main(argc,argv)
	int argc;
	char** argv;
{
	char source[32];
	char output[32];
	char block[256];
	struct buffer b;
	long made;
	long i;
	int fd;
	int n;
	int ch;

	strcpy(source, "/tmp/psrcXXXXXX");
	strcpy(output, "/tmp/poutXXXXXX");
	fd = mkstemp(source);
	if (fd < 0)
		fail("mkstemp source");
	made = 0;
	while (made < TEST_SIZE) {
		n = TEST_SIZE - made > (long)sizeof block ?
		    sizeof block : TEST_SIZE - made;
		for (i = 0; i < n; ++i)
			block[i] = value_at(made + i);
		if (write(fd, (char *)block, n) != n)
			fail("write source");
		made += n;
	}
	close(fd);
	fd = mkstemp(output);
	if (fd < 0)
		fail("mkstemp output");
	close(fd);
	unlink(output);
	b.left = b.right = -1;
	if (b_open(&b, source) < 0)
		fail("b_open");
	if (b_size(&b) != TEST_SIZE || b_pos(&b) != 0) {
		fprintf(stderr, "test_buffer: size %ld, position %ld\n",
		    (long)b_size(&b), (long)b_pos(&b));
		fail("initial size or position");
	}
	for (i = 0; i < TEST_SIZE; ++i) {
		if (b_get(&b, i) != value_at(i))
			fail("loaded byte order");
	}
	if (b_seek(&b, (off_t)1000) < 0 || b.changed)
		fail("seek changed document");
	if (b_insert(&b, 'Z') < 0)
		fail("insert");
	if (b_delete(&b) != value_at(1000))
		fail("delete");
	if (b_seek(&b, b_size(&b)) < 0 || b_insert(&b, 'Q') < 0)
		fail("append");
	if (b_save(&b, output) < 0 || b.changed)
		fail("save");
	b_close(&b);
	fd = open(output, O_RDONLY, 0);
	if (fd < 0)
		fail("open output");
	for (i = 0; i < TEST_SIZE + 1; ++i) {
		if (read(fd, (char *)&block[0], 1) != 1)
			fail("short output");
		ch = i == 1000 ? 'Z' : i == TEST_SIZE ? 'Q' : value_at(i);
		if (block[0] != ch)
			fail("saved contents");
	}
	if (read(fd, (char *)&block[0], 1) != 0)
		fail("long output");
	close(fd);
	unlink(source);
	unlink(output);
	printf("buffer tests passed\n");
	return 0;
}
