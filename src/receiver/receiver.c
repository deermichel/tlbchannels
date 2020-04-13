#include <fcntl.h>
// #include <sys/stat.h>
#include <unistd.h>
#include "../asm.h"
#include "../memory.h"
#include "../packet.h"
#include "cli.h"

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

// receive packet
void receive_packet(packet_t *packet) {
    // prepare packet
    memset(packet->raw, 0xFF, PACKET_SIZE);

    // receiver loop
    // for (int i = 0; i < args.window; i++) {
    // }

    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // usleep(100); when on same thead!

    // get packet from access bits
    pread(pteaccess_fd, packet->raw64, PACKET_SIZE, 0);
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
    // FILE *output = fopen(args.filename, "w");
    // if (output == NULL) {
    //     printf("error opening output file '%s': %s\n", args.filename, strerror(errno));
    //     exit(1);
    // }

    // receiver loop
    packet_t packet;
    while (1) {
        receive_packet(&packet);

        // data stop
        if (packet.header[0] == 0xEE) break;

        // check header
        static uint8_t next_sqn = 0;
        uint8_t expected_header = 0xD0 | (next_sqn & 0x0F);
        if (packet.header[0] != expected_header) continue;
        next_sqn++;

        // debug
        if (args.verbose) {
            printf("rcv: ");
            for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet.raw[i]);
            printf("\n");
        }
    }

    // cleanup
    dealloc_mem(mem, mem_size);
    close(pteaccess_fd);
    return 0;
}