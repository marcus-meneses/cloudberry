#include "../../sdk/application.h"
#include "../../sdk/stream.h"
static struct cb_client client;
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;CB_REQUIRE(cb_application_start(packet,&start));cb_handle_t out=cb_standard(1);CB_REQUIRE(out && cb_write_all(&client,out,start.cwd,start.header.cwd_bytes)==CB_OK && cb_write_all(&client,out,"\n",1)==CB_OK);cb_exit(0);}
