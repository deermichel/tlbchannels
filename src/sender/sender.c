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
    // uint32_t packets_sent = send_data(buffer, st.st_size);
     uint32_t packets_sent = 0;

    // cleanup
    munlock(buffer, st.st_size);
    munmap(buffer, st.st_size);
    return packets_sent;
}

// send string
uint32_t send_string(const char *string) {
    // return send_data((uint8_t*)string, strlen(string));
    return 0;
}

// entry point
int main(int argc, char **argv) {
    // parse cli args
    argp_parse(&argp, argc, argv, 0, 0, 0);

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
    record_packet(NULL);

    // stats
    double secs = end_time.tv_sec - start_time.tv_sec + (double)(end_time.tv_nsec - start_time.tv_nsec) / 1000000000;
    printf("bandwidth limit: %.3f kB/s\n", (bytes_sent / secs) / 1000.0);
    printf("time: %f s\n", secs);

    // cleanup
    dealloc_mem(mem, mem_size);
    return 0;
}