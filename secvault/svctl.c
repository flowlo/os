#include <assert.h>
#include <errno.h>
#include <fcntl.h> // For open
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h> // For ioctl
#include <sys/types.h>
#include <unistd.h> // For exit

#include "secvault.h"

#define USAGE "%s [-c <size>|-e|-d] <secvault id>"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#define SV_CMD_INVALID 0
#define SV_CMD_CREATE  1
#define SV_CMD_DELETE  2
#define SV_CMD_INIT    3
#define SV_MAX_SIZE    1 << 20

#define SV_MAX_SIZE_ERROR "size must be between 1 and " STR(SV_MAX_SIZE)

static char* program_name = "svctl";

/**
 * @brief terminate program on program error
 * @param eval exit code
 * @param error_msg error message
 */
static void bail_out(int eval, const char* error_msg);

/**
 * @brief prints usage information to stderr
 * @details uses global variable command
 */
static void usage(void);

static void bail_out(int eval, const char* error_msg)
{
	(void) fprintf(stderr, "%s: %s", program_name, error_msg);
	if (errno != 0) {
		(void) fprintf(stderr, ": %s", strerror(errno));
	}
	(void) fprintf(stderr, "\n");
	exit(eval);
}

static void usage(void)
{
	(void) fprintf(stderr, "%s: ", program_name);
	(void) fprintf(stderr, USAGE, program_name);
	(void) fprintf(stderr, "\n");
}

void ioctl_create_secvault(int fd, int id, char* key, int size)
{
	struct secvault_create_params cp = {
		.id   = id,
		.size = size,
	};

	memset(&cp.key[0], 0, SECVAULT_KEY_SIZE);
	strncpy(&(cp.key[0]), key, SECVAULT_KEY_SIZE);

	if (ioctl(fd, SECVAULT_IOCTL_CREATE, &cp) < 0) {
		bail_out(EXIT_FAILURE, "ioctl_create_secvault");
	}
}
void ioctl_delete_secvault(int fd, int id)
{
	if (ioctl(fd, SECVAULT_IOCTL_DELETE, id) < 0) {
		bail_out(EXIT_FAILURE, "ioctl_delete_secvault");
	}
}

void ioctl_erase_secvault(int fd, int id)
{
	if (ioctl(fd, SECVAULT_IOCTL_ERASE, id) < 0) {
		bail_out(EXIT_FAILURE, "ioctl_erase_secvault");
	}
}

static void str2int(char *str, int *val)
{
	char *endptr;
	errno = 0;
	*val = strtol(str, &endptr, 10);

	if (
		(errno == ERANGE && (*val == LONG_MAX || *val == LONG_MIN))
		|| (errno != 0 && *val == 0)
	) {
		bail_out(EXIT_FAILURE, "strtol");
	}

	if (endptr == str) {
		bail_out(EXIT_FAILURE, "No digits were found");
	}
}

int main(int argc, char** argv)
{
	int fd, ret_val, opt, size, id;
	char cmd = SV_CMD_INVALID;
	char key[SECVAULT_KEY_SIZE+1];

	while ((opt = getopt(argc, argv, "c:ed")) != -1) {
		switch (opt) {
		case 'c':
			if (cmd != SV_CMD_INVALID) {
				usage();
				bail_out(EXIT_FAILURE, "another alternative \
					option to -c was provided already");
			}
			cmd = SV_CMD_CREATE;
			str2int(optarg, &size);
			if (size < 1 || size > SV_MAX_SIZE) {
				usage();
				bail_out(EXIT_FAILURE, SV_MAX_SIZE_ERROR);
			}
			break;
		case 'e':
			if (cmd != SV_CMD_INVALID) {
				usage();
				bail_out(EXIT_FAILURE, "another alternative \
					option to -e was provided already");
			}
			cmd = SV_CMD_INIT;
			break;
		case 'd':
			if (cmd != SV_CMD_INVALID) {
				usage();
				bail_out(EXIT_FAILURE, "another alternative \
					option to -d was provided already");
			}
			cmd = SV_CMD_DELETE;
			break;
		case '?':
			usage();
			bail_out(EXIT_FAILURE, "invalid option");
		default: assert(0);
		}
	}

	if (optind < argc) {
		str2int(argv[optind], &id);
	} else {
		usage();
		bail_out(EXIT_FAILURE, "no id provided");
	}

	if (cmd == SV_CMD_INVALID) {
		usage();
		bail_out(EXIT_FAILURE, "invalid command provided");
	}

	fd = open(SECVAULT_CTL_DEVICE_NAME, 0);
	if (fd < 0) {
		bail_out(EXIT_FAILURE, "can't open sv_ctl device");
	}

	switch (cmd) {
	case SV_CMD_CREATE:
		fgets(&key[0], SECVAULT_KEY_SIZE, stdin);
		ioctl_create_secvault(fd, id, &key[0], size);
		break;
	case SV_CMD_DELETE:
		ioctl_delete_secvault(fd, id);
		break;
	case SV_CMD_INIT:
		ioctl_erase_secvault(fd, id);
		break;
	default: bail_out(EXIT_FAILURE, "invalid command");
	}
	close(fd);
	return EXIT_SUCCESS;
}
