#include <stdio.h>

void genenc() {
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
}

void gendec() {
    // p0 p1 d3 p2 d2 d1 d0 p3
    // d7 d6 d5 d4 d3 d2 d1 d0
    for (int d = 0; d < 256; d++) {
        int p0 = ((d >> 7) ^ (d >> 5) ^ (d >> 3) ^ (d >> 1)) & 1;
        int p1 = ((d >> 6) ^ (d >> 5) ^ (d >> 2) ^ (d >> 1)) & 1;
        int p2 = ((d >> 4) ^ (d >> 3) ^ (d >> 2) ^ (d >> 1)) & 1;
        int p3 = ((d >> 7) ^ (d >> 6) ^ (d >> 5) ^ (d >> 4) ^ (d >> 3) ^ (d >> 2) ^ (d >> 1) ^ (d >> 0)) & 1;

        int syn = (p2 << 2) | (p1 << 1) | p0;

        if (syn == 0) {
            int data = (((d >> 5) & 1) << 3) | ((d >> 1) & 7);
            if (p3 == 0) {
                // everything correct
                // printf("0x%02X \t ---- \t okay \t %d \n", d, data);

            } else {
                // parity defect
                // printf("0x%02X \t ---- \t pdef \t %d \n", d, data);
            }
            printf("0x%02XFF, ", data);

        } else {
            if (p3 == 0) {
                // 2bit error
                // printf("0x%02X \t ---- \t ---- \n", d);
                printf("0x0000, ");
            } else {
                // 1bit error
                int corr = d ^ (1 << (8 - syn));
                int data = (((corr >> 5) & 1) << 3) | ((corr >> 1) & 7);
                printf("0x%02XFF, ", data);
                // printf("0x%02X \t 0x%02X \t corr \t %d \n", d, corr, data);
            }

        }

        if (d % 16 == 15) printf("\n");
        
    }
}

int main() {
    gendec();
    return 0;
}