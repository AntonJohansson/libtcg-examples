#include "analyze-max-stack.h"
#include "common.h"
#include <qemu/libtcg/libtcg.h>
#include <stdio.h>

typedef struct MfpEdge{
    TbNode *src;
    TbNode *dst;
} MfpEdge;

typedef struct MfpEdgeQueue {
    MfpEdge *edges;
    size_t len;
    size_t used;
    size_t bottom;
    size_t top;
} MfpEdgeQueue;

void mfp_push(MfpEdgeQueue *queue, MfpEdge edge) {
    assert(queue->used < queue->len);
    queue->edges[queue->top] = edge;
    queue->top = (queue->top + 1) % queue->len;
    ++queue->used;
}

MfpEdge mfp_pop(MfpEdgeQueue *queue) {
    assert(queue->used > 0);
    MfpEdge edge = queue->edges[queue->bottom];
    queue->bottom = (queue->bottom + 1) % queue->len;
    --queue->used;
    return edge;
}

static MfpStackState mfp_transfer_max_stack_size(LibTcgInterface *libtcg,
                                                 Memory *memory,
                                                 TbNode *root, TbNode *n,
                                                 bool stack_grows_down) {
    (void) stack_grows_down;
    LibTcgArchInfo arch_info = libtcg->get_arch_info();
    MfpStackState new_state = n->stack_state[0];
    for (size_t i = 0; i < n->tb.instruction_count; ++i) {
        LibTcgInstruction *inst = &n->tb.list[i];
        int64_t offset = 0;
        if (is_stack_ld_fancy(arch_info, memory, n, inst, i, &offset)) {
            new_state.max_ld_size = MAX(new_state.max_ld_size, offset);
        } else if (is_stack_st_fancy(arch_info, memory, n, inst, i, &offset)) {
            new_state.max_st_size = MAX(new_state.max_st_size, offset);
        } else if (inst->opcode == LIBTCG_op_call) {
            LibTcgHelperInfo info = libtcg->get_helper_info(inst);
            if ((info.func_flags & LIBTCG_CALL_NO_WRITE_GLOBALS) == 0) {
                // If the helper writes to globals, we have to be conservative
                // and assume it touches the stack
                new_state.max_ld_size = STACK_SIZE_TOP;
                new_state.max_st_size = STACK_SIZE_TOP;
            }
        } else {
            bool is_direct;
            uint64_t address;
            if (is_pc_write(arch_info, inst, &is_direct, &address)) {
                if (!is_direct || find_tb_containing(root, address) == NULL) {
                    new_state.max_ld_size = STACK_SIZE_TOP;
                    new_state.max_st_size = STACK_SIZE_TOP;
                }
            }
        }
        n->stack_state[i] = new_state;
    }
    return new_state;
}

void compute_max_stack_size(LibTcgInterface *libtcg,
                            Memory *memory,
                            TbNode *root,
                            bool stack_grows_down) {
    StackMarker marker = stack_marker(&memory->temporary);

    MfpEdgeQueue queue = {0};
    for (TbNode *n = root; n != NULL; n = n->next) {
        queue.len += n->num_succ;
    }

    queue.len *= 10;
    queue.edges = stack_alloc(&memory->temporary, queue.len * sizeof(MfpEdge));
    for (TbNode *n = root; n != NULL; n = n->next) {
        const int64_t init_stack_size = (n == root) ? 0 : STACK_SIZE_BOTTOM;
        n->stack_state = stack_alloc(&memory->temporary, sizeof(MfpStackState)*n->tb.instruction_count);
        n->stack_state[0].max_st_size = init_stack_size;
        n->stack_state[0].max_ld_size = init_stack_size;
        for (size_t i = 0; i < n->num_succ; ++i) {
            mfp_push(&queue, (MfpEdge) {
                .src = n,
                .dst = n->succ[i].dst_node,
            });
        }
    }

    while (queue.used > 0) {
        MfpEdge edge = mfp_pop(&queue);

        // transfer
        printf("  node: %lx\n", edge.src->address);
        printf("    [0] %ld %ld\n", edge.src->stack_state[0].max_ld_size, edge.src->stack_state[0].max_st_size);
        MfpStackState new_state = mfp_transfer_max_stack_size(libtcg,
                                                              memory,
                                                              root,
                                                              edge.src,
                                                              stack_grows_down);
        printf("    [1] %ld %ld\n", new_state.max_ld_size, new_state.max_st_size);

        bool less_than = new_state.max_ld_size <= edge.dst->stack_state[0].max_ld_size &&
                         new_state.max_st_size <= edge.dst->stack_state[0].max_st_size;
        if (!less_than) {
            // combine
            edge.dst->stack_state[0].max_ld_size = MAX(edge.dst->stack_state[0].max_ld_size, new_state.max_ld_size);
            edge.dst->stack_state[0].max_st_size = MAX(edge.dst->stack_state[0].max_st_size, new_state.max_st_size);
            for (size_t i = 0; i < edge.dst->num_succ; ++i) {
                mfp_push(&queue, (MfpEdge) {
                    .src = edge.dst,
                    .dst = edge.dst->succ[i].dst_node,
                });
            }
        }
    }

    for (TbNode *n = root; n != NULL; n = n->next) {
        MfpStackState s = mfp_transfer_max_stack_size(libtcg, memory, root, n, stack_grows_down);
        printf("final %lx %ld %ld\n", n->address, s.max_ld_size, s.max_st_size);
    }

    //free(queue.edges);
    stack_reset_to_marker(&memory->temporary, marker);
}
