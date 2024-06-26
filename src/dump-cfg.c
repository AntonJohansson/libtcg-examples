#include "loadelf.h"
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <limits.h>
#include <qemu/libtcg/libtcg.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define MAX(a,b)    (((a) > (b)) ?  (a) : (b))
#define MIN(a,b)    (((a) < (b)) ?  (a) : (b))
#define ABS(a)      (((a) <  0 ) ? -(a) : (a))

static inline size_t ciel_quotient(size_t size,
                                   size_t multiple) {
    return (size + multiple - 1) / multiple;
}

static inline size_t ciel_multiple(size_t size,
                                   size_t multiple) {
    return multiple + ciel_quotient(size, multiple);
}

typedef struct Bitset {
    uint8_t *bytes;
    size_t size;
} Bitset;

Bitset bitset_alloc(size_t size) {
    return (Bitset) {
        .bytes = calloc(ciel_quotient(size, 8), 1),
        .size = size
    };
}

void bitset_set(Bitset set, size_t bit) {
    assert(bit < set.size);
    set.bytes[bit/8] |= (1 << (bit % 8));
}

bool bitset_isset(const Bitset set, size_t bit) {
    assert(bit < set.size);
    return (set.bytes[bit/8] >> (bit % 8)) & 1;
}

typedef enum EdgeType {
    DIRECT,
    INDIRECT,
    FALLTHROUGH,
} EdgeType;

struct TbNode;

typedef struct Edge {
    size_t src_instruction;
    struct TbNode *dst_node;
    EdgeType type;
} Edge;

#define STACK_SIZE_BOTTOM -1
#define STACK_SIZE_TOP INT64_MAX

typedef struct MfpStackState {
    int64_t max_st_size;
    int64_t max_ld_size;
} MfpStackState;

#define MAX_EDGES 64

typedef struct TbNode {
    uint64_t address;
    LibTcgInstructionList list;
    struct TbNode *next;
    size_t num_succ;
    size_t num_pred;
    Edge succ[MAX_EDGES];
    Edge pred[MAX_EDGES];

    MfpStackState stack_state;
    Bitset srcset;
} TbNode;

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

// Returns true if inst is an indirect or direct jump, false otherwise.
// If it's a direct jump address will contain the destination address.
bool is_jump(LibTcgInterface *libtcg, LibTcgInstruction *inst,
             bool *is_direct, uint64_t *address) {
    if (inst->opcode != LIBTCG_op_mov_i32 &&
        inst->opcode != LIBTCG_op_mov_i64) {
        return false;
    }

    LibTcgArgument *dst = &inst->output_args[0];
    if (dst->kind != LIBTCG_ARG_TEMP ||
        dst->temp->kind != LIBTCG_TEMP_GLOBAL) {
        return false;
    }
    if (dst->temp->mem_offset != libtcg->pc) {
        return false;
    }

    LibTcgArgument *src = &inst->input_args[0];
    assert(src->kind == LIBTCG_ARG_TEMP);
    if (src->temp->kind == LIBTCG_TEMP_CONST) {
        *is_direct = true;
        *address = src->temp->val;
    } else {
        *is_direct = false;
    }
    return true;
}

bool is_stack_ld(LibTcgInterface *libtcg,
                 TbNode *n,
                 LibTcgInstruction *inst,
                 size_t inst_index,
                 int64_t *offset) {
    if (inst->opcode != LIBTCG_op_qemu_ld_a32_i32 &&
        inst->opcode != LIBTCG_op_qemu_ld_a64_i32 &&
        inst->opcode != LIBTCG_op_qemu_ld_a32_i64 &&
        inst->opcode != LIBTCG_op_qemu_ld_a64_i64) {
        return false;
    }

    LibTcgArgument *ptr_op = &inst->input_args[0];
    assert(ptr_op->kind == LIBTCG_ARG_TEMP);
    LibTcgInstruction *ptr_inst = NULL;
    for (int j = inst_index - 1; j > 0; --j) {
        LibTcgInstruction *inst = &n->list.list[j];
        if (inst->nb_oargs > 0 &&
            inst->output_args[0].temp->index == ptr_op->temp->index) {
            ptr_inst = inst;
            break;
        }
    }
    if (ptr_inst == NULL) {
        return false;
    }
    if (ptr_inst->opcode == LIBTCG_op_add_i32 ||
        ptr_inst->opcode == LIBTCG_op_add_i64) {
        LibTcgArgument *op0 = &ptr_inst->input_args[0];
        LibTcgArgument *op1 = &ptr_inst->input_args[1];
        if (op0->kind != LIBTCG_ARG_TEMP ||
            op0->temp->kind != LIBTCG_TEMP_GLOBAL ||
            op1->kind != LIBTCG_ARG_TEMP ||
            op1->temp->kind != LIBTCG_TEMP_CONST) {
            return false;
        }
        if (op0->temp->mem_offset != libtcg->bp) {
            return false;
        }
        *offset = ABS(op1->temp->val);
        return true;
    } else {
        return false;
    }
}

bool is_stack_st(LibTcgInterface *libtcg,
                 TbNode *n,
                 LibTcgInstruction *inst,
                 size_t inst_index,
                 int64_t *offset) {
    if (inst->opcode != LIBTCG_op_qemu_st_a32_i32 &&
        inst->opcode != LIBTCG_op_qemu_st_a64_i32 &&
        inst->opcode != LIBTCG_op_qemu_st_a32_i64 &&
        inst->opcode != LIBTCG_op_qemu_st_a64_i64) {
        return false;
    }

    LibTcgArgument *ptr_op = &inst->input_args[1];
    assert(ptr_op->kind == LIBTCG_ARG_TEMP);
    LibTcgInstruction *ptr_inst = NULL;
    for (int j = inst_index - 1; j > 0; --j) {
        LibTcgInstruction *inst = &n->list.list[j];
        if (inst->nb_oargs > 0 &&
            inst->output_args[0].temp->index == ptr_op->temp->index) {
            ptr_inst = inst;
            break;
        }
    }
    if (ptr_inst == NULL) {
        return false;
    }
    if (ptr_inst->opcode == LIBTCG_op_add_i32 ||
        ptr_inst->opcode == LIBTCG_op_add_i64) {

        LibTcgArgument *op0 = &ptr_inst->input_args[0];
        LibTcgArgument *op1 = &ptr_inst->input_args[1];
        if (op0->kind != LIBTCG_ARG_TEMP ||
            op0->temp->kind != LIBTCG_TEMP_GLOBAL ||
            op1->kind != LIBTCG_ARG_TEMP ||
            op1->temp->kind != LIBTCG_TEMP_CONST) {
            return false;
        }
        if (op0->temp->mem_offset != libtcg->bp) {
            return false;
        }
        *offset = ABS(op1->temp->val);
        return true;
    } else {
        return false;
    }
}

TbNode *find_tb_containing(TbNode *n, uint64_t address) {
    for (; n != NULL; n = n->next) {
        if (address >= n->address &&
            address < n->address + n->list.size_in_bytes) {
            return n;
        }
    }
    return NULL;
}

int find_instruction_from_address(TbNode *n, uint64_t address) {
    int i = 0;
    for (; i < n->list.instruction_count; ++i) {
        LibTcgInstruction *inst = &n->list.list[i];
        if (inst->opcode != LIBTCG_op_insn_start) {
            continue;
        }
        LibTcgArgument *arg = &inst->constant_args[0];
        assert(arg->kind == LIBTCG_ARG_CONSTANT);
        if (arg->constant == address) {
            return i;
        }
    }
    return -1;
}

void add_edge(TbNode *src, TbNode *dst,
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

MfpStackState mfp_transfer_max_stack_size(LibTcgInterface *libtcg,
                                          TbNode *root, TbNode *n,
                                          MfpStackState state) {
    MfpStackState new_state = state;
    for (int i = 0; i < n->list.instruction_count; ++i) {
        LibTcgInstruction *inst = &n->list.list[i];
        int64_t offset = 0;
        if (is_stack_ld(libtcg, n, inst, i, &offset)) {
            new_state.max_ld_size = MAX(state.max_ld_size, offset);
        } else if (is_stack_st(libtcg, n, inst, i, &offset)) {
            new_state.max_st_size = MAX(state.max_st_size, offset);
        } else {
            bool is_direct;
            uint64_t address;
            if (is_jump(libtcg, inst, &is_direct, &address)) {
                if (is_direct && find_tb_containing(root, address) != NULL) {
                    continue;
                }
                new_state.max_ld_size = STACK_SIZE_TOP;
                new_state.max_st_size = STACK_SIZE_TOP;
            }
        }
    }
    return new_state;
}

void compute_max_stack_size(LibTcgInterface *libtcg, TbNode *root) {
    MfpEdgeQueue queue = {0};
    for (TbNode *n = root; n != NULL; n = n->next) {
        queue.len += n->num_succ;
    }

    queue.len *= 10;
    queue.edges = malloc(queue.len * sizeof(MfpEdge));
    for (TbNode *n = root; n != NULL; n = n->next) {
        const int64_t init_stack_size = (n == root) ? 0 : STACK_SIZE_BOTTOM;
        n->stack_state.max_st_size = init_stack_size;
        n->stack_state.max_ld_size = init_stack_size;
        for (int i = 0; i < n->num_succ; ++i) {
            mfp_push(&queue, (MfpEdge) {
                .src = n,
                .dst = n->succ[i].dst_node,
            });
        }
    }

    while (queue.used > 0) {
        MfpEdge edge = mfp_pop(&queue);

        // transfer
        MfpStackState new_state = mfp_transfer_max_stack_size(libtcg,
                                                              root,
                                                              edge.src,
                                                              edge.src->stack_state);

        bool less_than = new_state.max_ld_size <= edge.dst->stack_state.max_ld_size ||
                         new_state.max_st_size <= edge.dst->stack_state.max_st_size;
        if (!less_than) {
            // combine
            edge.dst->stack_state.max_ld_size = MAX(edge.dst->stack_state.max_ld_size, new_state.max_ld_size);
            edge.dst->stack_state.max_st_size = MAX(edge.dst->stack_state.max_st_size, new_state.max_st_size);
            for (int i = 0; i < edge.dst->num_succ; ++i) {
                mfp_push(&queue, (MfpEdge) {
                    .src = edge.dst,
                    .dst = edge.dst->succ[i].dst_node,
                });
            }
        }
    }

    for (TbNode *n = root; n != NULL; n = n->next) {
        MfpStackState s = mfp_transfer_max_stack_size(libtcg, root, n, n->stack_state);
        printf("%lx ld %ld\n", n->address, s.max_ld_size);
        printf("%lx st %ld\n", n->address, s.max_st_size);
    }

    free(queue.edges);
}

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
                        out = inst->output_args[j].temp;
                        break;
                    }
                }
            } else {
                int64_t offset;
                if (is_stack_st(libtcg, src.node, inst, i, &offset) &&
                    offset == src.stack_load.offset) {
                    printf("ststst\n");
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
                    printf("ldld\n");
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
                        if (inst->input_args[k].kind == LIBTCG_ARG_TEMP) {
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

            printf("not null %d\n", i);
            if (i == 1) {
                printf("prop \n");
                for (int j = 0; j < src.node->num_pred; ++j) {
                    TbNode *n = src.node->pred[j].dst_node;
                    if (n == src.node) {
                        continue;
                    }
                    printf("index %d\n", n->list.instruction_count);
                    src.node = n;
                    src.index = n->list.instruction_count;
                    src_push(&srcs, src);
                }
            }
        }
    }
}

int main(int argc, char **argv) {
    if (argc > 3) {
        exit(1);
    }
    const char *file = argv[1];
    const char *fnname = argv[2];

    ElfData *data = elf_data(file);
    if (data == NULL) {
        return 1;
    }

    ElfByteView view;
    if (!elf_function(data, fnname, &view)) {
        free(data);
        return 1;
    }

    void *handle = dlopen(data->libtcg, RTLD_LAZY);
    if (handle == NULL) {
        fprintf(stderr, "Failed to dlopen \"%s\"", data->libtcg);
        free(data);
        return 1;
    }

    TbNode *root = NULL;
    TbNode *top = NULL;

    char buf[256] = {0};
    LIBTCG_FUNC_TYPE(libtcg_load) *libtcg_load = dlsym(handle, "libtcg_load");
    LibTcgInterface libtcg = libtcg_load();
    LibTcgContext *context = libtcg.context_create(&(LibTcgDesc){0});
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

    compute_max_stack_size(&libtcg, root);
    find_sources(&libtcg, root, 0x11e3, 4, 1);

    FILE *fd = fopen("out.dot", "w");
    assert(fd != NULL);
    fputs("digraph {\n", fd);
    fputs("nodesep = 5\n", fd);
    fputs("ranksep = 5\n", fd);
    for (TbNode *n = root; n != NULL; n = n->next) {
        fprintf(fd, "\"%lx\" [shape = \"record\", label=\"{\n", n->address);
        for (int i = 0; i < n->list.instruction_count; ++i) {
            LibTcgInstruction *inst = &n->list.list[i];
                memset(buf, 0, 256);
                libtcg.dump_instruction_to_buffer(inst, buf, 256);
            fprintf(fd, "<%d>", i);
            if (n->srcset.size > 0 && bitset_isset(n->srcset, i)) {
                fputs("(Y) ", fd);
            }
            fputs(buf, fd);
            fputs("\\l", fd);
            if (i < n->list.instruction_count-1) {
                fputs("|", fd);
            }
            fputs("\n", fd);
        }
        fputs("}\"];\n", fd);
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


    //char buf[256] = {0};
    //for (int i = 0; i < top->list.instruction_count; ++i) {
    //    libtcg.dump_instruction_to_buffer(&top->list.list[i], buf, 256);
    //    puts(buf);
    //}

    libtcg.context_destroy(context);
    dlclose(handle);
    free(data);

    return 0;
}
