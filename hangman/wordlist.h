/**
 * @file bufferedFileRead.h
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2016-01-10
 *
 * @brief Module that provides type and utilities for a linked list of strings.
 **/

#ifndef WORDLIST_H
#define WORDLIST_H

#include <stdio.h>
#include <stdbool.h>

/**
 * @brief A structure to store lists of words.
 */
struct wordlist {
	struct wordlist* next; /**< Pointer to the next element in the list. */
	char* value; /**< Actual value of the word. */
};

/**
 * @brief Reads the content of a FILE* into a linked list.
 * @details The content of FILE* is read line by line and appended to the list
 * the passed pointer references. If the pointer is NULL, it will be pointed at
 * a new list.
 * @param *f The already opened file to read from.
 * @param **wordlist Reference to a pointer for the list to use.
 * @param *n will be set to the number of words read. 
 * @return A value different from 0 if an error (such as memory allocation or too long lines) occurs, 0 otherwise.
 */
int wordlist_read(FILE *f, struct wordlist** l, size_t* n);

/**
 * @brief Frees the allocated space of a wordlist and all the content inside
 * @param *l A pointer to the list to be free'd.
 */
void wordlist_free(struct wordlist* l);

struct wordlist* wordlist_copy(struct wordlist* l);

void wordlist_print(struct wordlist* l);

#endif
