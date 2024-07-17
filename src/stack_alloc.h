#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct StackBlock StackBlock;

typedef struct StackBlock {
    uint8_t *memory;
    size_t used;
    size_t size;
    StackBlock *next;
} StackBlock;

typedef struct StackAllocator {
    StackBlock *root;
    StackBlock *last;
} StackAllocator;

typedef struct StackMarker {
    StackBlock *block;
    size_t offset;
} StackMarker;

typedef struct StackSize {
    size_t num_blocks;
    size_t total_size;
    size_t total_used;
} StackSize;

void        *stack_alloc(StackAllocator *stack, size_t size_in_bytes);
void        *stack_alloc_zero(StackAllocator *stack, size_t size_in_bytes);
StackMarker stack_marker(StackAllocator *stack);
StackSize   stack_size(StackAllocator *stack);
void        stack_reset(StackAllocator *stack);
void        stack_reset_to_marker(StackAllocator *stack, StackMarker marker);
void        stack_free_all(StackAllocator *stack);
