#include "spaces.h"
#define ADDR UINT64_C(0x000ffffffffff000)
#define P UINT64_C(1)
#define W UINT64_C(2)
#define U UINT64_C(4)
#define PS UINT64_C(128)
#define NX (UINT64_C(1)<<63)
static uint64_t *pointer(struct boot_pages *p,uint64_t at){return boot_physical_pointer(p,at&ADDR);}
static struct boot_reservation *data(struct boot_pages *p,uint64_t physical,uintptr_t owner){
    if(!physical || (physical&4095))return NULL;
    for(struct boot_reservation_chunk *c=p->reservations.first;c;c=c->next)
        for(size_t i=0;i<c->count;++i){struct boot_reservation *r=&c->entries[i];
            if(r->kind==BOOT_RES_PAGES && r->owner==owner && physical>=r->base && physical-r->base<r->bytes)return r;}
    return NULL;
}
static bool owned_table(struct boot_pages *p,uint64_t physical,uintptr_t owner){
    for(struct boot_reservation_chunk *c=p->reservations.first;c;c=c->next)
        for(size_t i=0;i<c->count;++i){struct boot_reservation *r=&c->entries[i];if(r->base==physical && r->bytes==4096 && r->kind==BOOT_RES_SPACE_TABLE && r->owner==owner && !r->mappings)return true;}
    return false;
}
static bool user_address(uintptr_t va){return va>=4096 && va<BOOT_USER_LIMIT && !(va&4095);}
static bool permissions(unsigned flags){return !(flags&~3u) && flags!=3;}
bool boot_spaces_prepare(struct boot_pages *p,uint64_t root){
    if(!p || !root || (root&~ADDR) || p->kernel_root)return false;
    uint64_t *top=pointer(p,root);
    /* Preseed the shared upper half: later kernel mappings update shared PDPTs,
     * even in slots that were empty when a process root was created. */
    for(unsigned i=256;i<512;++i){
        if(top[i]&P){if(top[i]&PS)return false;top[i]&=~U;}
        else{uint64_t page=boot_pages_reserve(p,1,BOOT_RES_PAGE_TABLE,0);if(!page)return false;top[i]=page|P|W;}
    }
    p->kernel_root=root;return true;
}
uint64_t boot_space_create(struct boot_pages *p,uintptr_t owner){
    if(!p || !p->kernel_root || !owner)return 0;
    uint64_t root=boot_pages_reserve(p,1,BOOT_RES_SPACE_TABLE,owner);if(!root)return 0;
    uint64_t *out=pointer(p,root),*in=pointer(p,p->kernel_root);
    for(unsigned i=256;i<512;++i)out[i]=in[i]&~U;
    return root;
}
static uint64_t *leaf(struct boot_pages *p,uint64_t root,uintptr_t va,uintptr_t owner,int create){
    if(!user_address(va))return NULL;
    uint64_t *table=pointer(p,root);
    for(unsigned shift=39;shift>12;shift-=9){uint64_t *entry=&table[(va>>shift)&511];
        if(!(*entry&P)){
            if(!create)return NULL;
            uint64_t child=boot_pages_reserve(p,1,BOOT_RES_SPACE_TABLE,owner);if(!child)return NULL;
            *entry=child|P|W|U;
        }else if((*entry&PS) || !(*entry&U) || !(*entry&W) || (*entry&NX))return NULL;
        table=pointer(p,*entry);
    }
    return &table[(va>>12)&511];
}
bool boot_space_map(struct boot_pages *p,uint64_t root,uintptr_t owner,uintptr_t va,uint64_t physical,unsigned flags){
    if(!p || !owned_table(p,root,owner) || !permissions(flags) || !user_address(va) || (physical&~ADDR))return false;
    struct boot_reservation *r=data(p,physical,owner);if(!r || r->mappings==UINT32_MAX)return false;
    uint64_t *entry=leaf(p,root,va,owner,1);if(!entry || (*entry&P))return false;
    *entry=physical|P|U|((flags&BOOT_USER_WRITE)?W:0)|((flags&BOOT_USER_EXEC)?0:NX);++r->mappings;return true;
}
bool boot_space_unmap(struct boot_pages *p,uint64_t root,uintptr_t owner,uintptr_t va){
    if(!p || !owned_table(p,root,owner))return false;
    uint64_t *entry=leaf(p,root,va,0,0);if(!entry || !(*entry&P) || !(*entry&U))return false;
    struct boot_reservation *r=data(p,*entry&ADDR,owner);if(!r || !r->mappings)return false;
    *entry=0;--r->mappings;return true;
}
bool boot_space_protect(struct boot_pages *p,uint64_t root,uintptr_t va,unsigned flags){
    if(!permissions(flags))return false;
    uint64_t *entry=leaf(p,root,va,0,0);if(!entry || !(*entry&P) || !(*entry&U))return false;
    *entry=(*entry&~(W|NX))|((flags&BOOT_USER_WRITE)?W:0)|((flags&BOOT_USER_EXEC)?0:NX);return true;
}
bool boot_space_query(struct boot_pages *p,uint64_t root,uintptr_t va,uint64_t *physical,unsigned *flags){
    if(!p || !physical || !flags)return false;
    uint64_t *entry=leaf(p,root,va,0,0);if(!entry || !(*entry&P) || !(*entry&U))return false;
    *physical=*entry&ADDR;*flags=((*entry&W)?BOOT_USER_WRITE:0)|((*entry&NX)?0:BOOT_USER_EXEC);return true;
}
/* Returns after a single mutation. No data/table memory is reused while an
 * active root could retain translations; the kernel enforces inactivity. */
static unsigned reap(struct boot_pages *p,uint64_t root,uintptr_t owner,unsigned level){
    uint64_t *table=pointer(p,root);unsigned count=level==4?256:512;
    for(unsigned i=0;i<count;++i)if(table[i]&P){
        if(level==1){struct boot_reservation *r=data(p,table[i]&ADDR,owner);if(!r || !r->mappings)return 0;--r->mappings;table[i]=0;return 1;}
        if((table[i]&PS) || !owned_table(p,table[i]&ADDR,owner))return 0;
        unsigned result=reap(p,table[i]&ADDR,owner,level-1);if(result==2){table[i]=0;return 1;}return result;
    }
    return boot_pages_release(p,root,1,BOOT_RES_SPACE_TABLE,owner)?2:0;
}
unsigned boot_space_reap(struct boot_pages *p,uint64_t root,uintptr_t owner){
    return p && owned_table(p,root,owner)?reap(p,root,owner,4):0;
}
