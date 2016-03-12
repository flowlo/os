/**
 * @file bufferedFileRead.c
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2016-01-10
 *
 * @brief Implementation of the bufferedFileRead module
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "wordlist.h"

#define MAXLINELEN 1024

struct wordlist* wordlist_append(struct wordlist* l) {
	return (l->next = (struct wordlist*)malloc(sizeof(struct wordlist)));
}

void wordlist_free(struct wordlist* l) {
	struct wordlist* cur = l;
	struct wordlist* next;

	while (cur != NULL) {
		next = cur->next;
		free(cur);
		cur = next;
	}
}

struct wordlist* wordlist_copy(struct wordlist* l) {
	if (l == NULL) {
		return NULL;
	}

	struct wordlist* copy = (struct wordlist*)malloc(sizeof(struct wordlist));

	copy->value = (char *)malloc(strlen(l->value) + 1);
	strcpy(copy->value, l->value);

	if (l->next != NULL) {
		copy->next = wordlist_copy(l->next);
	}

	return copy;
}

int wordlist_read(FILE *f, struct wordlist** words, size_t *n) {
	char buf[MAXLINELEN];

	*n = 0;

	struct wordlist* prev = *words;
	struct wordlist* curr = NULL;

	unsigned char *s = (void*)buf;
	unsigned char *ptr = s;

	while (fgets(buf, MAXLINELEN, f) != NULL) {
		buf[strcspn(buf, "\n")] = '\0';

		if (buf[0] == '\0') continue;
		s = (void*)buf;

		while (*s != '\0') {
			if (isalpha(*s) || *s == 0x20) {
				*(ptr++) = toupper(*s);
			}
			s++;
		}
		*ptr = '\0';

		curr = (struct wordlist*)malloc(sizeof(struct wordlist));
		curr->value = (char *)malloc(strlen(buf) + 1);
		(void) strcpy(curr->value, buf);

		if (*words == NULL) {
			*words = curr;
		}

		if (prev != NULL) {
			prev->next = curr;
		}

		prev = curr;
		curr = NULL;
		(*n)++;
	}

	return 0;
}

void wordlist_print(struct wordlist* l) {
	if (l == NULL) {
		printf("NULL!");
	}

	while (l != NULL) {
		printf("%s, ", l->value);
		l = l->next;
	}

	printf("\n");
}
