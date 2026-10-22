#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../sdk/runtime.h"
cb_status_t cb_system_submit(const void *p,uint64_t n,struct cb_request_id *id){(void)p;(void)n;(void)id;return CB_RESOURCE_FAILURE;}
int main(void){
    struct cb_runtime rt={0};struct cb_client client={.pending={1,2},.bytes=sizeof(struct cb_reply_header)};
    struct cb_reply_header old={.version={sizeof(old),1,0},.status=CB_DENIED};memcpy(client.result,&old,sizeof(old));
    assert(cb_runtime_submit(&rt,&client,9,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,1,NULL,0)==CB_BUSY && rt.failure==CB_BUSY && !rt.failure_bytes && client.pending.serial==2);
    client.pending=(struct cb_request_id){0};
    assert(cb_runtime_submit(&rt,&client,9,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,1,NULL,0)==CB_RESOURCE_FAILURE && rt.failure==CB_RESOURCE_FAILURE && !rt.failure_bytes && !client.pending.serial);
    puts("PASS runtime submit: busy/transport failure preserves request ownership and never presents an old reply as a new error frame");
}
