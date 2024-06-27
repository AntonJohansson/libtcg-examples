#pragma once

#include <qemu/libtcg/libtcg.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <assert.h>
#include <stdlib.h>

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

static Bitset bitset_alloc(size_t size) {
    return (Bitset) {
        .bytes = calloc(8*ciel_quotient(size, 64), 1),
        .size = size
    };
}

static void bitset_set(Bitset set, size_t bit) {
    assert(bit < set.size);
    set.bytes[bit/8] |= (1 << (bit % 8));
}

static bool bitset_isset(const Bitset set, size_t bit) {
    assert(bit < set.size);
    return (set.bytes[bit/8] >> (bit % 8)) & 1;
}

static bool bitset_equal(const Bitset a, const Bitset b) {
    if (a.size != b.size) {
         return false;
    }
    for (int i = 0; i < ciel_quotient(a.size, 64); ++i) {
        uint64_t ia = ((uint64_t *)a.bytes)[i];
        uint64_t ib = ((uint64_t *)b.bytes)[i];
        if (ia != ib) {
            return false;
        }
    }
    return true;
}

static bool bitset_subset(const Bitset a, const Bitset b) {
    assert(a.size == b.size);
    bool has_subset = false;
    for (int i = 0; i < ciel_quotient(a.size, 64); ++i) {
        uint64_t ia = ((uint64_t *)a.bytes)[i];
        uint64_t ib = ((uint64_t *)b.bytes)[i];
        if (ia != ib) {
            if ((ia | ib) == ib) {
                has_subset = true;
            } else {
                return false;
            }
        }
    }
    return has_subset;
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

#define MAX_EDGES 64

typedef struct TbNode {
    uint64_t address;
    LibTcgInstructionList list;
    struct TbNode *next;
    size_t num_succ;
    size_t num_pred;
    Edge succ[MAX_EDGES];
    Edge pred[MAX_EDGES];

    //MfpStackState stack_state;
    Bitset srcset;
} TbNode;

// Returns true if inst is an indirect or direct jump, false otherwise.
// If it's a direct jump address will contain the destination address.
bool is_jump(LibTcgInterface *libtcg, LibTcgInstruction *inst,
             bool *is_direct, uint64_t *address);
TbNode *find_tb_containing(TbNode *n, uint64_t address);
int find_instruction_from_address(TbNode *n, uint64_t address);
bool is_stack_ld(LibTcgInterface *libtcg,
                 TbNode *n,
                 LibTcgInstruction *inst,
                 size_t inst_index,
                 int64_t *offset);
bool is_stack_st(LibTcgInterface *libtcg,
                 TbNode *n,
                 LibTcgInstruction *inst,
                 size_t inst_index,
                 int64_t *offset);
