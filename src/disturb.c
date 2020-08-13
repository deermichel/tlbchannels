#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// micro-benchmark for tlb interference
int main(int argc, char **argv) {
    // allocate memory
    size_t num_bytes = 1024L * 1024L * atoi(argv[1]);
    const int sleep = atoi(argv[2]);
    printf("Allocating %ld MB\n", num_bytes / 1024 / 1024);
    char *mem = (char*)malloc(num_bytes);
    printf("Running every %d us...\n", sleep);

    // read/writes
    while (1) {
        for (size_t i = 0; i < num_bytes; i += 4096) {
            mem[i]++;
        }
        if (sleep > 0) usleep(sleep);
    }

    // cleanup
    free(mem);
    return 0;
}