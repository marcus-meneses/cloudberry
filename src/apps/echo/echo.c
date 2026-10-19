#include "../../sdk/application.h"
#include "../../sdk/stream.h"
static struct cb_client client;
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));cb_handle_t out=cb_standard(1);if(!out)cb_exit(CB_BAD_HANDLE);for(uint64_t i=1;i<start.header.argc;++i){const char *s=cb_application_argument(&start,i);if(i>1)CB_REQUIRE(cb_write_all(&client,out," ",1)==CB_OK);CB_REQUIRE(cb_write_all(&client,out,s,cb_length(s))==CB_OK);}CB_REQUIRE(cb_write_all(&client,out,"\n",1)==CB_OK);cb_exit(0);}
