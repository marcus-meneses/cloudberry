/* Run the production UTF-8/text/scroll model against retained row bitmaps.
 * Capture frames so newline boundaries cannot silently be coalesced. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../sdk/service.h"
static cb_status_t mock_invoke(struct cb_client *,cb_handle_t,struct cb_interface_id,unsigned,const void *,size_t,uint64_t);
#define cb_invoke mock_invoke
#define _start text_provider_start
#include "../handlers/text/text.c"
#undef _start
#undef cb_invoke
static struct text_cell storage[10*4],shown[10*4];static struct row_damage rows[4];
static unsigned copies,fills,presents,batches,fail_fill;
static struct text_cell frames[128][10*4];
static struct text_cell cached[4][10];static int positions[4];static struct cb_graphics_place placed[4];
static cb_status_t mock_invoke(struct cb_client *c,cb_handle_t h,struct cb_interface_id id,unsigned op,const void *p,size_t n,uint64_t deadline){
    (void)c;(void)h;(void)id;(void)deadline;
    if(op==CB_GRAPHICS_PRESENT){++presents;for(unsigned y=0;y<grid.rows;y++)for(unsigned x=0;x<grid.columns;x++)shown[y*grid.columns+x]=(struct text_cell){' ',0xdddddd,0};for(unsigned row=0;row<4;row++)if(positions[row]>=0&&positions[row]<4)for(unsigned x=0;x<10;x++)shown[positions[row]*10+x]=cached[row][x];assert(presents<=128);memcpy(frames[presents-1],shown,sizeof(shown));return CB_OK;}
    /* Scrolling must never fill/erase the screen or copy screen pixels. */
    assert(op!=CB_GRAPHICS_FILL && op!=CB_GRAPHICS_BLIT);
    if(op==CB_GRAPHICS_SURFACE_PLACE){assert(n==4*sizeof(struct cb_graphics_place));for(unsigned i=0;i<4;i++){struct cb_graphics_place q;memcpy(&q,(const char *)p+i*sizeof(q),sizeof(q));assert(q.surface>=2&&q.surface<6&&q.parent==1);positions[q.surface-2]=q.y/cell_height;}return CB_OK;}
    if(op==CB_GRAPHICS_SURFACE_SCROLL){++copies;struct cb_graphics_scroll q;memcpy(&q,p,sizeof(q));assert(q.surface==1&&!q.dx);for(unsigned i=0;i<4;i++)positions[i]+=q.dy/(int)cell_height;return CB_OK;}
    if(op==CB_GRAPHICS_RECTANGLE){++fills;if(fail_fill){fail_fill=0;return CB_RESOURCE_FAILURE;}struct cb_graphics_draw q;memcpy(&q,p,sizeof(q));assert(q.surface>=2&&q.surface<6&&q.x[1]==80&&q.y[1]==17);for(unsigned x=0;x<10;x++)cached[q.surface-2][x]=(struct text_cell){' ',0xdddddd,0};return CB_OK;}
    assert(op==CB_GRAPHICS_TEXT_BATCH);++batches;struct cb_graphics_text_batch header;memcpy(&header,p,sizeof(header));assert(header.flags==CB_GRAPHICS_DEFER && header.surface>=2 && header.surface<6);for(size_t at=sizeof(header);at<n;at+=sizeof(struct cb_glyph)){struct cb_glyph g;memcpy(&g,(const char *)p+at,sizeof(g));assert(g.x/cell_width<grid.columns && !g.y);cached[header.surface-2][g.x/cell_width]=(struct text_cell){g.codepoint,g.foreground,g.background};}return CB_OK;
}
static void initialize(void){grid=(struct cb_text_info){10,4,0,0};cell_width=8;cell_height=17;cells=storage;damage=rows;viewport=1;placements=placed;for(unsigned i=0;i<4;i++)damage[i]=(struct row_damage){.surface=i+2};assert(!clear());copies=fills=presents=batches=0;}
static void matching(void){for(unsigned y=0;y<grid.rows;y++)for(unsigned x=0;x<grid.columns;x++){struct text_cell a=*cell_at(y,x),b=shown[y*grid.columns+x];assert(a.codepoint==b.codepoint && a.background==b.background && (a.codepoint==' ' || a.foreground==b.foreground));}}
int main(void){
    initialize();char output[240];for(unsigned i=0;i<60;i++){output[i*4]='0'+i/10;output[i*4+1]='0'+i%10;output[i*4+2]='!';output[i*4+3]='\n';}assert(!write((void *)output,sizeof(output)));assert(copies==57 && fills==57 && presents==60 && batches==60);assert(!commit() && presents==60);
    for(unsigned i=0;i<60;i++){unsigned row=i<3?i:2;assert(frames[i][row*10].codepoint=='0'+i/10 && frames[i][row*10+1].codepoint=='0'+i%10 && frames[i][row*10+2].codepoint=='!');}assert(cell_at(0,0)->codepoint=='5' && cell_at(0,1)->codepoint=='7');matching();
    initialize();set_cell(2,3,'X',0x123456,0x112233);assert(!commit());grid.row=3;assert(!write((void *)"a\nb\n",4));assert(!commit() && copies==2 && fills==2 && presents==3);assert(cell_at(0,3)->foreground==0x123456);matching();
    /* Failed clear after accepted copy is repaired without copying twice. */
    initialize();assert(!write((void *)"one\ntwo\nthree\n",14));assert(!commit());grid.row=3;fail_fill=1;assert(write((void *)"next\nnot accepted",17)==CB_RESOURCE_FAILURE && copies==1);assert(!commit() && copies==1);assert(grid.column==0 && cell_at(3,0)->codepoint==' ');matching();
    /* A trailing partial line is committed at the WRITE boundary, without
     * postponing preceding newline frames. CRLF retains one newline boundary. */
    initialize();assert(!write((void *)"a\r\nb\nlast",9));assert(presents==2 && frames[0][0].codepoint=='a' && frames[0][10].codepoint==' ' && frames[1][10].codepoint=='b' && shown[20].codepoint==' ');assert(!commit() && presents==3 && shown[20].codepoint=='l');matching();
    initialize();assert(!write((void *)"\n\n\n\n",4));assert(copies==1 && presents==1);matching();
    initialize();assert(!write((void *)"1234567890X",11));assert(presents==1 && shown[0].codepoint=='1' && shown[10].codepoint==' ');assert(!commit());assert(grid.row==1 && grid.column==1);matching();assert(!write((void *)"\r\xc3",2));assert(!commit());assert(remaining==1);assert(!write((void *)"\xa7\bZ",3));assert(!commit());assert(cell_at(1,0)->codepoint=='Z');matching();
    /* Direct graphics users may have changed the screen between text calls. */
    shown[10].codepoint='?';grid.row=1;grid.column=0;assert(!write((void *)"Z",1));assert(!commit() && shown[10].codepoint=='Z');unsigned before=presents;assert(!commit() && presents==before);
    puts("PASS text presentation: newline frames, trailing partial lines, CRLF, blank lines, wrap, retained row surfaces, split UTF-8, external redraw and failed-copy continuation");
}
