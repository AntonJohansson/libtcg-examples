#pragma once

#include "util.h"
#include <stdio.h>
#include <string.h>

typedef enum Arch {
    ARCH_NONE = 0,
    ARCH_AARCH64_BE,
    ARCH_AARCH64,
    ARCH_ALPHA,
    ARCH_ARMEB,
    ARCH_ARM,
    ARCH_CRIS,
    ARCH_HEXAGON,
    ARCH_HPPA,
    ARCH_I386,
    ARCH_LOONGARCH64,
    ARCH_M68K,
    ARCH_MICROBLAZEEL,
    ARCH_MICROBLAZE,
    ARCH_MIPS64EL,
    ARCH_MIPS64,
    ARCH_MIPSEL,
    ARCH_MIPS,
    ARCH_MIPSN32EL,
    ARCH_MIPSN32,
    ARCH_NIOS2,
    ARCH_OR1K,
    ARCH_PPC64LE,
    ARCH_PPC64,
    ARCH_PPC,
    ARCH_RISCV32,
    ARCH_RISCV64,
    ARCH_S390X,
    ARCH_SH4EB,
    ARCH_SH4,
    ARCH_SPARC32PLUS,
    ARCH_SPARC64,
    ARCH_SPARC,
    ARCH_X86_64,
    ARCH_XTENSAEB,
    ARCH_XTENSA,
} Arch;

static const char *arch_names[] = {
    [ARCH_AARCH64_BE]   = "aarch64_be",
    [ARCH_AARCH64]      = "aarch64",
    [ARCH_ALPHA]        = "alpha",
    [ARCH_ARMEB]        = "armeb",
    [ARCH_ARM]          = "arm",
    [ARCH_CRIS]         = "cris",
    [ARCH_HEXAGON]      = "hexagon",
    [ARCH_HPPA]         = "hppa",
    [ARCH_I386]         = "i386",
    [ARCH_LOONGARCH64]  = "loongarch64",
    [ARCH_M68K]         = "m68k",
    [ARCH_MICROBLAZEEL] = "microblazeel",
    [ARCH_MICROBLAZE]   = "microblaze",
    [ARCH_MIPS64EL]     = "mips64el",
    [ARCH_MIPS64]       = "mips64",
    [ARCH_MIPSEL]       = "mipsel",
    [ARCH_MIPS]         = "mipsn32el",
    [ARCH_MIPSN32EL]    = "mipsn32",
    [ARCH_MIPSN32]      = "mips",
    [ARCH_NIOS2]        = "nios2",
    [ARCH_OR1K]         = "or1k",
    [ARCH_PPC64LE]      = "ppc64le",
    [ARCH_PPC64]        = "ppc64",
    [ARCH_PPC]          = "ppc",
    [ARCH_RISCV32]      = "riscv32",
    [ARCH_RISCV64]      = "riscv64",
    [ARCH_S390X]        = "s390x",
    [ARCH_SH4EB]        = "sh4eb",
    [ARCH_SH4]          = "sh4",
    [ARCH_SPARC32PLUS]  = "sparc32plus",
    [ARCH_SPARC64]      = "sparc64",
    [ARCH_SPARC]        = "sparc",
    [ARCH_X86_64]       = "x86_64",
    [ARCH_XTENSAEB]     = "xtensaeb",
    [ARCH_XTENSA]       = "xtensa",
};

static Arch arch_from_str(const char *str) {
    for (int i = 0; i < ARRLEN(arch_names); ++i) {
        if (i == ARCH_NONE) {
            continue;
        }
        if (strcmp(arch_names[i], str) == 0) {
            return i;
        }
    }
    return ARCH_NONE;
}

static const char *arch_libtcg_file(Arch arch) {
    static char buf[64] = {0};
    const char *name = arch_names[arch];
    snprintf(buf, ARRLEN(buf)-1, "libtcg-%s.so", name);
    return buf;
}
