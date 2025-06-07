#pragma once

#include <stdint.h>
#include <string.h>

typedef uint32_t cpu_type_t;
typedef uint32_t cpu_subtype_t;
// Types for memory mapping
typedef uintptr_t vm_offset_t;
typedef size_t    vm_size_t;

// ABI64 flag used in cputype
#define CPU_ARCH_ABI64 0x01000000

typedef enum {
    NX_UnknownByteOrder,
    NX_LittleEndian,
    NX_BigEndian
} NXByteOrder;

typedef struct {
    const char *name;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    NXByteOrder byteorder;
    const char *description;
} NXArchInfo;

static const NXArchInfo *NXGetLocalArchInfo(void) {
    static const NXArchInfo x86_64_host = {
        "x86_64", 0x01000007, 3, NX_LittleEndian, NULL
    };
    return &x86_64_host;
}

// Mach-O constants
#define MH_MAGIC        0xfeedface
#define MH_MAGIC_64     0xfeedfacf
#define MH_KEXT_BUNDLE  0xb
#define MH_INCRLINK     0x2

#define LC_SEGMENT      0x1
#define LC_SEGMENT_64   0x19
#define LC_SYMTAB       0x2
#define LC_UUID         0x1b

#define SG_NORELOC      0x4

#define N_EXT           0x01
#define N_UNDF          0x0
#define N_INDR          0xa
#define N_DESC_DISCARDED 0x20

#define SEG_LINKEDIT    "__LINKEDIT"

#define TRUE 1
#define FALSE 0
typedef int boolean_t;

#define mach_vm_round_page(x) (((x) + 0xfff) & ~0xfff)

// Mach-O structs (simplified)
struct mach_header {
    uint32_t magic;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
};

struct segment_command {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct nlist {
    union {
        uint32_t n_strx;
    } n_un;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint32_t n_value;
};

struct symtab_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
};

struct uuid_command {
    uint32_t cmd;
    uint32_t cmdsize;
    uint8_t uuid[16];
};

struct mach_header_64 {
    uint32_t magic;
    cpu_type_t cputype;
    cpu_subtype_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
};

struct segment_command_64 {
    uint32_t cmd;
    uint32_t cmdsize;
    char segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
};

struct nlist_64 {
    union {
        uint32_t n_strx;
    } n_un;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint64_t n_value;
};

// Dummy byte swapping functions for Linux (no-op)
static inline void swap_mach_header(struct mach_header *h, NXByteOrder order) {}
static inline void swap_mach_header_64(struct mach_header_64 *h, NXByteOrder order) {}
static inline void swap_segment_command(struct segment_command *s, NXByteOrder order) {}
static inline void swap_segment_command_64(struct segment_command_64 *s, NXByteOrder order) {}
static inline void swap_symtab_command(struct symtab_command *s, NXByteOrder order) {}
static inline void swap_uuid_command(struct uuid_command *s, NXByteOrder order) {}
static inline void swap_nlist(struct nlist *n, int count, NXByteOrder order) {}
static inline void swap_nlist_64(struct nlist_64 *n, int count, NXByteOrder order) {}

#define MH_CIGAM      0xcefaedfe
#define MH_CIGAM_64   0xcffaedfe

static inline uint32_t OSSwapInt32(uint32_t x) {
    return __builtin_bswap32(x);
}
