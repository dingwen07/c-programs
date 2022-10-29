CC = gcc
CFLAGS = -Wall -I../include


all: sockcomm

sockcomm: sockcomm.o buffer.o
	$(CC) $(CFLAGS) -o sockcomm sockcomm.o buffer.o

sockcomm.o: sockcomm.c
	$(CC) $(CFLAGS) -c sockcomm.c

buffer.o: ../buffer/buffer.c
	$(CC) $(CFLAGS) -c ../buffer/buffer.c

clean:
	rm -f *.o sockcomm
