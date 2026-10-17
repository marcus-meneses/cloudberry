#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../kmodules/shared/manifest.h"
struct context {unsigned resources,operations;};
static int resource(void *c,const char *name,const char *scope,int publish){struct context *x=c;assert(name[0]=='@' || name[0]=='/');assert(!strcmp(scope,"self") || !strcmp(scope,"keyboard"));assert(publish==0 || publish==1);++x->resources;return 1;}
static int operation(void *c,const char *name){struct context *x=c;assert(*name);++x->operations;return 1;}
static int parse(const char *text,struct context *out){char *copy=malloc(strlen(text)+1);assert(copy);strcpy(copy,text);struct manifest_parser p={copy,copy+strlen(text),out,resource,operation};int ok=manifest_parse(&p);free(copy);return ok;}
int main(void){struct context c={0};assert(parse("{\"resources\":[{\"operations\":[\"read\",\"wr\\u0069te\"],\"name\":\"/@test\",\"scope\":\"keyboard\",\"publish\":true}],\"version\":1}",&c));assert(c.resources==1 && c.operations==2);
    const char *bad[]={"", "{}", "{\"version\":10,\"resources\":[]}","{\"version\":1,\"version\":1,\"resources\":[]}","{\"version\":1,\"resources\":[{\"name\":\"@x\",\"operations\":[\"read\",]}]}","{\"version\":1,\"resources\":[]}junk","{\"version\":1,\"resources\":[{\"name\":\"@x\",\"operations\":[],\"publish\":truth}]}","{\"version\":1,\"resources\":[{\"name\":\"@x\",\"operations\":[],\"unknown\":0}]}"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){c=(struct context){0};assert(!parse(bad[i],&c));}
    char many[8192];strcpy(many,"{\"version\":1,\"resources\":[");for(unsigned i=0;i<64;++i){char row[96];snprintf(row,sizeof(row),"%s{\"name\":\"@device%u\",\"operations\":[\"read\"]}",i?",":"",i);strcat(many,row);}strcat(many,"]}");c=(struct context){0};assert(parse(many,&c) && c.resources==64 && c.operations==64);
    puts("PASS manifest: strict grammar, reordered/escaped fields, malformed input and 64 resources without a count ceiling");return 0;
}
