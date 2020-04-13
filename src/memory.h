// memory allocation, address acrobatics

#ifndef MEMORY_H
#define MEMORY_H

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "arch.h"

// type used for virtual addresses (at least 48 bits)
typedef uint64_t addr_t;

// base address for memory allocation
#define BASE_ADDR ((addr_t)0x100000000000)

// calculate address that targets given tlb set
#define ADDR(base, set, way) ((addr_t)(base) | ((addr_t)(set) << VA_SET_OFFSET) | ((addr_t)(way) << VA_WAY_OFFSET))

// allocate read-only memory backed by the zero page
uint8_t* alloc_mem(addr_t addr, size_t bytes) {
    uint8_t *mem = mmap((void*)addr, bytes, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mem == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    return mem;
}

// deallocate memory
void dealloc_mem(uint8_t *mem, size_t bytes) {
    if (munmap(mem, bytes) == -1) {
        printf("error deallocating memory: %s\n", strerror(errno));
        exit(1);
    }
}

#endif // MEMORY_H