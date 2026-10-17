#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../boot/spaces.h"
static _Alignas(4096) unsigned char memory[16*1024*1024];
static struct boot_pages pages;
static size_t table_count(void){size_t n=0;for(struct boot_reservation_chunk *c=pages.reservations.first;c;c=c->next)for(size_t i=0;i<c->count;++i)if(c->entries[i].kind==BOOT_RES_SPACE_TABLE)++n;return n;}
static size_t references(void){size_t n=0;for(struct boot_reservation_chunk *c=pages.reservations.first;c;c=c->next)for(size_t i=0;i<c->count;++i)n+=c->entries[i].mappings;return n;}
static unsigned step(uint64_t root,uintptr_t owner,struct boot_space_reaper *walk){
    size_t before=table_count()+references();unsigned result=boot_space_reap(&pages,root,owner,walk);
    size_t after=table_count()+references();assert(after<=before && before-after<=BOOT_SPACE_REAP_BATCH);return result;
}
static void destroy(uint64_t root,uintptr_t owner){struct boot_space_reaper walk={0};unsigned result,n=0;do{result=step(root,owner,&walk);assert(++n<5000);}while(result==1);assert(result==2);}
static void continuation_tests(void){
    uint64_t root=boot_space_create(&pages,42),data=boot_pages_reserve(&pages,512,BOOT_RES_PAGES,42);assert(root && data);
    for(unsigned i=0;i<512;++i)assert(boot_space_map(&pages,root,42,UINT64_C(0x400000)+(uint64_t)i*4096,data+(uint64_t)i*4096,0));
    struct boot_space_reaper walk={0};assert(step(root,42,&walk)==1 && references()==512-BOOT_SPACE_REAP_BATCH);
    struct boot_space_reaper before=walk;
    assert(!step(root,43,&walk) && !memcmp(&before,&walk,sizeof(walk)));
    /* Failure must retain its position, not skip a malformed entry on retry. */
    uint64_t *leaf=boot_physical_pointer(&pages,walk.tables[walk.depth]);unsigned index=walk.next[walk.depth];uint64_t saved=leaf[index];
    leaf[index]&=~UINT64_C(4);assert(!step(root,42,&walk) && !memcmp(&before,&walk,sizeof(walk)));leaf[index]=saved;
    unsigned calls=1,result;do{result=step(root,42,&walk);assert(++calls<100);}while(result==1);
    assert(result==2 && !table_count() && !references() && boot_pages_release(&pages,data,512,BOOT_RES_PAGES,42));
    /* Sparse high slots cross every hierarchy boundary and force scan-only
     * continuations. Shared upper-half tables must remain untouched. */
    root=boot_space_create(&pages,42);data=boot_pages_reserve(&pages,16,BOOT_RES_PAGES,42);assert(root && data);
    for(unsigned i=0;i<15;++i)assert(boot_space_map(&pages,root,42,((uint64_t)i<<39)+UINT64_C(0x7ff000),data+(uint64_t)i*4096,0));
    assert(boot_space_map(&pages,root,42,BOOT_USER_LIMIT-4096,data+15*4096,0));destroy(root,42);
    assert(!table_count() && !references() && boot_pages_release(&pages,data,16,BOOT_RES_PAGES,42));
}
int main(void){
    struct boot_memory_entry region={4096,sizeof(memory),0};struct boot_memory_entry *entries[]={&region};struct boot_memory_map map={0,1,entries};
    assert(boot_pages_init(&pages,&map,(uintptr_t)memory-4096));
    uint64_t master=boot_pages_reserve(&pages,1,BOOT_RES_OTHER,0);uint64_t *top=boot_physical_pointer(&pages,master);
    uint64_t existing=boot_pages_reserve(&pages,1,BOOT_RES_PAGE_TABLE,0);top[511]=existing|7;
    assert(boot_spaces_prepare(&pages,master) && !(top[511]&4));
    for(unsigned i=256;i<512;++i)assert((top[i]&7)==3);
    assert(!boot_spaces_prepare(&pages,master) && !boot_space_create(&pages,0));
    uint64_t a=boot_space_create(&pages,42),b=boot_space_create(&pages,43),c=boot_space_create(&pages,42);assert(a && b && c && table_count()==3);
    uint64_t *at=boot_physical_pointer(&pages,a),*bt=boot_physical_pointer(&pages,b);
    for(unsigned i=0;i<256;++i)assert(!at[i] && !bt[i]);
    for(unsigned i=256;i<512;++i)assert(at[i]==top[i] && bt[i]==top[i] && !(at[i]&4));
    /* A future kernel mapping in a previously empty slot is shared immediately. */
    uint64_t *shared=boot_physical_pointer(&pages,top[350]&~UINT64_C(4095));shared[9]=0x123003;
    assert(((uint64_t *)boot_physical_pointer(&pages,at[350]&~UINT64_C(4095)))[9]==0x123003);
    uint64_t frame=boot_pages_reserve(&pages,2,BOOT_RES_PAGES,42),other=boot_pages_reserve(&pages,1,BOOT_RES_PAGES,43);assert(frame && other);
    uint64_t va=UINT64_C(0x400000),physical=0;unsigned flags=99;
    assert(!boot_space_map(&pages,a,42,0,frame,1));assert(!boot_space_map(&pages,a,42,va+1,frame,1));
    assert(!boot_space_map(&pages,a,42,BOOT_USER_LIMIT,frame,1));assert(!boot_space_map(&pages,a,42,UINT64_C(0xffff900000000000),frame,1));
    assert(!boot_space_map(&pages,a,43,va,other,1));assert(!boot_space_map(&pages,a,42,va,other,1));
    assert(!boot_space_map(&pages,a,42,va,frame,3) && !boot_space_map(&pages,a,42,va,frame,4));
    assert(boot_space_map(&pages,a,42,va,frame,BOOT_USER_WRITE));assert(!boot_space_map(&pages,a,42,va,frame+4096,1));
    assert(boot_space_map(&pages,b,43,va,other,BOOT_USER_EXEC));assert(boot_space_map(&pages,c,42,va,frame,0));
    assert(boot_space_query(&pages,a,va,&physical,&flags) && physical==frame && flags==BOOT_USER_WRITE);
    assert(boot_space_query(&pages,b,va,&physical,&flags) && physical==other && flags==BOOT_USER_EXEC);
    assert(!boot_space_query(&pages,c,va+4096,&physical,&flags));
    assert(!boot_pages_release(&pages,frame,2,BOOT_RES_PAGES,42));
    assert(boot_space_protect(&pages,a,va,BOOT_USER_EXEC));assert(!boot_space_protect(&pages,a,va,3));
    assert(boot_space_query(&pages,a,va,&physical,&flags) && flags==BOOT_USER_EXEC);
    assert(boot_space_unmap(&pages,a,42,va) && !boot_space_unmap(&pages,a,42,va));
    assert(!boot_pages_release(&pages,frame,2,BOOT_RES_PAGES,42));destroy(c,42);
    assert(boot_pages_release(&pages,frame,2,BOOT_RES_PAGES,42));
    destroy(a,42);destroy(b,43);assert(!table_count());assert(boot_pages_release(&pages,other,1,BOOT_RES_PAGES,43));
    continuation_tests();
    /* Exhaustion after the root is allocated: partial intermediate tables stay
     * reachable until bounded inactive-root disposal; no data pin is leaked. */
    a=boot_space_create(&pages,42);frame=boot_pages_reserve(&pages,1,BOOT_RES_PAGES,42);assert(a && frame);
    uint64_t spare=boot_pages_reserve(&pages,2,BOOT_RES_PAGES,99);assert(spare);
    uint64_t held[5000],counts[5000];size_t n=0;
    while(n<5000 && (held[n]=boot_pages_reserve(&pages,16,BOOT_RES_PAGES,99))){counts[n]=16;++n;}
    while(n<5000 && (held[n]=boot_pages_reserve(&pages,1,BOOT_RES_PAGES,99))){counts[n]=1;++n;}assert(n && n<5000);
    assert(boot_pages_release(&pages,spare,2,BOOT_RES_PAGES,99));
    assert(!boot_space_map(&pages,a,42,va,frame,1) && table_count()>1);destroy(a,42);assert(!table_count());assert(boot_pages_release(&pages,frame,1,BOOT_RES_PAGES,42));
    for(size_t i=0;i<n;++i)assert(boot_pages_release(&pages,held[i],counts[i],BOOT_RES_PAGES,99));
    puts("PASS address spaces: private roots, shared supervisor upper half, owned user mappings, W^X/bounds, mapping references, permissions, teardown and OOM");return 0;
}
