/*!
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @file   calc.c
 * @date   2015-10-11
 * @brief  A program to evaluate simple postfix expressions.
 *
 * Reads from files passed via argv or standard input otherwise. Expressions
 * must come as one per line, and will produce one line of output (value on the
 * stack after performing all operations. Known operations are +, -, *, / with
 * their usual meaning and s (sine) and c (cosine).
 * If something goes wrong, expet a dirty exit status.
 */

#include <math.h>    // cos, fabs, sin
#include <stdio.h>   // printf
#include <unistd.h>  // getopt
#include <stdbool.h> // bool
#include <stdlib.h>  // free, malloc, strtod
#include <ctype.h>   // isspace

#define MAX_LINE 1024

/*!
 * Global memory preallocated and used for storing immediates and intermediate
 * results in computation.
 * There is one global stack, not suitable for concurrent use.
 */
static double stack[1024];

/*!
 * Points to the top of stack, i.e. where to pop from or push to.
 */
static size_t tos = 0;

/*!
 * Decrements the top of the stack pointer and returns the value it refers to.
 *
 * @returns The value that was remove from the top of the stack.
 * @details Accesses globals stack (r) and tos (rw).
 */
double pop(void) {
	return stack[--tos];
}

/*!
 * Sets the top of stack to the passed value and increments the top of stack
 * pointer.
 * @details Accesses globals stack (rw) and tos (rw).
 */
void push(double v) {
	stack[tos++] = v;
}

/*!
 * Returns true iff there is nothing on the stack, false otherwise.
 *
 * @retval true The stack is empty.
 * @retval false The stack is not empty.
 * @details Accesses global tos (w).
 */
bool empty(void) {
	return tos == 0;
}

/*!
 * Reads the passed line and evaluates it as a postfix expression.
 *
 * Results a re written to standard output.
 *
 * @param line  The string to evaluate.
 * @param fi    A flag that indicates whether to coerce the result to an
 *              integer value. That is, after all computation.
 * @param fa    A flag that indicates whether to take the absolute value
 *              of the result. That is, after all computation.
 * @returns Zero if everything went well, a positive number if an error ocurred.
 */
int calc(char *line, bool fi, bool fa) {
	char *i = line, *end = NULL;
	double v, w;

	while (*i != 0) {
		if (isspace(*i)) {
			i++;
			continue;
		}

		v = strtod(i, &end);
		if (v != 0.0 && i != end) {
			push(v);
			i = end;
			continue;
		}

		switch (*i) {
		case '+':
			push(pop() + pop());
			break;
		case '-':
			v = pop();
			w = pop();
			push(w - v);
			break;
		case '*':
			push(pop() * pop());
			break;
		case '/':
			v = pop();
			w = pop();
			push(w / v);
			break;
		case 's':
			push(sin(pop()));
			break;
		case 'c':
			push(cos(pop()));
			break;
		default:
			return 1;
		}
		i++;
	}

	v = pop();

	// Unbalanced stack is not good.
	if (!empty()) {
		return 1;
	}

	if (fa) {
		v = fabs(v);
	}

	if (fi) {
		(void)printf("%d\n", (int)v);
	} else {
		(void)printf("%lf\n", v);
	}

	return 0;
}

/*!
 * Consumes a file and passes it to calc line by line. Empty lines are skipped.
 *
 * @param f     The file to be read. Must be closed by the caller.
 * @param fi    A flag that indicates whether to coerce results to an
 *              integer value. That is, after all computation.
 * @param fa    A flag that indicates whether to take the absolute value
 *              of results. That is, after all computation.
 * @returns Zero if everything went well, a positive number if an error ocurred.
 */
int split(FILE *f, bool fi, bool fa) {
	char *ln = (char*)malloc(sizeof(char) * MAX_LINE);
	int i = 0, r = 0, c = 0;

	while (!feof(f)) {
		c = getc(f);

		// Read nothing special, buffer that.
		if (c != '\n' && c != EOF) {
			ln[i++] = c;
			continue;
		}

		// Newline or EOF, append NUL to terminate line.
		ln[i] = '\0';
		if (i == 0) {
			continue;
		}

		// Do the math.
		r = calc(ln, fi, fa);
		if (r != 0) {
			break;
		}

		i = 0;
	}

	free(ln);

	return 0;
}


int main(int argc, char **argv) {
	bool fi = false, fa = false;

	int opt = -1;

	while ((opt = getopt(argc, argv, "ai")) != -1) {
		switch (opt) {
		case 'a':
			fa = true;
			break;
		case 'i':
			fi = true;
			break;
		default:
			(void)fprintf(stderr, "Usage: %s [-i] [-a] [file1 [file2 ...]]\n", argv[0]);
			return 1;
		}
	}

	if (optind >= argc) {
		return split(stdin, fi, fa);
	}

	for (int i = optind; i < argc; i++) {
		FILE *f = fopen(argv[i], "r");
		int r = split(f, fi, fa);
		(void)fclose(f);
		if (r != 0) {
			return r;
		}
	}

	return 0;
}
