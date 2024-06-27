#include "loadelf.h"
#include "cmdline.h"
#include "util.h"
#include "common.h"
#include "analyze-reg-src.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <qemu/libtcg/libtcg.h>
#include <qemu/libtcg/libtcg_loader.h>
static void add_edge(TbNode *src, TbNode *dst,
              size_t instruction_index,
              EdgeType type) {
    // add src -> dst edge
    assert(src->num_succ < MAX_EDGES);
    src->succ[src->num_succ++] = (Edge) {
        .src_instruction = instruction_index,
        .dst_node = dst,
        .type = type,
    };

    // add src <- dst edge
    assert(dst->num_pred < MAX_EDGES);
    dst->pred[dst->num_pred++] = (Edge) {
        .src_instruction = 0,
        .dst_node = src,
        .type = type,
    };
}

int main(int argc, char **argv) {
    bool help = false;
    bool bytes = false;
    bool dump_ir = false;
    bool analyze_max_stack = false;
    unsigned long offset = 0;
    unsigned long size = 0;
    const char *file = NULL;
    const char *section = NULL;
    const char *function = NULL;
    const char *arch_name = NULL;
    const char *dump_cfg = NULL;
    CmdLineRegTuple analyze_reg_src = {0};

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
        {"--dump-ir",   "-i", "",       "dump lifted IR to stdout", CMDLINE_OPTION_BOOL,   .b = &dump_ir},
        {"--dump-cfg",  "-c", "[out.dot]", "compute CFG and dump to [out.dot] in Graphviz's DOT format", CMDLINE_OPTION_STR,   .str = &dump_cfg},
        {"--analyze-max-stack",  "-m", "", "analyze maximum stack offset that is read/written for each lifted instruction, dumped along with CFG/IR", CMDLINE_OPTION_BOOL,   .b = &analyze_max_stack},
        {"--analyze-reg-src",  "-r", "hex:ulong:ulong", "find instructions that contribute to the value of given TCG register", CMDLINE_OPTION_REG_TUPLE,   .reg_tuple = &analyze_reg_src},
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
    LibTcgArch arch;
    if (file) {
        if (size > 0) {
            if (arch_name == NULL) {
                fprintf(stderr, "[error]: Specify an architecture with --arch\n\n");
                goto error;
            }
            arch = libtcg_arch_from_str(arch_name);
            if (arch == LIBTCG_ARCH_NONE) {
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
        arch = libtcg_arch_from_str(arch_name);
        if (arch == LIBTCG_ARCH_NONE) {
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

    LibTcgInterface libtcg;
    LibTcgContext *context;
    libtcg_open(arch, &(LibTcgDesc){0}, &libtcg, &context);

    TbNode *root = NULL;
    TbNode *top = NULL;
    size_t off = 0;
    while (off < view.size) {
        TbNode *n = malloc(sizeof(TbNode));
        *n = (TbNode) {
            .address = view.address + off,
            .list = libtcg.translate(context,
                                     view.data + off,
                                     view.size - off,
                                     view.address + off,
                                     0),
        };
        off += n->list.size_in_bytes;

        if (root == NULL) {
            root = n;
            top = n;
        } else {
            top->next = n;
            top = n;
        }
    }

    if (dump_cfg != NULL) {
        for (TbNode *n = root; n != NULL; n = n->next) {
            for (int i = 0; i < n->list.instruction_count; ++i) {
                LibTcgInstruction *inst = &n->list.list[i];
                //memset(buf, 0, 256);
                //libtcg.dump_instruction_to_buffer(inst, buf, 256);
                //puts(buf);
                bool is_direct;
                uint64_t address;
                if (is_jump(&libtcg, inst, &is_direct, &address) && is_direct) {
                    TbNode *succ = find_tb_containing(root, address);
                    if (succ == NULL) {
                        continue;
                    }
                    if (address == succ->address) {
                        add_edge(n, succ, i, DIRECT);
                    } else {
                        int j = find_instruction_from_address(succ, address);
                        if (j == -1) {
                            printf("couldnt find %lx\n", address);
                            continue;
                        }

                        size_t total_size = succ->list.size_in_bytes;
                        size_t instruction_count = succ->list.instruction_count;
                        TbNode *new_node = malloc(sizeof(TbNode));
                        *new_node = *succ;

                        succ->list.instruction_count = j;
                        succ->list.size_in_bytes = address - succ->address;
                        succ->next = new_node;

                        new_node->address = address;
                        new_node->list.instruction_count = instruction_count - j;
                        new_node->list.list += j;
                        new_node->list.size_in_bytes = total_size - (address - succ->address);

                        for (int i = 0; i < succ->num_succ;) {
                            if (succ->succ[i].src_instruction >= succ->list.instruction_count) {

                                succ->succ[i] = succ->succ[succ->num_succ-1];
                                --succ->num_succ;

                            } else {

                                ++i;
                            }
                        }

                        for (int i = 0; i < new_node->num_succ;) {
                            if (new_node->succ[i].src_instruction < succ->list.instruction_count) {

                                new_node->succ[i] = new_node->succ[new_node->num_succ-1];
                                --new_node->num_succ;

                            } else {

                                new_node->succ[i].src_instruction -= succ->list.instruction_count;

                                ++i;
                            }
                        }

                        add_edge(succ, new_node, j-1, FALLTHROUGH);
                        add_edge(n,    new_node, i, DIRECT);
                    }
                } else if (n->next &&
                    i == n->list.instruction_count-1 &&
                    n->num_succ == 0) {
                    add_edge(n, n->next, i, FALLTHROUGH);
                }
            }
        }

        find_sources(&libtcg, root, 0x11e3, 4, 1);

        FILE *fd = fopen(dump_cfg, "w");
        assert(fd != NULL);
        fputs("digraph {\n", fd);
        fputs("nodesep = 5\n", fd);
        fputs("ranksep = 5\n", fd);
        char buf[256] = {0};
        for (TbNode *n = root; n != NULL; n = n->next) {
            fprintf(fd, "\"%lx\" [shape = \"none\", label=<\n", n->address);
            fputs("<table border=\"0\" cellspacing=\"0\">", fd);
            for (int i = 0; i < n->list.instruction_count; ++i) {
                LibTcgInstruction *inst = &n->list.list[i];
                memset(buf, 0, 256);
                libtcg.dump_instruction_to_buffer(inst, buf, 256);

                fputs("<tr>\n", fd);
                fprintf(fd, "<td port=\"%d\" border=\"1\" align=\"left\">", i);
                if (n->srcset.size > 0 && bitset_isset(n->srcset, i)) {
                    fputs("(Y) ", fd);
                }
                fputs(buf, fd);
                fputs("</td>", fd);
                //fputs("\\l", fd);
                //if (i < n->list.instruction_count-1) {
                //    fputs("|", fd);
                //}
                fputs("</tr>\n", fd);
            }
            fputs("</table>", fd);
            fputs(">];\n", fd);
        }
        for (TbNode *n = root; n != NULL; n = n->next) {
            for (int i = 0; i < n->num_succ; ++i) {
                if (n->succ[i].type == FALLTHROUGH) {
                    fprintf(fd, "\"%lx\":%d:s -> \"%lx\":n [style = dashed]\n",
                            n->address, n->succ[i].src_instruction,
                            n->succ[i].dst_node->address);
                } else {
                    fprintf(fd, "\"%lx\":%d -> \"%lx\":n\n",
                            n->address, n->succ[i].src_instruction,
                            n->succ[i].dst_node->address);
                }
            }
        }
        fputs("}\n", fd);
        fclose(fd);
    } else {
        char buf[256] = {0};
        for (TbNode *n = root; n != NULL; n = n->next) {
            for (int i = 0; i < n->list.instruction_count; ++i) {
                libtcg.dump_instruction_to_buffer(&n->list.list[i], buf, ARRLEN(buf));
                puts(buf);
            }
        }
    }

    libtcg_close(arch);
    free(data.buffer);

    return 0;

error:
    print_help(stderr,
               pos_options, ARRLEN(pos_options),
               named_options, ARRLEN(named_options));
    return -1;
}
