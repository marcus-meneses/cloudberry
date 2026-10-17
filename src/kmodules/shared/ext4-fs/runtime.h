#ifndef CLOUDBERRY_EXT4_RUNTIME_H
#define CLOUDBERRY_EXT4_RUNTIME_H
#include <stddef.h>
void *ext4_user_malloc(size_t);
void *ext4_user_calloc(size_t,size_t);
void *ext4_user_realloc(void *,size_t);
void ext4_user_free(void *);
void cloudberry_ext4_failure(int);
#endif
