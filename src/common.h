#pragma once

#include "stack_alloc.h"
#include <qemu/libtcg/libtcg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <assert.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))
#define MAX(a,b)    (((a) > (b)) ?  (a) : (b))
#define MIN(a,b)    (((a) < (b)) ?  (a) : (b))
#define ABS(a)      (((a) <  0 ) ? -(a) : (a))

#ifndef likely
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif

typedef struct TbNode TbNode;
typedef struct SrcInfo SrcInfo;

#define MAX_EDGES 256

typedef struct Memory {
    StackAllocator temporary;
    StackAllocator persistent;
} Memory;

typedef enum EdgeType {
    DIRECT,
    INDIRECT,
    FALLTHROUGH,
} EdgeType;

typedef struct Edge {
    size_t src_instruction;
    struct TbNode *dst_node;
    EdgeType type;
} Edge;

typedef struct MfpStackState {
    int64_t max_st_size;
    int64_t max_ld_size;
} MfpStackState;

typedef struct TbNode {
    uint64_t address;
    LibTcgTranslationBlock tb;
    struct TbNode *next;
    size_t num_exits;
    size_t num_succ;
    size_t num_pred;
    Edge succ[MAX_EDGES];
    Edge pred[MAX_EDGES];

    MfpStackState *stack_state;
    SrcInfo **reg_src_info;
} TbNode;

static inline int64_t largest_stack_offset(bool stack_grows_down,
                                           int64_t off0, int64_t off1) {
    return (stack_grows_down) ? MIN(off0, off1) : MAX(off0, off1);
}

// Returns true if inst is an indirect or direct jump, false otherwise.
// If it's a direct jump address will contain the destination address.
bool is_pc_write(LibTcgArchInfo arch, LibTcgInstruction *inst,
                 bool *is_direct, uint64_t *address);
bool is_jump(LibTcgInterface *libtcg, LibTcgInstruction *inst,
             bool *is_direct, uint64_t *address);
TbNode *find_tb_containing(TbNode *n, uint64_t address);
int find_instruction_from_address(TbNode *n, uint64_t address);
bool is_stack_ld_fancy(LibTcgArchInfo arch_info,
                       Memory *memory,
                       TbNode *n,
                       LibTcgInstruction *inst,
                       size_t inst_index,
                       int64_t *offset);
bool is_stack_st_fancy(LibTcgArchInfo arch_info,
                       Memory *memory,
                       TbNode *n,
                       LibTcgInstruction *inst,
                       size_t inst_index,
                       int64_t *offset);
