# OSUE 2016W - Task 3
# Author: Lorenz Leutgeb
# Date: 2016-01-10

CFLAGS=-std=c99 -DENDEBUG -D_BSD_SOURCE -pedantic -Wall -g -D_XOPEN_SOURCE=500 -lrt -pthread

all: hangman-server hangman-client

%.o: %.c
	gcc ${CFLAGS} -c $^ -o $@

hangman-server: wordlist.o

clean:
	rm -rf hangman-server hangman-client *.o
