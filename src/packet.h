// packet definition, helper methods

#ifndef PACKET_H
#define PACKET_H

#include "arch.h"

// packet size (bytes)
#define PACKET_SIZE (TLB_SETS / 8)

// header size (bytes)
#define HEADER_SIZE 2

// payload size (bytes)
#define PAYLOAD_SIZE 30 // (PACKET_SIZE - HEADER_SIZE)

// reed solomon config
#define RS_TOTAL_SYMBOLS 255
#define RS_PARITY_SYMBOLS 32
#define RS_DATA_SYMBOLS (RS_TOTAL_SYMBOLS - RS_PARITY_SYMBOLS)

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

#endif // PACKET_H