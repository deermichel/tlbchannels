// debugging, verbose utils

#ifndef DEBUG_H
#define DEBUG_H

#include <stdio.h>
#include "packet.h"

// print packet to console
void print_packet(packet_t* packet) {
    // hexdump
    for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet->raw[i]);

    // string representation
    char string_buffer[PAYLOAD_SIZE + 1] = {0};
    // int is_string = 1;
    for (int i = 0; i < PAYLOAD_SIZE; i++) {
        if (packet->payload[i] < 0x20 || packet->payload[i] > 0x7E) {
            string_buffer[i] = '.';
        } else {
            string_buffer[i] = packet->payload[i];
        }
    }
    printf(" | %s\n", string_buffer);
}

#endif // DEBUG_H