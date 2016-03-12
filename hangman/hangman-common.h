/**
 * @file hangman-common.h
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2016-01-10
 *
 * @brief This module defines common constants and macros for both hangman-server and hangman-client.
 **/
#ifndef HANGMAN_COMMON_H
#define HANGMAN_COMMON_H

#include <stdbool.h>
#include <stdint.h>

#define MAX_ERROR (8) /**< The number of errors tolerated. */
#define MAX_WORD_LENGTH (80) /**< Maximum word-length to be expected. */

#define PERMISSION (0600) /**< UNIX file permission for semaphores and shm */
#define SHM_NAME ("/hangman-shm") /**< name of the shared memory */
#define SRV_SEM  ("/hangman-srv") /**< name of the server semaphore */
#define CLT_SEM  ("/hangman-clt") /**< name of the client semaphore */
#define RET_SEM  ("/hangman-ret") /**< name of the return semaphore */

/**
 * @brief Enumeration describing the various states a game can be in. Usually,
 * the server side will set the state, with "New" being the only exception, as
 * a client is allowed to set it.
 */
enum game_state {
	game_new,        /**< Indicates a new game is requested (set by client). */
	game_open,       /**< Set once w word is chosen and is ready to play. */
	game_impossible, /**< Indicates that there are no more words available. */
	game_lost,       /**< Set if the number of errors is not tolerated anymore. */
	game_won,        /**< Indiciates that the word was guessed correctly. */
};

/**
 * @brief Structure used for client-server communication. Will lie in shared
 * memory and allow client and server to exchange all relevant information.
 */
struct shm {
	unsigned int errors; /**< number of wrong guesses the client made */
	int clientno; /**< number to identfy the client */
	enum game_state status; /**< the state of the game */
	char tried_char; /**< character guessed by the client */
	char word[MAX_WORD_LENGTH]; /**< partly obsucred word */
	bool terminate; /**< to communicate termination */
};

/* Length of an array */
#define COUNT_OF(x) (sizeof(x)/sizeof(x[0]))

#endif // HANGMAN_COMMON_H
