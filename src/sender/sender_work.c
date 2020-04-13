#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../arch.h"

// entry point
int main(int argc, char **argv) {
    // allocate memory
    // uint64_t mem_bytes = 4096 * TLB_SETS; // TLB_SETS 4k pages
    uint64_t mem_bytes = 1024L * 1024 * 256 * 6; // enough 4k pages, 1MB * 256sets * 6ways
    uint8_t *mem = mmap((void*)BASE_ADDR, mem_bytes, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mem == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }

    // send data
    uint8_t send_buffer[128][TLB_SETS] = {0};
    for (int j = 0; j < 128; j++) {
        if (j % 2) {
            for (int k = 0; k < TLB_SETS; k++) send_buffer[j][k] = k % 2;
        } else {
            for (int k = 0; k < TLB_SETS; k++) send_buffer[j][k] = (k+1) % 2;
        }
    }

    // sender loop
    uint64_t addr = 0;
    uint8_t _; (void)_;
    int iterations = 0;
    int offset = 0;
    while (1) {
        // touch memory
        for (int set = 0; set < TLB_SETS; set++) {
            if (send_buffer[offset][set]) {
                for (int way = 0; way < 6; way++) {
                    addr = (BASE_ADDR | ((uint64_t)set << 12) | ((uint64_t)way << 28));
                    _ = *(uint8_t*)addr;
                }
            }
        }

        iterations++;
        if (iterations == 320) {
            offset = (offset + 1) % 128;
            iterations = 0;
        }
    }

    // cleanup
    munmap(mem, mem_bytes);
    return 0;
}