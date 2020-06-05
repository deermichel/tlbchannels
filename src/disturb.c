#include <stdio.h>
#include <stdlib.h>
#include "asm.h"
#include "memory.h"

// micro-benchmark for tlb interference
int main(int argc, char **argv) {
    // allocate memory
    size_t num_bytes = 1024L * 1024L * atoi(argv[1]);
    printf("Allocating %ld MB\n", num_bytes / 1024 / 1024);
    char *mem = malloc(num_bytes);
    printf("Running...\n");

    // read/writes
    while (1) {
        for (size_t i = 0; i < num_bytes; i += 4096) {
            mem[i]++;
        }
    }

    // cleanup
    free(mem);
    return 0;
}