#ifndef _SECVAULT_H
#define _SECVAULT_H

#include <linux/ioctl.h>

#define SECVAULT_MAJOR      231
#define SECVAULT_CTL_MINOR  0
#define SECVAULT_DATA_MINOR 1
#define SECVAULT_NUM_VAULTS 4

#define SECVAULT_KEY_SIZE 10

#define SECVAULT_CTL_DEVICE_NAME "/tmp/sv_ctl"

struct secvault_create_params {
	char id;
	char key[SECVAULT_KEY_SIZE+1];
	int size;
};

#define SECVAULT_IOCTL_CREATE _IOR(SECVAULT_MAJOR, 0, struct secvault_create_params *)
#define SECVAULT_IOCTL_DELETE _IOR(SECVAULT_MAJOR, 1, int)
#define SECVAULT_IOCTL_ERASE  _IOR(SECVAULT_MAJOR, 2, int)

int secvault_ctl_setup(void);
void secvault_ctl_cleanup(void);

/* setup char device with node numbers and initialize vaults */
int secvault_data_setup(void);

void secvault_data_cleanup(void);
int secvault_create(int id, char *key, int size);
int secvault_delete(int id);
int secvault_erase(int id);

#define DEBUG(...) printk (KERN_DEBUG __VA_ARGS__)

#endif /* __SECVAULT_H */
