#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <asm/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Provides the rogue character device");

static char rogue[] = "rogue";
#define len_rogue ((sizeof(rogue)/sizeof(rogue[0])) - 1)

static dev_t devnode;
static struct class *devclass = NULL;
static struct cdev devcdev;
static struct device *devdevice = NULL;

static int rogue_open(struct inode *, struct file *);
static int rogue_release(struct inode *, struct file *);
static ssize_t rogue_read(struct file *, char *, size_t, loff_t *);
static ssize_t rogue_write(struct file *, const char *, size_t, loff_t *);

static int count = 0;
static int cant = 0;
static char actionqueue[] = "blahblahblah";
#define len_queue ((sizeof(actionqueue)/sizeof(actionqueue[0])) - 1)

static struct file_operations dev_ops = {
	.owner = THIS_MODULE,
	.read = rogue_read,
	.write = rogue_write,
	.open = rogue_open,
	.release = rogue_release
};

int init_module() {
	int status;
	if((status = alloc_chrdev_region(&devnode, 0, 1, rogue)) < 0) {
		printk(KERN_ALERT "Can't rogue: %d\n", status);
		return status;
	}
	if(IS_ERR(devclass = class_create(THIS_MODULE, rogue))) {
		printk(KERN_ALERT "Can't rogue: %ld\n", PTR_ERR(devclass));
		return PTR_ERR(devclass);
	}
	cdev_init(&devcdev, &dev_ops);
	if((status = cdev_add(&devcdev, devnode, 1)) < 0) {
		printk(KERN_ALERT "Can't rogue: %d\n", status);
		return status;
	}
	if(IS_ERR(devdevice = device_create(devclass, NULL, devnode, NULL, rogue))) {
		printk(KERN_ALERT "Can't rogue: %ld\n", PTR_ERR(devdevice));
		return PTR_ERR(devdevice);
	}
	printk(KERN_INFO "Ready to rogue\n");
	return 0;
}

void cleanup_module() {
	device_destroy(devclass, devnode);
	cdev_del(&devcdev);
	class_destroy(devclass);
	unregister_chrdev_region(devnode, 1);
	printk(KERN_INFO "game over.\n");
}

static int rogue_open(struct inode *ino, struct file *fil) {
	fil->f_pos = 0;
	try_module_get(THIS_MODULE);
	return 0;
}

static int rogue_release(struct inode *ino, struct file *fil) {
	module_put(THIS_MODULE);
	return 0;
}


static ssize_t rogue_read(struct file *fil, char *buf, size_t len, loff_t *off) {
	register int amt = count;
	register int x = 0;
	while(amt-- > 0) {
		put_user(actionqueue[x++], buf++);
	}
	cant = count;
	count = 0;
	return cant;
}

static ssize_t rogue_write(struct file *fil, const char *buf, size_t len, loff_t *off) {
	/* cool. */
	register int amt = len;
	while(amt-- > 0) {
		if(count < len_queue) {
			get_user(actionqueue[count++], buf++);
		}
	}
	return len;
}
