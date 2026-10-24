/* FreeType memory is ordinary provider-owned C SDK memory. Only memory faces
 * are supported: never route FreeType filenames into an implicit host/POSIX FS. */
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYSTEM_H
#include <stdlib.h>
static void *ft_alloc(FT_Memory memory,long bytes){(void)memory;return bytes>0?malloc((size_t)bytes):NULL;}
static void ft_free(FT_Memory memory,void *p){(void)memory;free(p);}
static void *ft_resize(FT_Memory memory,long old,long size,void *p){(void)memory;(void)old;return size>=0?realloc(p,(size_t)size):NULL;}
FT_Memory FT_New_Memory(void){FT_Memory memory=calloc(1,sizeof(*memory));if(memory){memory->alloc=ft_alloc;memory->free=ft_free;memory->realloc=ft_resize;}return memory;}
void FT_Done_Memory(FT_Memory memory){free(memory);}
FT_Error FT_Stream_Open(FT_Stream stream,const char *path){(void)stream;(void)path;return FT_Err_Unimplemented_Feature;}
