#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/provider-definition.h"
#include "../interfaces/startup.h"
static int parse(const char *text){char *owned=malloc(strlen(text)+1);assert(owned);strcpy(owned,text);struct provider_definition d;int valid=provider_definition_parse(owned,strlen(text),&d);if(valid)assert(d.executable && d.manifest && d.stack_bytes);free(owned);return valid;}
int main(void){
    const char *base="version=1\nexecutable=handler.elf\nmanifest=manifest.json\ninstallation_id=01020304050607080910111213141516\n";
    assert(parse(base));char text[512];snprintf(text,sizeof(text),"%sready=/@custom\ncpu=3\nstack_bytes=262144\nfiles=config.cnf,data/one.bin\n",base);assert(parse(text));
    const char *bad[]={"version=1\n", "version=2\n", "version=1\nversion=1\n", "executable=../escape\n", "executable=/absolute\n", "ready=plain\n", "stack_bytes=0\n", "cpu=4294967296\n", "unknown=1\n", "installation_id=00000000000000000000000000000000\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){snprintf(text,sizeof(text),"%s%s",base,bad[i]);assert(!parse(text));}
    assert(pd_path("sub/thing.elf",0) && !pd_path("sub//thing",0) && !pd_path("sub/../thing",0) && !pd_path(".",1));
    unsigned char packet[128]={0};struct cb_startup h={.version={sizeof(h),1,0},.bytes=sizeof(h)+sizeof(struct cb_startup_file)+4,.files=1};struct cb_startup_file f={1,3};memcpy(packet,&h,sizeof(h));memcpy(packet+sizeof(h),&f,sizeof(f));memcpy(packet+sizeof(h)+sizeof(f),"xabc",4);
    const void *data;size_t bytes;assert(cb_startup_find(packet,h.bytes,"x",&data,&bytes) && bytes==3 && !memcmp(data,"abc",3));assert(!cb_startup_find(packet,h.bytes,"missing",&data,&bytes));
    f.data_bytes=UINT64_MAX;memcpy(packet+sizeof(h),&f,sizeof(f));assert(!cb_startup_find(packet,h.bytes,"x",&data,&bytes));f.data_bytes=3;memcpy(packet+sizeof(h),&f,sizeof(f));assert(!cb_startup_find(packet,h.bytes-1,"x",&data,&bytes));h.files=UINT64_MAX;memcpy(packet,&h,sizeof(h));assert(!cb_startup_find(packet,h.bytes,"x",&data,&bytes));
    puts("PASS provider definitions/startup: generic identity/paths/settings, unknown/duplicate refusal and copied file framing/overflow");return 0;
}
