#include "stack_alloc.h"
#include "common.h"
#include <stdio.h>
#include <assert.h>
#include <stdlib.h> // for exit()
#include <string.h> // for memset()

#define PAGE_SIZE 4096
#define MIN_BLOCK_SIZE PAGE_SIZE

static inline size_t round_to_page_size(size_t size) {
    return PAGE_SIZE * ((size + PAGE_SIZE - 1) / PAGE_SIZE);
}

static StackBlock *block_alloc(size_t size) {
    if (unlikely(size > MIN_BLOCK_SIZE)) {
        size = round_to_page_size(size);
    }

    void *ptr = malloc(sizeof(StackBlock) + size);
    if (ptr == NULL) {
        fprintf(stderr, "Failed to allocate block of size %lu\n", size);
        exit(-1);
    }

    StackBlock *block = ptr;
    block->memory = (uint8_t *) (block + 1);
    block->used = 0;
    block->size = size;
    block->next = NULL;

    return block;
}

static inline bool block_can_fit(StackBlock *block, size_t size) {
    return (block->size - block->used) >= size;
}

static inline void initialize(StackAllocator *stack) {
    if (unlikely(stack->root == NULL)) {
        stack->root = block_alloc(MIN_BLOCK_SIZE);
        stack->last = stack->root;
    }
}

void *stack_alloc(StackAllocator *stack, size_t size) {
    initialize(stack);

    if (unlikely(stack->root == NULL)) {
        stack->root = block_alloc(MIN_BLOCK_SIZE);
        stack->last = stack->root;
    }

    if (unlikely(!block_can_fit(stack->last, size))) {
        StackBlock *b = stack->last;
        // If we have subsequent blocks, try finding one that can fit the
        // requested size.
        for (; b->next != NULL; b = b->next) {
            if (block_can_fit(b->next, size)) {
                break;
            }
        }
        if (b->next == NULL) {
            b->next = block_alloc(size);
        }
        stack->last = b->next;
    }

    uint8_t *ptr = stack->last->memory + stack->last->used;
    stack->last->used += size;

    return (void *) ptr;
}

void *stack_alloc_zero(StackAllocator *stack, size_t size) {
    void *ptr = stack_alloc(stack, size);
    memset(ptr, 0, size);
    return ptr;
}

StackMarker stack_marker(StackAllocator *stack) {
    initialize(stack);
    return (StackMarker) {
        .block = stack->last,
        .offset = stack->last->used,
    };
}

StackSize stack_size(StackAllocator *stack) {
    initialize(stack);
    StackSize size = {0};
    for (StackBlock *b = stack->root; b != NULL; b = b->next) {
        ++size.num_blocks;
        size.total_size += b->size;
        size.total_used += b->used;
    }
    return size;
}

void stack_reset(StackAllocator *stack) {
    if (stack->root == NULL) {
        return;
    }
    for (StackBlock *b = stack->root; b != NULL; b = b->next) {
        b->used = 0;
    }
    stack->last = stack->root;
}

void stack_reset_to_marker(StackAllocator *stack, StackMarker marker) {
    if (stack->root == NULL) {
        return;
    }
    for (StackBlock *b = marker.block->next; b != NULL; b = b->next) {
        b->used = 0;
    }
    stack->last = marker.block;
    stack->last->used = marker.offset;
}

void stack_free_all(StackAllocator *stack) {
    if (stack->root == NULL) {
        return;
    }
    StackBlock *head = stack->root;
    do {
        StackBlock *tmp = head;
        head = head->next;
        free(tmp);
    } while(head != NULL);
}
