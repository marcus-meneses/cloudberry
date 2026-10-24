/* Exact engine coverage for test OCR; host file I/O belongs only to this tool. */
#include "../handlers/graphics-vga/font-engine.h"
#include <stdio.h>
#include <stdlib.h>
int main(int argc,char **argv){if(argc!=2)return 2;FILE *file=fopen(argv[1],"rb");if(!file)return 3;if(fseek(file,0,SEEK_END))return 4;long n=ftell(file);if(n<=0)return 5;rewind(file);unsigned char *p=malloc((size_t)n);if(!p || fread(p,1,(size_t)n,file)!=(size_t)n)return 6;fclose(file);if(!font_engine_open(p,(size_t)n))return 7;struct cb_graphics_font info;if(font_engine_info(0,&info))return 8;printf("{\"family\":\"%s\",\"width\":%u,\"height\":%u,\"ascent\":%u,\"glyphs\":[",info.family,info.cell_width,info.cell_height,info.ascent);for(unsigned cp=32;cp<127;++cp){const unsigned char *a;uint32_t w,h;if(font_engine_alpha(cp,0,&a,&w,&h))return 9;if(cp>32)putchar(',');putchar('[');for(size_t i=0;i<(size_t)w*h;++i)printf("%s%u",i?",":"",a[i]);putchar(']');}puts("]}");font_engine_close();free(p);return 0;}
