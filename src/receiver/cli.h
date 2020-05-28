// command-line interface stuff

#ifndef CLI_H
#define CLI_H

#include <argp.h>
#include <stdbool.h>

// meta
const char *argp_program_version = "receiver 1.0";
const char *argp_program_bug_address = "https://github.com/deermichel/tlbchannels/issues";
static char doc[] = "receiver for tlb-based covert channels";

// cli args docs
static struct argp_option options[] = {
    { "output", 'o', "FILE", 0, "write received payload to the specified file" },
    { "verbose", 'v', 0, 0, "produce verbose output" },
    { 0 },
};

// cli args struct
static struct {
    bool verbose;
    const char *filename;
} args = { 
    // defaults
    .verbose = false,
    .filename = NULL,
};

// cli args parser
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    switch (key) {
        case 'o':
            args.filename = arg;
            break;
        case 'v':
            args.verbose = true;
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
};

// final argp parser
static struct argp argp = { options, parse_opt, 0, doc };

#endif // CLI_H