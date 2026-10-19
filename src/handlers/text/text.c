#include "../../sdk/service.h"
#include "../../interfaces/text.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/display.h"
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT];
static cb_handle_t graphics,memory;
static struct cb_text_info grid;
static uint32_t *cells;
static uint32_t utf,remaining,minimum;
static struct cb_glyph glyph_batch[256];static size_t glyph_count;
static cb_status_t flush(void){if(!glyph_count)return CB_OK;cb_status_t status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_GLYPHS,glyph_batch,glyph_count*sizeof(*glyph_batch),0);if(!status)glyph_count=0;return status;}
static cb_status_t glyph(unsigned column,unsigned row,uint32_t cp,uint32_t fg,uint32_t bg){if(glyph_count==sizeof(glyph_batch)/sizeof(*glyph_batch)){cb_status_t status=flush();if(status)return status;}glyph_batch[glyph_count++]=(struct cb_glyph){column*8,row*16,cp,fg,bg,0};return CB_OK;}
static cb_status_t clear(void){struct cb_rectangle b={.width=grid.columns*8,.height=grid.rows*16};cb_status_t s=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_FILL,&b,sizeof(b),0);if(!s){glyph_count=0;for(size_t i=0;i<(size_t)grid.columns*grid.rows;++i)cells[i]=' ';grid.column=grid.row=0;utf=remaining=minimum=0;}return s;}
static cb_status_t newline(void){grid.column=0;if(++grid.row<grid.rows)return CB_OK;grid.row=grid.rows-1;cb_status_t flushed=flush();if(flushed)return flushed;
    struct cb_graphics_blit blit={.surface=0,.source=0,.source_y=16,.width=grid.columns*8,.height=(grid.rows-1)*16,.opacity=255};
    cb_status_t status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_BLIT,&blit,sizeof(blit),0);if(status)return status;
    struct cb_rectangle bottom={.y=(grid.rows-1)*16,.width=grid.columns*8,.height=16};status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_FILL,&bottom,sizeof(bottom),0);if(status)return status;
    for(size_t i=0;i<(size_t)(grid.rows-1)*grid.columns;++i)cells[i]=cells[i+grid.columns];
    for(size_t i=(size_t)(grid.rows-1)*grid.columns;i<(size_t)grid.rows*grid.columns;++i)cells[i]=' ';
    return CB_OK;
}
static cb_status_t put(uint32_t cp){if(cp=='\n')return newline();if(cp=='\r'){grid.column=0;return CB_OK;}if(cp=='\b'){if(grid.column)--grid.column;return CB_OK;}if(cp<32)return CB_OK;if(grid.column==grid.columns){cb_status_t s=newline();if(s)return s;}cb_status_t s=glyph(grid.column,grid.row,cp,0xdddddd,0);if(!s)cells[(size_t)grid.row*grid.columns+grid.column++]=cp;return s;}
static cb_status_t write(const unsigned char *s,size_t n){for(size_t i=0;i<n;++i){unsigned c=s[i];if(remaining){if((c&0xc0)==0x80){utf=(utf<<6)|(c&63);if(!--remaining){uint32_t cp=utf;if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))cp=0xfffd;cb_status_t st=put(cp);if(st)return st;}continue;}remaining=0;cb_status_t st=put(0xfffd);if(st)return st;}if(c<128){cb_status_t st=put(c);if(st)return st;}else if(c>=0xc2 && c<=0xf4){remaining=c<0xe0?1:c<0xf0?2:3;minimum=remaining==1?128:remaining==2?2048:65536;utf=c&((1u<<(6-remaining))-1);}else {cb_status_t st=put(0xfffd);if(st)return st;}}return CB_OK;}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    (void)startup;graphics=cb_acquire(CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_INFO,NULL,0,0)==CB_OK);struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));grid.columns=info.width/8;grid.rows=info.height/16;CB_REQUIRE(grid.columns && grid.rows && (uint64_t)grid.columns*grid.rows<=SIZE_MAX/4);struct cb_memory_request request={.bytes=(uint64_t)grid.columns*grid.rows*4};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&request,sizeof(request),0)==CB_OK);struct cb_memory_result allocated;cb_copy(&allocated,cb_result(&client),sizeof(allocated));cells=(void *)(uintptr_t)allocated.address;CB_REQUIRE(clear()==CB_OK);
    static const char *names[]={"info","clear","write","cell"};cb_handle_t pub=cb_publish(&client,CB_TEXT_NAME,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,names,4);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_HANDLE_RELEASE){cb_system_release(pub);cb_system_release(graphics);cb_system_release(memory);cb_exit(0);}continue;}
        CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const void *p=input+sizeof(d)+d.context_bytes;cb_status_t status=CB_BAD_REQUEST;const void *out=NULL;size_t n=0;
        if(d.call.handle==pub){if(d.call.operation==CB_TEXT_INFO && !d.call.payload_bytes){status=CB_OK;out=&grid;n=sizeof(grid);}else if(d.call.operation==CB_TEXT_CLEAR && !d.call.payload_bytes)status=clear();else if(d.call.operation==CB_TEXT_WRITE){status=write(p,d.call.payload_bytes);if(!status)status=flush();}else if(d.call.operation==CB_TEXT_CELL && d.call.payload_bytes==sizeof(struct cb_text_cell)){struct cb_text_cell c;cb_copy(&c,p,sizeof(c));if(!c.reserved && c.column<grid.columns && c.row<grid.rows && c.codepoint<=0x10ffff && !(c.codepoint>=0xd800 && c.codepoint<=0xdfff)){status=glyph(c.column,c.row,c.codepoint,c.foreground,c.background);if(!status)status=flush();if(!status)cells[(size_t)c.row*grid.columns+c.column]=c.codepoint;}}}
        CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
