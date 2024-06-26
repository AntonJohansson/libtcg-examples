#pragma once

#include "arch.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    uint8_t *buffer;
    size_t size;
    bool little_endian;
    bool is64bit;
    uint64_t shoff;
    uint16_t shentsize;
    uint16_t shnum;
    uint8_t *shstrtable;
    uint8_t *strtable;
    uint64_t machine;
    Arch arch;
} ElfData;

typedef struct {
    uint64_t address;
    uint8_t *data;
    size_t size;
} ElfByteView;

bool elf_data(const char *file, ElfData *data);
bool elf_section(ElfData *data, const char *section, ElfByteView *view);
bool elf_function(ElfData *data, const char *fnname, ElfByteView *view);
