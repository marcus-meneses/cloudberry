#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../kmodules/shared/process-elf.h"
int main(void){
    _Alignas(8) unsigned char image[8192]={0};struct elf_header *h=(void *)image;
    const unsigned char magic[]={0x7f,'E','L','F',2,1,1};memcpy(h->ident,magic,sizeof(magic));
    h->type=2;h->machine=62;h->version=1;h->ehsize=sizeof(*h);h->phsize=sizeof(struct elf_program);h->phoff=sizeof(*h);h->phnum=1;h->entry=0x400000;
    struct elf_program *p=(void *)(image+h->phoff);*p=(struct elf_program){.type=1,.flags=5,.offset=4096,.address=0x400000,.filesz=1,.memsz=4096,.alignment=4096};
    assert(process_elf_valid(image,sizeof(image)));
    assert(!process_elf_valid(image,1));h->machine=3;assert(!process_elf_valid(image,sizeof(image)));h->machine=62;
    h->phoff=UINT64_MAX;assert(!process_elf_valid(image,sizeof(image)));h->phoff=sizeof(*h)+1;assert(!process_elf_valid(image,sizeof(image)));h->phoff=sizeof(*h);
    p->filesz=4097;assert(!process_elf_valid(image,sizeof(image)));p->filesz=1;
    p->offset=8192;assert(!process_elf_valid(image,sizeof(image)));p->offset=4096;
    p->flags=7;assert(!process_elf_valid(image,sizeof(image)));p->flags=5;
    p->address=MEM_USER_LIMIT;assert(!process_elf_valid(image,sizeof(image)));p->address=0x400000;
    p->memsz=UINT64_MAX;assert(!process_elf_valid(image,sizeof(image)));p->memsz=4096;
    p->alignment=3;assert(!process_elf_valid(image,sizeof(image)));p->alignment=4096;
    h->entry=0x500000;assert(!process_elf_valid(image,sizeof(image)));h->entry=0x400000;
    h->phnum=2;p[1]=p[0];assert(!process_elf_valid(image,sizeof(image)));p[1].type=3;assert(!process_elf_valid(image,sizeof(image)));p[1].type=7;assert(!process_elf_valid(image,sizeof(image)));p[1].type=2;assert(!process_elf_valid(image,sizeof(image)));
    p[1]=(struct elf_program){.type=1,.filesz=1};assert(!process_elf_valid(image,sizeof(image)));
    puts("PASS process ELF: bounds, ABI, entry, permissions, overlaps, unsupported dynamic/TLS and malformed ranges");return 0;
}
