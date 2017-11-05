

MODULE_LICENSE("GPL"); // Currently using the MIT license
MODULE_AUTHOR("James Bruska and Robert Newman");
MODULE_DESCRIPTION("Dev Rouge: A kernel space rogue-like game");
MODULE_VERSION("0.1");

static devrogue_arg_t rogue_actions;

long ioctl_funcs( struct file *fp, unsigned int cmd, unsigned long arg ) {
	int ret = 0;

	devrogue_arg_t* rogue_actions_user = (devrogue *) arg;

	if (rogue_actions_user == NULL) {
		printk_d("User did not pass in arg\n");
		return (-EINVAL);
	}

	if (copy_from_user(&rogue_actions, rogue_actions_user, sizeof(devrogue_arg_t)) != 0)
	{
  		printk_d("lprof_ioctl: Could not copy cmd from userspace\n");
  		return (-EINVAL);
	}

	switch( cmd ) {
		case IOCTL_ACTIONS:
			printk( KERN_INFO "IT DOES A THINGS\n" );
			break;
		default:
			printk( KERN_INFO "Invalid command\n" );
		break;
	}

	struct file_operations fops = {
		open: open,
		read: read,
		unlocked_ioctl: ioctl_funcs,
		release: release
	};
}
