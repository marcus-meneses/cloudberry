#include "view.h"
#include "model.h"
#include <stdlib.h>
#include <string.h>
struct ed_view view;
static cb_status_t fill_surface(uint64_t id,uint32_t width,uint32_t height,uint32_t color){
    struct cb_graphics_draw r={
        .surface=id,.color=0xff000000|color,.fill=1,.x={
            0,width
        },.y={
            0,height
        }
    };
    return ep_gfx(CB_GRAPHICS_RECTANGLE,&r,sizeof(r),NULL,0);
}
static cb_status_t place(int begin,int end){
    for(int y=begin;y<end;y++){
        int body=y>=3&&y<view.rows-2;
        view.places[y-begin]=(struct cb_graphics_place){
            .surface=view.lines[y].surface,.parent=body?view.area:0,.y=(body?y-3:y)*view.ch,.flags=CB_SURFACE_VISIBLE
        };
    }
    return ep_gfx(CB_GRAPHICS_SURFACE_PLACE,view.places,(end-begin)*sizeof(*view.places),NULL,0);
}
cb_status_t ev_init(void){
    struct cb_graphics_info info;
    struct cb_graphics_font font;
    cb_status_t s=ep_gfx(CB_GRAPHICS_INFO,NULL,0,&info,sizeof(info));
    if(s)return s;
    s=ep_gfx(CB_GRAPHICS_FONT_INFO,NULL,0,&font,sizeof(font));
    if(s)return s;
    if(!font.cell_width||!font.cell_height||info.width>32767||info.height>32767)return CB_BAD_REQUEST;
    view.cw=font.cell_width;
    view.ch=font.cell_height;
    view.cols=info.width/font.cell_width;
    view.rows=info.height/font.cell_height;
    view.body=view.rows-5;
    view.width=info.width;
    view.height=info.height;
    if(view.cols<60||view.rows<20)return CB_UNAVAILABLE;
    view.lines=calloc(view.rows,sizeof(*view.lines));
    view.places=calloc(view.rows,sizeof(*view.places));
    if(!view.lines||!view.places)return CB_RESOURCE_FAILURE;
    struct cb_graphics_create_ex create={
        view.cols*view.cw,view.body*view.ch,CB_SURFACE_CONTAINER,0
    };
    s=ep_gfx(CB_GRAPHICS_SURFACE_CREATE,&create,sizeof(create),&view.area,sizeof(view.area));
    if(s)return s;
    struct cb_graphics_place area={
        .surface=view.area,.y=3*view.ch,.flags=CB_SURFACE_VISIBLE
    };
    s=ep_gfx(CB_GRAPHICS_SURFACE_PLACE,&area,sizeof(area),NULL,0);
    if(s)return s;
    for(int y=0;y<view.rows;y++){
        struct ed_row *r=&view.lines[y];
        r->cells=calloc(view.cols,sizeof(*r->cells));
        r->front=calloc(view.cols,sizeof(*r->front));
        if(!r->cells||!r->front)return CB_RESOURCE_FAILURE;
        for(int x=0;x<view.cols;x++)r->front[x].ch=UINT32_MAX;
        create=(struct cb_graphics_create_ex){
            view.cols*view.cw,view.ch,0,0
        };
        s=ep_gfx(CB_GRAPHICS_SURFACE_CREATE,&create,sizeof(create),&r->surface,sizeof(r->surface));
        if(s)return s;
        s=fill_surface(r->surface,create.width,create.height,ED_BG);
        if(s)return s;
    }
    s=place(0,view.rows);
    if(!s)s=fill_surface(0,view.width,view.height,ED_BG);
    return s;
}
void ev_begin(void){
    for(int y=0;y<view.rows;y++)ev_fill(0,y,view.cols,ED_TEXT,ED_BG);
}
void ev_code(int x,int y,uint32_t c,uint32_t fg,uint32_t bg){
    if(x>=0&&x<view.cols&&y>=0&&y<view.rows)view.lines[y].cells[x]=(struct ed_cell){
        c=='\t'?' ':c,fg,bg
    };
}
void ev_fill(int x,int y,int width,uint32_t fg,uint32_t bg){
    for(int i=0;i<width;i++)ev_code(x+i,y,' ',fg,bg);
}
/* UI strings are UTF-8 too. Decode in place, without per-label heap traffic. */
void ev_put(int x,int y,const char *s,uint32_t fg,uint32_t bg,int limit){
    const unsigned char *p=(const unsigned char *)s;
    int used=0;
    while(*p&&(limit<0||used<limit)){
        unsigned first=*p++,extra=0;
        uint32_t cp=first,minimum=0;
        if(first>=0xc2&&first<=0xdf){
            extra=1;
            cp=first&31;
            minimum=0x80;
        }
        else if(first>=0xe0&&first<=0xef){
            extra=2;
            cp=first&15;
            minimum=0x800;
        }
        else if(first>=0xf0&&first<=0xf4){
            extra=3;
            cp=first&7;
            minimum=0x10000;
        }
        else if(first>=0x80)cp=0xfffd;
        for(unsigned i=0;i<extra;i++){
            if((*p&0xc0)!=0x80){
                cp=0xfffd;
                break;
            }
            cp=(cp<<6)|(*p++&63);
        }
        if(cp<minimum||!cb_unicode_scalar(cp))cp=0xfffd;
        ev_code(x+used++,y,cp,fg,bg);
    }
}
void ev_caret(int x,int y){
    if(x>=0&&x<view.cols&&y>=0&&y<view.rows){
        struct ed_cell *c=&view.lines[y].cells[x];
        c->fg=ED_BG;
        c->bg=ED_CURSOR;
    }
}
cb_status_t ev_scroll(int delta){
    if(!delta||delta>=view.body||delta<=-view.body)return CB_OK;
    struct cb_graphics_scroll scroll={
        view.area,0,-delta*view.ch
    };
    cb_status_t s=ep_gfx(CB_GRAPHICS_SURFACE_SCROLL,&scroll,sizeof(scroll),NULL,0);
    if(s)return s;
    int n=(delta%view.body+view.body)%view.body;
    /* Rotate bitmap IDs together with their pixel-cache descriptions. */
    for(int step=0;step<n;step++){
        struct ed_row first=view.lines[3];
        memmove(view.lines+3,view.lines+4,(view.body-1)*sizeof(*view.lines));
        view.lines[3+view.body-1]=first;
    }
    return place(3,view.rows-2);
}
cb_status_t ev_flush(void){
    struct {
        struct cb_graphics_text_batch header;
        struct cb_glyph glyphs[500];
    }
    packet;
    for(int y=0;y<view.rows;y++){
        struct ed_row *r=&view.lines[y];
        packet.header=(struct cb_graphics_text_batch){
            .surface=r->surface,.flags=CB_GRAPHICS_DEFER
        };
        size_t n=0;
        for(int x=0;x<view.cols;x++)if(memcmp(&r->cells[x],&r->front[x],sizeof(struct ed_cell))){
            struct ed_cell c=r->cells[x];
            packet.glyphs[n++]=(struct cb_glyph){
                x*view.cw,0,c.ch,c.fg,c.bg,0
            };
            if(n==500){
                cb_status_t s=ep_gfx(CB_GRAPHICS_TEXT_BATCH,&packet,sizeof(packet.header)+n*sizeof(struct cb_glyph),NULL,0);
                if(s)return s;
                n=0;
            }
        }
        if(n){
            cb_status_t s=ep_gfx(CB_GRAPHICS_TEXT_BATCH,&packet,sizeof(packet.header)+n*sizeof(struct cb_glyph),NULL,0);
            if(s)return s;
        }
    }
    uint64_t zero=0;
    cb_status_t s=ep_gfx(CB_GRAPHICS_PRESENT,&zero,sizeof(zero),NULL,0);
    if(!s)for(int y=0;y<view.rows;y++)memcpy(view.lines[y].front,view.lines[y].cells,view.cols*sizeof(struct ed_cell));
    return s;
}
void ev_close(void){
    if(view.lines)for(int y=0;y<view.rows;y++){
        if(view.lines[y].surface)(void)ep_gfx(CB_GRAPHICS_SURFACE_RELEASE,&view.lines[y].surface,8,NULL,0);
        free(view.lines[y].cells);
        free(view.lines[y].front);
    }
    if(view.area)(void)ep_gfx(CB_GRAPHICS_SURFACE_RELEASE,&view.area,8,NULL,0);
    if(view.width){
        uint64_t zero=0;
        (void)fill_surface(0,view.width,view.height,0);
        (void)ep_gfx(CB_GRAPHICS_PRESENT,&zero,8,NULL,0);
    }
    free(view.lines);
    free(view.places);
    memset(&view,0,sizeof(view));
}
