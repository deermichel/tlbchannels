#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../memory.h"
#include "../packet.h"
#include "cli.h"

// send packet
size_t send_packet(const uint8_t *payload, size_t length) {
    // prepare packet
    size_t tosend = (length > PAYLOAD_SIZE) ? PAYLOAD_SIZE : length;
    packet_t packet;
    memset(packet.raw, 0xFF, PACKET_SIZE);

    // pack data
    if (payload == NULL) {
        packet.header[0] = 0xEE;
    } else {
        memcpy(packet.payload, payload, tosend);

        // header
        static uint8_t sqn = 0;
        packet.header[0] = 0xD0 | (sqn++ % 4);

        // crc32
        uint32_t checksum = _mm_crc32_u8(0, packet.header[0]);
        for (int i = 0; i < PAYLOAD_SIZE; i++) {
            checksum = _mm_crc32_u8(checksum, packet.payload[i]);
        }
        checksum >>= 16;
        memcpy(&packet.header[1], &checksum, sizeof(uint16_t));
    }

    // debug
    if (args.verbose) {
        printf("snd: ");
        for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet.raw[i]);
        printf("\n");
    }

    // sender loop
    for (int i = 0; i < args.window; i++) {
        for (int set = TLB_SETS - 1; set >= 0; set--) { // from payload to header -> ensure that payload is complete!
            if (packet.raw[set / 8] & (1 << (set % 8))) {
                // to send a 1-bit, we evict the whole stlb set by accessing addresses that fall into the respective 
                // stlb set. we access 16 addresses to ensure full eviction in dtlb + stlb.
                for (int way = 0; way < 16; way++) {
                    TOUCH_MEMORY(ADDR(BASE_ADDR, set ^ (TLB_SETS / 2), way));
                }
            }
        }
    }

    return tosend;
}

// send raw data
void send_data(const uint8_t *buffer, size_t length) {
    while (length > 0) {
        size_t sent = send_packet(buffer, length);
        buffer += sent;
        length -= sent;
    }
}

// send file
void send_file(const char* filename) {
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
    send_data(buffer, st.st_size);

    // cleanup
    munlock(buffer, st.st_size);
    munmap(buffer, st.st_size);
}

// send string
void send_string(const char *string) {
    send_data((uint8_t*)string, strlen(string));
}

// entry point
int main(int argc, char **argv) {
    // parse cli args
    argp_parse(&argp, argc, argv, 0, 0, 0);

    // allocate memory
    const size_t mem_size = ADDR(0, 0, 16); // to address all sets with up to 16 ways (wasteful!)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // send data
    switch (args.mode) {
        case MODE_SEND_STRING:
            send_string(args.string);
            break;
        case MODE_SEND_FILE:
            send_file(args.filename);
            break;
    }

    // send data stop
    for (int i = 0; i < 100; i++) send_packet(NULL, 0);

    // cleanup
    dealloc_mem(mem, mem_size);
    return 0;
}