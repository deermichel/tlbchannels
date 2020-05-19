# Makefile for pteaccess kernel module

obj-m += pteaccess.o

all:
	make -C /lib/modules/5.4.0-29-generic/build M=$(PWD) modules

clean:
	make -C /lib/modules/5.4.0-29-generic/build M=$(PWD) clean

test:
	sudo dmesg -C
	sudo insmod pteaccess.ko
	sudo rmmod pteaccess.ko
	sudo dmesg