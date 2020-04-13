# Makefile for receiver program

all:
	# cd ../pteaccess && make
	gcc -Wall -DARCH_BROADWELL -o receiver receiver.c

clean:
	rm -f receiver

test:
	sudo dmesg -C
	sudo insmod pteaccess/pteaccess.ko
	./receiver
	sudo rmmod pteaccess
	sudo dmesg