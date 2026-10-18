#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../handlers/keyboard/hid.h"
static struct cb_keyboard_event events[512];static size_t count;
static uint64_t emit(void *context,uint32_t kind,uint16_t usage,uint32_t cp,uint32_t modifiers,uint64_t origin,uint32_t flags){(void)context;assert(count<512);struct cb_keyboard_event *e=&events[count++];*e=(struct cb_keyboard_event){.version={sizeof(*e),1,0},.sequence=count,.device_id=1,.timestamp_ns=1,.kind=kind,.usage_page=usage?7:0,.usage=usage,.codepoint=cp,.modifiers=modifiers,.origin_sequence=origin,.flags=flags};assert(cb_keyboard_event_valid(e));return count;}
static struct keyboard_layout load(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);rewind(f);char *json=malloc(n);assert(json && fread(json,1,n,f)==(size_t)n);fclose(f);struct keyboard_layout layout;assert(keyboard_layout_parse(&layout,json,n));free(json);return layout;}
int main(int argc,char **argv){assert(argc==3);struct keyboard_layout ibm=load(argv[1]),br=load(argv[2]);struct keyboard_hid s={0};uint8_t report[8]={0,0,4,0,0,0,0,0};assert(keyboard_report(&s,&ibm,report,emit,NULL));assert(count==2 && events[0].kind==CB_KEY_DOWN && events[1].kind==CB_KEY_TEXT && events[1].codepoint=='a' && events[1].origin_sequence==events[0].sequence);
    assert(keyboard_report(&s,&ibm,report,emit,NULL) && count==2);keyboard_repeat(&s,&ibm,emit,NULL);assert(count==4 && events[2].kind==CB_KEY_REPEAT && events[3].codepoint=='a' && events[3].flags==CB_KEY_SYNTHETIC);
    report[0]=2;report[2]=5;assert(keyboard_report(&s,&ibm,report,emit,NULL));assert(events[count-1].codepoint=='B' && s.pressed[225] && s.modifiers==2 && !s.pressed[4]);
    report[0]=1;report[2]=44;assert(keyboard_report(&s,&ibm,report,emit,NULL) && events[count-1].kind==CB_KEY_TEXT && !events[count-1].codepoint);
    report[0]=0;report[2]=79;assert(keyboard_report(&s,&ibm,report,emit,NULL));assert(events[count-1].kind==CB_KEY_DOWN && events[count-1].usage==79);
    report[2]=report[3]=6;assert(keyboard_report(&s,&ibm,report,emit,NULL));size_t before=count;assert(keyboard_report(&s,&ibm,report,emit,NULL) && count==before);
    report[2]=1;assert(!keyboard_report(&s,&ibm,report,emit,NULL) && events[count-1].kind==CB_KEY_RESET && !s.pressed[6] && !s.modifiers);
    report[2]=51;report[3]=0;assert(keyboard_report(&s,&br,report,emit,NULL) && events[count-1].codepoint==0xe7);
    report[2]=47;assert(keyboard_report(&s,&br,report,emit,NULL) && s.accent==0x301);report[2]=4;assert(keyboard_report(&s,&br,report,emit,NULL));assert(events[count-2].codepoint=='a' && events[count-1].codepoint==0x301 && !s.accent);
    report[0]=64;report[2]=20;assert(keyboard_report(&s,&br,report,emit,NULL) && events[count-1].codepoint=='/');keyboard_reset(&s,emit,NULL);assert(!s.pressed[230] && !s.pressed[20] && !s.repeat_key);
    const char *bad[]={"{}","{\"version\":1,\"keys\":[[4,55296,null,null,null]]}","{\"version\":1,\"keys\":[[256,1,2,3,4]]}","{\"version\":1,\"keys\":[[4,97,65,null,null],[4,1,2,3,4]]}","{\"version\":1,\"keys\":[],\"dead\":[[4,0]]}"};for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);++i){struct keyboard_layout l;assert(!keyboard_layout_parse(&l,bad[i],strlen(bad[i])));}
    const char *selected;size_t selected_bytes;assert(keyboard_config_layout("layout=abnt2\n",13,&selected,&selected_bytes) && selected_bytes==5);assert(!keyboard_config_layout("layout=../bad\n",14,&selected,&selected_bytes));assert(!keyboard_config_layout("layout=ibm\nlayout=abnt2\n",24,&selected,&selected_bytes));
    puts("PASS keyboard: IBM/ABNT2 definitions, modifiers, repeats, Unicode/NUL/combining text, structured arrows, rollover/reset and malformed layouts");return 0;
}
