#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/random.h>
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
static void rogue_draw_enemies(void);

static void rogue_update_state(char);
static void rogue_endgame(void);
static void rogue_do_ai(void);
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
static const char stat_fmt[] = "Health: %2d    Enemies Killed: %3d    Player Level: %2d    Floor: %2d    %10s\n";
static char statbar[82];

static const char lose_fmt[] = "***********************************You Died.************************************\n";
static const char win_fmt[] =  "                                 SORRY NOTHING                                  \n";


static char gamebuffer[PLAYFIELD_WIDTH*PLAYFIELD_HEIGHT+1];

static int playerPos = 1+PLAYFIELD_WIDTH; // Start the player at coordinate 1,1 where 0,0 is the top left
static int enemyPos[30];
static int enemyHealth[30];


#define STUPID_THRESHOLD 100
#define PLAYER_HEALTH_MAX 99
#define LEVEL_MAX 99
#define ENEMIES_MAX 600
#define DAMAGE_RATIO 5

static unsigned char rand = 0;

static int playerHealth = 20; // Cap at 99
static int playerMaxHealth = 20;
static int enemiesKilled = 0; // Cap at 600
static int playerLevel = 1; // Cap at 99
static int currentFloor = 1; // Cap at 99
static int enemyCount = 1;

static int gameOver = 0;

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

static void rogue_draw_enemies() {
	int i = enemyCount;
	while(i-- > 0) {
		if(enemyHealth[i] > 0) {
			gamebuffer[enemyPos[i]] = 'X';
		} else {
			gamebuffer[enemyPos[i]] = '.';
		}
	}
}

static void genLevel( void ) {
	unsigned char rand;
        int i, j, num_doors, num_doors_available, box_size_x, box_size_y, box_pos_x, box_pos_y, player_x, player_y;
        int doors[6];	

	// Draw basic fullscreen room
	int y = (PLAYFIELD_HEIGHT-3);
	int x = (PLAYFIELD_WIDTH-3);
	while(y-- > 1) {
		x = (PLAYFIELD_WIDTH-3);
		gamebuffer[0+PLAYFIELD_WIDTH*y] = '*';
		while(x-- > 1) {
			gamebuffer[x+PLAYFIELD_WIDTH*y] = '*';
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

	box_size_x = 80 / 15;
        box_size_y = 24 / 8;
        box_pos_x = 1;
        get_random_bytes(&rand, sizeof(rand));
        box_pos_y = (rand%(PLAYFIELD_HEIGHT - 3)) + 1;
        get_random_bytes(&rand, sizeof(rand));
        num_doors = rand % 1 + 1;
        num_doors_available = 0;

	j = 0;
        while(j < 5){
                i = 0;
                box_pos_x = 1;
                get_random_bytes(&rand, sizeof(rand));
                box_pos_y = (rand%(PLAYFIELD_HEIGHT - 3)) + 1;
                --num_doors_available;
                while(box_pos_x < PLAYFIELD_WIDTH-3) {
                        x = 0;
                        y = 0;

                        while (x < box_size_x && (x + box_pos_x) < PLAYFIELD_WIDTH-3){
                                //printk("AHH: %d < %d &&  %d < %d\n", x, box_size_x, box_pos_x, PLAYFIELD_WIDTH-3);
                                while (y < box_size_y && (y + box_pos_y) < PLAYFIELD_HEIGHT-3) {
                                        gamebuffer[(x+box_pos_x) + PLAYFIELD_WIDTH * (box_pos_y+y)] = '.';
                                        //printk("(%d, %d)\n", (x+box_pos_x), (box_pos_y+y));
                                        ++y;
                                }
                                y = 0;
                                ++x;
                        }

                        if (i++ > 10000){num_doors_available++;}

                        // update board 
                        if ( (box_pos_x == 1 && box_pos_y == doors[1]) ||
                                (box_pos_y == 1 && box_pos_x == doors[0]) ||
                                (((box_pos_x + box_size_x) <= doors[0]) && box_pos_y == doors[1]) ||
                                (((box_pos_y + box_size_y) <= doors[1]) && box_pos_x == doors[0]))
                                { ++num_doors_available; }
                
                        get_random_bytes(&rand, sizeof(rand));
                        if (rand % 2) {box_pos_x += (rand % box_size_x); }
                        get_random_bytes(&rand, sizeof(rand));
                        if (rand % 2 == 0){
                                box_pos_y += (rand % (2 * box_size_y)) - box_size_y;
                        } else {
                                box_pos_y +=  rand % (box_size_y);
                        }

                        if (box_pos_x < 1) { box_pos_x = 1; }
                        if (box_pos_x > PLAYFIELD_WIDTH-3) { box_pos_x = PLAYFIELD_WIDTH-3; }
                        if (box_pos_y < 1) { box_pos_y = 1; }
                        if (box_pos_y > PLAYFIELD_HEIGHT-3) { box_pos_y = PLAYFIELD_HEIGHT-3; }

                        }
                ++j;    
        }

	i = 0;
	doors[0] = PLAYFIELD_WIDTH - 2;
	do{
		get_random_bytes(&rand, sizeof(rand));
                doors[1] = rand % (PLAYFIELD_HEIGHT-2) + 1;
	} while (gamebuffer[doors[0]+doors[1]*PLAYFIELD_WIDTH-2] == '*');
	gamebuffer[doors[0]+doors[1]*PLAYFIELD_WIDTH] = '^';
        /*while ( i++ <= num_doors ) {
                get_random_bytes(&rand, sizeof(rand));
                switch(rand % 4){
                        case 0:
				do{
                                	doors[2*i] = 0;
					get_random_bytes(&rand, sizeof(rand));
                                	doors[2*i+1] = rand % (PLAYFIELD_WIDTH-3) + 1;
				} while (gamebuffer[1+doors[1]*PLAYFIELD_WIDTH] == '*');
				gamebuffer[1+doors[1]*PLAYFIELD_WIDTH] = '^';
                                break;
                        case 1:
				do{
                                	get_random_bytes(&rand, sizeof(rand));
                               		doors[2*i] = rand % (PLAYFIELD_HEIGHT-3) + 1;
                                	doors[2*i+1] = 0;
				} while (gamebuffer[doors[0]] == '*');
				gamebuffer[1+doors[1]*PLAYFIELD_WIDTH] = '^';
                                break;
                        case 2:
				do{
                                	doors[2*i] = 80;
                                	get_random_bytes(&rand, sizeof(rand));
                                	doors[2*i+1] = rand % (PLAYFIELD_WIDTH-3) + 1;
				} while (gamebuffer[(PLAYFIELD_WIDTH - 3)+doors[1]*PLAYFIELD_WIDTH] == '*');
				gamebuffer[1+doors[1]*PLAYFIELD_WIDTH] = '^';
                                break;
                        case 3:
				do{
                                	get_random_bytes(&rand, sizeof(rand));
                                	doors[2*i] = rand % (PLAYFIELD_HEIGHT-3) + 1;
                                	doors[2*i+1] = 80;
				} while (gamebuffer[doors[0]+(PLAYFIELD_HEIGHT-1)*PLAYFIELD_WIDTH] == '*');
				gamebuffer[1+doors[1]*PLAYFIELD_WIDTH] = '^';
                                break;
                        default:
                                printk("YOU GET SUM FUK");
                }
        }*/

	
	get_random_bytes(&rand, sizeof(rand));
	enemyCount = rand % 25 + 5;
	i = 0;
	while (i < enemyCount){
		do{
			get_random_bytes(&rand, sizeof(rand));
			player_x = rand % (PLAYFIELD_WIDTH - 3) + 1;
			get_random_bytes(&rand, sizeof(rand));
        	        player_y = rand % (PLAYFIELD_HEIGHT-3) + 1;
		} while (gamebuffer[player_x+player_y*PLAYFIELD_WIDTH+1] != '.');
		enemyPos[i] = player_x+player_y*PLAYFIELD_WIDTH;
		gamebuffer[ player_x+player_y*PLAYFIELD_WIDTH ] = 'X';
		enemyHealth[i] = 100;
		++i;
	}
	
	
	player_x = 1;
	do{
		get_random_bytes(&rand, sizeof(rand));
                player_y = rand % (PLAYFIELD_HEIGHT-3) + 1;
	} while (gamebuffer[player_x+player_y*PLAYFIELD_WIDTH+1] != '.');
	playerPos = player_x+player_y*PLAYFIELD_WIDTH;
	gamebuffer[playerPos] = '@';
}

int init_module() {
	int status;
	
	genLevel();

	gamebuffer[(PLAYFIELD_WIDTH-1)+0*PLAYFIELD_WIDTH] = '\n';
	gamebuffer[(PLAYFIELD_WIDTH-1)+(PLAYFIELD_HEIGHT-2)*PLAYFIELD_WIDTH] = '\n';
	gamebuffer[sizeof(gamebuffer)-1] = '\0';

	rogue_draw_stat();
	rogue_draw_enemies();

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
	printk(KERN_INFO "+----------------------------------------------------------------------------------------------------+\n|:::::::::::::~:::++++++++++++++++++++++++++++++++o++++o+++++++++++++++:::::::::+::::::++:::::~:~::::|\n|:::~::::::::~:~~~:::++++++++++++++++++++++++++o+o+ooo++o+o+++++++++++++++:::::::::~:::::::+:::::::::|\n|:::::::::+::::::~~~:~::+++++++++++++++++++++ooooo++oooooooo+++++++++::::+::::~~::~::~::~::::::~~:~~:|\n|:+::::+:+:+++:::::~~::+++++++++++++++++++oo++::~~~~...~~~::++++++++++++::::::::::::::::::~::~:::::~:|\n|+::+:+:++::::+:~~::::~::+++++++++++++oo++:............. .   ..~~++++++++:+:::::::::~::~:::~~~:::~::~|\n|:++:::::::::::::::~::::::+++++++++ooo+~.  ...................    .~:+++++++:::~:~~::~:~::~::::~~:~~:|\n|::::::~::::::::::::::~::::::+++++++:.  ........~~.~..~..........    .:+++++:::::::~:~:::~::::~~:::~~|\n|:::::::::~:::::::::::::::+++:++++:....~.~~~~~~~~~..~........ .. ..    .++++++:::::::::~:::~~::::~:::|\n|::::::::::::~::::::::::::++++++:...~~~~~::~:~:~~~~~~~.....~...... .     +o++++::::~:~::~:~::~:::~:~:|\n|::~:::~:::::::::::::+::+++++++. ...~~~~~:~:~~:~~.~....~........... . .   +o++++:::::::::::~::::~::::|\n|:::::~:::::::~:::+:+++++++++:...~~~::~:~::~~~~~~~..~~..~~....... ....     oo++++::::~:::~:::::::::::|\n|:::::::::~:::::::::+:++++++~..~~~~~~:~~:~~~~~~~~~~~.~~~..~~........  ..   .o+++++:::::::::::~:::::::|\n|:::::::+:::::::::::::+++++~..~~.~~~~~~~~~~~~~~~~~.~~~~.~.~~..........      ~o++++:::::::::::::::::::|\n|:::::::::::::::::++:+:+++~..~~.~~~~~~~~~:~:~~~~~.~~..~~.~...~.~....  ...    ++++++:++:::::::::::::::|\n|::::::::+:::::+::::+:+++~..~~~~~~~~:~~~~~:~~::~:~.~..~~~.~.~........... .   ~o+++++:::::::::::::::::|\n|+:+::+:+:::+:+:::+::++++.~.~~~~~~~~~:::::~:~~~~~~~~~~~..~.~.~~..... .. .  .  +o++++:+++++++:+::+:+::|\n|:++::::+:+:+:++:+:++++++~.~~~~~~~~~~~~~~:~~::~~~~~~~~~~~.~~............    ..+o++++++++++++:::::+:+:|\n|+::++++::++:+::+:+:+++++~~~~~.~.~~~~~~::~:~~~~~~~~~~~~~~~~~.~~~........ ..~..:+oo+++++++:+:+++++:+:+|\n|++::+++:+:+::+::+:++++++~.~~~~~~~~~~~~:~:~:~::~:~:~~~~~~~~~~~.~..... ..~~~~~.~~+o+++++++++:++:+:++::|\n|:+++::+++:++++++++++++++:...~~~~~~~~~:~~:~~~.~~~~~~::~~~~~~~~~.~......~~~~::~~~+o+++++++++++++:++:++|\n|++++++++++++++++++++++++:~.~.~.~~~~~~:~~~....~.~~.~~~~~~:~~~~..~~.. ..~..~~~~~~ooo++++++++++++++++++|\n|++++++++++++++++++++++++~....~~~~~~~~~.~~~~:::::::~~~::~~~:~~~~.....~~...::~~~~oo+++++++++++++++++++|\n|++++++++++++++++++++++++~ ...~~~~~~~~~~~:~:~:~~::::::~:~~:~~~~~......~~~.~~...:ooo++++++++++++++++++|\n|++++++++++++++++++++++++:...~.~~~~~~~:~~~~~~..~~.~~~~~~::~~~~~~~~~....~. ~~~~~+oo+oo++++++++++++++++|\n|+++++++++++++++++++++++++:.~~....~~~~~~~~. ~:: ....~~~~~~~~~~~.~.~...~~~~:~::~:ooooo++++++++++++++++|\n|++++++++++++++++++++++++++:..:~ . ~~~::~~..~:~~~~~~~~::~~~~~~~~.~.~....~..~~....~:+ooo++++++++++++++|\n|++++++++++++++++++++++++++o~ :~....~:~~:~~~..~~~~~~::::~:~~~~..~~~.~~..~~ ....      :+oo++++++++++++|\n|+++++++++++++++++++++++++++:......~~:::~::~~~~~~::::+:::~:~~~~~~.~~....~.......      .+oo+++++++++++|\n|+++++++++++++++++++++++o+o+:~~~~~.~:::~::~~~.~~~~:::+:::~:~~~~~~~~.~..........        .ooo++++++++++|\n|++++o++++++++o++++++o++++++:~~:~~~:::::::::~..~~~::::::~~~~~~~~..~~..~........         ~+ooo++++++++|\n|o+o+++o+ooooo+o+o+++++++o+o+~~~~~~:::::::::~.~~~~.~~:~~:~~~~~~~~~~~.~...... ..          .~+oo+++++++|\n|ooooooooo++++ooooooooooooooo~~~~~~~::~~~~~~~:~:~~~~~.~~~~~~~~~~~~~.~...... . .    .       .+ooo+++++|\n|o+++ooo+.. ...~ooooooooooo++:~~~.  ....~~~::~:::~:~..~.~~~~~~~~~~~...~....               . +ooooo+++|\n|.....~.   ..   ~~....~:~..    .~..... .~.~~~~::::::~.~~~~~~~~~~~~~.... ..  .        .  .. ~+ooooooo+|\n|.  .   .    ...    .       ..   ~.~....~~::++++++~~~~~.~~~~~~~~~...... . ..        .  .  :oooooooo+o|\n|   . ..  ..    . . . . ..  .   :o+~... .:+++++:~~~::~~~~~~~~.~~..... .   .             ~+oooo+oooooo|\n|    . .   .  .        .   .   ~oooo:......~~~~~::::::~~~~~~~~~.... .   .~.           ~+ooooooooooooo|\n|        .   .    .. .         +oo+oo+~..~~.~~~~::::::~:~~~~..~...     ..           .+ooooooooooooooo|\n|            .        .       .oo+ooo+o+~.~~.~~~~:::~:~:~~~~~...      ..          .+ooooooooooooooooo|\n|                 .           ~oo+oo+o+oo:..~~~~~:~~:~:~~~~...      ...          :ooooooooooooooooooo|\n|                             +ooo+oo+o+o+..~~~~~~:~::~~~...      .            ~ooooooooooooooooooooo|\n|                 .           +o++o+oo+o++. ..~~~~~~~~....       .           ~ooooooooooooooooooooooo|\n|                             +oooooooo+++++~..~~~~~..          .          ~+oooooooooooooooooooooooo|\n|                            .ooo+++o+oo++ooo+~                          .+oooooooooooooooooooooooooo|\n|                            ~oo+ooooo+ooooo++~                         :oooooooooooooooooooooooooooo|\n|:.                          :ooooo+oooooooo+                         ~+ooooooooooooooooooooooooooooo|\n|:.                          +ooo+o+ooooooooo:        .              :oooo+oooooooooooooooooooooooooo|\n|:                           ooooo+ooooooooooo+    .                :oo+o+ooooooooooooooooooooooooooo|\n|.. ... . .                 ~oooooooooooooooooo+   ..~             +o++oooooooooooooooooooooooooooooo|\n|~  ..  . ....              +oooo++ooooooooooooo+.:++.            ++++ooooooooooooooooooooooooooooooo|\n+----------------------------------------------------------------------------------------------------+\n");
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

#define IS_ENEMY(e) (e>=0x41 && e<=0x5A)

static int rogue_find_enemy(int pos) {
	int i = 0;
	for(i = 0; i<enemyCount; i++) {
		if(pos == enemyPos[i])
			return i;
	}
	return -1;
}

static void rogue_fight_enemy(int pos) {
	int enemy = 0;
	enemy = rogue_find_enemy(pos);
	if(enemy >= 0) {
		if(enemyHealth[enemy] > 0) {
			enemyHealth[enemy] -= (playerLevel*DAMAGE_RATIO);
			if(enemyHealth[enemy] <= 0) {
				enemyHealth[enemy] = 0;
				enemiesKilled++;
			}
		}
	}
}

static char rogue_inflict_damage(int pos) {
	char x = 0;
	if(rogue_find_enemy(pos+PLAYFIELD_WIDTH)>=0) {
		x = 1;
		get_random_bytes(&rand, sizeof(rand));
		if(rand%20>10) {
			playerHealth -= currentFloor*2;
		}
	}
	if(rogue_find_enemy(pos-PLAYFIELD_WIDTH)>=0) {
		x = 1;
		get_random_bytes(&rand, sizeof(rand));
		if(rand%20>10) {
			playerHealth -= currentFloor*2;
		}
	}
	if(rogue_find_enemy(pos-1)>=0) {
		x = 1;
		get_random_bytes(&rand, sizeof(rand));
		if(rand%20>10) {
			playerHealth -= currentFloor*2;
		}
	}
	if(rogue_find_enemy(pos+1)>=0) {
		x = 1;
		get_random_bytes(&rand, sizeof(rand));
		if(rand%20>10) {
			playerHealth -= currentFloor*2;
		}
	}
	return x;
}

#define ABS(v) v*((v<0)*(-1)+(v>0))

static void rogue_do_ai() {
	int i = 0;
	int canmove = 0;
	int px = 0;
	int py = 0;
	int ex = 0;
	int ey = 0;
	for(i = 0; i<enemyCount; i++) {
		gamebuffer[enemyPos[i]] = '.';
		if(enemyHealth[i] > 0) {
			get_random_bytes(&rand, sizeof(rand));
			if(rand<STUPID_THRESHOLD) {
				// Move randomly
				while(canmove == 0) {
					get_random_bytes(&rand, sizeof(rand));
					switch(rand%4) {
						case 0: // up
							if(enemyPos[i] - PLAYFIELD_WIDTH >= 0) { 
								if(gamebuffer[enemyPos[i]-PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] -= PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
							break;
						case 1: // down
							if((enemyPos[i] + PLAYFIELD_WIDTH)/PLAYFIELD_WIDTH < (PLAYFIELD_HEIGHT-1)) {
								if(gamebuffer[enemyPos[i]+PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] += PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
							break;
						case 2: // left
							if((enemyPos[i]%PLAYFIELD_WIDTH)-1 >= 0) {
								if(gamebuffer[enemyPos[i]-1] == '.') {
									enemyPos[i] -= 1;
									canmove = 1;
								}
							}
							break;
						case 3: // right
							if((enemyPos[i]%PLAYFIELD_WIDTH)+1 < PLAYFIELD_WIDTH) {
								if(gamebuffer[enemyPos[i]+1] == '.') {
									enemyPos[i] += 1;
									canmove = 1;
								}
							}
							break;
					}
				}
				gamebuffer[enemyPos[i]] = 'X';
			} else {
				// Move towards player
				px = playerPos % PLAYFIELD_WIDTH;
				py = playerPos/PLAYFIELD_WIDTH;
				ex = enemyPos[i] % PLAYFIELD_WIDTH;
				ey = enemyPos[i]/PLAYFIELD_WIDTH;

				if(ABS(ex-px) > ABS(ey-py)) {
					if(ex > (px+1)) {
						// Try Left
						if((enemyPos[i]%PLAYFIELD_WIDTH)-1 >= 0) {
							if(gamebuffer[enemyPos[i]-1] == '.') {
								enemyPos[i] -= 1;
								canmove = 1;
							}
						}
					} else if(ex < (px-1)) {
						// Try Right
						if((enemyPos[i]%PLAYFIELD_WIDTH)+1 < PLAYFIELD_WIDTH) {
							if(gamebuffer[enemyPos[i]+1] == '.') {
								enemyPos[i] += 1;
								canmove = 1;
							}
						}
					}
					if(!canmove) {
						if(ey > (py+1)) {
							// Try up
							if(enemyPos[i] - PLAYFIELD_WIDTH >= 0) { 
								if(gamebuffer[enemyPos[i]-PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] -= PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
						} else if(ey < (py-1)) {
							// Try down
							if((enemyPos[i] + PLAYFIELD_WIDTH)/PLAYFIELD_WIDTH < (PLAYFIELD_HEIGHT-1)) {
								if(gamebuffer[enemyPos[i]+PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] += PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
						}
					}
				} else {
						if(ey > (py+1)) {
							// Try up
							if(enemyPos[i] - PLAYFIELD_WIDTH >= 0) { 
								if(gamebuffer[enemyPos[i]-PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] -= PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
						} else if(ey < (py-1)) {
							// Try down
							if((enemyPos[i] + PLAYFIELD_WIDTH)/PLAYFIELD_WIDTH < (PLAYFIELD_HEIGHT-1)) {
								if(gamebuffer[enemyPos[i]+PLAYFIELD_WIDTH] == '.') {
									enemyPos[i] += PLAYFIELD_WIDTH;
									canmove = 1;
								}
							}
						}
						if(!canmove) {
							if(ex > (px+1)) {
								// Try Left
								if((enemyPos[i]%PLAYFIELD_WIDTH)-1 >= 0) {
									if(gamebuffer[enemyPos[i]-1] == '.') {
										enemyPos[i] -= 1;
										canmove = 1;
									}
								}
							} else if(ex < (px-1)) {
								// Try Right
								if((enemyPos[i]%PLAYFIELD_WIDTH)+1 < PLAYFIELD_WIDTH) {
									if(gamebuffer[enemyPos[i]+1] == '.') {
										enemyPos[i] += 1;
										canmove = 1;
									}
								}
							}
						}
				}

				
				gamebuffer[enemyPos[i]] = 'X';
			}
		}
	}
}

static void rogue_endgame() {
	int y = (PLAYFIELD_HEIGHT-1);
	int x = (PLAYFIELD_WIDTH-1);
	int i = 0;
	switch(gameOver) {
		case 1:
			while(y-- > 0) {
				x = (PLAYFIELD_WIDTH-1);
				while(x-- > 0) {
					gamebuffer[x+PLAYFIELD_WIDTH*y] = '*';
				}
				gamebuffer[(PLAYFIELD_WIDTH-1)+PLAYFIELD_WIDTH*y] = '\n';
			}
			for(i = 0; i<PLAYFIELD_WIDTH; i++) {
				gamebuffer[PLAYFIELD_WIDTH*PLAYFIELD_HEIGHT/2+i] = lose_fmt[i];
			}
			break;
		case 2:
			while(y-- > 0) {
				x = (PLAYFIELD_WIDTH-1);
				while(x-- > 0) {
					gamebuffer[x+PLAYFIELD_WIDTH*y] = ' ';
				}
				gamebuffer[(PLAYFIELD_WIDTH-1)+PLAYFIELD_WIDTH*y] = '\n';
			}
			for(i = 0; i<PLAYFIELD_WIDTH; i++) {
				gamebuffer[PLAYFIELD_WIDTH*PLAYFIELD_HEIGHT/2+i] = win_fmt[i];
			}
			break;
	}
}

#define ROGUE_UPDATE_LEVEL playerLevel = 1+(enemiesKilled/10)
#define ROGUE_UPDATE_HEALTH playerMaxHealth = 20+(playerLevel-1)
static void rogue_update_state(char action) {
	char shouldNotHealth = 0;
	gamebuffer[playerPos] = '.';
	switch(action) {
		case 'u':
			if(playerPos - PLAYFIELD_WIDTH >= 0) {
				if(gamebuffer[playerPos-PLAYFIELD_WIDTH] == '.') {
					playerPos -= PLAYFIELD_WIDTH;
				} else if(gamebuffer[playerPos-PLAYFIELD_WIDTH] == '^') {
					genLevel();
				} else if(IS_ENEMY(gamebuffer[playerPos-PLAYFIELD_WIDTH])) {
					rogue_fight_enemy(playerPos-PLAYFIELD_WIDTH);
				}
			}
			break;
		case 'd':
			if((playerPos + PLAYFIELD_WIDTH)/PLAYFIELD_WIDTH < (PLAYFIELD_HEIGHT-1)) {
				if(gamebuffer[playerPos+PLAYFIELD_WIDTH] == '.') {
					playerPos += PLAYFIELD_WIDTH;
				} else if(gamebuffer[playerPos+PLAYFIELD_WIDTH] == '^') {
					genLevel();
				} else if(IS_ENEMY(gamebuffer[playerPos+PLAYFIELD_WIDTH])) {
					rogue_fight_enemy(playerPos+PLAYFIELD_WIDTH);
				}
			}
			break;
		case 'l':
			if((playerPos%PLAYFIELD_WIDTH)-1 >= 0) {
				if((gamebuffer[playerPos-1] == '.')) {
					playerPos -= 1;
				} else if(gamebuffer[playerPos-1] == '^') {
					genLevel();
				} else if(IS_ENEMY(gamebuffer[playerPos-1])) {
					rogue_fight_enemy(playerPos-1);
				}
			}
			break;
		case 'r':
			if((playerPos%PLAYFIELD_WIDTH)+1 < PLAYFIELD_WIDTH) {
				if((gamebuffer[playerPos+1] == '.')) {
					playerPos += 1;
				} else if(gamebuffer[playerPos+1] == '^') {
					genLevel();
				} else if(IS_ENEMY(gamebuffer[playerPos+1])) {
					rogue_fight_enemy(playerPos+1);
				}
			}
			break;
	}
	rogue_do_ai();
	shouldNotHealth = rogue_inflict_damage(playerPos);

	ROGUE_UPDATE_LEVEL;
	ROGUE_UPDATE_HEALTH;

	if(playerHealth <= 0) {
		gameOver = 1;
	} else {
		if(playerHealth < playerMaxHealth && !shouldNotHealth)
			playerHealth += 1;
		if(enemiesKilled >= 600)
			gameOver = 2;
	}
	rogue_draw_stat();
	rogue_draw_enemies();
	gamebuffer[playerPos] = '@';

	if(gameOver > 0) {
		rogue_endgame();
	}
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
	if(gameOver == 99) {
		//unload_module();
		return 0;
	} else if(gameOver>0)
	{
		gameOver = 99;
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
