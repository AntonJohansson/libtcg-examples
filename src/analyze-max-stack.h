#pragma once

#include <stdbool.h>

#define STACK_SIZE_BOTTOM -1
#define STACK_SIZE_TOP INT64_MAX

typedef struct LibTcgInterface LibTcgInterface;
typedef struct StackAllocator StackAllocator;
typedef struct TbNode TbNode;
typedef struct Memory Memory;

void compute_max_stack_size(LibTcgInterface *libtcg,
                            Memory *memory,
                            TbNode *root,
                            bool stack_grows_down);
