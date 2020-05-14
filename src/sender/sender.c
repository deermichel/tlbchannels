#include <fcntl.h>
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
size_t send_packet(const uint8_t *payload, size_t length) {
    // prepare packet
    size_t tosend = (length > PAYLOAD_SIZE) ? PAYLOAD_SIZE : length;
    packet_t packet, tempacket;
    memset(packet.raw, 0xFF, PACKET_SIZE);

    // pack data
    if (payload == NULL) {
        packet.header[0] = 0xEE;
    } else {
        memcpy(packet.payload, payload, tosend);

        // header
        static uint8_t sqn = 0;
        packet.header[0] = (sqn++ % 2);

        // checksum
        // print_packet(&packet);
        uint8_t zeros = _mm_popcnt_u64(~packet.raw64[0]) + _mm_popcnt_u64(~packet.raw64[1]);
        packet.header[0] |= (zeros << 1);
    }

    // debug before
    if (args.verbose && payload != NULL) {
        printf("bsnd: ");
        print_packet(&packet);
    }

    // hamming
    tempacket = packet;
    // encode_8_4(&packet);

    // debug after
    if (args.verbose && payload != NULL) {
        printf("asnd: ");
        print_packet(&packet);
        printf("\n");
    }

    // sender loop
    tempacket.start = rdtsc();
    for (int i = 0; i < args.window; i++) {
        for (int set = 0; set < TLB_SETS; set++) {
            if (packet.raw[set / 8] & (1 << (set % 8))) {
                // to send a 1-bit, we evict the whole stlb set by accessing addresses that fall into the respective 
                // stlb set. we access 16 addresses to ensure full eviction in dtlb + stlb.
                for (int way = 0; way < 17; way++) { // TODO: WHY 17?
                    TOUCH_MEMORY(ADDR(BASE_ADDR, set ^ (TLB_SETS / 2), way % 16));
                }
            }
        }
    }
    tempacket.end = rdtsc();
    if (payload != NULL) record_packet(&tempacket); // logging

    return tosend;
}

// send raw data
uint32_t send_data(const uint8_t *buffer, size_t length) {
    uint32_t packets_sent = 0;
    while (length > 0) {
        size_t sent = send_packet(buffer, length);
        buffer += sent;
        length -= sent;
        packets_sent++;
    }
    return packets_sent;
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
    uint32_t packets_sent = 0;
    switch (args.mode) {
        case MODE_SEND_STRING:
            packets_sent = send_string(args.string);
            break;
        case MODE_SEND_FILE:
            packets_sent = send_file(args.filename);
            break;
    }

    // end time
    struct timespec end_time;
    clock_gettime(CLOCK_MONOTONIC, &end_time);

    // send data stop
    // send_packet(NULL, 0);
    for (int i = 0; i < 1000; i++) send_packet(NULL, 0);

    // end logging
    record_packet(NULL);

    // stats
    double secs = end_time.tv_sec - start_time.tv_sec + (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000;
    printf("packets sent: %d (%d bytes)\n", packets_sent, packets_sent * PAYLOAD_SIZE);
    printf("bandwidth limit: %.3f kB/s\n", ((packets_sent * PAYLOAD_SIZE) / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    dealloc_mem(mem, mem_size);
    return 0;
}