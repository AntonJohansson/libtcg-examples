#include "cmdline.h"
#include <string.h>
#include <limits.h>
#include <stdlib.h>

static void print_named_cmdline_option(FILE *fd, CmdLineOption *option) {
    fprintf(fd, "%16s%16s%16s    %s\n", option->long_name, option->short_name, option->format, option->desc);
}

static void print_pos_cmdline_option(FILE *fd, CmdLineOption *option) {
    fprintf(fd, "%16s%8s    %s\n", option->long_name, option->format, option->desc);
}

static bool parse_option(CmdLineOption *option, const char *value) {
    option->parsed = true;
    switch (option->type) {
    case CMDLINE_OPTION_STR:
        if (value == NULL) {
            return false;
        }
        *option->str = value;
        break;
    case CMDLINE_OPTION_ULONG:
        if (value == NULL) {
            return false;
        }
        *option->ulong = strtoul(value, NULL, 10);
        if (*option->ulong == ULONG_MAX) {
            return false;
        }
        break;
    case CMDLINE_OPTION_HEX:
        if (value == NULL) {
            return false;
        }
        *option->ulong = strtoul(value, NULL, 16);
        if (*option->ulong == ULONG_MAX) {
            return false;
        }
        break;
    case CMDLINE_OPTION_BOOL:
        *option->b = true;
        break;
    case CMDLINE_OPTION_REG_TUPLE:
        if (value == NULL) {
            return false;
        }
        char *end = NULL;
        unsigned long long t0 = strtoull(value, &end, 16);
        if (t0 == ULONG_MAX || *end != ':' || *(end+1) == 0) {
            return false;
        }
        unsigned long t1 = strtoul(end+1, &end, 10);
        if (t1 == ULONG_MAX || *end != ':' || *(end+1) == 0) {
            return false;
        }
        unsigned long t2 = strtoul(end+1, &end, 10);
        if (t2 == ULONG_MAX || *end != 0) {
            return false;
        }
        *option->reg_tuple = (CmdLineRegTuple) {
            .src_instruction_address = t0,
            .tcg_instruction_offset = t1,
            .operand_index = t2,
            .present = true,
        };
        break;
    default:
        abort();
    }
    return true;
}

bool parse_options(CmdLineOption *pos_options,   size_t num_pos_options,
                   CmdLineOption *named_options, size_t num_named_options,
                   int argc, char **argv) {
    size_t pos_index = 0;
    for (int i = 1; i < argc; ++i) {
        const char *option = argv[i];
        size_t option_len = strlen(option);
        if (option_len > 1 && option[0] == '-') {
            bool long_name = option_len > 2 && option[1] == '-';
            for (int j = 0; j < num_named_options; ++j) {
                CmdLineOption *named_option = &named_options[j];
                if (named_option->parsed) {
                    continue;
                }
                const char *name = (long_name) ? named_option->long_name
                                               : named_option->short_name;
                if (strcmp(option, name) != 0) {
                    continue;
                }

                const char *value = NULL;
                if (i+1 < argc &&
                    named_option->type != CMDLINE_OPTION_BOOL) {
                    value = argv[++i];
                }

                if (!parse_option(named_option, value)) {
                    fprintf(stderr, "[error]: Invalid value \"%s\" for option \"%s\"\n", value, option);
                    print_named_cmdline_option(stderr, named_option);
                }

                break;
            }
        } else {
            if (pos_index >= num_pos_options) {
                fprintf(stderr, "[error]: Invalid positional option \"%s\"\n", option);
                return false;
            }
            CmdLineOption *pos_option = &pos_options[pos_index++];
            if (!parse_option(pos_option, option)) {
                fprintf(stderr, "[error]: Invalid positional option \"%s\"\n", option);
                print_pos_cmdline_option(stderr, pos_option);
            }
        }
    }

    for (int i = 0; i < num_pos_options; ++i) {
        CmdLineOption *pos_option = &pos_options[i];
        if (pos_option->required && !pos_option->parsed) {
            fprintf(stderr, "[error]: Missing required positional option:\n");
            print_pos_cmdline_option(stderr, pos_option);
        }
    }
    for (int i = 0; i < num_named_options; ++i) {
        CmdLineOption *named_option = &named_options[i];
        if (named_option->required && !named_option->parsed) {
            fprintf(stderr, "[error]: Missing required named option:\n");
            print_named_cmdline_option(stderr, named_option);
        }
    }

    return true;
}

void print_help(FILE *fd,
                CmdLineOption *pos_options,   size_t num_pos_options,
                CmdLineOption *named_options, size_t num_named_options) {

    fprintf(fd, "Positional options\n");
    print_pos_cmdline_option(fd, &(CmdLineOption){
        .long_name = "option",
        .format    = "format",
        .desc      = "desc",
    });
    for (int i = 0; i < num_pos_options; ++i) {
        CmdLineOption *pos_option = &pos_options[i];
        print_pos_cmdline_option(fd, pos_option);
    }

    fprintf(fd, "\nName options\n");
    print_named_cmdline_option(fd, &(CmdLineOption){
        .long_name  = "long name",
        .short_name = "short name",
        .format     = "format",
        .desc       = "desc",
    });
    for (int i = 0; i < num_named_options; ++i) {
        CmdLineOption *named_option = &named_options[i];
        print_named_cmdline_option(fd, named_option);
    }
}
