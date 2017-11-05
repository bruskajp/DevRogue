#!/bin/sh

# Set ROGUE_IP to the IP of the dev VM
sshpass -p "rogue" ssh root@$ROGUE_IP "rm -rf 7hrl"
sshpass -p "rogue" scp -r src root@$ROGUE_IP:/root/7hrl
sshpass -p "rogue" ssh root@$ROGUE_IP "rmmod devrogue; cd 7hrl; make; insmod devrogue.ko"
