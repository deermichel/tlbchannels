#!/bin/sh

(cd ../src/receiver/ptreceiver && make)
scp ../src/receiver/ptreceiver/ptreceiver.ko vm1:.
ssh vm1 sudo rmmod ptreceiver.ko
ssh vm1 sudo insmod ptreceiver.ko

(cd ../src && make kreceiver CFLAGS="-DARCH_BROADWELL")
(cd ../src && make sender CFLAGS="-DARCH_BROADWELL -DNUM_EVICTIONS=10 -DCHK_CRC8")
scp ../bin/kreceiver vm1:.
scp ../bin/sender vm2:.