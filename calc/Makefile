all: calc.c
	gcc -std=c99 -pedantic -Wall -D_XOPEN_SOURCE=500 -D_BSD_SOURCE -g -c calc.c
	gcc -lm calc.o -o calc
clean:
	rm calc calc.o
