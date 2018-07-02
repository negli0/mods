#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <asm/current.h>
#include <asm/uaccess.h>

MODULE_LICENSE("Dual BSD/GPL");

#define DRIVER_NAME "caesar"
#define KEY 5

static int caesar_devs = 1; /* device count */
static int caesar_major = 0; /* dynamic allocation */
module_param(caesar_major, uint, 0); /* args when running insmod caesar_major=<args> */
static struct cdev caesar_cdev;

struct caesar_data {
	rwlock_t lock;
	char *data;
	int key;
};

ssize_t caesar_write(struct file *filp, const char __user *buf, size_t count,
		loff_t *f_ops)
{
	struct caesar_data *p = filp->private_data;
	int retval = 0;
	int i;

	printk(KERN_ALERT "%s: count %ld pos %lld\n", __func__, count, *f_ops);

	if (count > 0) {
		write_lock(&p->lock);

		p->data = (char *)kmalloc((sizeof(char) * count), GFP_KERNEL);
		if (!p) {
			retval = -ENOMEM;
			printk(KERN_ALERT "%s:%d failed to kmalloc\n", __func__, __LINE__);
			write_unlock(&p->lock);
			goto error;
		}
		if (copy_from_user(p->data, buf, count)) {
			printk(KERN_ALERT "%s:%d failed to copy_from_user\n", __func__, __LINE__);
			retval = -EFAULT;
			write_unlock(&p->lock);
			goto error;
		}
		retval = count;
		for (i = 0; i < count; i++) {
			if ('A' <= p->data[i] && p->data[i] <= 'Z') {
				p->data[i] = 'A' + (p->data[i] + KEY) % 26;
			} else if ('a' <= p->data[i] && p->data[i] <= 'z') {
				p->data[i] = 'a' + (p->data[i] + KEY) % 26;
			}
		}
		write_unlock(&p->lock);
	}

error:
	return retval;
}

ssize_t caesar_read(struct file *filp, char __user *buf, size_t count, 
		loff_t *f_ops)
{
	ssize_t retval = 0;
	struct caesar_data *p = filp->private_data;

	printk("%s: count %ld pos %lld\n", __func__, count, *f_ops);

	read_lock(&p->lock);
	if (copy_to_user(buf, p->data, count)) {
		printk(KERN_ALERT "%s:%d failed to copy_to_user\n", __func__, __LINE__);
		retval = -EFAULT;
		goto error;
	}
	retval = count;

error:
	return retval;
}

static int caesar_open(struct inode *inode, struct file *file)
{
	struct caesar_data *p;

	printk(KERN_ALERT "%s: major %d minor %d (pid %d)\n", __func__,
			imajor(inode),
			iminor(inode),
			current->pid
		  );

	p = (struct caesar_data *)kmalloc(sizeof(struct caesar_data), GFP_KERNEL);
	if (p == NULL) {
		printk("%s:%d Not memory.\n", __func__, __LINE__);
		return -ENOMEM;
	}

	p->key = KEY;
	rwlock_init(&p->lock);
	
	file->private_data = p;

	return 0;
}

static int caesar_close(struct inode *inode, struct file *file)
{
	printk(KERN_ALERT "%s: major %d minor %d (pid %d)\n", __func__,
			imajor(inode),
			iminor(inode),
			current->pid
		  );

	if (file->private_data) {
		struct caesar_data *p = file->private_data;
		kfree(p->data);
		kfree(file->private_data);
		file->private_data = NULL;
	}
	return 0;
}

struct file_operations caesar_fops = {
	.open = caesar_open,
	.release = caesar_close,
	.read = caesar_read,
	.write = caesar_write,
};

static int caesar_init(void)
{
	/* create device number */
#if 1
	dev_t dev /*= MKDEV(caesar_major, 0)*/;
#else
	dev_t dev = MKDEV(caesar_major, 0);
#endif
	int alloc_ret = 0;
	int major;
	int cdev_err = 0;

	/* dynamically allocate device number */
	alloc_ret = alloc_chrdev_region(&dev, 0, caesar_devs, DRIVER_NAME);
	if (alloc_ret) {
		goto error;
	}
	/* MAJOR(dev) returns major device number got by alloc_chrdev_region */
	caesar_major = major = MAJOR(dev);

	/* init character-type device with file operations */
	cdev_init(&caesar_cdev, &caesar_fops);
	caesar_cdev.owner = THIS_MODULE;

	cdev_err = cdev_add(&caesar_cdev, MKDEV(caesar_major, 0), caesar_devs);
	if (cdev_err) {
		goto error;
	}

	printk(KERN_ALERT "%s driver(major %d) installed.\n", DRIVER_NAME, major);

	return 0;

error:
	if (cdev_err == 0) {
		cdev_del(&caesar_cdev);
	}
	if (alloc_ret == 0) {
		unregister_chrdev_region(dev, caesar_devs);
	}
	return -1;
}

static void caesar_exit(void)
{
	/* get device number */
	dev_t dev = MKDEV(caesar_major, 0);

	cdev_del(&caesar_cdev);
	unregister_chrdev_region(dev, caesar_devs);

	printk(KERN_ALERT "%s driver removed.\n", DRIVER_NAME);
}

module_init(caesar_init);
module_exit(caesar_exit);