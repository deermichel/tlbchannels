#define ARCH_BROADWELL

#include <stdio.h>
#include <x86intrin.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include "./asm.h"
#include "./crc.h"
#include "./packet.h"

#define ITER 9999

// #ifdef CHK_BERGER // berger codes
// #elif defined(CHK_CRC8) // crc8
//         packet.header[1] = crc8(packet.raw, PACKET_SIZE - 1);


int main() {
    struct timespec start_time, end_time;
    packet_t packet;

    uint64_t berger = 0;
    uint64_t crc = 0;
    uint64_t custom = 0;

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (int j = 0; j < (uint64_t)ITER; j++) {
        packet.raw[2]++;
        packet.raw[8]--;
        packet.raw[14]=packet.raw[2]*packet.raw[8];

        uint64_t start = rdtsc();

        packet.header[1] = 0xFF; // prevent overflow
        uint8_t zeros = 0;
        for (int i = 0; i < PACKET_SIZE / 8; i++) {
            zeros += _mm_popcnt_u64(~packet.raw64[i]);
        }
        packet.header[1] = zeros;

        berger += rdtsc() - start;
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("berger:\t%ld\t%ld\n", end_time.tv_nsec - start_time.tv_nsec, berger);

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (int j = 0; j < (uint64_t)ITER; j++) {
        packet.raw[2]++;
        packet.raw[8]--;
        packet.raw[14]=packet.raw[2]*packet.raw[8];

        uint64_t start = rdtsc();

        packet.header[1] = crc8(packet.raw, PACKET_SIZE - 1);

        crc += rdtsc() - start;
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("crc:\t%ld\t%ld\n", end_time.tv_nsec - start_time.tv_nsec, crc);

    clock_gettime(CLOCK_MONOTONIC, &start_time);
    for (int j = 0; j < (uint64_t)ITER; j++) {
        packet.raw[2]++;
        packet.raw[8]--;
        packet.raw[14]=packet.raw[2]*packet.raw[8];

        uint64_t start = rdtsc();

        packet.header[1] = ~(packet.header[0] ^ packet.payload[0]);

        custom += rdtsc() - start;
    }
    clock_gettime(CLOCK_MONOTONIC, &end_time);
    printf("custom:\t%ld\t%ld\n", end_time.tv_nsec - start_time.tv_nsec, custom);
}