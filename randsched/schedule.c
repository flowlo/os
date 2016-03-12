/**
 * @file schedule.c
 * @author Lorenz Leutgeb <e1127842@student.tuwien.ac.at>
 * @date 2015-11-13
 *
 * @brief A simple task scheduler and logger.
 *
 * This program will execute two files passed as command line arguments. One of
 * them being treated to obtain state information, the other to to be triggered
 * by failure of the first one.
 * Output is logged to standard output and a logfile (also passed via command
 * line argument).
 */

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

/**
 * Holds the value passed via initial delay flag.
 */
static unsigned int opt_s = 1;

/**
 * Holds the value passed via time frame flag.
 */
static unsigned int opt_f = 0;

/**
 * Filename of the program to execute. Populated using the first positional
 * argument.
 */
static char *opt_program;

/**
 * Filename of the program to be executed in case of emergency. Populed from
 * the second positional argument.
 */
static char *opt_emergency;

/**
 * Filename of the logfile that is used for writing log messages. Corresponds to
 * the third positional argument. If it the file does not exist, an attempt will
 * be made to create it.
 */
static char *opt_logfile;

static char *progname = "schedule"; /**< Name of this program. */

/**
 * Flag used to indicate teardown via signal.
 */
volatile sig_atomic_t quit = false;

/**
 * Executes programs and waits for them to exit.
 * @brief Similar to popen, this function will fork a new process and execute
 * the program identified by the passed filename. Also, it redirects stdout of
 * the command.
 * @details Reads globals opt_s, opt_f, opt_program and opt_emergency.
 * @param program Filename of the program to be executed.
 * @param pfd File descriptor to point standard output at.
 * @return Returns a negative number if an error occured during execution so an
 *         exit could not have been determined. Otherwise the exit code of the
 *         program is returned, which may be zero or a positive number.
 */
static int exec_prog(const char *program, int pfd);

/**
 * Helper function for printing error messages.
 * @brief Prints the program name, a formatted string an errno (if set) to
 * standard error. Call it like the printf family.
 * @param fmt Format string to print
 * @details Reads global variable progname.
 * @return Returns always returns EXIT_FAILURE to be propagated up the stack and
 *         returned from main.
 */
static int bail(const char *fmt, ...);

/**
 * Mandatory usage function.
 * @brief Displays information on how to invoke this program from the command
 * line. Lists all possible arguments.
 * @details Reads global variable progname.
 */
static void usage(void);

/**
 * Signal handler called upon receipt of SIGINT or SIGTERM.
 * @detail Writes global variable quit.
 * @param sig The signal to handle.
 */
static void handle_signal(int sig);

/**
 * Set up signal handling by registering the handle_signal function for SIGINT
 * and SIGTERM.
 * @detail Reads global variable/function pointer handle_signal.
 */
static void setup_signals(void);

/**
 * Utility function to parse arguments and initialize globals.
 * @detail Using getopt, this function iterates through the passed arguments
 * In case unknown options are provided or this function is unable to interpret
 * certain parameters, it will print the usage.
 */
static void parse_args(int argc, char **argv);

/**
 * Program entry point.
 * @brief Root of IPC. Does print logs and write them to log file. A forked
 * child process takes care of running the commands passed via command line
 * arguments.
 * @param argc The argument counter.
 * @param argv The argument vector.
 * @return Returns EXIT_SUCCESS in case everything goes right, EXIT_FAILURE
 *         otherwise.
 */
int main(int argc, char **argv) {
	setup_signals();
	parse_args(argc, argv);

	// Print pipe ends. This will be forwarded
	// to parent's stadandard output and the
	// logfile.
	int ppfd[2];

	// Logging pipe ends. This will be forwarded
	// to standard output only.
	int lpfd[2];

	if (pipe(ppfd) < 0 || pipe(lpfd) < 0) {
		return bail("pipe");
	}

	pid_t child = fork();

	if (child < 0) {
		return bail("fork");
	}

	if (child == 0) {
		// Close read ends.
		(void) close(ppfd[0]);
		(void) close(lpfd[0]);

		int prog;

		do {
			// Sleep for [s;s + f] seconds.
			if (sleep(opt_s + (!opt_f ? rand() % opt_f : 0)) != 0) {
				break;
			}

			if ((prog = exec_prog(opt_program, ppfd[1])) < 0) {
				(void) close(ppfd[1]);
				(void) close(lpfd[1]);
				return bail("exec_prog");
			}

		} while (prog == 0 && !quit);

		if (prog != 0) {
			prog = exec_prog(opt_emergency, lpfd[1]);
		}

		char *msg;
		size_t msglen = 0;

		if (prog == EXIT_SUCCESS) {
			msg = "EMERGENCY SUCCESSFUL\n";
			msglen = strlen(msg);
		} else if (prog == EXIT_FAILURE){
			msg = "EMERGENCY UNSUCCESSFUL\n";
			msglen = strlen(msg);
		}

		// Lazy. write may not have written the whole buffer
		// in one go, but this is not considered here.
		if (write(ppfd[1], msg, msglen) != msglen) {
			prog = EXIT_FAILURE;
		}

		// Now close this pipe to make the parent consume
		// the logging pipe.
		(void) close(ppfd[1]);
		(void) close(lpfd[1]);

		return prog == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	(void) close(ppfd[1]);
	(void) close(lpfd[1]);

	int logfd = open(opt_logfile,
		O_WRONLY|O_APPEND|O_CREAT,
		S_IRUSR|S_IWUSR|S_IRGRP|S_IWGRP
	);

	if (logfd < 0) {
		return bail("open");
	}

	int c;
	char buf;
	while (!quit) {
		c = read(ppfd[0], &buf, 1);

		if (c < 0) {
			(void) close(ppfd[0]);
			(void) close(lpfd[0]);
			(void) close(logfd);

			// If we were interrupted but
			// handled the signal, just carry on.
			// We should run into a closed pipe.
			if (errno == EINTR && quit) {
				continue;
			}
			return bail("read (ppfd)");
		}

		if (c == 0) {
			break;
		}

		if (write(logfd, &buf, 1) != 1
		||  write(STDOUT_FILENO, &buf, 1) != 1) {
			(void) close(ppfd[0]);
			(void) close(lpfd[0]);
			(void) close(logfd);
			return bail("write (logfd, stdout)");
		}
	}

	(void) close(ppfd[0]);

	while (!quit) {
		c = read(lpfd[0], &buf, 1);

		if (c < 0) {
			(void) close(lpfd[0]);
			(void) close(logfd);
			return bail("read (lpfd)");
		}

		if (c == 0) {
			break;
		}

		if (write(STDOUT_FILENO, &buf, 1) != 1) {
			(void) close(lpfd[0]);
			(void) close(logfd);
			return bail("write (stdout)");
		}
	}

	(void) close(lpfd[0]);

	int status;
	(void) wait(&status);

	(void) close(logfd);
	return EXIT_SUCCESS;
}

static int exec_prog(const char *program, int pfd) {
	pid_t child = fork();

	if (child < 0) {
		return bail("exec_prog: fork");
	}

	if (child == 0) {
		sigset_t blocked_signals;
		(void) sigfillset(&blocked_signals);
		(void) sigprocmask(SIG_BLOCK, &blocked_signals, NULL);

		// Reroute standard out to passed
		// pipe file descriptor.
		// Note that this must be done
		// after forking and in the child
		// only.
		if (dup2(pfd, STDOUT_FILENO) < 0) {
			return bail("exec_prog: dup2");
		}
		if (execlp(program, program, NULL) < 0) {
			return bail("exec_prog: execlp");
		}
	}

	int status;
	(void) wait(&status);

	// If the child exited in a normal fashion,
	// go ahead and return the exit status (which
	// may indicate an error during execution as
	// such).
	if (WIFEXITED(status)) {
		return WEXITSTATUS(status);
	}

	return bail("exec_prog: exited dirty");
}

static int bail(const char *fmt, ...) {
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

	return EXIT_FAILURE;
}

static void handle_signal(int sig) {
	quit = true;
}

static void setup_signals(void) {
	const int signals[] = { SIGINT, SIGTERM };
	struct sigaction s;

	s.sa_handler = handle_signal;
	s.sa_flags   = 0;
	if (sigfillset(&s.sa_mask) < 0) {
		(void) bail("sigfillset");
		return;
	}
	for (int i = 0; i < 2; i++) {
		if (sigaction(signals[i], &s, NULL) < 0) {
			(void) bail("sigaction");
		}
	}
}

static void parse_args(int argc, char **argv) {
	if (argc > 0) {
		progname = argv[0];
	}

	int c;
	while ((c = getopt(argc, argv, "s:f:")) != -1) {
		switch (c) {
			case 's':
				errno = 0;
				opt_s = (int)strtoul(optarg, NULL, 10);
				if (errno == ERANGE) {
					usage();
					return EXIT_FAILURE;
				}
				break;
			case 'f':
				errno = 0;
				opt_f = (int)strtoul(optarg, NULL, 10);
				if (errno == ERANGE) {
					usage();
					return EXIT_FAILURE;
				}
				break;
			case '?':
				usage();
				return EXIT_FAILURE;
		}
	}


	if (argv[optind] == NULL
	|| argv[optind + 1] == NULL
	|| argv[optind + 2] == NULL) {
		usage();
		return EXIT_FAILURE;
	}

	opt_program   = argv[optind];
	opt_emergency = argv[optind + 1];
	opt_logfile   = argv[optind + 2];
}

static void usage(void) {
	(void) fprintf(stderr, "SYNOPSIS:\n");
	(void) fprintf(stderr, "\t%s [-s <seconds>] [-f <seconds>] <program> <emergency> <logfile>\n\n", progname);
	(void) fprintf(stderr, "\t-s <seconds>   Zeitfenster Anfang (Default: 1 Sekunde)\n");
	(void) fprintf(stderr, "\t-f <seconds>   max. Zeitfenster Dauer (Default: 0 Sekunden)\n");
	(void) fprintf(stderr, "\t<program>      Programm inkl. Pfad, das wiederholt ausgefuehrt werden soll\n");
	(void) fprintf(stderr, "\t<emergency>    Programm inkl. Pfad, das im Fehlerfall ausgefuehrt wird\n");
	(void) fprintf(stderr, "\t<logfile>      Pfad zu einer Datei, in der die Ausgabe von <program> sowie\n");
	(void) fprintf(stderr, "\t               Erfolg/Misserfolg von <emergency> protokolliert werden\n");
}
