#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum CmdLineOptionType {
    CMDLINE_OPTION_STR,
    CMDLINE_OPTION_ULONG,
    CMDLINE_OPTION_HEX,
    CMDLINE_OPTION_BOOL,
} CmdLineOptionType;

typedef struct CmdLineOption {
    const char *long_name;
    const char *short_name;
    const char *format;
    const char *desc;
    CmdLineOptionType type;
    union {
        const char **str;
        unsigned long *ulong;
        bool *b;
    };
    bool required;
    bool parsed;
} CmdLineOption;

bool parse_options(CmdLineOption *pos_options,   size_t num_pos_options,
                   CmdLineOption *named_options, size_t num_named_options,
                   int argc, char **argv);

void print_help(FILE *fd,
                CmdLineOption *pos_options,   size_t num_pos_options,
                CmdLineOption *named_options, size_t num_named_options);
