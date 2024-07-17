#pragma once

#include "common.h"

#define SRC_INFO_MAX_BRANCHES_PER_CHILD 8

struct SrcInfo;

typedef struct SrcInfoBranch {
    size_t num_branches;
    struct SrcInfo *branches;
} SrcInfoBranch;

typedef struct SrcInfo {
    TbNode *node;
    size_t inst_index;
    int8_t op_index;

    SrcInfoBranch *children;
} SrcInfo;

SrcInfo *find_sources(LibTcgArchInfo arch_info,
                      Memory *memory,
                      TbNode *n,
                      uint64_t inst_index,
                      uint64_t arg_index);
