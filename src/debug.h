// debugging, verbose utils

#ifndef DEBUG_H
#define DEBUG_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "packet.h"

// log output file
#define LOG_FILENAME "packets_log"

// log buffer size (packets)
#define LOG_BUFFER_SIZE 20000

// print packet to console
void print_packet(packet_t *packet) {
    // hexdump
    for (int i = 0; i < PACKET_SIZE; i++) printf("%02X ", packet->raw[i]);

    // string representation
    char string_buffer[PAYLOAD_SIZE + 1] = {0};
    for (int i = 0; i < PAYLOAD_SIZE; i++) {
        if (packet->payload[i] < 0x20 || packet->payload[i] > 0x7E) {
            string_buffer[i] = '.';
        } else {
            string_buffer[i] = packet->payload[i];
        }
    }
    printf(" | %s\n", string_buffer);
}

// record packet to log
void record_packet(packet_t *packet) {
    static FILE *tmpfile = NULL;
    static packet_t buffer[LOG_BUFFER_SIZE];
    static size_t index = 0;

    // open file
    if (tmpfile == NULL) {
        tmpfile = fopen(LOG_FILENAME ".tmp", "w+");
        if (tmpfile == NULL) {
            printf("error opening tmp log file '"LOG_FILENAME".tmp': %s\n", strerror(errno));
            exit(1);
        }
    }

    // log packet
    if (packet != NULL) {
        buffer[index++] = *packet;
    }

    // flush
    if ((index == LOG_BUFFER_SIZE) || (packet == NULL)) {
        fwrite(buffer, sizeof(packet_t), index, tmpfile);
        fflush(tmpfile);
        index = 0;
    }

    // close file, create csv
    if (packet == NULL) {
        // open csv file
        FILE *csvfile = fopen(LOG_FILENAME ".csv", "w+");
        if (csvfile == NULL) {
            printf("error opening csv log file '"LOG_FILENAME".csv': %s\n", strerror(errno));
            exit(1);
        }

        // convert to csv
        rewind(tmpfile);
        while ((index = fread(buffer, sizeof(packet_t), LOG_BUFFER_SIZE, tmpfile)) > 0) {
            for (int i = 0; i < index; i++) {
                fprintf(csvfile, "%lu,%lu,", buffer[i].start, buffer[i].end); // timestamps
                for (int j = 0; j < PACKET_SIZE; j++) fprintf(csvfile, "%02X ", buffer[i].raw[j]); // raw packet
                fprintf(csvfile, "\n");
            }
        }

        // finalize
        fflush(csvfile);
        fclose(csvfile);
        fclose(tmpfile);
        remove(LOG_FILENAME ".tmp");
        tmpfile = NULL;
    }
}

#endif // DEBUG_H