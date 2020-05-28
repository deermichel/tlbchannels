# Makefile for sender and receiver program

# DEFINES: make CFLAGS="-D..."
# -----------
# ARCH_BROADWELL		build for broadwell architecture
# ARCH_SKYLAKE			build for skylake architecture
# CHK_BERGER			use berger codes checksum
# CHK_CRC8				use crc8 checksum
# CHK_CUSTOM			use custom checksum
# NUM_EVICTIONS			number of evictions per set during sender loop (most likely = TLB_WAYS)
# PACKET_SIZE			packet size in bytes (defaults to maximum = TLB_SETS / 8)
# RDTSC_THRESHOLD		threshold to determine whether tlb entry got evicted when using timestamps
# RDTSC_WINDOW			number of tsc probe iterations per packet receive loop (use accessed bits = leave undefined)
# RECORD_PACKETS		log packets including start / end timestamps
# REED_SOLOMON			number of reed solomon parity bytes (disable rs = leave undefined)

.PHONY: all receiver sender clean

all: receiver sender

receiver:
	mkdir -p ../bin
	gcc -Wall -Wno-unused-result -g -O3 -msse4.2 $(CFLAGS) -o ../bin/receiver receiver/receiver.c -lfec

sender:
	mkdir -p ../bin
	gcc -Wall -g -O3 -msse4.2 $(CFLAGS) -o ../bin/sender sender/sender.c -lfec

clean:
	rm -Rf ../bin
