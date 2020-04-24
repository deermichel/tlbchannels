#include <time.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../debug.h"
#include "../memory.h"
#include "../packet.h"
#include "pteaccess/interface.h"
#include "cli.h"

// output buffer size (multiples of payload size)
#define BUFFER_SIZE 20000

// receive packet via accessed bits
void receive_packet_pteaccess(packet_t *packet) {
    // prepare packet - why? descheduled?
    memset(packet->raw, 0xFF, PACKET_SIZE);

    packet->start = rdtsc();

    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // get packet from access bits
    pread(pteaccess_fd, packet->raw64, PACKET_SIZE, 0);

    packet->end = rdtsc();
}

uint16_t evictions[128] = {0};

// receive packet via timestamps
void receive_packet_rdtsc(packet_t *packet) {
    // clear packet
    memset(packet->raw, 0x00, PACKET_SIZE);

    // get packet from access times
    // usleep(0);
    // printf("%d", probe(ADDR(BASE_ADDR, 0, 0)) > 110); 

    // count
    for (int i = 0; i < 8; i++) {
        // evictions[0] += (probe(ADDR(BASE_ADDR, 0, 0)) > 110 ? 1 : 0);
        for (int set = 0; set < 128; set++) {
            evictions[set] += (probe(ADDR(BASE_ADDR, set, 0)) > 55 ? 1 : 0);
        }
    }

    // evaluate
    // for (int set = 0; set < 8; set++) {
    //     printf("%d\t", evictions[set]);
    // }
    // printf("\n");

    // reset
    for (int set = 0; set < 128; set++) {
        packet->raw[set / 8] |= ((evictions[set] > 4 ? 1 : 0) << (set % 8));
        evictions[set] = 0;
    }

    // for (int i = 0; i < 3; i++) {

    //     packet->raw[0] |= (probe(ADDR(BASE_ADDR, 0, 0)) > 110 ? 1 : 0);
    //     for (int set = 1; set < 128; set++) {
    //         // usleep(0);
    //         // printf("%d", probe(ADDR(BASE_ADDR, set, 0)) > 55); // > 45); on bare
    //         packet->raw[set / 8] |= ((probe(ADDR(BASE_ADDR, set, 0)) > 55 ? 1 : 0) << (set % 8));
    //     }

    // }

    // for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet->raw[i]);
    // printf("\n");
}

// entry point
int main(int argc, char **argv) {
    // parse cli args
    argp_parse(&argp, argc, argv, 0, 0, 0);

    // allocate memory
    const size_t mem_size = ADDR(0, 0, 16); // to address all sets with up to 16 ways (wasteful!)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // configure kernel module, send vaddrs
    pteaccess_open();
    pteaccess_configure(TLB_SETS);
    for (int set = 0; set < TLB_SETS; set++) {
        pteaccess_set_addr(ADDR(BASE_ADDR, set, 0), set);
    }

    // open output file
    FILE *output = fopen(args.filename, "w");
    if (output == NULL) {
        printf("error opening output file '%s': %s\n", args.filename, strerror(errno));
        exit(1);
    }

    // output buffer
    uint8_t *buffer = malloc(BUFFER_SIZE * PAYLOAD_SIZE);
    uint32_t offset = 0;

    // receiver loop
    packet_t packet;
    uint32_t packets_received = 0;
    struct timespec first_packet_time, now;
    while (1) {
        receive_packet_pteaccess(&packet);

        // data stop
        if (packet.header[0] == 0xEE && packet.header[1] == 0xFF && packet.header[2] == 0xFF) break;

        // check header
        static uint8_t next_sqn = 0;
        uint8_t expected_header = 0xD0 | (next_sqn % 4);
        if (packet.header[0] != expected_header) {
            // printf("corrupt header - ");
            continue;
        }

        // checksum
        uint32_t checksum = _mm_crc32_u8(0, packet.header[0]);
        for (int i = 0; i < PAYLOAD_SIZE; i++) {
            checksum = _mm_crc32_u8(checksum, packet.payload[i]);
        }
        checksum >>= 16;
        if (memcmp(&checksum, &packet.header[1], sizeof(uint16_t)) != 0) {
            // printf("corrupt crc32: %0X - \n", checksum);
            continue;
        }

        // debug
        if (args.verbose) {
            printf("rcv: ");
            print_packet(&packet);
        }

        // all right!
        record_packet(&packet); // logging
        next_sqn++;

        // count packets
        packets_received++;
        if (packets_received == 1) clock_gettime(CLOCK_MONOTONIC, &first_packet_time);

        // save to buffer
        memcpy(buffer + offset, packet.payload, PAYLOAD_SIZE);
        offset += PAYLOAD_SIZE;

        // flush buffer
        if (offset == BUFFER_SIZE * PAYLOAD_SIZE) {
            fwrite(buffer, 1, offset, output);
            fflush(output);
            offset = 0;
            if (args.verbose) printf("buffer flushed\n");
        }
    }

    // flush buffer
    fwrite(buffer, 1, offset, output);
    fflush(output);

    // end logging
    record_packet(NULL);

    // stats
    clock_gettime(CLOCK_MONOTONIC, &now);
    double secs = now.tv_sec - first_packet_time.tv_sec + (double)(now.tv_nsec - first_packet_time.tv_nsec) / 1000000000;
    printf("received: %d (%d bytes)\n", packets_received, packets_received * PAYLOAD_SIZE);
    printf("bandwidth: %.3f kB/s\n", ((packets_received * PAYLOAD_SIZE) / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    fclose(output);
    dealloc_mem(mem, mem_size);
    pteaccess_close();
    return 0;
}