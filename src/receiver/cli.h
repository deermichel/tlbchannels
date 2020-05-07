// command-line interface stuff

// TODO: fix weird output in --help

#ifndef CLI_H
#define CLI_H

#include <argp.h>
#include <stdbool.h>

// meta
const char *argp_program_version = "receiver 0.1";
const char *argp_program_bug_address = "https://github.com/deermichel/tlbchannels/issues";
static char doc[] = "receiver for tlb-based covert channels";

// cli args docs
static struct argp_option options[] = {
    { "output", 'o', "FILE", 0, "write received payload to the specified file" },
    { "window", 'w', "NUMBER[,NUMBER]", 0, "set receiver window (iterations per packet) and optional evictions threshold - only for rdtsc probing" },
    { "verbose", 'v', 0, 0, "produce verbose output" },
    { "rdtsc", 'r', "NUMBER", 0, "use rdtsc probing (with threshold)" },
    { 0 },
};

// cli args struct
static struct {
    enum { MODE_PROBE_PTEACCESS, MODE_PROBE_RDTSC } mode;
    bool verbose;
    int window;
    int window_threshold;
    const char *filename;
    int rdtsc_threshold;
} args = { 
    // defaults
    .mode = MODE_PROBE_PTEACCESS,
    .verbose = false,
    .window = 8,
    .window_threshold = 4,
    .filename = NULL,
};

// cli args parser
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    char *token;
    switch (key) {
        case 'o':
            args.filename = arg;
            break;
        case 'r':
            args.mode = MODE_PROBE_RDTSC;
            args.rdtsc_threshold = atoi(arg);
            break;
        case 'v':
            args.verbose = true;
            break;
        case 'w':
            token = strtok(arg, ",");
            args.window = atoi(token);
            token = strtok(NULL, ",");
            args.window_threshold = (token) ? atoi(token) : (args.window / 2);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
};

// final argp parser
static struct argp argp = { options, parse_opt, 0, doc };

#endif // CLI_H