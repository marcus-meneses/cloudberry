#include "../../sdk/system.h"
#include "../../interfaces/startup.h"
__attribute__((noreturn)) void _start(uint64_t argument){
    const struct cb_startup *p=(const void *)(uintptr_t)argument;const void *data;size_t bytes;
    if(!p || !cb_startup_find(p,p->bytes,"arbitrary.data",&data,&bytes) || bytes!=15)cb_exit(1);
    const char *s=data;static const char expected[]="generic-startup";for(unsigned i=0;i<bytes;++i)if(s[i]!=expected[i])cb_exit(2);
    cb_exit(0);
}
