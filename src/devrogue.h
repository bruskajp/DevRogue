#ifndef DEVROUGE_H
#define DEVROUGE_H

#include <linux/ioctl.h>

#define IOC_MAGIC 'k'

#define IOCTL_ACTIONS _IOW(IOC_MAGIC, 0, char *)

#define DEVICE_NAME "devRouge"

#define DEVICE_PATH "/dev/" DEVICE_NAME

typedef struct
{
	unsigned int move_dir;
	unsigned int action;
} devrouge_arg_t;

#define DEBUG
#ifdef DEBUG
	#define printk_d(...) printk(KERN_INFO "DevRouge: " __VA_ARGS__)
#else
	#define printk_d(...)
#endif // DEBUG

#endif // DEVROUGE_H
