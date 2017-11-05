#!/bin/sh

IP=128.153.144.247
sshpass -p "rogue" scp -r src root@$IP:/root/7hrl
sshpass -p "rogue" ssh root@$IP "rmmod devrogue; cd 7hrl; make; insmod devrogue.ko"
