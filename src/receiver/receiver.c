#include <fec.h>
#include <time.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../debug.h"
#include "../hamming.h"
#include "../memory.h"
#include "../packet.h"
#include "pteaccess/interface.h"
#include "cli.h"

// output buffer size (multiples of payload size)
#define BUFFER_SIZE 20000

// receive packet via accessed bits
void receive_packet_pteaccess(packet_t *packet) {
    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // get packet from access bits
    pteaccess_get_bits(packet->raw, PACKET_SIZE);
}

// receive packet via timestamps
void receive_packet_rdtsc(packet_t *packet) {
    // probe and count evictions
    // int evictions[TLB_SETS] = {0};
    // for (int i = 0; i < args.window; i++) {
    //     for (int set = 0; set < TLB_SETS; set++) {
    //         evictions[set] += (probe(ADDR(BASE_ADDR, set, 0)) > args.rdtsc_threshold ? 1 : 0);
    //     }
    // }

    // evaluate and write packet
    // for (int set = 0; set < TLB_SETS; set++) {
    //     packet->raw[set / 8] |= ((evictions[set] >= args.window_threshold ? 1 : 0) << (set % 8));
    // }
    for (int i = 0; i < 2; i++) {
        for (int set = 0; set < TLB_SETS; set++) {
            packet->raw[set / 8] |= (probe(ADDR(BASE_ADDR, set, 0)) > args.rdtsc_threshold ? 1 : 0) << (set % 8);
        }
    }
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

    // open temp file
    FILE *temp_out = fopen("out.tmp", "w+");
    if (temp_out == NULL) {
        printf("error opening output file '%s': %s\n", "out.tmp", strerror(errno));
        exit(1);
    }

    // output buffer
    uint8_t *buffer = malloc(BUFFER_SIZE * PACKET_SIZE);
    uint32_t offset = 0;

    // receiver loop
    packet_t packet;
    uint32_t packets_received = 0;
    struct timespec first_packet_time, now;
    while (1) {
        // read raw packet
        memset(packet.raw, 0x00, PACKET_SIZE); // reset
        packet.start = rdtsc(); // log start tsc
        switch (args.mode) {
            case MODE_PROBE_PTEACCESS:
                receive_packet_pteaccess(&packet);
                break;
            case MODE_PROBE_RDTSC:
                receive_packet_rdtsc(&packet);
                break;
        }
        packet.end = rdtsc(); // log end tsc

        // data stop
        static uint8_t stop_count = 0;
        if (packet.header[0] == 0xEE && packet.header[1] == 0xEE && packet.payload[0] == 0xFF) {
            if (stop_count++ == 100) break;
        }

        // decode seq
        // uint8_t msb = DECODES[packet.header[0]];
        // if (msb == 0xFF) continue; // damaged seq
        // uint8_t lsb = DECODES[packet.header[1]];
        // if (lsb == 0xFF) continue; // damaged seq
        // uint8_t seq = (msb << 4) | lsb;
        // static uint8_t last_seq = (uint8_t)-1;
        // if (seq == last_seq || seq == 0) continue; // same or invalid seq
        // if (seq == 0xFF && packet.header[0] == 0xFF && packet.header[1] == 0xFF) continue; // special case, to exclude tlb flushes
        // last_seq = seq;

        // seq
        static uint8_t last_seq = (uint8_t)-1;
        uint8_t seq = packet.header[0];
        if (seq == 0 || (~seq & 0xFF) != packet.header[1] || seq == last_seq) continue; // same or invalid seq
        // printf("%02x \n", seq, ~seq);
        last_seq = seq;

        // all right!
        record_packet(&packet); // logging

        // debug
        if (args.verbose) {
            printf("rcv: ");
            print_packet(&packet);
        }

        // count packets
        packets_received++;
        if (packets_received == 1) clock_gettime(CLOCK_MONOTONIC, &first_packet_time);

        // save to buffer
        memcpy(buffer + offset, packet.raw, PACKET_SIZE);
        offset += PACKET_SIZE;

        // flush buffer
        if (offset == BUFFER_SIZE * PACKET_SIZE) {
            fwrite(buffer, 1, offset, temp_out);
            fflush(temp_out);
            offset = 0;
            if (args.verbose) printf("buffer flushed\n");
        }
    }

    // flush buffer
    fwrite(buffer, 1, offset, temp_out);
    fflush(temp_out);
    free(buffer);

    // end logging
    record_packet(NULL);

    // map tmp file into memory
    uint8_t *packet_buffer = mmap(NULL, ftell(temp_out), PROT_READ, MAP_PRIVATE | MAP_POPULATE, fileno(temp_out), 0);
    if (packet_buffer == (void*)-1) {
        printf("error creating file mapping for '%s': %s\n", "out.tmp", strerror(errno));
        exit(1);
    }

    // pack packets into rs blocks
    uint8_t *rs_blocks = malloc(PAYLOAD_SIZE * RS_TOTAL_SYMBOLS);
    if (rs_blocks == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    for (int i = 0; i < packets_received; i++) {
        memcpy(packet.raw, &packet_buffer[i * PACKET_SIZE], PACKET_SIZE);
        uint8_t seq = packet.header[0];

        // copy data
        for (int j = 0; j < PAYLOAD_SIZE; j++) {
            int block = j;
            int symbol = seq - 1;
            // printf("packet %d, payload %d, block %d, symbol %d\n", i, j, block, symbol);

            rs_blocks[block * RS_TOTAL_SYMBOLS + symbol] = packet.payload[j];

            // if (block < num_blocks) {
            //     packet.payload[j] = rs_blocks[block * RS_TOTAL_SYMBOLS + symbol];
            // }
        }
    }

    // decode and save rs blocks
    for (int i = 0; i < 30; i++) {
        uint8_t *current_block = &rs_blocks[i * RS_TOTAL_SYMBOLS];

        // decode
        int result = decode_rs_8(current_block, NULL, 0, 0);

        // debug
        printf("block %d (decoding result: %d):\n", i, result);
        for (int i = 0; i < RS_TOTAL_SYMBOLS; i++) {
            printf("%02x ", current_block[i]);
            if (i % 32 == 31) printf("\n");
        }
        printf("\n\n");
    }

    free(rs_blocks);

    // stats
    clock_gettime(CLOCK_MONOTONIC, &now);
    double secs = now.tv_sec - first_packet_time.tv_sec + (double)(now.tv_nsec - first_packet_time.tv_nsec) / 1000000000;
    printf("packets received: %d (%d bytes)\n", packets_received, packets_received * PAYLOAD_SIZE);
    printf("bandwidth: %.3f kB/s\n", ((packets_received * PAYLOAD_SIZE) / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    fclose(temp_out);
    dealloc_mem(mem, mem_size);
    pteaccess_close();
    return 0;
}