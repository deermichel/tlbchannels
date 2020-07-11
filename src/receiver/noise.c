// analysis: noise measurement

#include <time.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../debug.h"
#include "../memory.h"
#include "../packet.h"
#include "pteaccess/interface.h"

// receive packet
void receive_packet(packet_t *packet) {
    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // get packet from access bits
    pteaccess_get_bits(packet->raw, PACKET_SIZE);
}

// entry point
int main(int argc, char **argv) {
    // allocate memory
    const size_t mem_size = ADDR(0, 0, 1); // (wasteful, but maps to zero-page)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // configure kernel module, send vaddrs
    pteaccess_open();
    pteaccess_configure(TLB_SETS);
    for (int set = 0; set < TLB_SETS; set++) {
        pteaccess_set_addr(ADDR(BASE_ADDR, set, 0), set);
    }

    // receiver loop
    packet_t packet;
    int evictions[TLB_SETS] = {0};
    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (1) {
        // read raw packet
        memset(packet.raw, 0x00, PACKET_SIZE); // reset
        receive_packet(&packet);

        // count
        for (int i = 0; i < TLB_SETS; i++) {
            if (packet.raw[i / 8] & (1 << (i % 8))) {
                evictions[i]++;
            }
        }

        // time
        clock_gettime(CLOCK_MONOTONIC, &now);
        double secs = now.tv_sec - start.tv_sec + (double)(now.tv_nsec - start.tv_nsec) / 1000000000;
        if (secs > atoi(argv[1])) break;
    }

    // print result
    for (int i = 0; i < TLB_SETS; i++) {
        // printf("%d\t", evictions[i]);
        // if (i % 16 == 15) printf("\n");
        printf("%d,", evictions[i]);
    }
    printf("\n");

    // cleanup
    dealloc_mem(mem, mem_size);
    pteaccess_close();
    return 0;
}