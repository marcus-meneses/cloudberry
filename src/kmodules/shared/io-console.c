#include "io-console.h"
#include "../../interfaces/display.h"
#include "../../interfaces/filesystem.h"
#include "ext4-fs.h"
#include "mem-pages.h"
static struct framebuffer screen;
static uint64_t owner,filesystem_owner,graphics_owner,volume;static const struct kernel_surfaces *console_hub;
/* Resident I/O-backend state, never exposed through a handle. The shadow is
 * allocated lazily; a full initial paint avoids reading slow framebuffer RAM. */
static struct mem_pages shadow_pages;
static uint32_t *shadow;
static unsigned shadow_valid;
#ifndef CB_DISPLAY_READ_PIXEL
#define CB_DISPLAY_READ_PIXEL(p) (*(p))
#endif
static int allocate_shadow(void){
    if(shadow)return 1;
    if(!console_hub || !screen.width || !screen.height || screen.height>SIZE_MAX/screen.width/4)return 0;
    size_t bytes=(size_t)screen.width*screen.height*4;if(bytes>SIZE_MAX-4095)return 0;
    kernel_surface_call allocate=kernel_surface_find(console_hub,"mem.pages_alloc");
    if(!allocate || allocate((bytes+4095)/4096,0,(uintptr_t)&shadow_pages,0,0,0).status)return 0;
    shadow=(void *)shadow_pages.address;return shadow!=NULL;
}
static int synchronize_shadow(void){
    if(!allocate_shadow())return 0;
    if(shadow_valid)return 1;
    for(uint32_t y=0;y<screen.height;++y){const volatile uint32_t *row=(const void *)((uintptr_t)screen.address+y*screen.pitch);for(uint32_t x=0;x<screen.width;++x)shadow[(size_t)y*screen.width+x]=CB_DISPLAY_READ_PIXEL(row+x);}
    shadow_valid=1;return 1;
}
int io_console_init(const struct framebuffer_response *r){
    if(!r || !r->count || !r->frames || !r->frames[0])return 0;
    screen=*r->frames[0];if(screen.red_shift>24 || screen.green_shift>24 || screen.blue_shift>24)return 0;uint32_t red=255u<<screen.red_shift,green=255u<<screen.green_shift,blue=255u<<screen.blue_shift;if((red&green) || (red&blue) || (green&blue))return 0;return screen.address && screen.width && screen.height && screen.width<=UINT32_MAX && screen.height<=UINT32_MAX && screen.bpp==32 && screen.model==1 && screen.red_size==8 && screen.green_size==8 && screen.blue_size==8 && screen.red_shift<=24 && screen.green_shift<=24 && screen.blue_shift<=24 && screen.width<=UINT64_MAX/4 && screen.pitch>=screen.width*4 && screen.height<=UINTPTR_MAX/screen.pitch && screen.height*screen.pitch<=UINTPTR_MAX-(uintptr_t)screen.address;
}
void io_console_bind(const char *name,uint64_t pid,uint64_t filesystem){const char *g=name,*h="graphics";while(*g && *g==*h){++g;++h;}if(!*g && !*h)graphics_owner=pid;const char *a=name,*b="filesystem";while(*a && *a==*b){++a;++b;}if(!*a && !*b){filesystem_owner=pid;volume=filesystem;}const char *s="display";while(*name && *name==*s){++name;++s;}if(!*name && !*s)owner=pid;}
static void copy(void *dst,const void *src,size_t n){unsigned char *d=dst;const unsigned char *s=src;while(n--)*d++=*s++;}
int io_console_invoke(struct io_invocation *r){
    if((r->operation<CB_IO_DISPLAY_INFO || r->operation>CB_IO_DISPLAY_BLIT) && r->operation!=CB_IO_DISPLAY_COPY)return 0;
    r->status=CB_DENIED;if(!owner || owner!=r->process)return 1;r->status=CB_BAD_REQUEST;
    if(r->operation==CB_IO_DISPLAY_INFO){if(!r->bytes && r->capacity>=sizeof(struct cb_display_info)){struct cb_display_info info={screen.width,screen.height};copy(r->output,&info,sizeof(info));r->actual=sizeof(info);r->status=CB_OK;}return 1;}
    if(r->operation==CB_IO_DISPLAY_COPY){
        struct cb_display_copy box;if(r->bytes!=sizeof(box))return 1;copy(&box,r->input,sizeof(box));
        if(box.x>screen.width || box.y>screen.height || box.source_x>screen.width || box.source_y>screen.height || box.width>screen.width-box.x || box.width>screen.width-box.source_x || box.height>screen.height-box.y || box.height>screen.height-box.source_y)return 1;
        if(!box.width || !box.height){r->status=CB_OK;return 1;}
        if(!synchronize_shadow()){r->status=CB_UNAVAILABLE;return 1;}
        int reverse_y=box.y>box.source_y,reverse_x=box.y==box.source_y && box.x>box.source_x;
        for(uint32_t i=0;i<box.height;++i){uint32_t y=reverse_y?box.height-1-i:i;
            uint32_t *out=shadow+(size_t)(box.y+y)*screen.width+box.x;
            const uint32_t *in=shadow+(size_t)(box.source_y+y)*screen.width+box.source_x;
            uint32_t first=box.width,last=0;
            for(uint32_t j=0;j<box.width;++j){uint32_t x=reverse_x?box.width-1-j:j,pixel=in[x];if(out[x]!=pixel){out[x]=pixel;if(x<first)first=x;if(x>last)last=x;}}
            if(first<box.width){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch+(uint64_t)box.x*4);for(uint32_t x=first;x<=last;++x)row[x]=out[x];}
        }r->status=CB_OK;return 1;
    }
    struct cb_rectangle box;if(r->bytes<sizeof(box))return 1;copy(&box,r->input,sizeof(box));
    if(box.reserved || box.x>screen.width || box.y>screen.height || box.width>screen.width-box.x || box.height>screen.height-box.y || box.color>0xffffff)return 1;
    uint64_t pixels=(uint64_t)box.width*box.height;if(pixels>SIZE_MAX/4)return 1;
    if(r->bytes!=sizeof(box)+(r->operation==CB_IO_DISPLAY_BLIT?pixels*4:0))return 1;
    (void)allocate_shadow();
    const unsigned char *data=(const unsigned char *)r->input+sizeof(box);
    if(r->operation==CB_IO_DISPLAY_FILL){
        uint32_t rgb=box.color,packed=((rgb>>16&255)<<screen.red_shift)|((rgb>>8&255)<<screen.green_shift)|((rgb&255)<<screen.blue_shift);
        for(uint32_t y=0;y<box.height;++y){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch);for(uint32_t x=0;x<box.width;++x){if(shadow)shadow[(size_t)(box.y+y)*screen.width+box.x+x]=packed;row[box.x+x]=packed;}}
        if(shadow && !box.x && !box.y && box.width==screen.width && box.height==screen.height)shadow_valid=1;
        r->status=CB_OK;return 1;
    }
    for(uint32_t y=0;y<box.height;++y){volatile uint32_t *row=(void *)((uintptr_t)screen.address+(box.y+y)*screen.pitch);for(uint32_t x=0;x<box.width;++x){uint32_t rgb=box.color;if(r->operation==CB_IO_DISPLAY_BLIT)copy(&rgb,data+((size_t)y*box.width+x)*4,4);uint32_t packed=((rgb>>16&255)<<screen.red_shift)|((rgb>>8&255)<<screen.green_shift)|((rgb&255)<<screen.blue_shift);if(shadow)shadow[(size_t)(box.y+y)*screen.width+box.x+x]=packed;row[box.x+x]=packed;}}
    if(shadow && !box.x && !box.y && box.width==screen.width && box.height==screen.height)shadow_valid=1;
    r->status=CB_OK;return 1;
}

void io_console_hub(const struct kernel_surfaces *hub){console_hub=hub;}
static cb_status_t fs_error(void){struct ext4_info info={0};kernel_surface_call get=kernel_surface_find(console_hub,"ext4_fs.info");if(!get || get(volume,(uintptr_t)&info,0,0,0,0).status)return CB_RESOURCE_FAILURE;return info.error==EXT4_NOT_FOUND?CB_NOT_FOUND:info.error==EXT4_READ_ONLY?CB_READ_ONLY:info.error==EXT4_BUSY?CB_BUSY:CB_RESOURCE_FAILURE;}
int io_filesystem_invoke(struct io_invocation *r){
    if(r->operation==CB_IO_OWNER_WATCH){r->status=CB_DENIED;if(r->process!=graphics_owner || !graphics_owner)return 1;r->status=CB_BAD_REQUEST;if(r->bytes!=sizeof(struct cb_io_owner_watch))return 1;struct cb_io_owner_watch w;copy(&w,r->input,sizeof(w));kernel_surface_call watch=kernel_surface_find(console_hub,"handles.owner_watch");if(watch && w.enable<=1)r->status=watch(r->process,w.process,w.token,w.enable,0,0).status?CB_RESOURCE_FAILURE:CB_OK;return 1;}
    if(r->operation!=CB_IO_FILESYSTEM)return 0;
    r->status=CB_DENIED;if(r->process!=filesystem_owner || !filesystem_owner)return 1;r->status=CB_BAD_REQUEST;
    if(r->bytes<32+sizeof(struct cb_filesystem_request))return 1;
    uint64_t owner_pid,op;copy(&owner_pid,r->input,8);copy(&op,(const unsigned char *)r->input+8,8);struct cb_filesystem_request req;copy(&req,(const unsigned char *)r->input+32,sizeof(req));
    if(!req.path_bytes || req.path_bytes>4095 || r->bytes!=32+sizeof(req)+req.path_bytes)return 1;
    char path[4096];copy(path,(const unsigned char *)r->input+32+sizeof(req),req.path_bytes);path[req.path_bytes]=0;if(path[0]!='/')return 1;
    for(size_t i=0;i<req.path_bytes;){if(!path[i])return 1;if(path[i]=='/'){if(i && path[i-1]=='/')return 1;++i;continue;}size_t begin=i;while(i<req.path_bytes && path[i]!='/'){if(!path[i])return 1;++i;}size_t n=i-begin;if((n==1 && path[begin]=='.') || (n==2 && path[begin]=='.' && path[begin+1]=='.'))return 1;}
    int writes=(op==CB_FILESYSTEM_OPEN && req.mode!=CB_FILE_READ) || op==CB_FILESYSTEM_REMOVE || op==CB_FILESYSTEM_MKDIR;
    int own_data=io_installation_data((const unsigned char *)r->input+16,path);
    if(writes && !own_data && ((req.path_bytes>=5 && path[1]=='b' && path[2]=='o' && path[3]=='o' && path[4]=='t' && (req.path_bytes==5 || path[5]=='/')) || (req.path_bytes>=7 && path[1]=='s' && path[2]=='y' && path[3]=='s' && path[4]=='t' && path[5]=='e' && path[6]=='m' && (req.path_bytes==7 || path[7]=='/')))){r->status=CB_DENIED;return 1;}
    if(op==CB_FILESYSTEM_OPEN){if(r->capacity<8 || req.cursor || req.mode<CB_FILE_READ || req.mode>CB_FILE_APPEND)return 1;kernel_surface_call open=kernel_surface_find(console_hub,"handles.file_open");struct kernel_surface_result opened=open?open(owner_pid,volume,(uintptr_t)path,req.mode,0,0):module_result(0,0);r->status=opened.status?opened.status:CB_OK;if(!opened.status){copy(r->output,&opened.value,8);r->actual=8;}return 1;}
    if(req.mode || (op!=CB_FILESYSTEM_LIST && req.cursor))return 1;
    if(op==CB_FILESYSTEM_STAT && r->capacity>=sizeof(struct cb_filesystem_stat)){struct ext4_stat info;kernel_surface_call stat=kernel_surface_find(console_hub,"ext4_fs.stat");if(stat(volume,(uintptr_t)path,(uintptr_t)&info,0,0,0).status)r->status=fs_error();else {struct cb_filesystem_stat out={info.bytes,(info.mode&0170000)==0040000,0};copy(r->output,&out,sizeof(out));r->actual=sizeof(out);r->status=CB_OK;}}
    else if(op==CB_FILESYSTEM_LIST){struct ext4_dirent entry;kernel_surface_call list=kernel_surface_find(console_hub,"ext4_fs.list");struct kernel_surface_result result=list(volume,(uintptr_t)path,(uintptr_t)&req.cursor,(uintptr_t)&entry,0,0);if(result.status)r->status=fs_error();else if(!result.value)r->status=CB_OK;else {struct cb_filesystem_entry out={.cursor=req.cursor,.directory=entry.type==2,.name_bytes=entry.name_bytes};copy(out.name,entry.name,entry.name_bytes);if(sizeof(out)>r->capacity){r->status=CB_BUFFER_TOO_SMALL;return 1;}copy(r->output,&out,sizeof(out));r->actual=sizeof(out);r->status=CB_OK;}}
    else if(op==CB_FILESYSTEM_REMOVE || op==CB_FILESYSTEM_MKDIR){kernel_surface_call mutate=kernel_surface_find(console_hub,"ext4_fs.mutate");r->status=mutate(volume,op==CB_FILESYSTEM_REMOVE?EXT4_MUTATE_REMOVE:EXT4_MUTATE_MKDIR,(uintptr_t)path,0,0,0).status?fs_error():CB_OK;}
    return 1;
}
