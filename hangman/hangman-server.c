/**
 * @file hangman-server.c
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2016-01-10
 *
 * @brief This module behaves as a server in the hangman game.
 * Clients can connect via shared memory. Synchronization via semaphores.
 * For detailed desctiption please refer to "hangman.pdf"
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "wordlist.h"
#include "hangman-common.h"

/**
 * @brief Structure representing a game
 */
struct game {
	char *secret; 			/**< Pointer to the secret word of the game */
	char obscured[MAX_WORD_LENGTH];	/**< A buffer storing the current partly unobscured version of the secret word */
	enum game_state status;		/**< Indicating in which state the game is */
	unsigned int errors;		/**< Counter for the number of errors in the game */
};

/**
 * @brief Structure representing a linked list of clients
 */
struct client {
	int clientno;			/**< The ID of the client used for identification */
	struct wordlist* words;		/**< A linked list of the used words */
	struct game current_game;	/**< The currently played game */
	size_t gamecnt;
	struct client *next;		/**< The next object in the linked list of clients */
};

/** @brief Name of the program */
static const char *progname = "hangman-server"; /* default name */

/** @brief word buffer */
static struct wordlist* words = NULL;

static size_t wordcnt;

/** @brief Signal indicator, gets set to 1 on SIGINT or SIGTERM */
volatile sig_atomic_t caught_sig = 0;

/** @brief Linked List of active clients */
static struct client *clients = NULL;

/** @brief client count for client number assignment */
static unsigned int client_count = 0;

/** @brief A boolean flag indicating whether the semaphores have been started creating or created yet */
static bool semaphores_set = false;

/** @brief Shared memory for client-server communication */
static struct shm *shared;

/** @brief Semaphores which tells the server when there is a request */
static sem_t *srv_sem;

/** @brief Semaphores which tells the clients when the server is ready */
static sem_t *clt_sem;

/** @brief Semaphores which tells the client who has sent the last request when there is an answer */
static sem_t *ret_sem;

/**
 * @brief starts a new game for a given client i.e. decides upon
 * a new word and resets all the necessary variables
 * @details global variables: words
 * @param *client pointer to the struct Client who wants a new game
 */
static void new_game(struct client *client);

/**
 * @brief Calculates the game result for a given client for
 * a given character and stores the results inside the client
 * @param *client pointer to the struct Client who sent the request
 * @param try the character he guessed
 */
static void calculate_results(struct client *c, char try);

/**
 * @brief Signal handler
 * @details global variables: caught_sig
 * @param sig Signal number catched
 */
static void signal_handler(int sig);

/**
 * @brief terminate program on program error
 * @details global variables: progname
 * @param exitcode exit code
 * @param fmt format string
 */
static void bail_out(int exitcode, const char *fmt, ...);

/**
 * @brief Free allocated resources and inform clients
 * via shared memory about shutdown
 * @details global variables: words, clients, semaphores_set, srv_sem, clt_sem, ret_sem
 */
static void free_resources(void);

/**
 * @brief This function Frees the whole linked list of struct Client
 * pointers whose head is the global variable clients.
 * Because these are all active clients, clt_sem is incremented for
 * each of them, so that they can terminate, when they
 * try to send something and see the terminate flag is set to true.
 * @details global variables: clients
 */

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

	free_resources();
	exit(exitcode);
}

static void free_resources(void)
{
	if (shared != NULL) {
		shared->terminate = true;

		struct client *curr = clients;
		struct client *next = NULL;

		while (curr != NULL) {
			next = curr->next;
			wordlist_free(curr->words);
			free(curr);
			curr = next;

			// Increment client semaphore for each
			// client so that they can terminate.
			if (sem_post(clt_sem) == -1) {
				(void) fprintf(stderr, "sem_post");
			}
		}

		if (munmap(shared, sizeof *shared) == -1) {
			(void) fprintf(stderr, "%s: munmap: %s\n", progname, strerror(errno));
		}

		if (shm_unlink(SHM_NAME) == -1) {
			(void) fprintf(stderr, "%s: shm_unlink: %s\n", progname, strerror(errno));
		}
	}
	if (semaphores_set) {
		if (sem_close(srv_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, SRV_SEM, strerror(errno));
		if (sem_close(clt_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, CLT_SEM, strerror(errno));
		if (sem_close(ret_sem) == -1) (void) fprintf(stderr, "%s: sem_close on %s: %s\n", progname, RET_SEM, strerror(errno));
		if (sem_unlink(SRV_SEM) == -1) (void) fprintf(stderr, "%s: sem_unlink on %s: %s\n", progname, SRV_SEM, strerror(errno));
		if (sem_unlink(CLT_SEM) == -1) (void) fprintf(stderr, "%s: sem_unlink on %s: %s\n", progname, CLT_SEM, strerror(errno));
		if (sem_unlink(RET_SEM) == -1) (void) fprintf(stderr, "%s: sem_unlink on %s: %s\n", progname, RET_SEM, strerror(errno));
	}
}

static void signal_handler(int sig)
{
	caught_sig = 1;
}

static void new_game(struct client *c)
{
	// Check whether the client has played all words.
	if (c->gamecnt >= wordcnt) {
		c->current_game.status = game_impossible;
		return;
	}

	unsigned int pos = rand() % (wordcnt - c->gamecnt++);

	struct wordlist* prev = NULL;
	struct wordlist* curr = c->words;

	for (int i = 0; i < pos; i++) {
		// This must hold, otherwise we
		// did some calculations wrong.
		assert(curr->next != NULL);

		prev = curr;
		curr = curr->next;
	}

	// Unwrap string stored within list node.
	char *v = curr->value;

	if (curr == c->words) {
		c->words = curr->next;
	} else {
		prev->next = curr->next;
	}

	free(curr);

	(void) memset(&c->current_game, 0, sizeof (struct game));
	c->current_game.secret = v;
	c->current_game.status = game_open;

	size_t length = strlen(c->current_game.secret);
	for (int i = 0; i < length; i++) {
		if (c->current_game.secret[i] == ' ') {
			c->current_game.obscured[i] = ' ';
		} else {
			c->current_game.obscured[i] = '_';
		}
	}
	c->current_game.obscured[length+1] = '\0';
}

static void calculate_results(struct client *client, char try)
{
	bool error = true;
	bool won = true;

	size_t length = strlen(client->current_game.secret);
	for (size_t i = 0; i < length; i++) {
		if (client->current_game.secret[i] == try) {
			client->current_game.obscured[i] = try;
			error = false;
		}
		won = won && (client->current_game.obscured[i] != '_');
	}

	if (won) {
		client->current_game.status = game_won;
		return;
	}

	if (!error) {
		return;
	}

	client->current_game.errors++;
	if (client->current_game.errors > MAX_ERROR) {
		client->current_game.status = game_lost;
		(void) strncpy(client->current_game.obscured, client->current_game.secret, MAX_WORD_LENGTH);
	}
}

/**
 * @brief Program entry point
 * @details global variables: all above defined
 * @param argc The argument counter
 * @param argv The argument vector
 * @return EXIT_SUCCESS or aborts with exit(EXIT_FAILURE)
 */
int main(int argc, char *argv[])
{
	srand(time(NULL));

	if (argc > 0) {
		progname = argv[0];
	}
	if (argc > 2) {
		fprintf(stderr, "Too many files\nUSAGE: %s [input_file]", progname);
		exit(EXIT_FAILURE);
	}

	int c;
	while ( (c = getopt(argc, argv, "")) != -1 ) {
		switch(c) {
			case '?':
				fprintf(stderr, "USAGE: %s [input_file]", progname);
				exit(EXIT_FAILURE);
			default:
				assert(0);
		}
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

	if (argc == 2) {
		FILE *f;
		char *path = argv[1];

		if( (f = fopen(path, "r")) == NULL ) {
			bail_out(EXIT_FAILURE, "fopen failed on file %s", path);
		}
		if (wordlist_read(f, &words, &wordcnt) != 0) {
			(void) fclose(f);
			bail_out(EXIT_FAILURE, "Error while reading file %s", path);
		};
		if (fclose(f) != 0) {
			bail_out(EXIT_FAILURE, "fclose failed on file %s", path);
		}
	} else {
		(void) printf("Please enter the game dictionary and finish with EOF\n");

		if (wordlist_read(stdin, &words, &wordcnt) != 0) {
			if (caught_sig) {
				free_resources();
				exit(EXIT_FAILURE);
			}
			bail_out(EXIT_FAILURE, "Error while reading dictionary from stdin");
		}

		(void) printf("Successfully read the dictionary. Ready.\n");
	}

	int shmfd = shm_open(SHM_NAME, O_RDWR | O_CREAT, PERMISSION);
	if (shmfd == -1) {
		bail_out(EXIT_FAILURE, "Could not open shared memory");
	}

	if ( ftruncate(shmfd, sizeof *shared) == -1) {
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

	semaphores_set = true;
	srv_sem = sem_open(SRV_SEM, O_CREAT | O_EXCL, PERMISSION, 0);
	clt_sem = sem_open(CLT_SEM, O_CREAT | O_EXCL, PERMISSION, 1);
	ret_sem = sem_open(RET_SEM, O_CREAT | O_EXCL, PERMISSION, 0);
	if (srv_sem == SEM_FAILED || clt_sem == SEM_FAILED || ret_sem == SEM_FAILED) {
		bail_out(EXIT_FAILURE, "sem_open %s %s %s", SRV_SEM, CLT_SEM, RET_SEM);
	}

	struct client *pre = NULL;
	struct client *cur = NULL;

	while (caught_sig == 0) {
		// Begin of critical section.
		if (sem_wait(srv_sem) == -1) {
			if (errno == EINTR) continue;
			bail_out(EXIT_FAILURE, "sem_wait");
		}

		if (shared->clientno == -1) {
			cur = (struct client *) malloc(sizeof (struct client));
			if (cur == NULL) {
				bail_out(EXIT_FAILURE, "malloc on creation of new client");
			}
			cur->words = wordlist_copy(words);
			cur->clientno = client_count++;
			cur->next = clients;
			cur->gamecnt = 0;
			clients = cur;
		} else {
			cur = clients;
			while (cur != NULL && cur->clientno != shared->clientno) {
				pre = cur;
				cur = cur->next;
			}
			if (cur == NULL) bail_out(EXIT_FAILURE, "Could not find client with number %d", shared->clientno);
		}

		// Client has terminated, free resources.
		if (shared->terminate) {
			if (cur == clients) {
				clients = cur->next;
			} else {
				pre->next = cur->next;
			}

			wordlist_free(cur->words);
			free(cur);

			shared->terminate = false;
			if (sem_post(clt_sem) == -1) {
				bail_out(EXIT_FAILURE, "sem_post");
			}
			continue;
		}

		if (shared->status == game_new) {
			// Client wants to play game.
			new_game(cur);
		} else {
			// Client sent a guess.
			calculate_results(cur, shared->tried_char);
		}

		shared->clientno = cur->clientno;
		shared->status = cur->current_game.status;
		shared->errors = cur->current_game.errors;
		strncpy(shared->word, cur->current_game.obscured, MAX_WORD_LENGTH);

		if (sem_post(ret_sem) == -1) {
			bail_out(EXIT_FAILURE, "sem_post");
		}

		// End of critical section.
	}

	free_resources();
	return EXIT_SUCCESS;
}
