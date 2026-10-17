/* Production parser/init with modeled allocation and atomic hub publication. */
#include "../kmodules/shared/config.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static struct {struct kernel_surfaces hub;struct kernel_surface methods[4];struct kernel_hub_table tables[8];} fixture;
static unsigned live,publications;static int budget=-1,reject_publish;
static struct kernel_surface_result cpu(MODULE_ARGS){MODULE_UNUSED();return module_result(0,1);}
static struct kernel_surface_result allocate(MODULE_ARGS){MODULE_UNUSED();if(!budget)return module_result(0,0);if(budget>0)--budget;void *p=calloc(b,c);if(p)++live;return module_result((uintptr_t)p,p!=NULL);}
static struct kernel_surface_result release(MODULE_ARGS){MODULE_UNUSED();assert(b && live);free((void *)b);--live;return module_result(1,1);}
static struct kernel_surface_result publish(MODULE_ARGS){
    MODULE_UNUSED();++publications;if(reject_publish || b>8-fixture.hub.table_count)return module_result(0,0);
    const struct kernel_hub_table *t=(void *)a;
    for(size_t i=0;i<b;++i){assert(t[i].bytes==sizeof(struct config_value));if(kernel_hub_find_table(&fixture.hub,t[i].name))return module_result(0,0);}
    for(size_t i=0;i<b;++i)fixture.tables[fixture.hub.table_count++]=t[i];
    return module_result(b,1);
}
static void reset(void){
    if(owned){for(size_t i=0;i<ITEMS;++i)if(owned[i].string)release(0,(uintptr_t)owned[i].string,0,0,0,0);release(0,(uintptr_t)owned,0,0,0,0);owned=NULL;}
    assert(!live);info=(struct config_info){0};fixture.hub.table_count=0;budget=-1;publications=0;reject_publish=0;
}
static struct kernel_surface_result parse(const char *text,size_t n){return config_surface_init((uintptr_t)&fixture.hub,1,(uintptr_t)text,n,0,0);}
static const struct config_value *get(const char *key){const struct kernel_hub_table *t=kernel_hub_find_table(&fixture.hub,key);assert(t);return (const void *)t->address;}
int main(void){
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.major=KERNEL_HUB_ABI,.count=4,.header_bytes=offsetof(__typeof__(fixture),methods),.tables_offset=offsetof(__typeof__(fixture),tables),.table_count=0,.table_capacity=8};
    const char *names[]={"cpu.control","mem.calloc","mem.free","hub.publish_tables"};kernel_surface_call calls[]={cpu,allocate,release,publish};
    for(unsigned i=0;i<4;++i){strcpy(fixture.methods[i].name,names[i]);fixture.methods[i].major=1;fixture.methods[i].call=calls[i];}
    char input[]=" # comment\r\nversion = 1\r\nboot.show_inventory=false\ndrivers.directory=/syst\xc3\xa9m\nstorage.timeout_ms=4294967295\ndrivers.partition_guid=550E8400-e29b-41d4-a716-446655440000";
    assert(!parse(input,sizeof(input)-1).status && info.ready && info.count==5 && live==2 && publications==1);
    const struct config_value *v=get("config.version");assert(v->major==1 && v->type==CONFIG_UINT && v->bytes==8 && *(const uint64_t *)v->data==1);
    assert(!*(const uint8_t *)get("config.boot.show_inventory")->data);
    const struct config_guid *g=get("config.drivers.partition_guid")->data;assert(g->a==0x550e8400 && g->b==0xe29b && g->c==0x41d4 && g->d[0]==0xa7 && g->d[7]==0);
    memset(input,'?',sizeof(input));assert(!strcmp(get("config.drivers.directory")->data,"/syst\xc3\xa9m"));
    assert(parse("version=1",9).value==CONFIG_ALREADY_READY && info.ready && live==2);reset();
    assert(!parse("version=1",9).status && info.count==4 && !kernel_hub_find_table(&fixture.hub,"config.drivers.partition_guid"));
    assert(*(const uint8_t *)get("config.boot.show_inventory")->data==1 && *(const uint64_t *)get("config.storage.timeout_ms")->data==1000);reset();
    struct {const char *text;unsigned error,line;} bad[]={
        {"",CONFIG_REQUIRED,0},{"version",CONFIG_SYNTAX,1},{"unknown=1",CONFIG_UNKNOWN,1},
        {"version=1\n version = 1",CONFIG_DUPLICATE,2},{"Version=1",CONFIG_UNKNOWN,1},
        {"version=2",CONFIG_VALUE,1},{"version=1\nboot.show_inventory=True",CONFIG_VALUE,2},
        {"version=1\nstorage.timeout_ms=-1",CONFIG_VALUE,2},{"version=1\nstorage.timeout_ms=18446744073709551616",CONFIG_VALUE,2},
        {"version=1\nstorage.timeout_ms=4294967296",CONFIG_VALUE,2},{"version=1\nstorage.timeout_ms=0x10",CONFIG_VALUE,2},
        {"version=1\ndrivers.partition_guid=550e8400-e29b-41d4-a716-44665544000z",CONFIG_VALUE,2},
        {"version=1\ndrivers.partition_guid=550e8400e29b-41d4-a716-446655440000",CONFIG_VALUE,2},
        {"version=1\ndrivers.directory=\xc0\x80",CONFIG_VALUE,2},{"version=1\ndrivers.directory=\xed\xa0\x80",CONFIG_VALUE,2},
        {"version=1\ndrivers.directory=\xf4\x90\x80\x80",CONFIG_VALUE,2}
    };
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i){struct kernel_surface_result r=parse(bad[i].text,strlen(bad[i].text));assert(r.status && r.value==bad[i].error && info.error==r.value && info.line==bad[i].line && !live && !publications && !fixture.hub.table_count);reset();}
    const char nul[]="version=1\n#comment\0hidden";assert(parse(nul,sizeof(nul)-1).value==CONFIG_SYNTAX && !live);reset();
    uint64_t number;assert(integer("18446744073709551615",20,&number) && number==UINT64_MAX);
    for(int i=0;i<3;++i){budget=i;assert(parse("version=1",9).value==CONFIG_MEMORY && !live && !publications && !fixture.hub.table_count);reset();}
    reject_publish=1;assert(parse("version=1",9).value==CONFIG_PUBLICATION && !live && !fixture.hub.table_count);reset();
    strcpy(fixture.tables[0].name,"config.version");fixture.hub.table_count=1;
    assert(parse("version=1",9).value==CONFIG_PUBLICATION && !live && fixture.hub.table_count==1);reset();
    puts("PASS config: typed values/defaults, owned lifetime, strict syntax/UTF-8/GUID/ranges, duplicates, missing keys, OOM rollback and publication failure");
}
