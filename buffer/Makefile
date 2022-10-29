CC = gcc
CFLAGS = -Wall -I../include

all: buffer.o test

buffer.o: buffer.c buffer.h
	$(CC) $(CFLAGS) -c buffer.c

test: test.c buffer.o
	$(CC) $(CFLAGS) -o test test.c buffer.o
