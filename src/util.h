#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define ARRLEN(arr) (sizeof(arr) / sizeof(arr[0]))

typedef struct ByteView {
    uint8_t *data;
    size_t size;
} ByteView;

static inline ByteView read_bytes_from_stdin(void) {
    FILE *fd = freopen(NULL, "rb", stdin);
    assert(fd != NULL);


    size_t size = 64;
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Failed to malloc file %ld bytes!\n", size);
        return (ByteView){0};
    }

    size_t bytes_read = fread(ptr, 1, size, fd);
    fclose(fd);
    printf("%ld\n", bytes_read);

    return (ByteView) {
        .data = ptr,
        .size = bytes_read,
    };
}

static inline ByteView read_bytes_from_file(const char *file,
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

    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Failed to malloc file %ld bytes!\n", size);
        return (ByteView){0};
    }

    size_t bytes_read = fread(ptr, 1, size, fd);
    assert(bytes_read == size);
    fclose(fd);

    return (ByteView) {
        .data = ptr,
        .size = size,
    };
}
