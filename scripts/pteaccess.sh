#!/bin/sh

# make
(cd ../src/receiver/pteaccess && make)

# copy
scp ../src/receiver/pteaccess/pteaccess.ko vm1:.

# load
ssh vm1 sudo rmmod pteaccess
ssh vm1 sudo insmod pteaccess.ko