// inline assembly

#ifndef ASM_H
#define ASM_H

#include <stdint.h>

// touch memory
#define TOUCH_MEMORY(addr) asm ("movl (%0), %%eax" :: "r" (addr) : "%eax")

// read timestamp counter
uint64_t rdtsc() {
    uint64_t a, d;
    asm volatile (
        "lfence \n" // prevent reordering
        "rdtsc  \n" // read tsc to eax:edx
        "lfence \n" // prevent reordering
            : "=a" (a), "=d" (d)
    );
    return (d << 32) | a;
}

#endif // ASM_H