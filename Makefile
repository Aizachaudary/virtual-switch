CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -g -O0 -pthread

all: switch host

switch: switch.o frame.o refcount.o queue.o mactable.o
	$(CC) $(CFLAGS) -o switch switch.o frame.o refcount.o queue.o mactable.o

host: host.o frame.o refcount.o
	$(CC) $(CFLAGS) -o host host.o frame.o refcount.o

%.o: %.c frame.h queue.h mactable.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f switch host *.o
