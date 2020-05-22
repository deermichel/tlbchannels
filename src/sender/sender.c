#include <fcntl.h>
#include <fec.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../debug.h"
#include "../hamming.h"
#include "../memory.h"
#include "../packet.h"
#include "cli.h"

// send packet
void send_packet(packet_t *packet) {
    // sender loop
    packet->start = rdtsc();
    for (int i = 0; i < args.window; i++) {
        for (int set = 0; set < TLB_SETS; set++) {
            if (packet->raw[set / 8] & (1 << (set % 8))) {
                // to send a 1-bit, we evict the whole stlb set by accessing addresses that fall into the respective 
                // stlb set. we access 16 addresses to ensure full eviction in dtlb + stlb.
                for (int way = 0; way < 6; way++) { // TODO: WHY 17?
                    TOUCH_MEMORY(ADDR(BASE_ADDR, set ^ (TLB_SETS / 2), way % 16));
                }
            }
        }
    }
    packet->end = rdtsc();
}

// send raw data
uint32_t send_data(const uint8_t *buffer, size_t length) {
    // pack data into rs blocks
    size_t num_bytes = length;
    uint32_t num_blocks = ceil(num_bytes / (double)RS_DATA_SYMBOLS);
    printf("preparing %d rs blocks for %ld bytes (%d bytes per block)\n", num_blocks, num_bytes, RS_DATA_SYMBOLS);
    uint8_t *rs_blocks = malloc(num_blocks * RS_TOTAL_SYMBOLS);
    if (rs_blocks == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    for (int i = 0; i < num_blocks; i++) {
        uint8_t *current_block = &rs_blocks[i * RS_TOTAL_SYMBOLS];
        size_t tosend = (length > RS_DATA_SYMBOLS) ? RS_DATA_SYMBOLS : length;

        // copy data
        memcpy(current_block, buffer, tosend);
        buffer += tosend; length -= tosend;

        // encode
        encode_rs_8(current_block, &current_block[RS_DATA_SYMBOLS], 0);

        // debug
        // printf("block %d:\n", i);
        // for (int i = 0; i < RS_TOTAL_SYMBOLS; i++) {
        //     printf("%02x ", current_block[i]);
        //     if (i % 32 == 31) printf("\n");
        // }
        // printf("\n\n");
    }

    // pack rs blocks into packets
    uint32_t num_packets = RS_TOTAL_SYMBOLS * ceil(num_blocks / (double)PAYLOAD_SIZE);
    printf("sending %d packets for %d rs blocks\n", num_packets, num_blocks);
    for (int i = 0; i < num_packets; i++) {
        packet_t packet;
        memset(packet.raw, 0x00, PACKET_SIZE);

        // copy data
        for (int j = 0; j < PAYLOAD_SIZE; j++) {
            int block = (i / RS_TOTAL_SYMBOLS) * PAYLOAD_SIZE + j;
            int symbol = i % RS_TOTAL_SYMBOLS;
            // printf("packet %d, payload %d, block %d, symbol %d\n", i, j, block, symbol);

            if (block < num_blocks) {
                packet.payload[j] = rs_blocks[block * RS_TOTAL_SYMBOLS + symbol];
            }
        }

        // seq num (hamming-8,4 encoded) - how to avoid problem with all 0x00 or 0xFF?
        uint8_t seq = (i % RS_TOTAL_SYMBOLS) + 1; // 0x01 to 0xFF (255 symbols)
        // packet.header[0] = ENCODES[seq >> 4]; // 4 msb
        // packet.header[1] = ENCODES[seq & 0x0F]; // 4 lsb
        packet.header[0] = seq;
        packet.header[1] = ~(seq ^ packet.payload[0]);

        // checksum (berger codes)
        // packet.header[1] = 0xFF;
        // uint8_t zeros = 0;
        // for (int i = 0; i < PACKET_SIZE / 8; i++) {
        //     zeros += _mm_popcnt_u64(~packet.raw64[i]);
        // }
        // packet.header[1] = zeros;

        // debug
        if (args.verbose) {
            printf("%d:\t", i);
            print_packet(&packet);
        }

        // send packet
        send_packet(&packet);
        record_packet(&packet); // logging

        // printf("%d:\t", i);
        // print_packet(&packet);
    }

    // cleanup
    free(rs_blocks);
    return num_bytes;
}

// send file
uint32_t send_file(const char* filename) {
    // open file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        printf("error opening file '%s': %s\n", filename, strerror(errno));
        exit(1);
    }

    // get file size
    struct stat st;
    if (fstat(fd, &st) == -1) {
        printf("error getting file size for '%s': %s\n", filename, strerror(errno));
        exit(1);
    }

    // map file into memory
    uint8_t *buffer = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);
    if (buffer == (void*)-1) {
        printf("error creating file mapping for '%s': %s\n", filename, strerror(errno));
        exit(1);
    }

    // lock pages in RAM (prevent page faults)
    mlock(buffer, st.st_size);

    // send data
    uint32_t packets_sent = send_data(buffer, st.st_size);

    // cleanup
    munlock(buffer, st.st_size);
    munmap(buffer, st.st_size);
    return packets_sent;
}

// send string
uint32_t send_string(const char *string) {
    return send_data((uint8_t*)string, strlen(string));
}

// entry point
int main(int argc, char **argv) {
    // parse cli args
    argp_parse(&argp, argc, argv, 0, 0, 0);

    // allocate memory
    const size_t mem_size = ADDR(0, 0, 16); // to address all sets with up to 16 ways (wasteful!)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // touch pages to create pte, prevent page faults during sending (-> zero-page mapping)
    for (int set = 0; set < TLB_SETS; set++) {
        for (int way = 0; way < 16; way++) {
            TOUCH_MEMORY(ADDR(BASE_ADDR, set, way));
        }
    }

    // start time
    struct timespec start_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    // send data
    uint32_t bytes_sent = 0;
    switch (args.mode) {
        case MODE_SEND_STRING:
            bytes_sent = send_string(args.string);
            break;
        case MODE_SEND_FILE:
            bytes_sent = send_file(args.filename);
            break;
    }

    // end time
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // send data stop
    packet_t packet;
    memset(packet.raw, 0xFF, PACKET_SIZE);
    packet.header[0] = 0x00; // invalid seq -> won't be included in data
    packet.header[1] = 0xEE;
    for (int i = 0; i < 1000; i++) send_packet(&packet);

    // end logging
    record_packet(NULL);

    // stats
    double secs = end_time.tv_sec - start_time.tv_sec + (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000;
    printf("bandwidth limit: %.3f kB/s\n", (bytes_sent / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    dealloc_mem(mem, mem_size);
    return 0;
}