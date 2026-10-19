#ifndef CLOUDBERRY_INTERFACE_TEXT_H
#define CLOUDBERRY_INTERFACE_TEXT_H
#include "handle.h"
#define CB_TEXT_NAME "/@text"
#define CB_TEXT_INTERFACE_ID {{0x43,0x42,0x54,0x45,0x58,0x31,0x46,0x3b,0x91,0xe1,0x40,0xa,0x1c,0x87,0xc0,0x9}}
enum {CB_TEXT_INFO=1,CB_TEXT_CLEAR,CB_TEXT_WRITE,CB_TEXT_CELL};
struct cb_text_info {uint32_t columns,rows,column,row;};
struct cb_text_cell {uint32_t column,row,codepoint,foreground,background,reserved;};
/* WRITE: UTF-8, newline, CR, backspace. CELL leaves cursor unchanged. */
#endif
