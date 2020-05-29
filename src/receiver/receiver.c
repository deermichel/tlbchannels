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
#ifdef RDTSC_WINDOW
        printf("rdtsc window: %d\n", RDTSC_WINDOW);
        printf("rdtsc threshold: %d\n", RDTSC_THRESHOLD);
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

        // skip presumably tlb flushes (0xFF) (rs requires either 0x00 or 0xFF seq - but we can afford dropping some packets)
#ifdef REED_SOLOMON
        int ones = 0;
        for (int i = 0; i < PACKET_SIZE / 8; i++) {
            ones += _mm_popcnt_u64(packet.raw64[i]);
        }
        if (ones == PACKET_SIZE * 8) continue;
#endif

        // seq
        static uint8_t last_seq = 0xFF;
        uint8_t seq = packet.header[0];
#ifdef REED_SOLOMON
        if (seq == 0x00 || seq == last_seq) continue; // same or invalid seq
#else
        if (seq == 0x00 || seq == 0xFF || seq == last_seq) continue; // same or invalid seq
#endif
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
    pteaccess_close();
    return 0;
}