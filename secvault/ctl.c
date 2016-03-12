#include <asm/uaccess.h> // For get_user and put_user
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h> // For printk
#include <linux/module.h>
#include <linux/types.h>

#include "secvault.h"

static dev_t ctl_devno;

static struct cdev ctl_cdev;

// Indicates whether cdev_add was called successfully.
static bool added = false;

// Implementation ioctl system call for secvault control device.
long secvault_ctl_ioctl(
	struct file *file,
	unsigned int ioctl_num,
	unsigned long ioctl_param
)
{
	struct secvault_create_params cp;
	int ret = 0;
	switch (ioctl_num) {
	case SECVAULT_IOCTL_CREATE:
		copy_from_user(&cp, (struct secvault_create_params *) ioctl_param, sizeof(cp));
		ret = secvault_create(cp.id, &(cp.key[0]), cp.size);
		break;
	case SECVAULT_IOCTL_DELETE:
		ret = secvault_delete((int) ioctl_param);
		break;
	case SECVAULT_IOCTL_ERASE:
		ret = secvault_erase((int) ioctl_param);
		break;
	default: printk(KERN_WARNING "Invalid ioctl num %d", ioctl_num);
		 ret = -1;
	}

	return ret;
}

struct file_operations ctl_fops = {
	.owner          = THIS_MODULE,
	.unlocked_ioctl = secvault_ctl_ioctl,
};

int secvault_ctl_setup(void)
{
	int retval;
	ctl_devno = MKDEV(SECVAULT_MAJOR, SECVAULT_CTL_MINOR);
	retval = register_chrdev_region(ctl_devno, 1, "sv_ctl");

	if (retval != 0) {
		secvault_ctl_cleanup();
		return retval;
	}

	cdev_init(&ctl_cdev, &ctl_fops);
	ctl_cdev.owner = THIS_MODULE;
	ctl_cdev.ops = &ctl_fops;
	retval = cdev_add(&ctl_cdev, ctl_devno, 1);

	if (retval < 0) {
		secvault_ctl_cleanup();
		return retval;
	}

	added = true;
	return retval;
}

void secvault_ctl_cleanup(void)
{
	// Do not call cdev_del when cdev_add wasn't successfully called yet.
	if (added) {
		cdev_del(&ctl_cdev);
	}
	unregister_chrdev_region(ctl_devno, 1);
}
