#include <fec.h>
#include <time.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../crc.h"
#include "../debug.h"
#include "../memory.h"
#include "../packet.h"
#include "pteaccess/interface.h"
#include "cli.h"

// output buffer size
#define BUFFER_SIZE 20000

// receive packet
void receive_packet(packet_t *packet) {
#ifdef RDTSC_WINDOW // via timestamps

    for (int i = 0; i < RDTSC_WINDOW; i++) {
        for (int set = 0; set < TLB_SETS; set++) {
            packet->raw[set / 8] |= (probe(ADDR(BASE_ADDR, set, 0)) > RDTSC_THRESHOLD ? 1 : 0) << (set % 8);
        }
    }

#else // via accessed bits

    // touch pages to create tlb entries
    for (int set = 0; set < TLB_SETS; set++) {
        TOUCH_MEMORY(ADDR(BASE_ADDR, set, 0));
    }

    // get packet from access bits
    pteaccess_get_bits(packet->raw, PACKET_SIZE);

#endif
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
#ifdef CHK_BERGER
        printf("checksum: berger codes\n");
#elif defined(CHK_CRC8)
        printf("checksum: crc8\n");
#elif defined(CHK_CUSTOM)
        printf("checksum: custom\n");
#endif
#ifdef RDTSC_WINDOW
        printf("rdtsc window: %d\n", RDTSC_WINDOW);
        printf("rdtsc threshold: %d\n", RDTSC_THRESHOLD);
#endif
        printf("-------------------\n");
    }

    // allocate memory
    const size_t mem_size = ADDR(0, 0, 1); // (wasteful, but maps to zero-page)
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
#ifdef RECORD_PACKETS
        packet.start = rdtsc(); // log start tsc
#endif
        receive_packet(&packet);
#ifdef RECORD_PACKETS
        packet.end = rdtsc(); // log end tsc
#endif

        // data stop
        static uint8_t stop_count = 0;
        if (is_data_stop(&packet) && stop_count++ == 100) break;

        // checksum
#ifdef CHK_BERGER // berger codes
        uint8_t should = packet.header[1];
        packet.header[1] = 0xFF;
        uint8_t zeros = 0;
        for (int i = 0; i < PACKET_SIZE / 8; i++) {
            zeros += _mm_popcnt_u64(~packet.raw64[i]);
        }
        if (zeros != should) continue; // invalid chksum
        packet.header[1] = should;
#elif defined(CHK_CRC8) // crc8
        if (crc8(packet.raw, PACKET_SIZE) != 0) continue;
#elif defined(CHK_CUSTOM) // custom xor
        if (packet.header[1] != (~(packet.header[0] ^ packet.payload[0]) & 0xFF)) continue;
#endif

        // seq
        static uint8_t last_seq = 0xFF;
        uint8_t seq = packet.header[0];
        // skip presumably tlb flushes (0xFF) (we can afford dropping packets with rs)
        if (seq == 0x00 || seq == 0xFF || seq == last_seq) continue; // same or invalid seq
        last_seq = seq;

        // all right!
#ifdef RECORD_PACKETS
        record_packet(&packet); // logging
#endif

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
#ifdef RECORD_PACKETS
    record_packet(NULL);
#endif

    // map tmp file into memory
    uint8_t *packet_buffer = mmap(NULL, ftell(temp_out), PROT_READ, MAP_PRIVATE | MAP_POPULATE, fileno(temp_out), 0);
    if (packet_buffer == (void*)-1) {
        printf("error creating file mapping for '%s': %s\n", "out.tmp", strerror(errno));
        exit(1);
    }

    // open out file
    FILE *out = fopen(args.filename, "w");
    if (out == NULL) {
        printf("error opening output file '%s': %s\n", args.filename, strerror(errno));
        exit(1);
    }

    // extract data from packets
    for (int i = 0; i < packets_received; i++) {
        memcpy(packet.raw, &packet_buffer[i * PACKET_SIZE], PACKET_SIZE);
        fwrite(packet.payload, 1, PAYLOAD_SIZE, out);
    }

    // finalize, cleanup
    // int last_nonzero_byte = decode_rs_blocks(rs_blocks, used_symbols, rs_codec, out, &bytes_ok, &bytes_corrected, &bytes_corrupt);
    // int trailing_zero_bytes = RS_DATA_SYMBOLS * PAYLOAD_SIZE - (last_nonzero_byte + 1);
    // int length_without_trailing_zeros = ftell(out) - trailing_zero_bytes;
    fflush(out);
    // ftruncate(fileno(out), length_without_trailing_zeros); // remove trailing zero bytes
    fclose(out);
    fclose(temp_out);
    remove("out.tmp");

    // stats
    clock_gettime(CLOCK_MONOTONIC, &now);
    double secs = now.tv_sec - first_packet_time.tv_sec + (double)(now.tv_nsec - first_packet_time.tv_nsec) / 1000000000;
    printf("packets received: %d\n", packets_received);
    // int bytes_total = bytes_ok + bytes_corrected + bytes_corrupt;
    // printf("bytes ok: %d (%.1f%%) | corrected: %d (%.1f%%) | corrupt: %d (%.1f%%) | total: %d (truncated: %d)\n", 
    //     bytes_ok, bytes_ok * 100.0 / bytes_total, 
    //     bytes_corrected, bytes_corrected * 100.0 / bytes_total, 
    //     bytes_corrupt, bytes_corrupt * 100.0 / bytes_total,
    //     bytes_total, length_without_trailing_zeros);
    // printf("bandwidth: %.3f kB/s\n", (bytes_total / secs) / 1000.0);
    printf("bytes received: %d\n", packets_received * PAYLOAD_SIZE);
    printf("time: %f s\n", secs);
    printf("bandwidth: %.3f kB/s\n", (packets_received * PAYLOAD_SIZE / secs) / 1000.0);

    // cleanup
    dealloc_mem(mem, mem_size);
    pteaccess_close();
    return 0;
}