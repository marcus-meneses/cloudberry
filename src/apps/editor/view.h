#ifndef CLOUDBERRY_EDITOR_VIEW_H
#define CLOUDBERRY_EDITOR_VIEW_H
#include "platform.h"
enum {
    ED_TEXT=0xe5edf5,ED_DIM=0x9eaebf,ED_BG=0x111923,ED_BAR=0x20334c,ED_PANEL=0x263746,ED_SELECTED=0x29415d,ED_ACCENT=0x65dcac,ED_CURSOR=0xd5ecfa
};
struct ed_cell {
    uint32_t ch,fg,bg;
};
struct ed_row {
    uint64_t surface;
    struct ed_cell *cells,*front;
};
struct ed_view {
    int cols,rows,cw,ch,body;
    uint32_t width,height;
    uint64_t area;
    struct ed_row *lines;
    struct cb_graphics_place *places;
};
extern struct ed_view view;
cb_status_t ev_init(void);
void ev_close(void);
void ev_begin(void);
void ev_fill(int,int,int,uint32_t,uint32_t);
void ev_put(int,int,const char *,uint32_t,uint32_t,int);
void ev_code(int,int,uint32_t,uint32_t,uint32_t);
void ev_caret(int,int);
cb_status_t ev_scroll(int);
cb_status_t ev_flush(void);
#endif
