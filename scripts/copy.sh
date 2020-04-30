#!/bin/sh

# make
(cd ../src/receiver && make)
(cd ../src/sender && make)

# copy
scp ../src/receiver/receiver vm1:.
scp ../src/sender/sender vm2:.