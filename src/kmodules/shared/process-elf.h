#ifndef CLOUDBERRY_PROCESS_ELF_H
#define CLOUDBERRY_PROCESS_ELF_H
#include <stddef.h>
#include <stdint.h>
#include "mem-spaces.h"
struct elf_header {unsigned char ident[16];uint16_t type,machine;uint32_t version;uint64_t entry,phoff,shoff;uint32_t flags;uint16_t ehsize,phsize,phnum,shsize,shnum,shstr;};
struct elf_program {uint32_t type,flags;uint64_t offset,address,physical,filesz,memsz,alignment;};
static inline int process_elf_valid(const unsigned char *image,size_t bytes){
    if(((uintptr_t)image&7) || bytes<sizeof(struct elf_header))return 0;
    const struct elf_header *h=(const void *)image;
    if(h->ident[0]!=0x7f || h->ident[1]!='E' || h->ident[2]!='L' || h->ident[3]!='F' || h->ident[4]!=2 || h->ident[5]!=1 || h->ident[6]!=1 || h->type!=2 || h->machine!=62 || h->version!=1 || h->ehsize!=sizeof(*h) || h->phsize!=sizeof(struct elf_program) || !h->phnum || (h->phoff&7) || h->phoff>bytes || h->phnum>(bytes-h->phoff)/sizeof(struct elf_program))return 0;
    const struct elf_program *programs=(const void *)(image+h->phoff);int executable=0;
    for(unsigned i=0;i<h->phnum;++i){const struct elf_program *s=&programs[i];
        if(s->type==2 || s->type==3 || s->type==7)return 0; /* dynamic/interpreter/TLS require explicit support */
        if(s->type!=1)continue;
        if(!s->memsz){if(s->filesz)return 0;continue;}
        if(s->filesz>s->memsz || s->offset>bytes || s->filesz>bytes-s->offset || s->address<4096 || (s->address&4095) || (s->offset&4095) || s->address>=MEM_USER_LIMIT || s->memsz>MEM_USER_LIMIT-s->address || (s->flags&~7u) || (s->flags&3)==3 || !(s->flags&4) || (s->alignment && ((s->alignment&(s->alignment-1)) || s->alignment>4096)))return 0;
        uint64_t end=(s->address+s->memsz+4095)&~UINT64_C(4095);
        for(unsigned j=0;j<i;++j){const struct elf_program *q=&programs[j];if(q->type==1 && q->memsz && s->address<((q->address+q->memsz+4095)&~UINT64_C(4095)) && q->address<end)return 0;}
        if((s->flags&1) && h->entry>=s->address && h->entry<s->address+s->memsz)executable=1;
    }
    return executable;
}
#endif
