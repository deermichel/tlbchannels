// packet definition, helper methods

#ifndef PACKET_H
#define PACKET_H

#include "arch.h"

// packet size (bytes)
#define PACKET_SIZE (TLB_SETS / 8)

// header size (bytes)
#define HEADER_SIZE 3

// payload size (bytes)
#define PAYLOAD_SIZE (PACKET_SIZE - HEADER_SIZE)

// packet definition
typedef struct {
    // data
    union {
        struct {
            uint8_t header[HEADER_SIZE];
            uint8_t payload[PAYLOAD_SIZE];
        };
        uint8_t raw[PACKET_SIZE];
    };

    // timestamps for logging
    uint64_t start;
    uint64_t end;
} packet_t;

#endif // PACKET_H