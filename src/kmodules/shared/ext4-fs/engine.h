#ifndef CLOUDBERRY_EXT4_ENGINE_H
#define CLOUDBERRY_EXT4_ENGINE_H
#include "../ext4-fs.h"
struct ext4_engine;
int ext4_engine_setup(const struct kernel_surfaces *,uintptr_t);
int ext4_engine_enter(struct ext4_engine *);
void ext4_engine_leave(void);
struct ext4_engine *ext4_engine_mount(const struct block_device *,int readonly,uint64_t id,unsigned *error);
int ext4_engine_abort_enter(struct ext4_engine *);
void ext4_engine_disconnect(struct ext4_engine *);
int ext4_engine_abort(struct ext4_engine *);
int ext4_engine_unmount(struct ext4_engine *);
unsigned ext4_engine_error(struct ext4_engine *);
int ext4_engine_stat(struct ext4_engine *,const char *,struct ext4_stat *);
struct kernel_surface_result ext4_engine_read(struct ext4_engine *,const char *,uint64_t,void *,size_t);
struct kernel_surface_result ext4_engine_write(struct ext4_engine *,const char *,uint64_t,const void *,size_t,unsigned);
int ext4_engine_mutate(struct ext4_engine *,unsigned,const char *,uint64_t,const char *);
struct kernel_surface_result ext4_engine_list(struct ext4_engine *,const char *,uint64_t *,struct ext4_dirent *);
int ext4_engine_sync(struct ext4_engine *);
#endif
