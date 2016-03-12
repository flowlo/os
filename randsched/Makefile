#!
# @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
# @file   Makefile
# @date   2015-11-13

CC=gcc
CFLAGS=-std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g
LDFLAGS=

.PHONY: all clean

all: rventgas rshutdown schedule

%: %.o
	gcc ${LDFLAGS} $^ -o $@

%.o: %.c
	gcc ${CFLAGS} -c $^ -o $@

r*.o: r*.h

clean:
	rm -f rventgas rshutdown schedule *.o
