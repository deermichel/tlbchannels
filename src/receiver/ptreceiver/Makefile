# Makefile for ptreceiver kernel module

obj-m += ptreceiver.o

all:
	make -C /lib/modules/5.4.0-33-generic/build M=$(PWD) EXTRA_CFLAGS="-DARCH_BROADWELL -msse4.2" modules

custom:
	make -C /lib/modules/5.4.0-custom/build M=$(PWD) EXTRA_CFLAGS="-DARCH_BROADWELL -msse4.2" modules

clean:
	make -C /lib/modules/5.4.0-33-generic/build M=$(PWD) clean

test:
	sudo dmesg -C
	sudo insmod ptreceiver.ko
	sudo rmmod ptreceiver.ko
	sudo dmesg