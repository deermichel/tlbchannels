#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../arch.h"

// base address for memory allocation
#define BASE_ADDR 0x100000000000

// receiver iterations
#define RECV_ITERATIONS 200

// file descriptor of kernel module proc file
static int pteaccess_fd = -1;

// open kernel module proc file
void open_pteaccess() {
    pteaccess_fd = open("/proc/pteaccess", O_RDWR);
    if (pteaccess_fd < 0) {
        printf("error opening '/proc/pteaccess': %s\n", strerror(errno));
        exit(1);
    }
}

// entry point
int main(int argc, char **argv) {
    open_pteaccess();

    // allocate memory
    // uint64_t mem_bytes = 4096 * TLB_SETS; // TLB_SETS 4k pages
    uint64_t mem_bytes = 1024L * 1024 * 256 * 6; // enough 4k pages, 1MB * 256sets * 6ways
    uint8_t *mem = mmap((void*)BASE_ADDR, mem_bytes, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (mem == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    
    // send vaddrs to kernel module
    uint64_t addr = 0;
    uint8_t _; (void)_;
    pwrite(pteaccess_fd, (void*)&addr, sizeof(uint64_t), TLB_WAYS * TLB_SETS); // configure
    for (int way = 0; way < TLB_WAYS; way++) {
        for (int set = 0; set < TLB_SETS; set++) {
            addr = (BASE_ADDR | ((uint64_t)set << 12) | ((uint64_t)way << 28));
            _ = *(uint8_t*)addr; // touch memory to create pte (-> zero-page mapping)
            pwrite(pteaccess_fd, (void*)&addr, sizeof(uint64_t), set + TLB_SETS * way);
        }
    }

    // receiver loop
    uint8_t bits[TLB_WAYS * TLB_SETS / 8] = {0};
    uint8_t recv_buffer[RECV_ITERATIONS][TLB_SETS] = {0};
    int evictions[TLB_SETS] = {0};
    // int histogram[TLB_WAYS + 1] = {0};
    int iteration = 0;
    printf("start receiving\n");
    while (iteration < RECV_ITERATIONS) {
        // touch memory
        for (int way = 0; way < TLB_WAYS; way++) {
            for (int set = 0; set < TLB_SETS; set++) {
                addr = (BASE_ADDR | ((uint64_t)set << 12) | ((uint64_t)way << 28));
                _ = *(uint8_t*)addr;
            }
        }

        // get access bits and count tlb evictions (1 byte contains 8 access bits / sets)
        pread(pteaccess_fd, bits, TLB_WAYS * TLB_SETS / 8, 0);
        for (int i = 0; i < TLB_WAYS * TLB_SETS; i++) {
            if (bits[i / 8] & (1 << (i % 8))) evictions[i % TLB_SETS]++;
        }

        // save in buffer
        for (int i = 0; i < TLB_SETS; i++) {
            printf("%d", evictions[i] == TLB_WAYS);
            recv_buffer[iteration][i] = evictions[i];
            evictions[i] = 0;
        }
        printf("\n\n");

        // iteration++;

        // print
        // if (iteration == 1) {
        //     for (int i = 0; i < TLB_SETS; i++) {
        //         printf("%d", evictions[i] == TLB_WAYS);
        //         evictions[i] = 0;
        //     }
        //     printf("\n\n");
        //     iteration = 0;

            // for (int i = 0; i < TLB_SETS; i++) {
            //     // printf("%d\t", evictions[i]);
            //     histogram[evictions[i]]++;
            //     evictions[i] = 0;
            // }
            // for (int i = 0; i <= TLB_WAYS; i++) {
            //     printf("%d: %d\t", i, histogram[i]);
            //     histogram[i] = 0;
            // }
            // printf("\n");
            // iteration = 0;
        // }
        // break;
    }
    printf("stop receiving\n");

    // write buffer to file
    FILE *file = fopen("output.txt", "w");
    for (int iter = 0; iter < RECV_ITERATIONS; iter++) {
        for (int i = 0; i < TLB_SETS; i++) {
            fprintf(file, "%d", recv_buffer[iter][i] == TLB_WAYS);
        }
        fprintf(file, "\n");
    }
    fclose(file);

    // cleanup
    munmap(mem, mem_bytes);
    close(pteaccess_fd);
    return 0;
}