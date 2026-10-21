#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hid-fixtures.h"
#include "../handlers/keyboard/report.h"
#include "../handlers/mouse/hid.h"
static long live,budget=-1;
static void *alloc(void *ctx,size_t n){(void)ctx;if(!budget)return NULL;if(budget>0)--budget;void *p=calloc(1,n);if(p)++live;return p;}
static void release(void *ctx,void *p){(void)ctx;if(p){--live;free(p);}}
int main(void){
    struct hid_report_info info;assert(hid_report_parse(&info,hid_combined,sizeof(hid_combined),alloc,release,NULL,NULL,NULL));assert(!live && info.numbered && info.input_bytes==9);
    unsigned char key[]={1,2,0,4,0,0,0,0,0},mouse[]={2,1,12,0xf9},bitmap[]={3,0xff};
    assert(hid_report_route(&info,key,sizeof(key))==HID_KEYBOARD && hid_report_route(&info,mouse,sizeof(mouse))==HID_MOUSE && hid_report_route(&info,key,2)==-1);
    struct keyboard_descriptor d;struct mouse_hid m;struct mouse_hid_input value;uint8_t pressed[256];
    assert(keyboard_descriptor_parse(&d,hid_combined,sizeof(hid_combined),alloc,release,NULL));assert(mouse_hid_parse(&m,hid_combined,sizeof(hid_combined),alloc,release,NULL));
    assert(keyboard_descriptor_decode(&d,key,sizeof(key),pressed)==1 && pressed[4] && pressed[225]);assert(mouse_hid_decode(&m,key,sizeof(key),&value)==0);
    assert(keyboard_descriptor_decode(&d,mouse,sizeof(mouse),pressed)==0);assert(mouse_hid_decode(&m,mouse,sizeof(mouse),&value)==1 && value.dx==12 && value.dy==-7 && value.buttons==1);
    assert(keyboard_descriptor_decode(&d,bitmap,sizeof(bitmap),pressed)==1);for(unsigned i=4;i<12;++i)assert(pressed[i]);assert(pressed[225]);
    memset(key+1,0,sizeof(key)-1);assert(keyboard_descriptor_decode(&d,key,sizeof(key),pressed)==1 && pressed[4] && !pressed[225]);
    bitmap[1]=0;assert(keyboard_descriptor_decode(&d,bitmap,sizeof(bitmap),pressed)==1 && !pressed[4]);key[3]=1;assert(keyboard_descriptor_decode(&d,key,sizeof(key),pressed)==-1 && !pressed[4]);
    keyboard_descriptor_free(&d);mouse_hid_free(&m);assert(!live);
    assert(hid_report_parse(&info,hid_shared_report,sizeof(hid_shared_report),alloc,release,NULL,NULL,NULL));unsigned char shared[]={0xff,20,0xfe};assert(hid_report_route(&info,shared,sizeof(shared))==(HID_KEYBOARD|HID_MOUSE));
    assert(keyboard_descriptor_parse(&d,hid_shared_report,sizeof(hid_shared_report),alloc,release,NULL));assert(mouse_hid_parse(&m,hid_shared_report,sizeof(hid_shared_report),alloc,release,NULL));assert(keyboard_descriptor_decode(&d,shared,sizeof(shared),pressed)==1);for(unsigned i=4;i<12;++i)assert(pressed[i]);assert(mouse_hid_decode(&m,shared,sizeof(shared),&value)==1 && value.dx==20 && value.dy==-2);assert(keyboard_descriptor_decode(&d,shared,1,pressed)==-1);keyboard_descriptor_free(&d);mouse_hid_free(&m);assert(!live);
    /* A selector inside the logical range but outside its usage set is invalid;
       it must not clamp to the last declared key and synthesize a false press. */
    unsigned char sparse[sizeof(hid_combined)];memcpy(sparse,hid_combined,sizeof(sparse));
    for(size_t i=0;i+1<sizeof(sparse);++i)if(sparse[i]==0x25 && sparse[i+1]==0x65){sparse[i+1]=0x7f;break;}
    assert(keyboard_descriptor_parse(&d,sparse,sizeof(sparse),alloc,release,NULL));key[3]=0x70;assert(keyboard_descriptor_decode(&d,key,sizeof(key),pressed)==-1);keyboard_descriptor_free(&d);assert(!live);
    for(long n=0;n<12;++n){budget=n;if(keyboard_descriptor_parse(&d,hid_combined,sizeof(hid_combined),alloc,release,NULL))keyboard_descriptor_free(&d);assert(!live);}budget=-1;
    for(size_t n=0;n<sizeof(hid_combined);++n){if(keyboard_descriptor_parse(&d,hid_combined,n,alloc,release,NULL))keyboard_descriptor_free(&d);assert(!live);}
    unsigned seed=1;unsigned char fuzz[128];for(unsigned n=0;n<2000;++n){for(unsigned i=0;i<sizeof(fuzz);++i){seed=seed*1664525+1013904223;fuzz[i]=seed>>24;}if(keyboard_descriptor_parse(&d,fuzz,n%sizeof(fuzz),alloc,release,NULL))keyboard_descriptor_free(&d);assert(!live);}
    puts("PASS HID demux: report IDs, shared reports, both codecs, NKRO, multi-report state, truncation, rollover, malformed input and allocation rollback");return 0;
}
