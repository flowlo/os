#include <asm/errno.h>
#include <asm/uaccess.h> /* for get_user and put_user */
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/module.h>
#include <linux/sched.h> /* current */
#include <linux/slab.h>
#include <linux/unistd.h> // For SEEK_*

#include "secvault.h"

static dev_t first_data_vault;

struct secvault_dev {
	char *data;
	char key[SECVAULT_KEY_SIZE+1];
	int size;
	int max_size;
	struct semaphore sem;
	struct cdev cdev;
	kuid_t owner;
};

#define owns(D) (uid_eq(((struct secvault_dev *)D)->owner, current_uid()))

static struct secvault_dev vaults[SECVAULT_NUM_VAULTS];

/* encrypts data */
static void secvault_crypt(
	int pos,
	char *data,
	char *dest,
	char *key,
	int count
);

/* removes all data and sets size to 0
 * the caller must ensure mutual exclusion and that the process
 * has the required permissions */
static int secvault_data_trim(struct secvault_dev *dev);

static int secvault_data_trim(struct secvault_dev *dev)
{
	if (memset(dev->data, 0, dev->max_size) == NULL) {
		return -1;
	} else {
		dev->size = 0;
		return 0;
	}
}

static int secvault_data_open(struct inode *inode, struct file *filp)
{
	int retval = 0;
	struct secvault_dev *dev;
	dev = container_of(inode->i_cdev, struct secvault_dev, cdev);
	filp->private_data = dev;

	if (!owns(dev)) {
		return -EACCES;
	}

	// Now trim to 0 the length of the device if open was write-only.
	if (
		(filp->f_flags & O_ACCMODE) == O_WRONLY &&
		(filp->f_flags & O_APPEND)  == 0
	) {
		if (down_interruptible(&dev->sem)) {
			retval = -ERESTARTSYS;
		} else {
			retval = secvault_data_trim(dev);
		}
		up(&dev->sem);
	}
	return retval;
}

static int secvault_data_release(struct inode *inode, struct file *filep)
{
	if (!owns(filep->private_data)) {
		return -EACCES;
	}
	return 0;
}

ssize_t secvault_data_read(
	struct file *filp,
	char __user *buff,
	size_t count,
	loff_t *f_pos
)
{
	struct secvault_dev *dev = filp->private_data;
	ssize_t retval = 0;
	long curr_pos = (long) *f_pos;
	char *decrypted = NULL;

	if (down_interruptible(&dev->sem)) {
		retval = -ERESTARTSYS;
		goto up;
	}

	if (!owns(dev)) {
		retval = -EACCES;
		goto up;
	}

	// Requested read is out of bounds.
	if (curr_pos > dev->size || curr_pos < 0) {
		retval = -EINVAL;
		goto up;
	}

	if (curr_pos + count > dev->size) {
		count -= (curr_pos + count - dev->size);
	}

	// Allocate buffer for decrypted data.
	if ((decrypted = kmalloc(count * sizeof(char), GFP_KERNEL)) == NULL) {
		retval = -ENOMEM;
		goto up;
	}

	// Decrypt requested data.
	secvault_crypt(curr_pos, &(dev->data[curr_pos]), decrypted,
			&(dev->key[0]), count);

	// Copy decrypted data to userspace.
	if (copy_to_user(buff, decrypted, count)) {
		retval = -EFAULT;
		goto up;
	}

	*f_pos += count;
	retval = count;

up:
	kfree(decrypted);
	up(&dev->sem);
	return retval;
}

loff_t secvault_data_llseek(struct file *filp, loff_t off, int whence)
{
	struct secvault_dev *dev = filp->private_data;
	loff_t newpos;

	if (!owns(dev)) {
		return -EACCES;
	}

	switch(whence) {
	case SEEK_SET:
		newpos = off;
		break;
	case SEEK_CUR:
		newpos = filp->f_pos + off;
		break;
	case SEEK_END:
		newpos = dev->size + off;
		break;
	default:
		return -EINVAL;
	}

	if (newpos < 0) return -EINVAL;

	filp->f_pos = newpos;
	return newpos;
}

ssize_t secvault_data_write(
	struct file *filp,
	const char __user *buff,
	size_t count,
	loff_t *f_pos
)
{
	struct secvault_dev *dev = filp->private_data;
	ssize_t retval = 0;
	long curr_pos = (long) *f_pos;
	ssize_t add_count = 0;

	if (down_interruptible(&dev->sem)) {
		retval = -ERESTARTSYS;
		goto up;
	}

	if (!owns(dev)) {
		retval = -EACCES;
		goto up;
	}

	// Seek to end of file before write if secvault was opened write only.
	if ((filp->f_flags & O_APPEND) != 0) {
		curr_pos = secvault_data_llseek(filp, 0, 2);
	}

	if (curr_pos < 0 || curr_pos > dev->size) {
		retval = -EINVAL;
		goto up;
	}

	if (curr_pos + count >= dev->size) {
		add_count =  (curr_pos + count - dev->size);
	}

	// Return EGBIG, if process tries to write more data
	// then fits into the secvault, nothing is written.
	if (dev->size + add_count > dev->max_size) {
		retval = -EFBIG;
		goto up;
	}

	if (copy_from_user(&dev->data[curr_pos], buff, count)) {
		retval = -EFAULT;
		goto up;
	}

	secvault_crypt(
		curr_pos,
		&dev->data[curr_pos],
		&(dev->data[curr_pos]),
		&(dev->key[0]),
		count
	);

	*f_pos += count;
	retval = count;
	dev->size += add_count;

up:
	up(&dev->sem);
	return retval;
}

struct file_operations data_fops = {
	.owner   = THIS_MODULE,
	.open    = secvault_data_open,
	.release = secvault_data_release,
	.write   = secvault_data_write,
	.read    = secvault_data_read,
	.llseek  = secvault_data_llseek,
};

/*
 * create new secvault device, allocate memory, set size and owner
 * id must be: 0 <= id < SECVAULT_NUM_VAULTS
 */
int secvault_create(int id, char *key, int size)
{
	int retval = 0, i;
	struct secvault_dev *vault = &(vaults[id]);

	if (down_interruptible(&vault->sem)) {
		retval = -ERESTARTSYS;
		goto up;
	}

	if (vault->data != NULL) {
		retval = -EEXIST;
		goto up;
	}

	vault->data = kmalloc((size) * sizeof(char), GFP_KERNEL);
	if (vault->data == NULL) {
		retval = -ENOMEM;
		goto up;
	}

	vault->size = 0;
	vault->max_size = size;

	/* set owner user id */
	vault->owner = current_uid();

	for (i = 0; i < SECVAULT_KEY_SIZE; i++){
		vault->key[i] = key[i];
	}
	vault->key[i] = 0;

	cdev_init(&vault->cdev, &data_fops);
	vault->cdev.owner = THIS_MODULE;
	vault->cdev.ops = &data_fops;
	retval = cdev_add(&(vault->cdev), first_data_vault+id, 1);
up:
	up(&vault->sem);
	return retval;
}

int secvault_erase(int id)
{
	int retval;

	if (vaults[id].data == NULL) {
		return  -ENXIO;
	}

	if (down_interruptible(&vaults[id].sem)) {
		retval = -ERESTARTSYS;
		goto up;
	}

	if (!owns(&vaults[id])) {
		retval = -EACCES;
		goto up;
	}

	retval = secvault_data_trim(&vaults[id]);

up:
	up(&vaults[id].sem);
	return retval;
}

int secvault_delete(int id)
{
	int retval;

	// Placeholder for uninitialized owner fields. After
	// deletion, nobody owns the vault.
	kuid_t nobody = { .val = -1 };

	if (vaults[id].data == NULL) {
		return -ENXIO;
	}

	if (down_interruptible(&vaults[id].sem)) {
		retval = -ERESTARTSYS;
		goto up;
	}

	if (!owns(&vaults[id])) {
		retval = -EACCES;
		goto up;
	}

	cdev_del(&(vaults[id].cdev));
	kfree(vaults[id].data);
	vaults[id].data = NULL;
	vaults[id].owner = nobody;
	retval = 0;

up:
	up(&vaults[id].sem);
	return retval;
}

static void secvault_crypt(
	int pos,
	char *data,
	char *dest,
	char *key,
	int count
)
{
	int i = 0;
	for (i = 0; i < count; i++) {
		dest[i] = data[i] ^ key[(pos + i) % SECVAULT_KEY_SIZE];
	}
}


int secvault_data_setup(void)
{
	int retval, i;

	first_data_vault = MKDEV(SECVAULT_MAJOR, SECVAULT_DATA_MINOR);
	retval = register_chrdev_region(
		first_data_vault,
		SECVAULT_NUM_VAULTS,
		"sv_data"
	);

	if (retval < 0) {
		secvault_data_cleanup();
	} else {
		for (i = 0; i < SECVAULT_NUM_VAULTS; i++){
			vaults[i].data = NULL;
			// Init mutex for each vault per process.
			sema_init(&(vaults[i].sem), 1);
		}
	}

	return retval;
}

void secvault_data_cleanup(void)
{
	int i;

	// Clean up all the vaults.
	for (i = 0; i < SECVAULT_NUM_VAULTS; i++) {
		(void) secvault_delete(i);
	}

	// Unregister devices.
	unregister_chrdev_region(first_data_vault, SECVAULT_NUM_VAULTS);
}
