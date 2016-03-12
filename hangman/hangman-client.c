/**
 * @file hangman-server.c
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2016-01-10
 *
 * @brief This module is a client to hangman-server.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <semaphore.h>
#include "hangman-common.h"
#include "gallows.h"

/** @brief Name of the program */
static const char *progname = "hangman-client"; /* default name */

/** @brief Signal indicator, set to 1 on SIGINT or SIGTERM. */
volatile sig_atomic_t caught_sig = 0;

/** @brief The ID of this client, assigned by the server upon registration. */
static int clientno = -1;

/** @brief Shared memory for client-server communication. */
static struct shm *shared;

/** @brief Semaphore which tells the server when there is a request. */
static sem_t *srv_sem;

/** @brief Semaphore which tells the clients when the server is ready. */
static sem_t *clt_sem;

/** @brief Semaphore which tells the client who has sent the last request when there is an answer. */
static sem_t *ret_sem;

/**
 * @brief terminate program on program error
 * @details Global variables: progname
 * @param exitcode exit code
 * @param fmt format string
 */
static void bail_out(int exitcode, const char *fmt, ...);

/**
 * @brief Signal handler
 * @details Global variables: caught_sig
 * @param sig Signal number
 */
static void signal_handler(int sig);

/**
 * @brief free allocated resources
 * @details Global variables: shared, srv_sem, clt_sem, ret_sem
 * @param soft if true, the server is informed about the coming shutdown, else the resources are free'd silently.
 */
static void free_resources(bool soft);

static void bail_out(int exitcode, const char *fmt, ...)
{
	va_list ap;

	(void) fprintf(stderr, "%s: ", progname);
	if (fmt != NULL) {
		va_start(ap, fmt);
		(void) vfprintf(stderr, fmt, ap);
		va_end(ap);
	}
	if (errno != 0) {
		(void) fprintf(stderr, ": %s", strerror(errno));
	}
	(void) fprintf(stderr, "\n");

	free_resources(true);
	exit(exitcode);
}

static void signal_handler(int sig)
{
	caught_sig = 1;
}

static void free_resources(bool soft)
{
	if (soft && shared != NULL) {
		// Begin of critical section.
		if (sem_wait(clt_sem) == -1) {
			if (errno != EINTR) {
				(void) fprintf(stderr, "%s: sem_wait\n", progname);
			} else {
				(void) fprintf(stderr, "%s: interrupted while trying to inform server about shutdown\n", progname);
			}
		} else {
			shared->terminate = true;
			shared->clientno = clientno;

			if (sem_post(srv_sem) == -1) {
				(void) fprintf(stderr, "%s: sem_post\n", progname);
			}
		}
		// End of critical section.
	}

	if (shared != NULL) {
		if (munmap(shared, sizeof *shared) == -1) {
			(void) fprintf(stderr, "%s: munmap: %s\n", progname, strerror(errno));
		}
	}

	if (sem_close(srv_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, SRV_SEM, strerror(errno));
	if (sem_close(clt_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, CLT_SEM, strerror(errno));
	if (sem_close(ret_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, RET_SEM, strerror(errno));
}

/**
 * @brief Program entry point
 * @details global variables: all defined in this module.
 * @param argc Argument counter
 * @param argv Argument values
 * @return EXIT_SUCCESS, if no error occurs. Exits with EXIT_FAILURE otherwise
 */
int main(int argc, char *argv[])
{
	if (argc > 0) {
		progname = argv[0];
 	}
	if (argc != 1) {
		fprintf(stderr, "No command line arguments allowed.\nUSAGE: %s", progname);
		exit(EXIT_FAILURE);
	}

	const int signals[] = {SIGINT, SIGTERM};
	struct sigaction s;

	s.sa_handler = signal_handler;
	s.sa_flags   = 0;
	if (sigfillset(&s.sa_mask) < 0) {
		bail_out(EXIT_FAILURE, "sigfillset");
	}
	for (int i = 0; i < COUNT_OF(signals); i++) {
		if (sigaction(signals[i], &s, NULL) < 0) {
			bail_out(EXIT_FAILURE, "sigaction");
		}
	}

	int shmfd = shm_open(SHM_NAME, O_RDWR , PERMISSION);
	if (shmfd == -1) {
		fprintf(stderr, "%s: No server accessible. Start hangman-server first!\n", progname);
		exit(EXIT_FAILURE);
	}

	if (ftruncate(shmfd, sizeof *shared) == -1) {
		(void) close(shmfd);
		bail_out(EXIT_FAILURE, "Could not ftruncate shared memory");
	}

	shared = mmap(NULL, sizeof *shared, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
	if (shared == MAP_FAILED) {
		(void) close(shmfd);
		bail_out(EXIT_FAILURE, "Could not mmap shared memory");
	}
	if (close(shmfd) == -1) {
		bail_out(EXIT_FAILURE, "Could not close shared memory file descriptor");
	}

	srv_sem = sem_open(SRV_SEM, 0);
	clt_sem = sem_open(CLT_SEM, 0);
	ret_sem = sem_open(RET_SEM, 0);
	if (srv_sem == SEM_FAILED || clt_sem == SEM_FAILED || ret_sem == SEM_FAILED) {
		bail_out(EXIT_FAILURE, "sem_open");
	}

	unsigned int round = 0, errors = 0, wins = 0, losses = 0;
	char c = '\0';
	char buf[MAX_WORD_LENGTH];
	char tried_chars[MAX_WORD_LENGTH];
	enum game_state game_status = game_new;
	(void) memset(&buf[0], 0, sizeof buf);
	(void) memset(&tried_chars[0], 0, sizeof tried_chars);

	while (caught_sig == 0) {
		if (game_status == game_open) {
			(void) printf("Your guess? ");
			if (fgets(&buf[0], MAX_WORD_LENGTH, stdin) == NULL) {
				if (errno == EINTR) continue;
				bail_out(EXIT_FAILURE, "fgets");
			}
			c = toupper(buf[0]);

			if (buf[1] != '\n') {
				(void) printf("Please enter only one letter.\n");
				continue;
			}
			if (!isalpha(c)) {
				(void) printf("Please enter a valid letter.\n");
				continue;
			}

			bool validChar = true;
			for (int i = 0; i < round; i++) {
				if (tried_chars[i] == c) {
					validChar = false;
					break;
				}
			}
			if (!validChar) {
				(void) printf("Please enter letter you have not tried yet.\n");
				continue;
			}

			tried_chars[round] = c;
			round++;
		}

		// Begin of critical section: Send request.
		if (sem_wait(clt_sem) == -1) {
			if (errno == EINTR) continue;
			bail_out(EXIT_FAILURE, "sem_wait");
		}

		if (shared->terminate) {
			free_resources(false);
			exit(EXIT_FAILURE);
		}

		shared->status = game_status;
		shared->clientno = clientno;
		shared->tried_char = c;

		if (sem_post(srv_sem) == -1) {
			bail_out(EXIT_FAILURE, "sem_post");
		}
		// End of critical section: Send request.

		// Begin of critical section: Receive answer.
		if (sem_wait(ret_sem) == -1) {
			if (errno == EINTR) {
				(void) sem_post(clt_sem);
				continue;
			}
			bail_out(EXIT_FAILURE, "sem_wait");
		}

		clientno = shared->clientno;
		strncpy(buf, shared->word, MAX_WORD_LENGTH);
		errors = shared->errors;
		game_status = shared->status;

		if (sem_post(clt_sem) == -1) {
			bail_out(EXIT_FAILURE, "sem_post");
		}
		// End of critical section: Receive answer.

		if (game_status == game_impossible) {
			(void) printf("You played all the available words. \n");
			break;
		}

		(void) printf("%s", gallows[errors]);

		if (game_status == game_open) {
			(void) printf ("\n\n Secret word: %s\n You guessed: %s\n\n", buf, tried_chars);

		} else {
			(void) printf("The word was %s\n", buf);

			if (game_status == game_won)  {
				(void) printf("Congratulations! You figured it out.\n");
				wins++;
			}
			if (game_status == game_lost) {
				(void) printf("Game Over! Want to try again?\n");
				losses++;
			}
			(void) printf("You have now won %d games and lost %d.\n", wins, losses);
			(void) printf("Press 'y' to start a new game or 'n' to stop playing.\n");

			c = tolower(fgetc(stdin));
			if (ferror(stdin)) bail_out(EXIT_FAILURE, "fgetc");

			if (c == 'y') {
				game_status = game_new;
				round = 0;
				errors = 0;
				(void) memset(tried_chars, 0, sizeof tried_chars);
			} else {
				break;
			}
		}
	}

	(void) printf("You have won %d games and lost %d. Bye bye!\n", wins, losses);

	free_resources(true);
	return EXIT_SUCCESS;
}
