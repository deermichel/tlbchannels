#include <stdio.h>

int main() {

    for (int d = 0; d < 16; d++) {
        int out =
            // p0 p1 d3 p2 d2 d1 d0 p3
            (((d >> 3) ^ (d >> 2) ^ (d >> 0)) & 1)  << 7 | // p0 = d3 d2 d0
            (((d >> 3) ^ (d >> 1) ^ (d >> 0)) & 1)  << 6 | // p1 = d3 d1 d0
            ((d >> 3) & 1)                          << 5 | // d3
            (((d >> 2) ^ (d >> 1) ^ (d >> 0)) & 1)  << 4 | // p2 = d2 d1 d0
            ((d >> 2) & 1)                          << 3 | // d2
            ((d >> 1) & 1)                          << 2 | // d1
            ((d >> 0) & 1)                          << 1 | // d0
            (((d >> 3) ^ (d >> 2) ^ (d >> 1)) & 1)  << 0   // p3 = d3 d2 d1 (parity)
        ;

        printf("0x%02X, ", out);

        // printf("%d\t0x%02X\t", d, out);
        // for (int i = 7; i >= 4; i--) printf("%d", (out >> i) & 1);
        // printf(" ");
        // for (int i = 3; i >= 0; i--) printf("%d", (out >> i) & 1);
        // printf("\n");
    }
    printf("\n");

    return 0;
}