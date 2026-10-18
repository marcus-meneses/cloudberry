#include "../../sdk/system.h"
#include "../../sdk/application.h"
/* A minimal ordinary application: no privileged or resource grants. */
__attribute__((noreturn)) void _start(const struct cb_startup *packet){struct cb_application_start start;if(!cb_application_start(packet,&start))cb_exit(CB_BAD_REQUEST);cb_exit(0);}
