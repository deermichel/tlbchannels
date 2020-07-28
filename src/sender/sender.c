#include <fcntl.h>
#include <fec.h>
#include <math.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../crc.h"
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
                    TOUCH_MEMORY(ADDR(BASE_ADDR, set ^ (TLB_SETS / 2), way % TLB_WAYS)); // send to adjacent hyperthread
                    // TOUCH_MEMORY(ADDR(BASE_ADDR, set, way)); // send to same hyperthread
                }
            }
        }
    }
#ifdef RECORD_PACKETS
    packet->end = rdtsc();
    record_packet(packet); // logging
#endif
}

// send raw data
uint32_t send_data(const uint8_t *buffer, size_t length) {
    size_t num_bytes = length;

#ifdef REED_SOLOMON // use reed solomon
    // pack data into rs blocks
    uint32_t num_blocks = ceil(num_bytes / (double)RS_DATA_SYMBOLS);
    printf("preparing %d rs blocks for %ld bytes (%d bytes per block)\n", num_blocks, num_bytes, RS_DATA_SYMBOLS);
    uint8_t *rs_blocks = malloc(num_blocks * RS_TOTAL_SYMBOLS);
    if (rs_blocks == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    void *rs_codec = init_rs_char(8, 0x187, 112, 11, RS_PARITY_SYMBOLS, 0); // based on the CCSDS code
    for (int i = 0; i < num_blocks; i++) {
        uint8_t *current_block = &rs_blocks[i * RS_TOTAL_SYMBOLS];
        size_t tosend = (length > RS_DATA_SYMBOLS) ? RS_DATA_SYMBOLS : length;

        // copy data
        memcpy(current_block, buffer, tosend);
        buffer += tosend; length -= tosend;

        // encode
        encode_rs_char(rs_codec, current_block, &current_block[RS_DATA_SYMBOLS]);

        // debug
        if (args.verbose) {
            printf("\n[block %d]\n", i);
            for (int i = 0; i < RS_TOTAL_SYMBOLS; i++) {
                printf("%02x ", current_block[i]);
                if (i % 32 == 31) printf("\n");
            }
            printf("\n\n");
        }
    }
    free_rs_char(rs_codec);

    // pack rs blocks into packets
    uint32_t num_packets = RS_TOTAL_SYMBOLS * ceil(num_blocks / (double)PAYLOAD_SIZE);
    printf("sending %d packets for %d rs blocks\n", num_packets, num_blocks);
    packet_t packet;
    for (int i = 0; i < num_packets; i++) {
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

        // seq
        uint8_t seq = (i % RS_TOTAL_SYMBOLS) + 1; // 0x01 to 0xFF (255 symbols)
        packet.header[0] = seq;

        // checksum
#ifdef CHK_BERGER // berger codes
        packet.header[1] = 0xFF; // prevent overflow
        uint8_t zeros = 0;
        for (int i = 0; i < PACKET_SIZE / 8; i++) {
            zeros += _mm_popcnt_u64(~packet.raw64[i]);
        }
        packet.header[1] = zeros;
#elif defined(CHK_CRC8) // crc8
        packet.header[1] = crc8(packet.raw, PACKET_SIZE - 1);
#elif defined(CHK_CUSTOM) // custom xor
        packet.header[1] = ~(seq ^ packet.payload[0]);
#endif

        // debug
        if (args.verbose) {
            printf("[%d]\t", i);
            print_packet(&packet);
        }

        // send
        send_packet(&packet);
    }

    // cleanup
    free(rs_blocks);

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
        static uint8_t seq = 1;
        packet.header[0] = seq;
        if (++seq == 0xFF) seq = 1; // exclude 0x00 and 0xFF to differentiate between no send & tlb flush

        // checksum
#ifdef CHK_BERGER // berger codes
        packet.header[1] = 0xFF; // prevent overflow
        uint8_t zeros = 0;
        for (int i = 0; i < PACKET_SIZE / 8; i++) {
            zeros += _mm_popcnt_u64(~packet.raw64[i]);
        }
        packet.header[1] = zeros;
#elif defined(CHK_CRC8) // crc8
        packet.header[1] = crc8(packet.raw, PACKET_SIZE - 1);
#elif defined(CHK_CUSTOM) // custom xor
        packet.header[1] = ~(packet.header[0] ^ packet.payload[0]);
#endif

        // debug
        if (args.verbose) {
            printf("[%d]\t", i);
            print_packet(&packet);
        }

        // send
        send_packet(&packet);
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
#ifdef CHK_BERGER
        printf("checksum: berger codes\n");
#elif defined(CHK_CRC8)
        printf("checksum: crc8\n");
#elif defined(CHK_CUSTOM)
        printf("checksum: custom\n");
#endif
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