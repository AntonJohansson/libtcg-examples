#pragma once

#include "cmdline.h"
#include <stdio.h>
#include <stdbool.h>

typedef struct TbNode TbNode;
typedef struct LibTcgInterface LibTcgInterface;
typedef struct StackAllocator StackAllocator;

typedef struct GraphvizSettings {
    float nodesep;
    float ranksep;
    bool dashed_fallthrough_edges;
    // Emit constants as signed integers, rather then hex,
    // skip constant instruction args. on loads/stores.
    bool compact_args;
} GraphvizSettings;

void graphviz_output(LibTcgInterface *libtcg, StackAllocator *stack,
                     GraphvizSettings settings,
                     FILE *fd, TbNode *root, bool analyze_max_stack,
                     CmdLineRegTuple analyze_reg_src, TbNode *reg_src_node,
                     int reg_src_index);
