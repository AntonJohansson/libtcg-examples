#include "common.h"
#include <qemu/libtcg/libtcg.h>
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include <stdio.h>
#include <string.h>

typedef struct SrcTemp {
    size_t temp_index;
} SrcTemp;

typedef struct SrcStackLoad {
    size_t temp_index;
    int64_t offset;
    LibTcgOpcode op;
} SrcStackLoad;

typedef enum SrcKind {
    SRC_TEMP = 0,
    SRC_STACK_LOAD,
} SrcKind;

typedef struct Src {
    TbNode *node;
    size_t index;
    SrcKind kind;
    union {
        SrcTemp temp;
        SrcStackLoad stack_load;
    };
} Src;

typedef struct SrcQueue {
    Src *srcs;
    size_t len;
    size_t used;
    size_t bottom;
    size_t top;
} SrcQueue;

void src_push(SrcQueue *queue, Src src) {
    assert(queue->used < queue->len);
    queue->srcs[queue->top] = src;
    queue->top = (queue->top + 1) % queue->len;
    ++queue->used;
}

Src src_pop(SrcQueue *queue) {
    assert(queue->used > 0);
    Src src = queue->srcs[queue->bottom];
    queue->bottom = (queue->bottom + 1) % queue->len;
    --queue->used;
    return src;
}

void find_sources(LibTcgInterface *libtcg,
                  TbNode *root,
                  uint64_t address,
                  uint64_t offset,
                  uint64_t arg_index) {
    TbNode *n = find_tb_containing(root, address);
    int index = find_instruction_from_address(n, address);
    assert(index != -1);

    LibTcgInstruction *inst = &n->list.list[index+offset];
    assert(arg_index >= inst->nb_oargs);
    arg_index -= inst->nb_oargs;
    assert(arg_index < inst->nb_iargs);

    LibTcgArgument *arg = &inst->input_args[arg_index];
    assert(arg->kind == LIBTCG_ARG_TEMP);

    SrcQueue srcs = {
        .srcs = malloc(128*sizeof(Src)),
        .len = 128,
    };

    src_push(&srcs, (Src){
        .node = n,
        .index = index + offset,
        .kind = SRC_TEMP,
        .temp = {
            .temp_index = arg->temp->index,
        },
    });
    while (srcs.used > 0) {
        Src src = src_pop(&srcs);
        assert(src.index > 0);
        for (int i = src.index-1; i > 0; --i) {
            LibTcgInstruction *inst = &src.node->list.list[i];
            LibTcgTemp *out = NULL;
            if (src.kind == SRC_TEMP) {
                if (inst->nb_oargs == 0) {
                    continue;
                }
                for (int j = 0; j < inst->nb_oargs; ++j) {
                    if (inst->output_args[j].kind == LIBTCG_ARG_TEMP && inst->output_args[j].temp->index == src.temp.temp_index) {
                        puts("temp\n");
                        out = inst->output_args[j].temp;
                        break;
                    }
                }
            } else {
                int64_t offset;
                if (is_stack_st(libtcg, src.node, inst, i, &offset) &&
                    offset == src.stack_load.offset) {
                    puts("st\n");
                    out = inst->input_args[0].temp;
                }
            }

            if (out != NULL) {
                static char buf[256];
                memset(buf, 0, 256);
                libtcg->dump_instruction_to_buffer(inst, buf, 256);
                puts(buf);

                if (src.node->srcset.size == 0) {
                    src.node->srcset = bitset_alloc(src.node->list.instruction_count);
                }
                bitset_set(src.node->srcset, i);

                int64_t offset;
                if (is_stack_ld(libtcg, src.node, inst, i, &offset)) {
                    src_push(&srcs, (Src){
                        .node = src.node,
                        .index = i,
                        .kind = SRC_STACK_LOAD,
                        .stack_load = {
                            .offset = offset,
                        },
                    });
                } else {
                    for (int k = 0; k < inst->nb_iargs; ++k) {
                        if (inst->input_args[k].kind == LIBTCG_ARG_TEMP &&
                            inst->input_args[k].temp->kind != LIBTCG_TEMP_CONST) {
                            printf("1 pushing %s\n", inst->input_args[k].temp->name);
                            src_push(&srcs, (Src){
                                .node = src.node,
                                .index = i,
                                .kind = SRC_TEMP,
                                .temp = {
                                    .temp_index = inst->input_args[k].temp->index,
                                },
                            });
                        }
                    }
                }

                break;
            }

            if (i == 1) {
                for (int j = 0; j < src.node->num_pred; ++j) {
                    TbNode *n = src.node->pred[j].dst_node;
                    if (n == src.node) {
                        continue;
                    }
                    src.node = n;
                    src.index = n->list.instruction_count;
                    src_push(&srcs, src);
                }
            }
        }
    }
}

