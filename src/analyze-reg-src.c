#include "analyze-reg-src.h"
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

typedef struct TbSet {
    TbNode *nodes[64];
    size_t num_nodes;
} TbSet;

typedef struct Src {
    TbNode *node;
    size_t index;
    SrcInfo *info;
    size_t info_origin;
    SrcKind kind;
    TbSet tbset;
    union {
        SrcTemp temp;
        SrcStackLoad stack_load;
    };
} Src;

static void tbset_set(TbSet *set, TbNode *n) {
    assert(set->num_nodes < 64);
    for (size_t i = 0; i < set->num_nodes; ++i) {
        if (set->nodes[i] == n) {
            return;
        }
    }
    set->nodes[set->num_nodes++] = n;
}

static bool tbset_isset(TbSet *set, TbNode *n) {
    for (size_t i = 0; i < set->num_nodes; ++i) {
        if (set->nodes[i] == n) {
            return true;
        }
    }
    return false;
}

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

SrcInfo *find_sources(LibTcgArchInfo arch_info,
                      Memory *memory,
                      TbNode *n,
                      uint64_t inst_index,
                      uint64_t arg_index) {
    LibTcgInstruction *inst = &n->tb.list[inst_index];
    assert(arg_index >= inst->nb_oargs);
    arg_index -= inst->nb_oargs;
    assert(arg_index < inst->nb_iargs);

    LibTcgArgument *arg = &inst->input_args[arg_index];
    assert(arg->kind == LIBTCG_ARG_TEMP);

    SrcInfo *info_root = stack_alloc_zero(&memory->persistent, sizeof(SrcInfo));
    info_root->node = n;
    info_root->inst_index = inst_index;
    info_root->op_index = -1;
    info_root->children = stack_alloc_zero(&memory->persistent, sizeof(SrcInfo)*inst->nb_iargs);

    StackMarker marker = stack_marker(&memory->temporary);

    SrcQueue srcs = {
        .srcs = stack_alloc(&memory->temporary, 512*sizeof(Src)),
        .len = 512,
    };

    src_push(&srcs, (Src){
        .node = n,
        .index = inst_index,
        .info = info_root,
        .info_origin = arg_index,
        .kind = SRC_TEMP,
        .temp = {
            .temp_index = arg->temp->index,
        },
    });

    bool has_loop = false;

    while (srcs.used > 0) {
        Src src = src_pop(&srcs);
        assert(src.index > 0);

        int8_t op_index = -1;
        LibTcgTemp *out = NULL;
        int i = src.index-1;
        for (; i > 0; --i) {
            LibTcgInstruction *inst = &src.node->tb.list[i];
            if (src.kind == SRC_TEMP) {
                if (inst->nb_oargs == 0) {
                    continue;
                }
                for (int j = 0; j < inst->nb_oargs; ++j) {
                    if (inst->output_args[j].kind == LIBTCG_ARG_TEMP && inst->output_args[j].temp->index == src.temp.temp_index) {
                        out = inst->output_args[j].temp;
                        op_index = j;
                        break;
                    }
                }
            } else {
                int64_t offset;
                if (is_stack_st_fancy(arch_info, memory, src.node, inst, i, &offset) &&
                    offset == src.stack_load.offset) {
                    out = inst->input_args[0].temp;
                }
            }

            if (out != NULL) {
                break;
            }
        }

        if (out != NULL) {
            LibTcgInstruction *inst = &src.node->tb.list[i];

            SrcInfo *info;
            {
                SrcInfoBranch *child = &src.info->children[src.info_origin];
                if (child->branches == NULL) {
                    child->branches = stack_alloc_zero(&memory->persistent, sizeof(SrcInfo)*SRC_INFO_MAX_BRANCHES_PER_CHILD);
                }
                info = &child->branches[child->num_branches++];
            }
            info->node = src.node;
            info->inst_index = i;
            info->op_index = op_index;
            info->children = stack_alloc_zero(&memory->persistent, sizeof(SrcInfo)*inst->nb_iargs);

            int64_t offset;
            if (is_stack_ld_fancy(arch_info, memory, src.node, inst, i, &offset)) {

                {
                    Src new_src = (Src){
                        .node = src.node,
                        .index = i,
                        .info = info,
                        .info_origin = 0,
                        .kind = SRC_STACK_LOAD,
                        .stack_load = {
                            .offset = offset,
                        }
                    };
                    memcpy(&new_src.tbset, &src.tbset, sizeof(TbSet));
                    src_push(&srcs, new_src);
                }
                if (inst->input_args[0].kind == LIBTCG_ARG_TEMP &&
                    inst->input_args[0].temp->kind != LIBTCG_TEMP_CONST) {
                    Src new_src = (Src){
                        .node = src.node,
                        .index = i,
                        .info = info,
                        .info_origin = 0,
                        .kind = SRC_TEMP,
                        .temp = {
                            .temp_index = inst->input_args[0].temp->index,
                        }
                    };
                    memcpy(&new_src.tbset, &src.tbset, sizeof(TbSet));
                    src_push(&srcs, new_src);
                }
            } else {
                for (int k = 0; k < inst->nb_iargs; ++k) {
                    if (inst->input_args[k].kind == LIBTCG_ARG_TEMP &&
                        inst->input_args[k].temp->kind != LIBTCG_TEMP_CONST) {
                        Src new_src = (Src){
                            .node = src.node,
                            .index = i,
                            .info = info,
                            .info_origin = k,
                            .kind = SRC_TEMP,
                            .temp = {
                                .temp_index = inst->input_args[k].temp->index,
                            },
                        };
                        memcpy(&new_src.tbset, &src.tbset, sizeof(TbSet));
                        src_push(&srcs, new_src);
                    }
                }
            }
        } else {
            for (size_t j = 0; j < src.node->num_pred; ++j) {
                TbNode *n = src.node->pred[j].dst_node;
                Src new_src = src;
                new_src.node = n;
                new_src.index = n->tb.instruction_count;
                if (!tbset_isset(&new_src.tbset, n)) {
                    tbset_set(&new_src.tbset, n);
                    src_push(&srcs, new_src);
                } else {
                    has_loop = true;
                }
            }
        }
    }

    //printf("has loop: %d\n", has_loop);

    stack_reset_to_marker(&memory->temporary, marker);

    return info_root;
}
