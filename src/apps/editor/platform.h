#ifndef CLOUDBERRY_EDITOR_PLATFORM_H
#define CLOUDBERRY_EDITOR_PLATFORM_H
#include "../../sdk/libc.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/keyboard.h"
#include "../../interfaces/mouse.h"
struct ed_platform {
    cb_handle_t graphics,keyboard,mouse,filesystem;
    struct cb_client client,keys,moves;
    struct cb_keyboard_read_focus keyread;
    struct cb_mouse_read mouseread;
    unsigned focused,hidden;
};
extern struct ed_platform platform;
extern unsigned ep_failed_op;
extern uint64_t ep_actual,ep_expected;
cb_status_t ep_init(void);
void ep_close(void);
cb_status_t ep_gfx(unsigned,const void *,size_t,void *,size_t);
cb_status_t ep_cursor(int);
cb_status_t ep_arm(void);
cb_status_t ep_wait(unsigned *);
cb_status_t ep_fs(unsigned,const char *,uint64_t,void *,size_t,size_t *);
cb_status_t ep_load(const char *,unsigned char **,size_t *);
void ep_free_file(void *);
cb_status_t ep_save(const char *,const void *,size_t);
#endif
