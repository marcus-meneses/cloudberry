#include "application.h"
#include "libc.h"
#include <stdlib.h>
#include <string.h>
extern int main(int,char **);
__attribute__((noreturn)) void _start(const struct cb_startup *packet){
    struct cb_application_start start;if(!cb_application_start(packet,&start) || start.header.argc>SIZE_MAX/sizeof(char *)-1 || start.header.argc>INT32_MAX || start.header.cwd_bytes>=4096)cb_exit(CB_BAD_REQUEST);
    char cwd[4096];memcpy(cwd,start.cwd,start.header.cwd_bytes);cwd[start.header.cwd_bytes]=0;if(!cb_libc_set_cwd(cwd))cb_exit(CB_BAD_REQUEST);
    char **argv=calloc(start.header.argc+1,sizeof(*argv));if(!argv)cb_exit(CB_RESOURCE_FAILURE);
    for(size_t i=0;i<start.header.argc;++i)argv[i]=(char *)cb_application_argument(&start,i);
    int status=main((int)start.header.argc,argv);free(argv);exit(status);
}
