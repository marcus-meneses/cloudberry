#ifndef CLOUDBERRY_EDITOR_MODEL_H
#define CLOUDBERRY_EDITOR_MODEL_H
#include <stdint.h>
#include <stddef.h>
struct ed_line {
    size_t refs,length;
    uint32_t text[];
};
struct ed_state {
    struct ed_line **lines;
    size_t count,row,col;
};
struct ed_snapshot {
    struct ed_snapshot *next;
    struct ed_state state;
};
struct ed_doc {
    struct ed_state state,saved;
    struct ed_snapshot *undo,*redo;
    size_t top,left;
    int bom,crlf,overwrite;
    char *path;
    uint32_t *clipboard;
    size_t clipboard_length;
};
int ed_decode(const unsigned char *,size_t,uint32_t **,size_t *);
char *ed_utf8(const uint32_t *,size_t,size_t *);
int ed_open(struct ed_doc *,const void *,size_t,const char *);
void ed_destroy(struct ed_doc *);
int ed_dirty(const struct ed_doc *);
int ed_saved(struct ed_doc *,const char *);
char *ed_serialize(const struct ed_doc *,size_t *);
int ed_insert(struct ed_doc *,const uint32_t *,size_t);
int ed_backspace(struct ed_doc *);
int ed_delete(struct ed_doc *);
int ed_copy(struct ed_doc *);
int ed_cut(struct ed_doc *);
void ed_undo(struct ed_doc *);
void ed_redo(struct ed_doc *);
void ed_move(struct ed_doc *,unsigned);
void ed_scroll(struct ed_doc *,ptrdiff_t);
int ed_find(struct ed_doc *,const uint32_t *,size_t);
#endif
