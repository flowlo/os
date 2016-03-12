#!
# @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
# @file   Makefile
# @date   2015-11-04

CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g
LDFLAGS=

.PHONY: all clean

all: server client

%: %.o
	gcc ${LDFLAGS} $^ -o $@

%.o: %.c
	gcc ${CFLAGS} -c $^ -o $@

clean:
	rm -f server client *.o
