# Makefile for ptmapper kernel module

obj-m += ptmapper.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean

test:
	sudo dmesg -C
	sudo insmod ptmapper.ko
	sudo rmmod ptmapper.ko
	sudo dmesg