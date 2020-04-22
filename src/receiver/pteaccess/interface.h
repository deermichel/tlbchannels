// interface for pteaccess kernel module

#ifndef PTEACCESS_INTERFACE_H
#define PTEACCESS_INTERFACE_H

#include <fcntl.h>
#include <unistd.h>
#include "../../asm.h"
#include "../../memory.h"

// file descriptor of kernel module proc file
static int pteaccess_fd = -1;

// open kernel module proc file
void pteaccess_open() {
    pteaccess_fd = open("/proc/pteaccess", O_RDWR);
    if (pteaccess_fd == -1) {
        printf("error opening kernel interface '/proc/pteaccess': %s\n", strerror(errno));
        exit(1);
    }
}

// configure kernel module (number of vaddrs)
void pteaccess_configure(uint64_t vaddrs_count) {
    addr_t vaddr = 0x0; // to configure use address 0x0
    pwrite(pteaccess_fd, (void*)&vaddr, sizeof(addr_t), vaddrs_count); // number of vaddrs
}

// set vaddr at index
void pteaccess_set_addr(addr_t addr, uint64_t index) {
    TOUCH_MEMORY(addr); // touch pages to create pte (-> zero-page mapping)
    pwrite(pteaccess_fd, (void*)&addr, sizeof(addr_t), index);
}

// retrieve accessed bits
void pteaccess_get_bits(void *buffer, size_t bytes) {
    pread(pteaccess_fd, buffer, bytes, 0);
}

// close proc file
void pteaccess_close() {
    close(pteaccess_fd);
}

#endif // PTEACCESS_INTERFACE_H