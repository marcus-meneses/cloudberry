#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../handlers/mouse/hid.h"
#include "../interfaces/mouse.h"
static long budget=-1,live;
static void *alloc(void *ctx,size_t bytes){(void)ctx;if(!budget)return NULL;if(budget>0)--budget;void *p=calloc(1,bytes);if(p)++live;return p;}
static void release(void *ctx,void *p){(void)ctx;if(p){assert(live>0);--live;free(p);}}
static const unsigned char basic[]={5,1,9,2,0xa1,1,9,1,0xa1,0,5,9,0x19,1,0x29,8,0x15,0,0x25,1,0x75,1,0x95,8,0x81,2,5,1,9,0x30,9,0x31,9,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,5,0x0c,0x0a,0x38,2,0x95,1,0x81,6,0xc0,0xc0};
static const unsigned char highres[]={5,1,9,2,0xa1,1,0x85,1,9,0x30,9,0x31,0x16,0,0x80,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,6,0x85,2,5,9,0x19,1,0x29,32,0x15,0,0x25,1,0x75,1,0x95,32,0x81,2,0xc0};
static const unsigned char multiplier[]={5,1,9,2,0xa1,1,0x85,1,0xa4,0x85,2,9,0x48,0x15,0,0x25,1,0x35,1,0x45,4,0x75,2,0x95,1,0xb1,2,0x75,6,0xb1,1,0xb4,9,0x30,9,0x31,9,0x38,0x15,0x81,0x25,0x7f,0x75,8,0x95,3,0x81,6,0xc0};
static const unsigned char absolute[]={5,1,9,2,0xa1,1,9,0x30,9,0x31,0x15,0,0x26,0xff,0x7f,0x75,16,0x95,2,0x81,2,0xc0};
int main(void){struct mouse_hid m;struct mouse_hid_input e;
    assert(mouse_hid_parse(&m,basic,sizeof(basic),alloc,release,NULL));unsigned char report[]={0x81,12,0xf9,2,0xff};assert(mouse_hid_decode(&m,report,sizeof(report),&e)==1 && e.dx==12 && e.dy==-7 && e.buttons==0x81 && e.changed==0x81 && e.wheel_y==2 && e.wheel_x==-1 && e.flags==CB_MOUSE_RELATIVE);
    uint64_t buttons=m.buttons;int64_t x=m.x;assert(mouse_hid_decode(&m,report,2,&e)==-1 && m.buttons==buttons && m.x==x);report[0]=0;assert(mouse_hid_decode(&m,report,sizeof(report),&e)==1 && e.changed==0x81 && !e.buttons);mouse_hid_free(&m);assert(!live);
    assert(mouse_hid_parse(&m,highres,sizeof(highres),alloc,release,NULL));unsigned char move[]={1,0,0x80,0xff,0x7f},button[]={2,0,0,0,0x80};assert(mouse_hid_decode(&m,move,sizeof(move),&e)==1 && e.dx==-32768 && e.dy==32767);assert(mouse_hid_decode(&m,button,sizeof(button),&e)==1 && e.buttons==(UINT64_C(1)<<31));assert(mouse_hid_decode(&m,move,sizeof(move),&e)==1 && e.buttons==(UINT64_C(1)<<31) && !e.changed);unsigned char unknown[]={3};assert(mouse_hid_decode(&m,unknown,1,&e)==-1);mouse_hid_free(&m);
    assert(mouse_hid_parse(&m,multiplier,sizeof(multiplier),alloc,release,NULL));assert(mouse_hid_feature_bytes(&m,2)==2);unsigned char feature[]={2,0xa0};mouse_hid_feature_apply(&m,2,feature,2);assert(mouse_hid_feature_prepare(&m,2,feature,2) && feature[1]==0xa1);mouse_hid_feature_apply(&m,2,feature,2);unsigned char wheel[]={1,1,2,4};assert(mouse_hid_decode(&m,wheel,4,&e)==1 && e.resolution_y==4 && e.wheel_y==4);mouse_hid_free(&m);
    assert(mouse_hid_parse(&m,absolute,sizeof(absolute),alloc,release,NULL));unsigned char point[]={0xff,0x7f,0,0};assert(mouse_hid_decode(&m,point,4,&e)==1 && e.x==65535 && e.y==0 && e.flags==CB_MOUSE_ABSOLUTE);mouse_hid_free(&m);
    /* Ignore keyboard collections without losing their report offsets/IDs. */
    const unsigned char keyboard[]={5,1,9,6,0xa1,1,0x85,7,5,7,0x19,0,0x29,0x65,0x15,0,0x25,0x65,0x75,8,0x95,6,0x81,0,0xc0};unsigned char composite[sizeof(keyboard)+sizeof(basic)+2];memcpy(composite,keyboard,sizeof(keyboard));memcpy(composite+sizeof(keyboard),basic,6);composite[sizeof(keyboard)+6]=0x85;composite[sizeof(keyboard)+7]=3;memcpy(composite+sizeof(keyboard)+8,basic+6,sizeof(basic)-6);
    assert(mouse_hid_parse(&m,composite,sizeof(composite),alloc,release,NULL));unsigned char combined[]={3,0x81,12,0xf9,2,0xff},keys[]={7,4,0,0,0,0,0};assert(mouse_hid_decode(&m,combined,sizeof(combined),&e)==1 && e.buttons==0x81);assert(mouse_hid_decode(&m,keys,sizeof(keys),&e)==0 && m.buttons==0x81);mouse_hid_free(&m);
    const unsigned char arrays[]={5,1,9,2,0xa1,1,9,0x30,0x15,0x81,0x25,0x7f,0x75,8,0x95,1,0x81,6,5,9,0x19,0,0x29,5,0x15,0,0x25,5,0x95,2,0x81,0,0xc0};assert(mouse_hid_parse(&m,arrays,sizeof(arrays),alloc,release,NULL));unsigned char selectors[]={0,1,5};assert(mouse_hid_decode(&m,selectors,sizeof(selectors),&e)==1 && e.buttons==17);selectors[1]=selectors[2]=0;assert(mouse_hid_decode(&m,selectors,sizeof(selectors),&e)==1 && !e.buttons && e.changed==17);mouse_hid_free(&m);
    unsigned char joystick[sizeof(basic)];memcpy(joystick,basic,sizeof(joystick));joystick[3]=4;assert(!mouse_hid_parse(&m,joystick,sizeof(joystick),alloc,release,NULL) && !live);
    for(size_t n=0;n<sizeof(basic);++n){if(mouse_hid_parse(&m,basic,n,alloc,release,NULL))mouse_hid_free(&m);assert(!live);}
    for(long n=0;n<16;++n){budget=n;if(mouse_hid_parse(&m,basic,sizeof(basic),alloc,release,NULL))mouse_hid_free(&m);budget=-1;assert(!live);}
    unsigned char broken[sizeof(basic)];memcpy(broken,basic,sizeof(basic));broken[0]=0xfe;assert(!mouse_hid_parse(&m,broken,sizeof(broken),alloc,release,NULL) && !live);
    unsigned seed=1;for(unsigned trial=0;trial<2000;++trial){unsigned char fuzz[128];size_t n=trial%sizeof(fuzz);for(size_t i=0;i<n;++i){seed=seed*1664525u+1013904223u;fuzz[i]=seed>>24;}if(mouse_hid_parse(&m,fuzz,n,alloc,release,NULL)){(void)mouse_hid_decode(&m,fuzz,n,&e);mouse_hid_free(&m);}assert(!live);}
    puts("PASS mouse HID: descriptor-driven axes/buttons/wheels/pan, IDs, 16-bit movement, independent reports, resolution feature preservation, absolute axes, malformed input and OOM cleanup");return 0;}
