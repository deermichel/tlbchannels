// interface for ptmapper kernel module

#ifndef PTMAPPER_INTERFACE_H
#define PTMAPPER_INTERFACE_H

#include <fcntl.h>
#include <unistd.h>
#include "../../asm.h"
#include "../../memory.h"

// page accessed bit flag for page table entry
#define PAGE_ACCESSED_FLAG 0x20

// file descriptor of kernel module proc file
static int ptmapper_fd = -1;

// pointer to page table for access bit monitoring
static uint64_t *page_table = NULL;

// open kernel module proc file
void ptmapper_open() {
    ptmapper_fd = open("/proc/ptmapper", O_RDWR);
    if (ptmapper_fd == -1) {
        printf("error opening kernel interface '/proc/ptmapper': %s\n", strerror(errno));
        exit(1);
    }
}

// map page table at base address
void ptmapper_map_table(addr_t base) {
    // send base address
    pwrite(ptmapper_fd, (void*)&base, sizeof(addr_t), 0);

    // map page table
    page_table = mmap(NULL, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, ptmapper_fd, 0);
    if (page_table == (void*)-1) {
        printf("error mapping page table: %s\n", strerror(errno));
        exit(1);
    }
    TOUCH_MEMORY(page_table);
}

// retrieve accessed bits
void ptmapper_get_bits(uint8_t *buffer, size_t bytes) {
    uint64_t *ptep = page_table;
    for (int i = 0; i < bytes * 8; i++, ptep++) {
        uint64_t pte = *ptep;
        buffer[i / 8] |= ((pte & PAGE_ACCESSED_FLAG) ? 1 : 0) << (i % 8);
        *ptep = pte & ~PAGE_ACCESSED_FLAG;
    }
}

// close proc file
void ptmapper_close() {
    munmap(page_table, 4096);
    close(ptmapper_fd);
}

#endif // PTMAPPER_INTERFACE_H