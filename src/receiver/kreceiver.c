#include <fec.h>
#include <time.h>
#include <x86intrin.h>
#include "../asm.h"
#include "../crc.h"
#include "../debug.h"
#include "../memory.h"
#include "../packet.h"
#include "ptreceiver/interface.h"
#include "cli.h"

#ifdef REED_SOLOMON
// decode and save rs blocks (returns index of last non-zero byte)
int decode_rs_blocks(uint8_t *rs_blocks, uint8_t *used_symbols, void *rs_codec, FILE *out, int *bytes_ok, int *bytes_corrected, int *bytes_corrupt) {
    int last_nonzero_byte = -1;
    for (int block = 0; block < PAYLOAD_SIZE; block++) {
        uint8_t *current_block = &rs_blocks[block * RS_TOTAL_SYMBOLS];

        // find erasures
        int eras_pos[RS_TOTAL_SYMBOLS] = {-1};
        int num_eras = 0;
        // printf("erasures: ");
        for (int i = 0; i < RS_TOTAL_SYMBOLS; i++) {
            if (used_symbols[i] == 0) {
                eras_pos[num_eras] = i;
                num_eras++;
                // printf("%d ", i);
            }
        } 
        // printf("(%d)\n", num_eras);

        // decode
        if (num_eras > RS_PARITY_SYMBOLS) num_eras = RS_PARITY_SYMBOLS; // necessary to prevent libfec segfaults
        int corrected_symbols = decode_rs_char(rs_codec, current_block, eras_pos, num_eras);

        // stats
        if (corrected_symbols == -1) {
            *bytes_corrupt += RS_DATA_SYMBOLS;
        } else {
            *bytes_ok += RS_DATA_SYMBOLS - corrected_symbols;
            *bytes_corrected += corrected_symbols;
        }

        // find last non-zero byte
        for (int i = 0; i < RS_DATA_SYMBOLS; i++) {
            if (current_block[i] != 0x00) last_nonzero_byte = RS_DATA_SYMBOLS * block + i;
        }

        // write to file
        fwrite(current_block, 1, RS_DATA_SYMBOLS, out);

        // debug
        if (args.verbose) {
            printf("\nblock %d (corrected symbols: %d):\n", block, corrected_symbols);
            for (int i = 0; i < RS_TOTAL_SYMBOLS; i++) {
                printf("%02x ", current_block[i]);
                if (i % 32 == 31) printf("\n");
            }
            printf("\n\n");
        }
    }
    return last_nonzero_byte;
}
#endif

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
#ifdef REED_SOLOMON
        printf("reed solomon: %d parity bytes\n", RS_PARITY_SYMBOLS);
#endif
        printf("-------------------\n");
    }

    // allocate memory
    const size_t mem_size = ADDR(0, 0, 1); // (wasteful, but maps to zero-page)
    uint8_t *mem = alloc_mem(BASE_ADDR, mem_size);

    // configure kernel module, send vaddrs
    ptreceiver_open();
    ptreceiver_configure(TLB_SETS);
    for (int set = 0; set < TLB_SETS; set++) {
        ptreceiver_set_addr(ADDR(BASE_ADDR, set, 0), set);
    }

    // open temp file
    FILE *temp_out = fopen("out.tmp", "w+");
    if (temp_out == NULL) {
        printf("error opening output file '%s': %s\n", "out.tmp", strerror(errno));
        exit(1);
    }

    // receiver loop
    uint8_t *buffer = ptreceiver_map_buffer();
    packet_t packet;
    uint32_t packets_received = 0;
    struct timespec first_packet_time, now;
    while (1) {
        int received = ptreceiver_fill_buffer();
        fwrite(buffer, 1, received * PACKET_SIZE, temp_out);
        fflush(temp_out);
        if (packets_received == 0) clock_gettime(CLOCK_MONOTONIC, &first_packet_time);
        packets_received += received;
        if (received < BUFFER_SIZE) break;
    }

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

#ifdef REED_SOLOMON // use reed solomon

    // pack packets into rs blocks
    uint8_t *rs_blocks = malloc(PAYLOAD_SIZE * RS_TOTAL_SYMBOLS);
    if (rs_blocks == (void*)-1) {
        printf("error allocating memory: %s\n", strerror(errno));
        exit(1);
    }
    memset(rs_blocks, 0x00, PAYLOAD_SIZE * RS_TOTAL_SYMBOLS);
    uint8_t last_seq = 0;
    int bytes_ok = 0; int bytes_corrected = 0; int bytes_corrupt = 0;
    uint8_t used_symbols[RS_TOTAL_SYMBOLS] = {0};
    void *rs_codec = init_rs_char(8, 0x187, 112, 11, RS_PARITY_SYMBOLS, 0);
    for (int i = 0; i < packets_received; i++) {
        memcpy(packet.raw, &packet_buffer[i * PACKET_SIZE], PACKET_SIZE);
        uint8_t seq = packet.header[0];

        // detect start of next set of blocks
        if (seq < 0x0F && last_seq > 0xF0) {
            decode_rs_blocks(rs_blocks, used_symbols, rs_codec, out, &bytes_ok, &bytes_corrected, &bytes_corrupt);

            // printf("- next set of blocks -\n");
            memset(rs_blocks, 0x00, PAYLOAD_SIZE * RS_TOTAL_SYMBOLS);
            memset(used_symbols, 0, RS_TOTAL_SYMBOLS);
        }
        last_seq = seq;

        // processing packet
        // printf("processing packet %d with seq %d\n", i, seq);
        // print_packet(&packet);
        int symbol = seq - 1;
        used_symbols[symbol] = 1;
        for (int block = 0; block < PAYLOAD_SIZE; block++) {
            rs_blocks[block * RS_TOTAL_SYMBOLS + symbol] = packet.payload[block];
        }
    }

    // finalize, truncate zero blocks
    int last_nonzero_byte = decode_rs_blocks(rs_blocks, used_symbols, rs_codec, out, &bytes_ok, &bytes_corrected, &bytes_corrupt);
    int trailing_zero_bytes = RS_DATA_SYMBOLS * PAYLOAD_SIZE - (last_nonzero_byte + 1);
    int length_without_trailing_zeros = ftell(out) - trailing_zero_bytes;
    fflush(out);
    ftruncate(fileno(out), length_without_trailing_zeros); // remove trailing zero bytes

    // rs stats
    int bytes_total = bytes_ok + bytes_corrected + bytes_corrupt;
    printf("bytes ok: %d (%.1f%%) | corrected: %d (%.1f%%) | corrupt: %d (%.1f%%) | total: %d (truncated: %d)\n", 
        bytes_ok, bytes_ok * 100.0 / bytes_total, 
        bytes_corrected, bytes_corrected * 100.0 / bytes_total, 
        bytes_corrupt, bytes_corrupt * 100.0 / bytes_total,
        bytes_total, length_without_trailing_zeros);

#else // no reed solomon

    // extract data from packets
    for (int i = 0; i < packets_received; i++) {
        memcpy(packet.raw, &packet_buffer[i * PACKET_SIZE], PACKET_SIZE);
        if (args.verbose) {
            printf("%d: ", i);
            print_packet(&packet);
        }
        fwrite(packet.payload, 1, PAYLOAD_SIZE, out);
    }
    int bytes_total = packets_received * PAYLOAD_SIZE;

#endif // REED_SOLOMON

    // cleanup
    fflush(out);
    fclose(out);
    fclose(temp_out);
    remove("out.tmp");

    // stats
    clock_gettime(CLOCK_MONOTONIC, &now);
    double secs = now.tv_sec - first_packet_time.tv_sec + (double)(now.tv_nsec - first_packet_time.tv_nsec) / 1000000000;
    printf("packets received: %d\n", packets_received);
    printf("bytes received: %d\n", bytes_total);
    printf("time: %f s\n", secs);
    printf("bandwidth: %.3f kB/s\n", (bytes_total / secs) / 1000.0);

    // cleanup
    dealloc_mem(mem, mem_size);
    ptreceiver_close(buffer);
    return 0;
}