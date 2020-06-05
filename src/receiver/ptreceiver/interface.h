// interface for ptreceiver kernel module

#ifndef PTRECEIVER_INTERFACE_H
#define PTRECEIVER_INTERFACE_H

#include <fcntl.h>
#include <unistd.h>
#include "../../asm.h"
#include "../../memory.h"

// buffer size (must match kernel module)
#define BUFFER_SIZE (4096 / PACKET_SIZE)

// file descriptor of kernel module proc file
static int ptreceiver_fd = -1;

// open kernel module proc file
void ptreceiver_open() {
    ptreceiver_fd = open("/proc/ptreceiver", O_RDWR);
    if (ptreceiver_fd == -1) {
        printf("error opening kernel interface '/proc/ptreceiver': %s\n", strerror(errno));
        exit(1);
    }
}

// map buffer from kernel space
uint8_t* ptreceiver_map_buffer() {
    uint8_t *buffer = mmap(NULL, PACKET_SIZE * BUFFER_SIZE, PROT_READ, MAP_PRIVATE, ptreceiver_fd, 0);
    if (buffer == (void*)-1) {
        printf("error mapping buffer: %s\n", strerror(errno));
        exit(1);
    }
    TOUCH_MEMORY(buffer); // trigger page fault
    return buffer;
}

// configure kernel module (number of vaddrs)
void ptreceiver_configure(uint64_t vaddrs_count) {
    addr_t vaddr = 0x0; // to configure use address 0x0
    pwrite(ptreceiver_fd, (void*)&vaddr, sizeof(addr_t), vaddrs_count); // number of vaddrs
}

// set vaddr at index
void ptreceiver_set_addr(addr_t addr, uint64_t index) {
    TOUCH_MEMORY(addr); // touch pages to create pte (-> zero-page mapping)
    pwrite(ptreceiver_fd, (void*)&addr, sizeof(addr_t), index);
}

// read packets into buffer
int ptreceiver_fill_buffer() {
    return pread(ptreceiver_fd, NULL, 0, 0);
}

// close proc file
void ptreceiver_close(uint8_t *buffer) {
    munmap(buffer, PACKET_SIZE * BUFFER_SIZE);
    close(ptreceiver_fd);
}

#endif // PTRECEIVER_INTERFACE_H 