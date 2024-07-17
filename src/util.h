#pragma once

#include "stack_alloc.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))

typedef struct StackAllocator StackAllocator;

typedef struct ByteView {
    uint8_t *data;
    size_t size;
} ByteView;

static inline ByteView read_bytes_from_stdin(StackAllocator *stack) {
    FILE *fd = freopen(NULL, "rb", stdin);
    assert(fd != NULL);

    // TODO: We just assume a maximum fixed size on the data coming
    // in from stdin, clearly not a good idea. Should probably allocate
    // in chunks or similar.
    size_t size = 1024;
    void *ptr = stack_alloc(stack, size);
    size_t bytes_read = fread(ptr, 1, size, fd);
    fclose(fd);

    return (ByteView) {
        .data = ptr,
        .size = bytes_read,
    };
}

static inline ByteView read_bytes_from_file(StackAllocator *stack,
                                            const char *file,
                                            size_t offset,
                                            size_t size) {
    FILE *fd = fopen(file, "r");
    if (!fd) {
        fprintf(stderr, "Failed to open file %s!\n", file);
        return (ByteView){0};
    }
    if (size == 0) {
        fseek(fd, 0, SEEK_END);
        size = ftell(fd);
    }
    fseek(fd, offset, SEEK_SET);

    void *ptr = stack_alloc(stack, size);
    size_t bytes_read = fread(ptr, 1, size, fd);
    assert(bytes_read == size);
    fclose(fd);

    return (ByteView) {
        .data = ptr,
        .size = size,
    };
}
