#include "loadelf.h"
#include "util.h"
#include <elf.h>
#include <endian.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#if __BYTE_ORDER == __BIG_ENDIAN
#define HOST_LE 0
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define HOST_LE 1
#else
#error We should do something else to chfce host endianness...
#endif

static inline uint16_t bswap16(ElfData *data, uint16_t v) {
    return (HOST_LE ^ data->little_endian) ? __builtin_bswap16(v) : v;
}

static inline uint32_t bswap32(ElfData *data, uint32_t v) {

    return (HOST_LE ^ data->little_endian) ? __builtin_bswap32(v) : v;
}

static inline uint64_t bswap64(ElfData *data, uint64_t v) {
    return (HOST_LE ^ data->little_endian) ? __builtin_bswap64(v) : v;
}

static inline uint64_t bswaptl(ElfData *data, uint64_t v) {
    return (data->is64bit) ? bswap64(data, v) : bswap32(data, v);
}

static LibTcgArch elf_machine_to_libtcg(bool little_endian,
                                  bool is64bit,
                                  uint64_t machine) {
    if (little_endian) {
        switch (machine) {
        case EM_386:          return LIBTCG_ARCH_I386;
        case EM_MIPS:         return (is64bit) ? LIBTCG_ARCH_MIPS64EL : LIBTCG_ARCH_MIPSEL;
        case EM_ARM:          return LIBTCG_ARCH_ARM;
        case EM_SH:           return LIBTCG_ARCH_SH4;
        case EM_X86_64:       return LIBTCG_ARCH_X86_64;
        case EM_CRIS:         return LIBTCG_ARCH_CRIS;
        case EM_XTENSA:       return LIBTCG_ARCH_XTENSA;
        case EM_PPC64:        return LIBTCG_ARCH_PPC64LE;
        case EM_ALTERA_NIOS2: return LIBTCG_ARCH_NIOS2;
        case EM_QDSP6:        return LIBTCG_ARCH_HEXAGON;
        case EM_AARCH64:      return LIBTCG_ARCH_AARCH64;
        case EM_MICROBLAZE:   return LIBTCG_ARCH_MICROBLAZEEL;
        case EM_LOONGARCH:    return LIBTCG_ARCH_LOONGARCH64;
        case EM_ALPHA:        return LIBTCG_ARCH_ALPHA;
        case EM_RISCV:        return (is64bit) ? LIBTCG_ARCH_RISCV64 : LIBTCG_ARCH_RISCV32;
        default:
            fprintf(stderr, "Unable to find LE machine %lu\n", machine);
            abort();
        }
    } else {
        switch (machine) {
        case EM_SPARC:        return LIBTCG_ARCH_SPARC;
        case EM_68K:          return LIBTCG_ARCH_M68K;
        case EM_MIPS:         return (is64bit) ? LIBTCG_ARCH_MIPS64 : LIBTCG_ARCH_MIPS;
        case EM_PARISC:       return LIBTCG_ARCH_HPPA;
        case EM_SPARC32PLUS:  return LIBTCG_ARCH_SPARC32PLUS;
        case EM_PPC:          return LIBTCG_ARCH_PPC;
        case EM_SH:           return LIBTCG_ARCH_SH4EB;
        case EM_PPC64:        return LIBTCG_ARCH_PPC64;
        case EM_S390:         return LIBTCG_ARCH_S390X;
        case EM_ARM:          return LIBTCG_ARCH_ARMEB;
        case EM_SPARCV9:      return LIBTCG_ARCH_SPARC64;
        case EM_OPENRISC:     return LIBTCG_ARCH_OR1K;
        case EM_XTENSA:       return LIBTCG_ARCH_XTENSAEB;
        case EM_AARCH64:      return LIBTCG_ARCH_AARCH64_BE;
        case EM_MICROBLAZE:   return LIBTCG_ARCH_MICROBLAZE;
        default:
            fprintf(stderr, "Unable to find BE machine %lu\n", machine);
            abort();
        }
    }
}

#define FIND_SECTION_HEADER(Shdr, data, ekind, ename, s_hdr)                \
    do {                                                                    \
        for (int i = 0; i < data->shnum; ++i) {                             \
            Shdr *sh = (Shdr *) (data->buffer +                             \
                                 data->shoff +                              \
                                 i*data->shentsize);                        \
            uint32_t name_off = bswap32(data, sh->sh_name);                 \
            const char *name = (const char *) (data->shstrtable +           \
                                               name_off);                   \
            if (bswap32(data, sh->sh_type) == ekind &&                      \
                strcmp(name, ename) == 0) {                                 \
                s_hdr = sh;                                                 \
                break;                                                      \
            }                                                               \
        }                                                                   \
    } while (0)

#define FIND_SYMBOL(Sym, data, s_hdr, ekind, ename, sym)                    \
    do {                                                                    \
        for (uint64_t off = 0;                                              \
             off < bswaptl(data, s_hdr->sh_size);                           \
             off += bswaptl(data, s_hdr->sh_entsize)) {                     \
            Sym *s = (Sym *) (data->buffer +                                \
                              bswaptl(data, s_hdr->sh_offset) +             \
                              off);                                         \
            uint64_t name_off = bswap32(data, s->st_name);                  \
            const char *name = (const char *) (data->strtable + name_off);  \
            uint8_t type = ELF64_ST_TYPE(s->st_info);                       \
            if ((type == ekind || type == STT_NOTYPE) &&                    \
                strcmp(name, ename) == 0) {                                 \
                sym = s;                                                    \
                break;                                                      \
            }                                                               \
        }                                                                   \
    } while (0)

#define POPULATE_ELF_DATA(Ehdr, Shdr, data)                                 \
    do {                                                                    \
        Ehdr *e_hdr = (Ehdr *) data->buffer;                                \
        data->shoff     = bswaptl(data, e_hdr->e_shoff);                    \
        data->shentsize = bswap16(data, e_hdr->e_shentsize);                \
        data->shnum     = bswap16(data, e_hdr->e_shnum);                    \
        uint16_t shstrndx = bswap16(data, e_hdr->e_shstrndx);               \
        Shdr *strtable_hdr = (Shdr *) (data->buffer +                       \
                                       data->shoff +                        \
                                       shstrndx*data->shentsize);           \
        assert(bswap32(data, strtable_hdr->sh_type) == SHT_STRTAB);         \
        data->shstrtable = data->buffer +                                   \
                           bswaptl(data, strtable_hdr->sh_offset);          \
        Shdr *s_hdr = NULL;                                                 \
        FIND_SECTION_HEADER(Shdr, data, SHT_STRTAB, ".strtab", s_hdr);      \
        if (s_hdr != NULL) {                                                \
            data->strtable = data->buffer +                                 \
                             bswaptl(data, s_hdr->sh_offset);               \
        } else {                                                            \
            data->strtable = 0;                                             \
        }                                                                   \
        data->machine = bswap16(data, e_hdr->e_machine);                    \
        data->arch = elf_machine_to_libtcg(data->little_endian,             \
                                           data->is64bit,                   \
                                           data->machine);                  \
        data->entrypoint = bswaptl(data, e_hdr->e_entry);                   \
    } while (0)

bool elf_data(StackAllocator *stack, const char *file, ElfData *data) {
    /* Start by reading entire file into a buffer */
    ByteView bytes = read_bytes_from_file(stack, file, 0, 0);
    if (bytes.data == NULL) {
        goto fail;
    }

    data->buffer = bytes.data;
    data->size = bytes.size;

    /* Verify ELF header */
    if (data->buffer[EI_MAG0] != ELFMAG0 ||
        data->buffer[EI_MAG1] != ELFMAG1 ||
        data->buffer[EI_MAG2] != ELFMAG2 ||
        data->buffer[EI_MAG3] != ELFMAG3) {
        fprintf(stderr, "Invalid ELF header for file %s!\n", file);
        goto fail;
    }

    data->little_endian = data->buffer[EI_DATA] == ELFDATA2LSB;
    data->is64bit = data->buffer[EI_CLASS] == ELFCLASS64;

    if (data->is64bit) {
        POPULATE_ELF_DATA(Elf64_Ehdr, Elf64_Shdr, data);
    } else {
        POPULATE_ELF_DATA(Elf32_Ehdr, Elf32_Shdr, data);
    }

    return data;
fail:
    free(data);
    return NULL;
}

bool elf_section(ElfData *data, const char *section, ElfByteView *view) {
    if (data->is64bit) {
        /* Loop over all section headers and look for .text */
        Elf64_Shdr *s_hdr = NULL;
        FIND_SECTION_HEADER(Elf64_Shdr, data, SHT_PROGBITS, section, s_hdr);
        if (s_hdr == NULL) {
            fprintf(stderr, "Couldn't find section \"%s\"\n", section);
            return false;
        }
        view->address = bswaptl(data, s_hdr->sh_addr);
        view->data = data->buffer + bswaptl(data, s_hdr->sh_offset);
        view->size = bswaptl(data, s_hdr->sh_size);
    } else {
        /* Loop over all section headers and look for .text */
        Elf32_Shdr *s_hdr = NULL;
        FIND_SECTION_HEADER(Elf32_Shdr, data, SHT_PROGBITS, section, s_hdr);
        if (s_hdr == NULL) {
            fprintf(stderr, "Couldn't find section \"%s\"\n", section);
            return false;
        }
        view->address = bswaptl(data, s_hdr->sh_addr);
        view->data = data->buffer + bswaptl(data, s_hdr->sh_offset);
        view->size = bswaptl(data, s_hdr->sh_size);
    }

    return true;
}

bool elf_function(ElfData *data, const char *fnname, ElfByteView *view) {
    if (data->is64bit) {
        /* Loop over all section headers and look for .text */
        Elf64_Shdr *s_hdr = NULL;
        FIND_SECTION_HEADER(Elf64_Shdr, data, SHT_SYMTAB, ".symtab", s_hdr);
        if (s_hdr == NULL) {
            fprintf(stderr, "Couldn't find symbol table\n");
            return false;
        }
        Elf64_Sym *sym = NULL;
        FIND_SYMBOL(Elf64_Sym, data, s_hdr, STT_FUNC, fnname, sym);
        if (sym == NULL) {
            fprintf(stderr, "Unable to find function symbol \"%s\"\n", fnname);
            return false;
        }
        s_hdr = (Elf64_Shdr *) (data->buffer + data->shoff + bswap16(data, sym->st_shndx)*data->shentsize);
        view->address = bswaptl(data, sym->st_value);
        uint64_t off = view->address - bswaptl(data, s_hdr->sh_addr);
        view->data = data->buffer + bswaptl(data, s_hdr->sh_offset) + off;
        view->size = bswaptl(data, sym->st_size);
    } else {
        /* Loop over all section headers and look for .text */
        Elf32_Shdr *s_hdr = NULL;
        FIND_SECTION_HEADER(Elf32_Shdr, data, SHT_SYMTAB, ".symtab", s_hdr);
        if (s_hdr == NULL) {
            fprintf(stderr, "Couldn't find symbol table\n");
            return false;
        }
        Elf32_Sym *sym = NULL;
        FIND_SYMBOL(Elf32_Sym, data, s_hdr, STT_FUNC, fnname, sym);
        if (sym == NULL) {
            fprintf(stderr, "Unable to find function symbol \"%s\"\n", fnname);
            return false;
        }
        s_hdr = (Elf32_Shdr *) (data->buffer + data->shoff + bswap16(data, sym->st_shndx)*data->shentsize);
        view->address = bswaptl(data, sym->st_value);
        uint64_t off = view->address - bswaptl(data, s_hdr->sh_addr);
        view->data = data->buffer + bswaptl(data, s_hdr->sh_offset) + off;
        view->size = bswaptl(data, sym->st_size);
    }

    return true;
}
