#ifndef CLOUDBERRY_EXT4_RUNTIME_H
#define CLOUDBERRY_EXT4_RUNTIME_H
/* The upstream eight-block default cannot retain even the inode/directory
 * working set of a normal installed-app path. Buffers are allocated lazily;
 * 128 blocks bound this cache to 512 KiB of data for a 4 KiB filesystem. */
#define CONFIG_BLOCK_DEV_CACHE_SIZE 128
#include <stddef.h>
void *ext4_user_malloc(size_t);
void *ext4_user_calloc(size_t,size_t);
void *ext4_user_realloc(void *,size_t);
void ext4_user_free(void *);
void cloudberry_ext4_failure(int);
#endif
