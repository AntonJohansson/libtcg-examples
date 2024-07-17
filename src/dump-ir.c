#include "graphviz.h"
#include "loadelf.h"
#include "cmdline.h"
#include "util.h"
#include "common.h"
#include "analyze-reg-src.h"
#include "analyze-max-stack.h"
#include "graphviz.h"
#include "stack_alloc.h"
#include <qemu/libtcg/libtcg.h>
#include <qemu/libtcg/libtcg_loader.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>

static Memory memory = {0};

static void *libtcg_alloc(size_t size) {
    return stack_alloc(&memory.persistent, size);
}

static void flatten_sources(StackAllocator *stack, SrcInfo *info) {
    LibTcgInstruction *inst = &info->node->tb.list[info->inst_index];
    if (info->node->reg_src_info == NULL) {
        info->node->reg_src_info = stack_alloc_zero(stack, sizeof(SrcInfo *)*info->node->tb.instruction_count);
    }
    info->node->reg_src_info[info->inst_index] = info;
    for (int i = 0; i < inst->nb_iargs; ++i) {
        if (info->children[i].num_branches == 0) {
            continue;
        }
        for (size_t j = 0; j < info->children[i].num_branches; ++j) {
            flatten_sources(stack, &info->children[i].branches[j]);
        }
    }
}

static void add_edge(TbNode *src, TbNode *dst,
              size_t instruction_index,
              EdgeType type) {
    for (int i = src->num_succ-1; i >= 0; --i) {
        if (src->succ[i].dst_node == dst) {
            return;
        }
    }

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
    bool optimize = false;
    bool h2tcg = false;
    bool debug = false;
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
        {"--optimize",  "-p", "", "optimize lifted TCG", CMDLINE_OPTION_BOOL, .b = &optimize},
        {"--h2tcg",     "-t", "", "use auto-generated TCG variants of helpers (EXPERIMENTAL)", CMDLINE_OPTION_BOOL, .b = &h2tcg},
        {"--debug",     "-d", "", "Enable debug logging", CMDLINE_OPTION_BOOL, .b = &debug},
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
            data_view = read_bytes_from_file(&memory.persistent,
                                             file, offset, size);
            if (data_view.data == NULL) {
                goto error;
            }
            view.address = offset;
            view.data = data_view.data;
            view.size = data_view.size;
        } else if (function != NULL) {
            if (!elf_data(&memory.persistent, file, &data)) {
                return -1;
            }
            arch = data.arch;
            if (!elf_function(&data, function, &view)) {
                return -1;
            }
        } else if (section != NULL) {
            if (!elf_data(&memory.persistent, file, &data)) {
                return -1;
            }
            arch = data.arch;
            if (!elf_section(&data, section, &view)) {
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
        data_view = read_bytes_from_stdin(&memory.persistent);
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
    libtcg_open(arch, &(LibTcgDesc){
        .mem_alloc = libtcg_alloc,
    }, &libtcg, &context);

    uint32_t flags = 0;
    if (optimize) {
        flags |= LIBTCG_TRANSLATE_OPTIMIZE_TCG;
    }
    if (h2tcg) {
        flags |= LIBTCG_TRANSLATE_HELPER_TO_TCG;
    }
    if (arch == LIBTCG_ARCH_ARM && ((view.address & 1) != 0)) {
        flags |= LIBTCG_TRANSLATE_ARM_THUMB;
        view.address &= ~((uint64_t) 1);
    }

    TbNode *root = NULL;
    TbNode *top = NULL;
    size_t off = 0;
    while (off < view.size) {
        uint64_t address = view.address + off;
        LibTcgTranslationBlock tb = libtcg.translate_block(context,
                                                           view.data + off,
                                                           view.size - off,
                                                           address,
                                                           flags);
        off += tb.size_in_bytes;
        if (tb.instruction_count == 0) {
            continue;
        }

        TbNode *n = stack_alloc(&memory.persistent, sizeof(TbNode));
        *n = (TbNode) {
            .address = address,
            .tb = tb,
        };

        if (root == NULL) {
            root = n;
            top = n;
        } else {
            top->next = n;
            top = n;
        }
    }

    if (dump_cfg != NULL) {
        LibTcgArchInfo arch_info = libtcg.get_arch_info();
        size_t num_indirect_jumps = 0;
        size_t num_jumps = 0;
        uint64_t jumps[16] = {0};
        for (TbNode *n = root; n != NULL; n = n->next) {
            num_indirect_jumps = 0;
            num_jumps = 0;

            for (size_t i = 0; i < n->tb.instruction_count; ++i) {
                LibTcgInstruction *inst = &n->tb.list[i];

                bool is_direct;
                uint64_t address;
                if (is_pc_write(arch_info, inst, &is_direct, &address)) {
                    if (is_direct) {
                        jumps[num_jumps++] = address;
                    } else {
                        ++num_indirect_jumps;
                    }
                } else if (inst->opcode == LIBTCG_op_exit_tb) {
                    ++n->num_exits;
                }
            }

            if (n->num_exits > 0) {
                for (size_t i = 0; i < num_jumps; ++i) {
                    uint64_t address = jumps[i];
                    TbNode *succ = find_tb_containing(root, address);
                    if (succ == NULL) {
                        continue;
                    }
                    if (address == succ->address) {
                        add_edge(n, succ, 0, DIRECT);
                    } else {
                        int j = find_instruction_from_address(succ, address);
                        if (j == -1) {
                            continue;
                        }

                        size_t total_size = succ->tb.size_in_bytes;
                        size_t instruction_count = succ->tb.instruction_count;
                        TbNode *new_node = stack_alloc(&memory.persistent,
                                                       sizeof(TbNode));
                        *new_node = *succ;

                        succ->tb.instruction_count = j;
                        succ->tb.size_in_bytes = address - succ->address;
                        succ->next = new_node;

                        new_node->address = address;
                        new_node->tb.instruction_count = instruction_count - j;
                        new_node->tb.list += j;
                        new_node->tb.size_in_bytes = total_size - (address - succ->address);

                        for (size_t i = 0; i < succ->num_succ;) {
                            if (succ->succ[i].src_instruction >= succ->tb.instruction_count) {

                                succ->succ[i] = succ->succ[succ->num_succ-1];
                                --succ->num_succ;

                            } else {

                                ++i;
                            }
                        }

                        succ->num_succ = 0;
                        new_node->num_pred = 0;

                        for (size_t i = 0; i < new_node->num_succ; ++i) {
                            new_node->succ[i].src_instruction -= succ->tb.instruction_count;
                        }
                        for (size_t i = 0; i < new_node->num_succ; ++i) {
                            TbNode *n = new_node->succ[i].dst_node;
                            for (size_t j = 0; j < n->num_pred; ++j) {
                                if (n->pred[j].dst_node == succ) {
                                    n->pred[j].dst_node = new_node;
                                }
                            }
                        }

                        add_edge(succ, new_node, j-1, FALLTHROUGH);
                        if (n->address != succ->address) {
                            add_edge(n,    new_node, 0, DIRECT);
                        }
                    }
                }
            }

            if (n->next && (n->num_exits == 0 || (num_jumps + num_indirect_jumps) < n->num_exits)) {
                add_edge(n, n->next, n->tb.instruction_count-1, FALLTHROUGH);
            }
        }

        TbNode *reg_src_node = NULL;
        int reg_src_index = 0;
        if (analyze_reg_src.present) {
            uint64_t address = analyze_reg_src.src_instruction_address;
            reg_src_node = find_tb_containing(root, address);
            reg_src_index = find_instruction_from_address(reg_src_node, address);
            if (reg_src_index == -1) {
                return -1;
            }
            reg_src_index += analyze_reg_src.tcg_instruction_offset;
            SrcInfo *info = find_sources(arch_info,
                                         &memory,
                                         reg_src_node,
                                         reg_src_index,
                                         analyze_reg_src.operand_index);
            flatten_sources(&memory.temporary, info);
        }

        if (analyze_max_stack) {
            bool stack_grows_down = true;
            compute_max_stack_size(&libtcg, &memory,
                                   root, stack_grows_down);
        }

        FILE *fd = fopen(dump_cfg, "w");
        graphviz_output(&libtcg, &memory.persistent,
                        (GraphvizSettings) {
                            .nodesep = 1.0f,
                            .ranksep = 1.0f,
                            .dashed_fallthrough_edges = false,
                            .compact_args = true,
                        },
                        fd, root, analyze_max_stack, analyze_reg_src, reg_src_node, reg_src_index);
        fclose(fd);
    } else if (dump_ir) {
        char buf[128] = {0};
        for (TbNode *n = root; n != NULL; n = n->next) {
            for (size_t i = 0; i < n->tb.instruction_count; ++i) {
                libtcg.dump_instruction_to_buffer(&n->tb.list[i], buf, ARRLEN(buf));
                puts(buf);
            }
        }
    }

    if (debug) {
        puts("Used memory:");
        StackSize size;
        size = stack_size(&memory.persistent);
        printf("  persisent memory %lu/%lu kiB in %lu blocks\n", size.total_used/1024, size.total_size/1024, size.num_blocks);
        size = stack_size(&memory.temporary);
        printf("  temporary memory %lu/%lu kiB in %lu blocks\n", size.total_used/1024, size.total_size/1024, size.num_blocks);
    }

    libtcg_close(arch);
    stack_free_all(&memory.persistent);
    stack_free_all(&memory.temporary);
    return 0;

error:
    print_help(stderr,
               pos_options, ARRLEN(pos_options),
               named_options, ARRLEN(named_options));
    stack_free_all(&memory.persistent);
    stack_free_all(&memory.temporary);
    return -1;
}
