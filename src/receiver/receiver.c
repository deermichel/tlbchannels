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
        // if (packet.header[0] == 0xEE && packet.header[1] == 0xFF && packet.header[2] == 0xFF) break;
        // static uint8_t endcount = 0;
        // if (packet.header[0] == 0xEE && packet.payload[0] == 0xFF && packet.payload[1] == 0xFF) {
        //     endcount++;
        //     if (endcount == 20) {
        //         break;
        //     }
        // };

        // if (packet.payload[0] == 0) continue;

        // checksum
        // uint8_t should = packet.header[0] >> 1;

        // packet.header[0] = packet.header[0] & 0x01;
        // uint8_t zeros = 0;
        // for (int i = 0; i < PACKET_SIZE / 8; i++) {
        //     zeros += _mm_popcnt_u64(~packet.raw64[i]);
        // }
        // if (zeros != should) {
        //     // printf("corrupt chksum -\n\n");
        //     continue;
        // }

        // seq
        // uint8_t seq = packet.header[0] & 0x01;
        // packet.header[0] |= (zeros << 1);
        // static uint8_t last_seq = (uint8_t)-1;
        // if (seq == last_seq) {
        //     // printf("same seq -\n\n");
        //     continue;
        // }
        // last_seq = seq;

        // check header
        // if ((packet.header[0] & 0xF0) != 0x60) {
        //     // printf("corrupt header -\n\n");
        //     continue;
        // }
        // uint8_t seq = (packet.header[0] & 0x01);
        // static uint8_t last_seq = (uint8_t)-1;
        // if (seq == last_seq) {
        //     // printf("same seq -\n\n");
        //     continue;
        // }
        // last_seq = seq;

        // static uint8_t next_sqn = 0;
        // uint8_t expected_header = 0xD0 | (next_sqn % 4);
        // if (packet.header[0] != expected_header) {
        //     // printf("corrupt header - ");
        //     continue;
        // }
        // printf("rcv: ");
        // print_packet(&packet);

        // checksum
        // uint32_t checksum = _mm_crc32_u8(0, packet.header[0]);
        // for (int i = 0; i < PAYLOAD_SIZE; i++) {
        //     checksum = _mm_crc32_u8(checksum, packet.payload[i]);
        // }
        // checksum >>= 16;
        // if (memcmp(&checksum, &packet.header[1], sizeof(uint16_t)) != 0) {
        //     // printf("corrupt crc32: %0X\n\n", checksum);
        //     continue;
        // }

        // check seq
        // static uint8_t last_seq = (uint8_t)-1;
        // if ((packet.header[0] & 0xF0) != 0xD0) {
        //     // printf("corrupt header -\n\n");
        //     continue;
        // }
        // // if (packet.header[0] == 0) continue;
        // if ((packet.header[0] & 0x0F) == last_seq) {
        //     // printf("same seq -\n\n");
        //     continue;
        // }
        // last_seq = packet.header[0] & 0x0F;

        // debug
        // if (args.verbose) {
        //     printf("brcv: ");
        //     print_packet(&packet);
        // }

        // hamming
        if (decode_8_4(&packet) == 0) continue;
        // record_packet(&packet); // logging

        // data stop
        static uint8_t endcount = 0;
        if (packet.header[0] == 0xEE && packet.payload[0] == 0xFF && packet.payload[1] == 0xFF) {
            endcount++;
            if (endcount == 20) {
                break;
            }
        };

        // seq
        if (packet.header[0] != 0x01 && packet.header[0] != 0x00) continue; // header
        uint8_t seq = packet.header[0] & 0x01;
        static uint8_t last_seq = (uint8_t)-1;
        if (seq == last_seq) {
            // printf("same seq -\n\n");
            continue;
        }
        last_seq = seq;

        // all right!
        record_packet(&packet); // logging
        // next_sqn++;

        // debug
        if (args.verbose) {
            printf("arcv: ");
            print_packet(&packet);
            printf("\n");
        }

        // count packets
        packets_received++;
        if (packets_received == 1) clock_gettime(CLOCK_MONOTONIC, &first_packet_time);
        // if (packets_received == 200) break;

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
    printf("packets received: %d (%d bytes)\n", packets_received, packets_received * PAYLOAD_SIZE);
    printf("bandwidth: %.3f kB/s\n", ((packets_received * PAYLOAD_SIZE) / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    fclose(output);
    dealloc_mem(mem, mem_size);
    pteaccess_close();
    return 0;
}