#include "loadelf.h"
#include "cmdline.h"
#include "util.h"
#include "arch.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <qemu/libtcg/libtcg.h>

int main(int argc, char **argv) {
    bool help = false;
    bool bytes = false;
    unsigned long offset = 0;
    unsigned long size = 0;
    const char *file = NULL;
    const char *section = NULL;
    const char *function = NULL;
    const char *arch_name = NULL;

    CmdLineOption pos_options[] = {
        {"[file]", "", "string", "file to read bytes from, used with --offset/--length, --section, --function", CMDLINE_OPTION_STR, .str = &file},
    };
    CmdLineOption named_options[] = {
        {"--help",      "-h", "",       "show help message",                                                   CMDLINE_OPTION_BOOL,  .b = &help},
        {"--offset",    "-o", "hex",    "given [file], translate region at --offset/--length",                 CMDLINE_OPTION_HEX, .ulong = &offset},
        {"--length",    "-l", "ulong",  "given [file], translate region at --offset/--length",                 CMDLINE_OPTION_ULONG, .ulong = &size},
        {"--section",   "-s", "string", "given [file], translate ELF section",                                 CMDLINE_OPTION_STR,   .str = &section},
        {"--function",  "-f", "string", "given [file], translate ELF function (requires symbols)",             CMDLINE_OPTION_STR,   .str = &function},
        {"--bytes",     "-b", "",       "translate bytes from stdin, requires --arch",                         CMDLINE_OPTION_BOOL,  .b = &bytes},
        {"--arch",      "-a", "string", "given bytes or [file]/--offset/--length, specify input architecture", CMDLINE_OPTION_STR,   .str = &arch_name},
    };
    if (!parse_options(pos_options, ARRLEN(pos_options),
                       named_options, ARRLEN(named_options),
                       argc, argv)
        || help) {
        fprintf(stderr, "[error]: Failed parsing options\n\n");
        goto error;
    }

    ElfData data = {0};
    ElfByteView view;
    ByteView data_view;
    Arch arch;
    if (file) {
        if (size > 0) {
            if (arch_name == NULL) {
                fprintf(stderr, "[error]: Specify an architecture with --arch\n\n");
                goto error;
            }
            arch = arch_from_str(arch_name);
            if (arch == ARCH_NONE) {
                fprintf(stderr, "[error]: Invalid architecture\n\n");
                goto error;
            }
            data_view = read_bytes_from_file(file, offset, size);
            if (data_view.data == NULL) {
                goto error;
            }
            view.address = offset;
            view.data = data_view.data;
            view.size = data_view.size;
        } else if (function != NULL) {
            if (!elf_data(file, &data)) {
                return -1;
            }
            arch = data.arch;
            if (!elf_function(&data, function, &view)) {
                free(data.buffer);
                return -1;
            }
        } else if (section != NULL) {
            if (!elf_data(file, &data)) {
                return -1;
            }
            arch = data.arch;
            if (!elf_section(&data, section, &view)) {
                free(data.buffer);
                return -1;
            }
        } else {
            fprintf(stderr, "[error]: Please specify either --offset/--length, --function, --section\n\n");
            goto error;
        }
    } else if (bytes) {
        if (arch_name == NULL) {
            fprintf(stderr, "[error]: Specify an architecture with --arch\n\n");
            goto error;
        }
        arch = arch_from_str(arch_name);
        if (arch == ARCH_NONE) {
            fprintf(stderr, "[error]: Invalid architecture\n\n");
            goto error;
        }
        data_view = read_bytes_from_stdin();
        if (data_view.data == NULL) {
            fprintf(stderr, "[error]: Failed to read data from stdin\n\n");
            goto error;
        }
        view.address = 0;
        view.data = data_view.data;
        view.size = data_view.size;
    } else {
        fprintf(stderr, "[error]: Please specify either [file] or --bytes\n\n");
        goto error;
    }

    const char *arch_file = arch_libtcg_file(arch);
    void *handle = dlopen(arch_file, RTLD_LAZY);
    if (handle == NULL) {
        fprintf(stderr, "Failed to dlopen \"%s\"", arch_file);
        free(data.buffer);
        return -1;
    }

    LIBTCG_FUNC_TYPE(libtcg_load) *libtcg_load = dlsym(handle, "libtcg_load");
    LibTcgInterface libtcg = libtcg_load();
    LibTcgContext *context = libtcg.context_create(&(LibTcgDesc){0});

    char buf[256] = {0};
    size_t off = 0;
    while (off < view.size) {
        LibTcgInstructionList list = libtcg.translate(context,
                                                      view.data + off,
                                                      view.size - off,
                                                      view.address + off,
                                                      0);
        off += list.size_in_bytes;

        for (int i = 0; i < list.instruction_count; ++i) {
            libtcg.dump_instruction_to_buffer(&list.list[i], buf, ARRLEN(buf));
            puts(buf);
        }
        libtcg.instruction_list_destroy(context, list);
    }

    libtcg.context_destroy(context);
    dlclose(handle);
    free(data.buffer);

    return 0;

error:
    print_help(stderr,
               pos_options, ARRLEN(pos_options),
               named_options, ARRLEN(named_options));
    return -1;
}
