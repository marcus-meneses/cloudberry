#include "../../sdk/service.h"
#include "../../interfaces/text.h"
#include "../../interfaces/graphics.h"
#include "../../interfaces/display.h"
#include "../../interfaces/io.h"
static struct cb_client client,reply;
static unsigned char input[CB_FRAME_LIMIT];
static cb_handle_t graphics,memory,io;
static struct cb_text_info grid;
struct text_cell {uint32_t codepoint,foreground,background;};
struct row_damage {uint32_t left,right;uint64_t surface;unsigned clear;};
static struct text_cell *cells;static struct row_damage *damage;
static uint32_t cell_width,cell_height,first_row,pending_scroll;
static uint64_t viewport,glyph_surface;static unsigned layout_dirty;
static struct cb_graphics_place *placements;
static uint32_t physical(unsigned row){return (first_row+row)%grid.rows;}
static struct text_cell *cell_at(unsigned row,unsigned col){return cells+(size_t)physical(row)*grid.columns+col;}
static void touch(unsigned row,unsigned col){struct row_damage *d=damage+physical(row);if(col<d->left)d->left=col;if(col>d->right)d->right=col;}
static void set_cell(unsigned row,unsigned col,uint32_t cp,uint32_t fg,uint32_t bg){*cell_at(row,col)=(struct text_cell){cp,fg,bg};touch(row,col);}

static uint32_t utf,remaining,minimum;
static struct cb_glyph glyph_batch[256];static size_t glyph_count;
/* Retain cells and batch glyphs within a line. Each newline/automatic wrap
 * presents before parsing the next line; WRITE also commits trailing text. */
static cb_status_t flush(void){if(!glyph_count)return CB_OK;static struct {struct cb_graphics_text_batch header;struct cb_glyph glyphs[256];} packet;packet.header=(struct cb_graphics_text_batch){.surface=glyph_surface,.flags=CB_GRAPHICS_DEFER};cb_copy(packet.glyphs,glyph_batch,glyph_count*sizeof(*glyph_batch));cb_status_t status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_TEXT_BATCH,&packet,sizeof(packet.header)+glyph_count*sizeof(*glyph_batch),0);if(!status)glyph_count=0;return status;}
static cb_status_t glyph(unsigned column,unsigned row,uint32_t cp,uint32_t fg,uint32_t bg){(void)row;if(glyph_count==sizeof(glyph_batch)/sizeof(*glyph_batch)){cb_status_t status=flush();if(status)return status;}glyph_batch[glyph_count++]=(struct cb_glyph){column*cell_width,0,cp,fg,bg,0};return CB_OK;}
static cb_status_t commit(void){
    cb_status_t status;unsigned work=pending_scroll || glyph_count || layout_dirty;
    for(unsigned row=0;row<grid.rows && !work;++row)work=damage[row].left<grid.columns || damage[row].clear;
    if(!work)return CB_OK;
    if(pending_scroll){
        if(pending_scroll<grid.rows){struct cb_graphics_scroll scroll={viewport,0,-(int32_t)(pending_scroll*cell_height)};status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_SCROLL,&scroll,sizeof(scroll),0);if(status)return status;}
        pending_scroll=0;layout_dirty=1;
    }
    if(layout_dirty){for(unsigned row=0;row<grid.rows;++row)placements[row]=(struct cb_graphics_place){.surface=damage[physical(row)].surface,.parent=viewport,.y=row*cell_height,.flags=CB_SURFACE_VISIBLE};
        size_t at=0;while(at<grid.rows){size_t n=grid.rows-at,limit=(CB_FRAME_LIMIT-1024)/sizeof(*placements);if(n>limit)n=limit;status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_PLACE,placements+at,n*sizeof(*placements),0);if(status)return status;at+=n;}layout_dirty=0;
    }
    /* Every physical row owns its bitmap. Scrolling changes positions only;
     * clear/repaint the recycled line, never erase the terminal viewport. */
    status=flush();if(status)return status;
    for(unsigned row=0;row<grid.rows;++row){struct row_damage *d=damage+physical(row);glyph_surface=d->surface;
        if(d->clear){struct cb_graphics_draw fill={.surface=d->surface,.color=0xff000000,.fill=1,.x={0,(int32_t)(grid.columns*cell_width)},.y={0,(int32_t)cell_height}};status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_RECTANGLE,&fill,sizeof(fill),0);if(status)return status;}
        for(unsigned col=d->left;col<=d->right && col<grid.columns;++col){struct text_cell *c=cell_at(row,col);if(d->clear && c->codepoint==' ' && !c->background)continue;status=glyph(col,row,c->codepoint,c->foreground,c->background);if(status)return status;}
        status=flush();if(status)return status;
    }
    uint64_t screen=0;status=cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_PRESENT,&screen,sizeof(screen),0);if(status)return status;
    for(unsigned row=0;row<grid.rows;++row){damage[row].left=grid.columns;damage[row].right=damage[row].clear=0;}
    return CB_OK;
}
static cb_status_t clear(void){
    glyph_count=0;first_row=pending_scroll=0;layout_dirty=1;
    for(size_t i=0;i<(size_t)grid.columns*grid.rows;++i)cells[i]=(struct text_cell){' ',0xdddddd,0};
    for(unsigned row=0;row<grid.rows;++row){damage[row].left=0;damage[row].right=grid.columns-1;damage[row].clear=1;}
    grid.column=grid.row=0;utf=remaining=minimum=0;return commit();
}
static cb_status_t newline(void){grid.column=0;if(++grid.row<grid.rows)return commit();grid.row=grid.rows-1;first_row=(first_row+1)%grid.rows;if(pending_scroll<grid.rows)++pending_scroll;struct text_cell *row=cell_at(grid.row,0);for(unsigned col=0;col<grid.columns;++col)row[col]=(struct text_cell){' ',0xdddddd,0};struct row_damage *d=damage+physical(grid.row);d->left=0;d->right=grid.columns-1;d->clear=1;return commit();}
static cb_status_t put(uint32_t cp){if(cp=='\n')return newline();if(cp=='\r'){grid.column=0;return CB_OK;}if(cp=='\b'){if(grid.column)--grid.column;return CB_OK;}if(cp<32)return CB_OK;if(grid.column==grid.columns){cb_status_t status=newline();if(status)return status;}set_cell(grid.row,grid.column++,cp,0xdddddd,0);return CB_OK;}
static cb_status_t write(const unsigned char *s,size_t n){for(size_t i=0;i<n;++i){unsigned c=s[i];if(remaining){if((c&0xc0)==0x80){utf=(utf<<6)|(c&63);if(!--remaining){uint32_t cp=utf;if(cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))cp=0xfffd;cb_status_t st=put(cp);if(st)return st;}continue;}remaining=0;cb_status_t st=put(0xfffd);if(st)return st;}if(c<128){cb_status_t st=put(c);if(st)return st;}else if(c>=0xc2 && c<=0xf4){remaining=c<0xe0?1:c<0xf0?2:3;minimum=remaining==1?128:remaining==2?2048:65536;utf=c&((1u<<(6-remaining))-1);}else {cb_status_t st=put(0xfffd);if(st)return st;}}return CB_OK;}
__attribute__((noreturn)) void _start(const struct cb_startup *startup){
    (void)startup;io=cb_acquire(CB_IO_NAME,(struct cb_interface_id)CB_IO_INTERFACE_ID);graphics=cb_acquire(CB_GRAPHICS_NAME,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID);memory=cb_acquire(CB_MEMORY_NAME,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID);CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_INFO,NULL,0,0)==CB_OK);struct cb_display_info info;cb_copy(&info,cb_result(&client),sizeof(info));struct cb_graphics_font font;CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_FONT_INFO,NULL,0,0)==CB_OK);cb_copy(&font,cb_result(&client),sizeof(font));cell_width=font.cell_width;cell_height=font.cell_height;CB_REQUIRE(cell_width && cell_height);grid.columns=info.width/cell_width;grid.rows=info.height/cell_height;CB_REQUIRE(grid.columns && grid.rows && (uint64_t)grid.columns*grid.rows<=(SIZE_MAX-7-(size_t)grid.rows*(sizeof(*damage)+sizeof(*placements)))/sizeof(*cells));struct cb_memory_request request={.bytes=(((uint64_t)grid.columns*grid.rows*sizeof(*cells)+7)&~UINT64_C(7))+(size_t)grid.rows*(sizeof(*damage)+sizeof(*placements))};CB_REQUIRE(cb_invoke(&client,memory,(struct cb_interface_id)CB_MEMORY_INTERFACE_ID,CB_MEMORY_ALLOCATE,&request,sizeof(request),0)==CB_OK);struct cb_memory_result allocated;cb_copy(&allocated,cb_result(&client),sizeof(allocated));cells=(void *)(uintptr_t)allocated.address;damage=(void *)((uintptr_t)cells+(((size_t)grid.columns*grid.rows*sizeof(*cells)+7)&~(size_t)7));placements=(void *)(damage+grid.rows);for(unsigned row=0;row<grid.rows;++row)damage[row]=(struct row_damage){0};
    struct cb_graphics_create_ex container={grid.columns*cell_width,grid.rows*cell_height,CB_SURFACE_CONTAINER,0};CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_CREATE,&container,sizeof(container),0)==CB_OK);cb_copy(&viewport,cb_result(&client),8);
    struct cb_graphics_place root={.surface=viewport,.flags=CB_SURFACE_VISIBLE};CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_PLACE,&root,sizeof(root),0)==CB_OK);
    for(unsigned row=0;row<grid.rows;++row){struct cb_graphics_create line={grid.columns*cell_width,cell_height};CB_REQUIRE(cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_CREATE,&line,sizeof(line),0)==CB_OK);cb_copy(&damage[row].surface,cb_result(&client),8);}CB_REQUIRE(clear()==CB_OK);
    static const char *names[]={"info","clear","write","cell","cursor"};cb_handle_t pub=cb_publish(&client,CB_TEXT_NAME,(struct cb_interface_id)CB_TEXT_INTERFACE_ID,names,5);
    for(;;){uint64_t bytes;CB_REQUIRE(cb_system_receive(input,sizeof(input),&bytes,0)==CB_OK && bytes>=sizeof(struct cb_delivery_header));struct cb_delivery_header d;cb_copy(&d,input,sizeof(d));if(d.call.flags&CB_CALL_SYSTEM_EVENT){if(d.call.operation==CB_HANDLE_RELEASE){for(unsigned row=0;row<grid.rows;++row)(void)cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_RELEASE,&damage[row].surface,8,0);(void)cb_invoke(&client,graphics,(struct cb_interface_id)CB_GRAPHICS_INTERFACE_ID,CB_GRAPHICS_SURFACE_RELEASE,&viewport,8,0);cb_system_release(pub);cb_system_release(graphics);cb_system_release(memory);cb_exit(0);}continue;}
        CB_REQUIRE(d.context_bytes<=bytes-sizeof(d) && d.call.payload_bytes==bytes-sizeof(d)-d.context_bytes);const void *p=input+sizeof(d)+d.context_bytes;cb_status_t status=CB_BAD_REQUEST;const void *out=NULL;size_t n=0;
        struct cb_admission caller;
        if(d.call.handle!=pub || !cb_admission_parse(input+sizeof(d),d.context_bytes,&caller))status=CB_DENIED;
        else if(d.call.operation!=CB_TEXT_INFO){struct cb_io_console_access access={caller.process,CB_CONSOLE_TEXT,0};status=cb_invoke(&client,io,(struct cb_interface_id)CB_IO_INTERFACE_ID,CB_IO_CONSOLE_ACCESS,&access,sizeof(access),0);}else status=CB_OK;
        if(status==CB_OK && d.call.handle==pub){status=CB_BAD_REQUEST;if(d.call.operation==CB_TEXT_INFO && !d.call.payload_bytes){status=CB_OK;out=&grid;n=sizeof(grid);}else if(d.call.operation==CB_TEXT_CLEAR && !d.call.payload_bytes)status=clear();else if(d.call.operation==CB_TEXT_WRITE){status=commit();if(!status)status=write(p,d.call.payload_bytes);if(!status)status=commit();}else if(d.call.operation==CB_TEXT_CURSOR && d.call.payload_bytes==sizeof(struct cb_text_cursor)){struct cb_text_cursor c;cb_copy(&c,p,sizeof(c));if(c.column<grid.columns && c.row<grid.rows){grid.column=c.column;grid.row=c.row;utf=remaining=minimum=0;status=CB_OK;}}else if(d.call.operation==CB_TEXT_CELL && d.call.payload_bytes==sizeof(struct cb_text_cell)){struct cb_text_cell c;cb_copy(&c,p,sizeof(c));if(!c.reserved && c.column<grid.columns && c.row<grid.rows && c.foreground<=0xffffff && c.background<=0xffffff && c.codepoint<=0x10ffff && !(c.codepoint>=0xd800 && c.codepoint<=0xdfff)){status=commit();if(!status){set_cell(c.row,c.column,c.codepoint,c.foreground,c.background);status=commit();}}}}
        CB_REQUIRE(cb_respond(&reply,&d,status,out,n)==CB_OK);
    }
}
