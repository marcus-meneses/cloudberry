#ifndef CLOUDBERRY_TEST_VIRGLRENDERER_API_H
#define CLOUDBERRY_TEST_VIRGLRENDERER_API_H
/* Host-test-only ABI subset of virglrenderer 0.9.1. The installed shared library
 * supplies the real decoder/GL implementation; no library enters Cloudberry.
 * https://chromium.googlesource.com/chromiumos/third_party/virglrenderer/+/d74a494ac29e003aef9f7044f810f2fdce4508f7/src/virglrenderer.h
 * Adapted declarations: see virglrenderer-LICENSE.txt.
 * v1 callbacks and these entry points precede 0.9.1 and retain their C ABI. */
#include <stdint.h>
#include <stdbool.h>
#include <sys/uio.h>
struct virgl_renderer_gl_ctx_param {int version;bool shared;int major_ver,minor_ver;};
struct virgl_renderer_callbacks {
    int version;void (*write_fence)(void *,uint32_t);
    void *(*create_gl_context)(void *,int,struct virgl_renderer_gl_ctx_param *);
    void (*destroy_gl_context)(void *,void *);int (*make_current)(void *,int,void *);
};
struct virgl_renderer_resource_create_args {uint32_t handle,target,format,bind,width,height,depth,array_size,last_level,nr_samples,flags;};
struct virgl_box {uint32_t x,y,z,w,h,d;};
int virgl_renderer_init(void *,int,struct virgl_renderer_callbacks *);
void virgl_renderer_cleanup(void *);
int virgl_renderer_context_create(uint32_t,uint32_t,const char *);
void virgl_renderer_context_destroy(uint32_t);
void virgl_renderer_ctx_attach_resource(int,int);
int virgl_renderer_resource_create(struct virgl_renderer_resource_create_args *,struct iovec *,uint32_t);
void virgl_renderer_resource_unref(uint32_t);
int virgl_renderer_submit_cmd(void *,int,int);
int virgl_renderer_transfer_read_iov(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,struct virgl_box *,uint64_t,struct iovec *,int);
int virgl_renderer_transfer_write_iov(uint32_t,uint32_t,int,uint32_t,uint32_t,struct virgl_box *,uint64_t,struct iovec *,unsigned);
#endif
