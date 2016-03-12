/*!
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @file   client.c
 * @date   2015-10-11
 * @brief  A program that plays mastermind.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <arpa/inet.h>
#include <stdbool.h>

/* === Constants === */

#define MAX_TRIES (35)
#define SLOTS (5)
#define COLORS (8)

#define READ_BYTES (1)
#define WRITE_BYTES (2)
#define BUFFER_BYTES (2)
#define SHIFT_WIDTH (3)
#define PARITY_ERR_BIT (6)
#define GAME_LOST_ERR_BIT (7)

#define EXIT_PARITY_ERROR (2)
#define EXIT_GAME_LOST (3)
#define EXIT_MULTIPLE_ERRORS (4)

// CLIENT LOGIC
#define MAX_POSSIBILITIES (0x8000)
#define COLOR_BITMASK (0x7)
#define COLOR(v, i) ((v & COLOR_BITMASK << (i * SHIFT_WIDTH)) >> (i * SHIFT_WIDTH))

static bool possibilities[MAX_POSSIBILITIES];

/* === Macros === */

#ifdef ENDEBUG
#define DEBUG(...) do { fprintf(stderr, __VA_ARGS__); } while(0)
#else
#define DEBUG(...)
#endif

/* Length of an array */
#define COUNT_OF(x) (sizeof(x)/sizeof(x[0]))

/* === Global Variables === */

/* Name of the program */
static const char *progname = "client"; /* default name */

/* File descriptor for server socket */
static int sockfd = -1;

/* This variable is set to ensure cleanup is performed only once */
volatile sig_atomic_t terminating = 0;

/* === Prototypes === */

/**
 * @brief Parse command line options
 * @param argc The argument counter
 * @param argv The argument vector
 * @param options Struct where parsed arguments are stored
 */
static void parse_args(int argc, char **argv, struct sockaddr_in *connection_data);

/**
 * @brief Read message from socket
 *
 * This code *illustrates* one way to deal with partial reads
 *
 * @param sockfd_con Socket to read from
 * @param buffer Buffer where read data is stored
 * @param n Size to read
 * @return Pointer to buffer on success, else NULL
 */
static uint8_t *read_from_server(int sockfd_con, uint8_t *buffer, size_t n);

/**
 * @brief Compute answer to request
 * @param req Client's guess
 * @param resp Buffer that will be sent to the client
 * @param secret The server's secret
 * @return Number of correct matches on success; -1 in case of a parity error
 */
static int compute_answer(uint16_t req, uint8_t *resp, uint8_t *secret);

/**
 * @brief terminate program on program error
 * @param exitcode exit code
 * @param fmt format string
 */
static void bail_out(int exitcode, const char *fmt, ...);

/**
 * @brief free allocated resources
 */
static void free_resources(void);

/* === Implementations === */

static uint8_t *read_from_server(int fd, uint8_t *buffer, size_t n)
{
	/* loop, as packet can arrive in several partial reads */
	size_t bytes_recv = 0;
	do {
		ssize_t r;
		r = recv(fd, buffer + bytes_recv, n - bytes_recv, 0);
		if (r <= 0) {
			return NULL;
		}
		bytes_recv += r;
	} while (bytes_recv < n);

	if (bytes_recv < n) {
		return NULL;
	}
	return buffer;
}

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
	/* clean up resources */
	DEBUG("Shutting down client\n");
	if(sockfd >= 0) {
		(void) close(sockfd);
	}
}

static int compute_answer(uint16_t req, uint8_t *resp, uint8_t *secret)
{
	int colors_left[COLORS];
	int guess[COLORS];
	uint8_t parity_calc, parity_recv;
	int red, white;
	int j;

	parity_recv = (req >> 15) & 1;

	/* extract the guess and calculate parity */
	parity_calc = 0;
	for (j = 0; j < SLOTS; ++j) {
		int tmp = req & 0x7;
		parity_calc ^= tmp ^ (tmp >> 1) ^ (tmp >> 2);
		guess[j] = tmp;
		req >>= SHIFT_WIDTH;
	}
	parity_calc &= 0x1;

	/* marking red and white */
	(void) memset(&colors_left[0], 0, sizeof(colors_left));
	red = white = 0;
	for (j = 0; j < SLOTS; ++j) {
		/* mark red */
		if (guess[j] == secret[j]) {
			red++;
		} else {
			colors_left[secret[j]]++;
		}
	}
	for (j = 0; j < SLOTS; ++j) {
		/* not marked red */
		if (guess[j] != secret[j]) {
			if (colors_left[guess[j]] > 0) {
				white++;
				colors_left[guess[j]]--;
			}
		}
	}

	/* build response buffer */
	resp[0] = red;
	resp[0] |= (white << SHIFT_WIDTH);
	if (parity_recv != parity_calc) {
		resp[0] |= (1 << PARITY_ERR_BIT);
		return -1;
	} else {
		return red;
	}
}

static void signal_handler(int sig)
{
	/* signals need to be blocked by sigaction */
	DEBUG("Caught Signal\n");
	free_resources();
	exit(EXIT_SUCCESS);
}

/**
 * @brief Program entry point
 * @param argc The argument counter
 * @param argv The argument vector
 * @return EXIT_SUCCESS on success, EXIT_PARITY_ERROR in case of an parity
 * error, EXIT_GAME_LOST in case client needed to many guesses,
 * EXIT_MULTIPLE_ERRORS in case multiple errors occured in one round
 */
int main(int argc, char *argv[])
{
	struct sockaddr_in connection_data;
	int round;
	int ret;

	parse_args(argc, argv, &connection_data);

	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd == -1) {
		bail_out(EXIT_FAILURE, "socket");
	}

	if(connect(sockfd, (struct sockaddr *)&connection_data, sizeof(connection_data)) == -1)
		bail_out(EXIT_FAILURE, "connect");

	ret = EXIT_SUCCESS;
	for (round = 0; round <= MAX_TRIES; ++round) {
		uint16_t last_try;
		static uint8_t buffer[BUFFER_BYTES];
		int error = 0;

		last_try = rand() % MAX_POSSIBILITIES;
		while (possibilities[last_try]) {
			last_try++;
			if (last_try >= MAX_POSSIBILITIES) {
				last_try = 0;
			}
		}

		uint16_t tmp = last_try;
		tmp ^= tmp >> 8;
		tmp ^= tmp >> 4;
		last_try |= (((0x6996 >> (tmp & 0xf)) & 1) << 15);

		buffer[0] = last_try;
		buffer[1] = last_try >> 8;

		/* send message to client */
		if(send(sockfd, &buffer[0], WRITE_BYTES,0) == -1)
			bail_out(EXIT_FAILURE, "error on send to client");

		/* read from answer */
		if (read_from_server(sockfd, &buffer[0], READ_BYTES) == NULL) {
			bail_out(EXIT_FAILURE, "read_from_server");
		}

		/* We got the answer from the server; now stop the game
		   if its over, or an error occured */
		if (*buffer & (1<<PARITY_ERR_BIT)) {
			(void) fprintf(stderr, "Parity error\n");
			error = 1;
			ret = EXIT_PARITY_ERROR;
		}

		if (*buffer & (1 << GAME_LOST_ERR_BIT)) {
			(void) fprintf(stderr, "Game lost\n");
			error = 1;
			if (ret == EXIT_PARITY_ERROR) {
				ret = EXIT_MULTIPLE_ERRORS;
			} else {
				ret = EXIT_GAME_LOST;
			}
		}

		if (error) {
			break;
		} else if ((*buffer & 0x7) == SLOTS) {
			(void) printf("%d\n", round);
			break;
		}

		for (uint16_t i = 0; i < MAX_POSSIBILITIES; i++) {
			if (possibilities[i]) {
				continue;
			}

			uint8_t resp;
			uint8_t try[SLOTS];
			for (int j = 0; j < SLOTS; j++) {
				try[j] = COLOR(i, j);
			}

			(void) compute_answer(last_try, &resp, try);
			if (resp != buffer[0]) {
				possibilities[i] = true;
			}
		}
	}

	/* we are done */
	free_resources();
	return ret;
}

static void parse_args(int argc, char **argv, struct sockaddr_in *connection_data)
{
	char *port_arg;
	char *adress_arg;
	char *endptr;
	long int port;

	if(argc > 0) {
		progname = argv[0];
	}
	if (argc < 3) {
		bail_out(EXIT_FAILURE, "Usage: %s <server-adress> <server-port>", progname);
	}
	adress_arg = argv[1];
	port_arg = argv[2];

	errno = 0;
	port = strtol(port_arg, &endptr, 10);

	if ((errno == ERANGE && (port == LONG_MAX || port == LONG_MIN))
		|| (errno != 0 && port == 0)) {
		bail_out(EXIT_FAILURE, "strtol");
	}

	if (endptr == port_arg) {
		bail_out(EXIT_FAILURE, "No digits were found");
	}

	/* If we got here, strtol() successfully parsed a number */

	if (*endptr != '\0') { /* In principal not necessarily an error... */
		bail_out(EXIT_FAILURE, "Further characters after <server-port>: %s", endptr);
	}

	/* check for valid port range */
	if (port < 1 || port > 65535)
	{
		bail_out(EXIT_FAILURE, "Use a valid TCP/IP port range (1-65535)");
	}

	connection_data->sin_family = AF_INET;
	connection_data->sin_port = htons(port);

	struct hostent *hent;
	if((hent = gethostbyname(adress_arg)) == NULL)
	  bail_out(EXIT_FAILURE, "does not contain valid hostname");

	(void) memcpy(&(connection_data->sin_addr), hent->h_addr_list[0], hent->h_length);
}
