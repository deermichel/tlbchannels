#include <fcntl.h>
#include <fec.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../debug.h"
#include "../memory.h"
#include "../packet.h"
#include "cli.h"

// send packet
void send_packet(packet_t *packet) {
    // sender loop
#ifdef RECORD_PACKETS
    packet->start = rdtsc();
#endif
    for (int i = 0; i < args.window; i++) {
        for (int set = 0; set < TLB_SETS; set++) {
            if (packet->raw[set / 8] & (1 << (set % 8))) {
                // to send a 1-bit, we evict the whole stlb set by accessing addresses that fall into the respective 
                // stlb set.
                for (int way = 0; way < NUM_EVICTIONS; way++) {
                    TOUCH_MEMORY(ADDR(BASE_ADDR, set ^ (TLB_SETS / 2), way)); // send to adjacent hyperthread
                    // TOUCH_MEMORY(ADDR(BASE_ADDR, set, way)); // send to same hyperthread
                }
            }
        }
    }
#ifdef RECORD_PACKETS
    packet->end = rdtsc();
#endif
}

// send raw data
uint32_t send_data(const uint8_t *buffer, size_t length) {
    size_t num_bytes = length;

#ifdef REED_SOLOMON

#else // no reed solomon

    // pack data into packets
    uint32_t num_packets = ceil(num_bytes / (double)PAYLOAD_SIZE);
    printf("sending %d packets for %ld bytes (%d bytes per packet)\n", num_packets, num_bytes, PAYLOAD_SIZE);
    packet_t packet;
    for (int i = 0; i < num_packets; i++) {
        // prepare packet
        memset(packet.raw, 0x00, PACKET_SIZE);
        size_t tosend = (length > PAYLOAD_SIZE) ? PAYLOAD_SIZE : length;
        memcpy(packet.payload, buffer, tosend);
        buffer += tosend; length -= tosend;

        // seq

        // checksum

        // debug
        if (args.verbose) {
            printf("[%d]\t", i);
            print_packet(&packet);
        }

        // send and record
        send_packet(&packet);
#ifdef RECORD_PACKETS
        record_packet(&packet); // logging
#endif
    }

#endif // REED_SOLOMON

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
    uint32_t bytes_sent = send_data(buffer, st.st_size);

    // cleanup
    munlock(buffer, st.st_size);
    munmap(buffer, st.st_size);
    return bytes_sent;
}

// send string
uint32_t send_string(const char *string) {
    return send_data((uint8_t*)string, strlen(string));
}

// entry point
int main(int argc, char **argv) {
    // parse cli args
    argp_parse(&argp, argc, argv, 0, 0, 0);

    // build flags, params
    if (args.verbose) {
        printf("-------------------\n");
        printf("tlb sets: %d\n", TLB_SETS);
        printf("packet size: %d bytes (%d payload, %d header)\n", PACKET_SIZE, PAYLOAD_SIZE, HEADER_SIZE);
        printf("num evictions: %d\n", NUM_EVICTIONS);
        printf("num iterations: %d\n", args.window);
#ifdef REED_SOLOMON
        printf("reed solomon: %d parity bytes\n", RS_PARITY_SYMBOLS);
#endif
        printf("-------------------\n");
    }

    // allocate memory
    const size_t mem_size = ADDR(0, 0, NUM_EVICTIONS); // (wasteful, but maps to zero-page)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // touch pages to create pte, prevent page faults during sending (-> zero-page mapping)
    for (int set = 0; set < TLB_SETS; set++) {
        for (int way = 0; way < NUM_EVICTIONS; way++) {
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
    create_data_stop(&packet);
    for (int i = 0; i < 1000; i++) send_packet(&packet);

    // end logging
#ifdef RECORD_PACKETS
    record_packet(NULL);
#endif

    // stats
    double secs = end_time.tv_sec - start_time.tv_sec + (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000;
    printf("bytes sent: %d\n", bytes_sent);
    printf("time: %f s\n", secs);
    printf("bandwidth limit: %.3f kB/s\n", (bytes_sent / secs) / 1000.0);

    // cleanup
    dealloc_mem(mem, mem_size);
    return 0;
}