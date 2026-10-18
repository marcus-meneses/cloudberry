#ifndef CLOUDBERRY_SDK_APPLICATION_H
#define CLOUDBERRY_SDK_APPLICATION_H
#include "../interfaces/startup.h"
#include "../interfaces/applications.h"
struct cb_application_start {struct cb_application_launch header;const unsigned char *arguments,*cwd;};
static inline int cb_application_start(const struct cb_startup *packet,struct cb_application_start *out){
    const void *data;size_t bytes;if(!packet || !out || !cb_startup_find(packet,packet->bytes,"application",&data,&bytes) || !cb_application_launch_valid(data,bytes))return 0;
    cb_startup_copy(&out->header,data,sizeof(out->header));out->arguments=(const unsigned char *)data+sizeof(out->header)+out->header.name_bytes;out->cwd=out->arguments+out->header.arguments_bytes;return 1;
}
static inline const char *cb_application_argument(const struct cb_application_start *start,uint64_t index){
    if(!start || index>=start->header.argc)return NULL;
    size_t at=0;while(index--){while(start->arguments[at])++at;++at;}return (const char *)start->arguments+at;
}
#endif
