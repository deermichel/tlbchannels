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

// measure access time
uint32_t probe(uint64_t address) {
    uint32_t time = 0;
    asm volatile (
        "lfence             \n" // prevent reordering
        "rdtsc              \n" // read tsc to eax:edx
        "lfence             \n" // prevent reordering
        "movl %%eax, %%edi  \n" // save lower bits (eax) to edi
        "movl (%1), %%eax   \n" // access probe address
        "lfence             \n" // prevent reordering
        "rdtsc              \n" // read tsc to eax:edx
        "lfence             \n" // prevent reordering
        "subl %%edi, %%eax  \n" // get diff tsc -> access time
            : "=a" (time)
            : "c" (address)
            : "%edx", "%edi"
    );
    return time;
}

#endif // ASM_H