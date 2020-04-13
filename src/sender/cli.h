// command-line interface stuff

// TODO: fix weird output in --help

#ifndef CLI_H
#define CLI_H

#include <argp.h>
#include <stdbool.h>

// meta
const char *argp_program_version = "sender 0.1";
const char *argp_program_bug_address = "https://github.com/deermichel/tlbchannels/issues";
static char doc[] = "sender for tlb-based covert channels";

// cli args docs
static struct argp_option options[] = {
    { "string", 's', "STRING", 0, "send the specified string" },
    { "file", 'f', "FILE", 0, "send the specified file" },
    { "window", 'w', "NUMBER", 0, "set sender window (iterations per packet)" },
    { "verbose", 'v', 0, 0, "produce verbose output" },
};

// cli args struct
static struct {
    enum { MODE_SEND_STRING, MODE_SEND_FILE } mode;
    bool verbose;
    int window;
    union {
        const char *string;
        const char *filename;
    };
} args = { 
    // defaults
    .verbose = false,
    .window = 1000,
    .string = "",
};

// cli args parser
static error_t parse_opt (int key, char *arg, struct argp_state *state) {
    switch (key) {
        case 's':
            args.mode = MODE_SEND_STRING;
            args.string = arg;
            break;
        case 'f':
            args.mode = MODE_SEND_FILE;
            args.filename = arg;
            break;
        case 'v':
            args.verbose = true;
            break;
        case 'w':
            args.window = atoi(arg);
            break;
        default:
            return ARGP_ERR_UNKNOWN;
    }
    return 0;
};

// final argp parser
static struct argp argp = { options, parse_opt, 0, doc };

#endif // CLI_H