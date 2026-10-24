/* Disposable provider binaries only. Abort after a real successful operation,
 * leaving production process-exit/bootstrap rollback responsible for cleanup. */
#include "../sdk/service.h"
#include "../interfaces/io.h"
#include "../interfaces/render-backend.h"
#if GPU_PAIR_FAULT_CODE < 201 || GPU_PAIR_FAULT_CODE > 204
#error Unsupported startup fault
#endif
static int same_interface(struct cb_interface_id a,struct cb_interface_id b){
    for(unsigned i=0;i<sizeof(a.bytes);++i)if(a.bytes[i]!=b.bytes[i])return 0;
    return 1;
}
static cb_status_t fault_invoke(struct cb_client *c,cb_handle_t handle,struct cb_interface_id interface,unsigned op,const void *p,size_t n,uint64_t deadline){
    cb_status_t status=cb_invoke(c,handle,interface,op,p,n,deadline);
    if(status)return status;
#if GPU_PAIR_FAULT_CODE == 203
    if(same_interface(interface,(struct cb_interface_id)CB_RENDER_BACKEND_INTERFACE_ID) && op==CB_RENDER_BACKEND_INFO)cb_exit(GPU_PAIR_FAULT_CODE);
#else
    if(same_interface(interface,(struct cb_interface_id)CB_IO_INTERFACE_ID) && op==CB_IO_GPU && n>=sizeof(struct cb_io_gpu_request)){
        struct cb_io_gpu_request q;cb_copy(&q,p,sizeof(q));
        if(q.operation==((GPU_PAIR_FAULT_CODE==201 || GPU_PAIR_FAULT_CODE==204)?CB_GPU_OPEN:CB_GPU_PRESENT))cb_exit(GPU_PAIR_FAULT_CODE);
    }
#endif
    return status;
}
#define cb_invoke fault_invoke
#if GPU_PAIR_FAULT_CODE == 203
#include "../handlers/graphics-opengl/graphics-opengl.c"
#else
#include "../handlers/display-opengl/display-opengl.c"
#endif
