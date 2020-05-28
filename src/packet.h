// packet definition, helper methods

#ifndef PACKET_H
#define PACKET_H

#include "arch.h"

// packet size (bytes)
#ifndef PACKET_SIZE
#define PACKET_SIZE (TLB_SETS / 8)
#endif

// header size (bytes)
#define HEADER_SIZE 2

// payload size (bytes)
#define PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)

// reed solomon config
#ifdef REED_SOLOMON
#define RS_TOTAL_SYMBOLS 255 // symbol size of 8 bit (1 byte) -> 2^8-1 = 255
#define RS_PARITY_SYMBOLS REED_SOLOMON
#define RS_DATA_SYMBOLS (RS_TOTAL_SYMBOLS - RS_PARITY_SYMBOLS)
#endif

// packet definition
typedef struct {
    // data
    union {
        struct {
            uint8_t payload[PAYLOAD_SIZE];
            uint8_t header[HEADER_SIZE];
        };
        uint8_t raw[PACKET_SIZE];
        uint64_t raw64[PACKET_SIZE / 8];
    };

    // timestamps for logging
    uint64_t start;
    uint64_t end;
} packet_t;

// create data stop packet
void create_data_stop(packet_t *packet) {
    memset(packet->raw, 0xFF, PACKET_SIZE);
    packet->header[0] = 0x00; // invalid seq -> won't be included in data
    packet->header[1] = 0xEE;
}

// return whether packet signals data stop
int is_data_stop(packet_t *packet) {
    return 0;
}

#endif // PACKET_H