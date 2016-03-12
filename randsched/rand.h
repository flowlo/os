#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#ifndef RAND_H
#define RAND_H

#define RAND(P, STR1, EXT1, STR2, EXT2)                              \
int main(int argc, char **argv) {                                    \
	if (argc > 1) {                                              \
		(void) fprintf(stderr, "SYNOPSIS\n\t%s\n", argv[0]); \
		return EXIT_FAILURE;                                 \
	}                                                            \
                                                                     \
	srand(time(NULL));                                           \
                                                                     \
	if (rand() % P) {                                            \
		(void) printf(#STR1);                                \
		return EXT1;                                         \
	}                                                            \
                                                                     \
	(void) printf(#STR2);                                        \
	return EXT2;                                                 \
}

#endif
