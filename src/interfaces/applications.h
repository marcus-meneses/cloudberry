#ifndef CLOUDBERRY_INTERFACE_APPLICATIONS_H
#define CLOUDBERRY_INTERFACE_APPLICATIONS_H
#include "handle.h"
#define CB_APPLICATIONS_NAME "/@applications"
#define CB_APPLICATIONS_INTERFACE_ID {{0x43,0x42,0x41,0x50,0x50,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x06}}
enum {CB_APPLICATION_LAUNCH=1,CB_APPLICATION_INFO,CB_APPLICATION_WAIT,CB_APPLICATION_STOP,CB_APPLICATION_RELEASE};
enum {CB_APPLICATION_RUNNING=1,CB_APPLICATION_EXITED,CB_APPLICATION_FAULTED};
/* Inline: installation name, argc NUL-terminated argument strings, absolute cwd.
 * All lengths are bytes, exclude padding; cwd excludes its NUL. Message limits
 * bound this first transport, not a per-process argv storage arena. */
struct cb_application_launch {struct cb_version version;uint64_t name_bytes,argc,arguments_bytes,cwd_bytes;};
struct cb_application_ref {uint64_t process;};
struct cb_application_status {uint64_t process,exit_code,fault_vector;uint32_t state,stop_requested;};
static inline int cb_application_launch_valid(const void *data,size_t bytes){
    if(!data || bytes<sizeof(struct cb_application_launch))return 0;
    struct cb_application_launch h;unsigned char *d=(unsigned char *)&h;const unsigned char *p=(const unsigned char *)data;for(size_t i=0;i<sizeof(h);++i)d[i]=p[i];
    if(h.version.bytes!=sizeof(h) || h.version.major!=1 || h.version.minor || !h.name_bytes || !cb_frame_fits(sizeof(h),h.name_bytes,h.arguments_bytes,bytes) || h.cwd_bytes!=bytes-sizeof(h)-h.name_bytes-h.arguments_bytes || !h.cwd_bytes)return 0;
    for(size_t i=0;i<h.name_bytes;++i){unsigned char c=p[sizeof(h)+i];if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_' || c=='-'))return 0;}
    uint64_t count=0;const unsigned char *args=p+sizeof(h)+h.name_bytes;for(size_t i=0;i<h.arguments_bytes;++i)if(!args[i])++count;
    if(count!=h.argc || (h.arguments_bytes && args[h.arguments_bytes-1]) || h.argc>h.arguments_bytes)return 0;
    const unsigned char *cwd=args+h.arguments_bytes;if(cwd[0]!='/')return 0;for(size_t i=0;i<h.cwd_bytes;++i)if(!cwd[i])return 0;return 1;
}
#endif
