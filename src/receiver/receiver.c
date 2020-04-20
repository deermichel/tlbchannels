#include <fcntl.h>
#include <unistd.h>
#include "../asm.h"
#include "../memory.h"
#include "../packet.h"
#include "cli.h"

// output buffer size (multiples of payload size)
#define BUFFER_SIZE 20000

// file descriptor of kernel module proc file
static int pteaccess_fd = -1;

// open kernel module proc file
void open_pteaccess() {
    pteaccess_fd = open("/proc/pteaccess", O_RDWR);
    if (pteaccess_fd == -1) {
        printf("error opening kernel interface '/proc/pteaccess': %s\n", strerror(errno));
        exit(1);
    }
}

// receive packet via accessed bits
void receive_packet_pteaccess(packet_t *packet) {
    // prepare packet - why? descheduled?
    memset(packet->raw, 0xFF, PACKET_SIZE);

    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // get packet from access bits
    pread(pteaccess_fd, packet->raw64, PACKET_SIZE, 0);
}

// receive packet via timestamps
void receive_packet_rdtsc(packet_t *packet) {
    // clear packet
    memset(packet->raw, 0x00, PACKET_SIZE);

    // get packet from access times
    for (int set = 0; set < 128; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
        usleep(0);
        packet->raw[set / 8] |= ((probe(ADDR(BASE_ADDR, set, 0)) > 140 ? 1 : 0) << (set % 8));
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
    open_pteaccess();
    addr_t vaddr = 0x0; // to configure use address 0x0
    pwrite(pteaccess_fd, (void*)&vaddr, sizeof(addr_t), TLB_SETS); // number of vaddrs
    for (int set = 0; set < TLB_SETS; set++) {
        vaddr = ADDR(BASE_ADDR, set, 0);
        TOUCH_MEMORY(vaddr); // touch pages to create pte (-> zero-page mapping)
        pwrite(pteaccess_fd, (void*)&vaddr, sizeof(addr_t), set);
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
    while (1) {
        receive_packet_rdtsc(&packet);

        // data stop
        if (packet.header[0] == 0xEE && packet.header[1] == 0xFF) break;

        // check header
        static uint8_t next_sqn = 0;
        uint8_t expected_header = 0xD0 | (next_sqn % 4);
        if (packet.header[0] != expected_header) continue;
        next_sqn++;

        // debug
        if (args.verbose) {
            printf("rcv: ");
            for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet.raw[i]);
            printf("\n");
        }

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

    // cleanup
    fclose(output);
    dealloc_mem(mem, mem_size);
    close(pteaccess_fd);
    return 0;
}