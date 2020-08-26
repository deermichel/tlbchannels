#!/bin/sh

(cd ../src && make CFLAGS="-DARCH_BROADWELL -DNUM_EVICTIONS=8 -DCHK_CUSTOM")
scp ../bin/receiver vm1:.
scp ../bin/sender vm2:.