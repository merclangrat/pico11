CC=cc
CFLAGS=-O
LDFLAGS=-n
OBJS=pico11.o buffer.o terminal.o compat.o

all: pico11

test: testbuf
	./testbuf

testbuf: testbuf.o buffer.o compat.o
	$(CC) $(LDFLAGS) -o testbuf testbuf.o buffer.o compat.o

testbuf.o: testbuf.c buffer.h pdpcompat.h
	$(CC) $(CFLAGS) -c testbuf.c

pico11: $(OBJS)
	$(CC) $(LDFLAGS) -o pico11 $(OBJS)

pico11.o: pico11.c buffer.h terminal.h pdpcompat.h
	$(CC) $(CFLAGS) -c pico11.c

buffer.o: buffer.c buffer.h pdpcompat.h
	$(CC) $(CFLAGS) -c buffer.c

terminal.o: terminal.c terminal.h pdpcompat.h
	$(CC) $(CFLAGS) -c terminal.c

compat.o: compat.c
	$(CC) $(CFLAGS) -c compat.c

clean:
	rm -f pico11 testbuf testbuf.o $(OBJS)
