# Makefile for sender program

# DEFINES: make CFLAGS="-D..."
# -----------
# ARCH_BROADWELL		build for broadwell architecture
# ARCH_SKYLAKE			build for skylake architecture
# NUM_EVICTIONS			number of evictions per set during sender loop (most likely = TLB_WAYS)
# RECORD_PACKETS		log packets including start / end timestamps

all:
	gcc -Wall -g -O3 -msse4.2 $(CFLAGS) -o sender sender.c -lfec

clean:
	rm -f sender