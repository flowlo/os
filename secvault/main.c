#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/types.h>

#include "secvault.h"

static int __init secvault_init(void);

static void __exit secvault_exit(void);

static int debug = 0;

module_param(debug, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
MODULE_PARM_DESC(debug, "debug flag");

// Set up data and control device.
static int __init secvault_init(void)
{
	return secvault_data_setup() + secvault_ctl_setup();
}

static void __exit secvault_exit(void)
{
	secvault_ctl_cleanup();
	secvault_data_cleanup();
}

module_init(secvault_init);
module_exit(secvault_exit);

MODULE_AUTHOR("Lorenz Leutgeb");
MODULE_LICENSE("GPL");
