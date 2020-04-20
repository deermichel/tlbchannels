# Makefile for receiver program

all:
	# cd ../pteaccess && make
	gcc -Wall -Wno-unused-result -g -O3 -msse4.2 -DARCH_SKYLAKE -o receiver receiver.c

clean:
	rm -f receiver

test:
	sudo dmesg -C
	sudo insmod pteaccess/pteaccess.ko
	./receiver
	sudo rmmod pteaccess
	sudo dmesg