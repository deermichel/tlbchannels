// hamming code calc

#ifndef HAMMING_H
#define HAMMING_H

#include <stdint.h>

// hamming-8,4 encoding codes
const uint8_t CODES[16] = {
    0x00, 0xD2, 0x55, 0x87, 0x99, 0x4B, 0xCC, 0x1E, 0xE1, 0x33, 0xB4, 0x66, 0x78, 0xAA, 0x2D, 0xFF,
};

// encode 8 bits to 16 bits (2x hamming-8,4)
uint16_t enc8_16(uint8_t input) {
    return CODES[input & 0x0F] | (CODES[(input & 0xF0) >> 4] << 8);
}

// decode 16 bits to 8 bits (2x hamming-8,4)
uint16_t dec8_16(uint16_t input) {
    
}

#endif // HAMMING_H