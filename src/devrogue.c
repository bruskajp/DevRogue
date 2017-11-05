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

static void rogue_draw_stat(void);
static void rogue_update_state(char);
static int rogue_open(struct inode *, struct file *);
static int rogue_release(struct inode *, struct file *);
static ssize_t rogue_read(struct file *, char *, size_t, loff_t *);
static ssize_t rogue_write(struct file *, const char *, size_t, loff_t *);

static int count_act = 0;
static char shouldDisplay = 1;
static char actionqueue[] = "blahblahblah";

#define PLAYFIELD_WIDTH (80+1)
#define PLAYFIELD_HEIGHT 24

// 8+2+4+16+3+4+7+2+4+30 = 80
static char stat_fmt[] = "Health: %2d    Enemies Killed: %3d    Player Level: %2d    Floor: %2d    %10s\n";
static char statbar[82];

static char gamebuffer[PLAYFIELD_WIDTH*PLAYFIELD_HEIGHT+1];

static int playerPos = 1+PLAYFIELD_WIDTH; // Start the player at coordinate 1,1 where 0,0 is the top left
static int enemyPos[30];

#define PLAYER_HEALTH_MAX 99
#define LEVEL_MAX 99
#define ENEMIES_MAX 600

static int playerHealth = 20; // Cap at 99
static int enemiesKilled = 0; // Cap at 600
static int playerLevel = 1; // Cap at 99
static int currentFloor = 1; // Cap at 99

#define len_queue ((sizeof(actionqueue)/sizeof(actionqueue[0])) - 1)

static struct file_operations dev_ops = {
	.owner = THIS_MODULE,
	.read = rogue_read,
	.write = rogue_write,
	.open = rogue_open,
	.release = rogue_release
};

static void rogue_draw_stat() {
	int i = 0;
	sprintf(statbar, stat_fmt, playerHealth, enemiesKilled, playerLevel, currentFloor, "");
	for(i = 0; i<PLAYFIELD_WIDTH; i++) {
		gamebuffer[i+(PLAYFIELD_HEIGHT-1)*PLAYFIELD_WIDTH] = statbar[i];
	}
}

int init_module() {
	int status;
	
	// Draw basic fullscreen room
	int y = (PLAYFIELD_HEIGHT-3);
	int x = (PLAYFIELD_WIDTH-3);
	while(y-- > 1) {
		x = (PLAYFIELD_WIDTH-3);
		gamebuffer[0+PLAYFIELD_WIDTH*y] = '*';
		while(x-- > 1) {
			gamebuffer[x+PLAYFIELD_WIDTH*y] = '.';
		}
		gamebuffer[(PLAYFIELD_WIDTH-2)+PLAYFIELD_WIDTH*y] = '*';
		gamebuffer[(PLAYFIELD_WIDTH-1)+PLAYFIELD_WIDTH*y] = '\n';
	}
	x = (PLAYFIELD_WIDTH-2);
	while(x-- > 0)
	{
		gamebuffer[x+0*PLAYFIELD_WIDTH] = '*';
		gamebuffer[x+(PLAYFIELD_HEIGHT-2)*PLAYFIELD_WIDTH] = '*';
	}
	gamebuffer[(PLAYFIELD_WIDTH-1)+0*PLAYFIELD_WIDTH] = '\n';
	gamebuffer[(PLAYFIELD_WIDTH-1)+(PLAYFIELD_HEIGHT-2)*PLAYFIELD_WIDTH] = '\n';
	gamebuffer[sizeof(gamebuffer)-1] = '\0';
	gamebuffer[playerPos] = '@';

	rogue_draw_stat();

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

static void rogue_update_state(char action) {
	gamebuffer[playerPos] = '.';
	switch(action) {
		case 'u':
			if(playerPos - PLAYFIELD_WIDTH >= 0)
				if((gamebuffer[playerPos-PLAYFIELD_WIDTH] == '.'))
					playerPos -= PLAYFIELD_WIDTH;
			break;
		case 'd':
			if((playerPos + PLAYFIELD_WIDTH)/PLAYFIELD_WIDTH < (PLAYFIELD_HEIGHT-1))
				if(gamebuffer[playerPos+PLAYFIELD_WIDTH] == '.')
					playerPos += PLAYFIELD_WIDTH;
			break;
		case 'l':
			if((playerPos%PLAYFIELD_WIDTH)-1 >= 0)
				if((gamebuffer[playerPos-1] == '.'))
					playerPos -= 1;
			break;
		case 'r':
			if((playerPos%PLAYFIELD_WIDTH)+1 < PLAYFIELD_WIDTH)
				if((gamebuffer[playerPos+1] == '.'))
					playerPos += 1;
			break;
	}
	if(playerHealth < PLAYER_HEALTH_MAX)
		playerHealth += 1;
	rogue_draw_stat();
	gamebuffer[playerPos] = '@';
}

static ssize_t rogue_read(struct file *fil, char *buf, size_t len, loff_t *off) {
	register int amt = sizeof(gamebuffer);
	register int x = 0;
	
	while(x < count_act) {
		rogue_update_state(actionqueue[x++]);
	}
	count_act = 0;
	x = 0;
	if(len < sizeof(gamebuffer)) {
		// TODO when len is less than the game screen size
	} else {
		while(amt-- > 0) {
			put_user(gamebuffer[x++], buf++);
		}
	}
	if(shouldDisplay) {
		shouldDisplay = 0;
		return sizeof(gamebuffer);
	} else { 
		return 0;
	}
}

static ssize_t rogue_write(struct file *fil, const char *buf, size_t len, loff_t *off) {
	/* cool. */
	register int amt = len;
	while(amt-- > 0) {
		if(count_act < len_queue) {
			shouldDisplay = 1;
			get_user(actionqueue[count_act++], buf++);
		}
	}
	return len;
}
