/* Reuse the host hub/allocator/block fixture; all I/O targets disposable files. */
#define main readonly_fixture_main
#include "storage-fs.c"
#undef main
#include <unistd.h>
static uint64_t writes,flushes,cut,fail_write,fail_flush;
static int tracing,disconnect_on_write;
static struct kernel_surface_result write_block(MODULE_ARGS){
    MODULE_UNUSED();assert(a==1 && b<=size/512 && c<=8 && c<=size/512-b && e>=c*512);
    if(disconnect_on_write && writes>=2){disconnect_on_write=0;assert(ext4_fs_surface_disconnect((uintptr_t)read_block,1,0,0,0,0).status);assert(partition_surface_disconnect((uintptr_t)read_block,1,0,0,0,0).status);return module_result(0,0);}
    if(fail_write && writes+1==fail_write){++writes;return module_result(0,0);}
    assert(!fseeko(image,b*512,SEEK_SET));assert(fwrite((void *)d,512,c,image)==c);++writes;
    if(tracing){const uint8_t *p=(void *)d;unsigned type=p[0]==0xc0 && p[1]==0x3b && p[2]==0x39 && p[3]==0x98?p[7]:0;printf("WRITE %llu %llu %u\n",(unsigned long long)(flushes+1),(unsigned long long)b,type);fflush(stdout);}
    return module_result(c*512,1);
}
static struct kernel_surface_result flush_block(MODULE_ARGS){MODULE_UNUSED();assert(!fflush(image) && !fsync(fileno(image)));++flushes;if(fail_flush && flushes==fail_flush)return module_result(0,0);if(cut && flushes==cut)_exit(77);return module_result(1,1);}
#define CALL(fn,a,b,c) (fn)((uintptr_t)(a),(uintptr_t)(b),(uintptr_t)(c),0,0,0)
static void setup(const char *filename,struct block_device *part){
    image=fopen(filename,"r+b");assert(image && !fseeko(image,0,SEEK_END));size=ftello(image);
    fixture.hub=(struct kernel_surfaces){.magic=KERNEL_SURFACES_MAGIC,.header_bytes=sizeof(struct kernel_surfaces)};
    method("cpu.control",cpu);method("threads.control",thread);method("irq.context",cpu);method("mem.calloc",allocate);method("mem.free",release);method("mem.realloc",resize_memory);
    method("partition.read",partition_surface_read);method("partition.control",partition_surface_control);method("partition.write",partition_surface_write);method("partition.flush",partition_surface_flush);
    assert(!CALL(partition_surface_init,&fixture.hub,1,0).status && !CALL(ext4_fs_surface_init,&fixture.hub,1,0).status);
    struct block_device raw={.major=1,.minor=1,.block_bytes=512,.max_transfer_bytes=4096,.blocks=size/512,.cookie=1,.read=read_block,.control=control,.write=write_block,.flush=flush_block};
    const uint8_t guid[16]={0xfd,0xea,0xea,0x9b,0x35,0x78,0x6a,0x44,0x82,0x58,0x00,0x33,0x18,0x78,0xd4,0xf3};
    assert(!partition_surface_find((uintptr_t)&raw,(uintptr_t)guid,(uintptr_t)part,sizeof(*part),0,0).status && block_writable(part));
    /* GPT bounds must prevent writes into either partition table. */
    uint8_t byte[512]={0};uint64_t before=writes;assert(part->write(part->cookie,part->blocks,1,(uintptr_t)byte,512,0).status && writes==before);
}
static void close_all(struct block_device *part,uint64_t id){assert(!CALL(ext4_fs_surface_unmount,id,0,0).status);assert(!CALL(partition_surface_close,part->cookie,0,0).status && !refs && !live);assert(!fclose(image));}
static void full_mutations(struct block_device *part,uint64_t id){
    const char *dir="/system/kmodules/drivers/newdir",*path="/system/kmodules/drivers/newdir/data.bin",*to="/system/kmodules/drivers/newdir/moved.bin";
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_MKDIR,(uintptr_t)dir,0,0,0).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_CREATE,(uintptr_t)path,0,0,0).status);
    assert(ext4_fs_surface_mutate(id,EXT4_MUTATE_CREATE,(uintptr_t)path,0,0,0).status);
    unsigned char data[90000],out[90000];for(unsigned i=0;i<sizeof(data);++i)data[i]=i*19+7;
    struct kernel_surface_result r=ext4_fs_surface_write(id,(uintptr_t)path,0,(uintptr_t)data,sizeof(data),0);assert(!r.status && r.value==sizeof(data));
    r=ext4_fs_surface_read(id,(uintptr_t)path,0,(uintptr_t)out,sizeof(out),0);if(r.status || r.value!=sizeof(out) || memcmp(out,data,sizeof(out))){struct ext4_info detail;CALL(ext4_fs_surface_info,id,&detail,0);size_t diff=0;while(diff<sizeof(out) && out[diff]==data[diff])++diff;fprintf(stderr,"readback status=%lu bytes=%lu error=%u diff=%zu got=%u expected=%u\n",r.status,r.value,detail.error,diff,diff<sizeof(out)?out[diff]:0,diff<sizeof(out)?data[diff]:0);}
    assert(!r.status && r.value==sizeof(out) && !memcmp(out,data,sizeof(out)));
    assert(!ext4_fs_surface_write(id,(uintptr_t)path,UINT64_MAX,(uintptr_t)"END",3,EXT4_WRITE_APPEND).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_TRUNCATE,(uintptr_t)path,137,0,0).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_TRUNCATE,(uintptr_t)path,8193,0,0).status);
    r=ext4_fs_surface_read(id,(uintptr_t)path,0,(uintptr_t)out,8193,0);assert(!r.status && r.value==8193 && !memcmp(out,data,137));for(unsigned i=137;i<8193;++i)assert(out[i]==0);
    assert(!ext4_fs_surface_write(id,(uintptr_t)path,12000,(uintptr_t)"TAIL",4,0).status);
    r=ext4_fs_surface_read(id,(uintptr_t)path,8193,(uintptr_t)out,12004-8193,0);assert(!r.status);for(unsigned i=0;i<12000-8193;++i)assert(!out[i]);assert(!memcmp(out+12000-8193,"TAIL",4));
    assert(ext4_fs_surface_mutate(id,EXT4_MUTATE_RMDIR,(uintptr_t)dir,0,0,0).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_RENAME,(uintptr_t)path,0,(uintptr_t)to,0).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_REMOVE,(uintptr_t)to,0,0,0).status);
    assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_RMDIR,(uintptr_t)dir,0,0,0).status);
    assert(!CALL(ext4_fs_surface_sync,id,0,0).status && writes && flushes);
    /* Conflicting read-only views of an active writable volume are refused. */
    struct ext4_info refused;assert(CALL(ext4_fs_surface_mount,part,&refused,0).status);
}
int main(int argc,char **argv){
    assert(argc>=3);struct block_device part;setup(argv[1],&part);struct ext4_info info;
    struct kernel_surface_result mounted=CALL(ext4_fs_surface_mount,&part,&info,EXT4_MOUNT_WRITE);
    if(mounted.status){fprintf(stderr,"mount failed error=%lu cookie=%llu\n",mounted.value,(unsigned long long)info.cookie);return 1;}uint64_t id=info.cookie;
    if(!strcmp(argv[2],"read-cache")){
        const char *dirs[]={"/cache","/cache/a","/cache/a/b","/cache/a/b/c"};
        for(unsigned i=0;i<4;++i)assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_MKDIR,(uintptr_t)dirs[i],0,0,0).status);
        const char *path="/cache/a/b/c/file";
        assert(!ext4_fs_surface_write(id,(uintptr_t)path,0,(uintptr_t)"CACHE",5,EXT4_WRITE_CREATE|EXT4_WRITE_EXCLUSIVE).status);
        struct ext4_stat st;uint64_t before=block_reads;
        assert(!CALL(ext4_fs_surface_stat,id,path,&st).status && st.bytes==5);
        uint64_t cold=block_reads-before;before=block_reads;
        for(unsigned i=0;i<20;++i)assert(!CALL(ext4_fs_surface_stat,id,path,&st).status && st.bytes==5);
        printf("CACHE cold=%llu repeated=%llu\n",(unsigned long long)cold,(unsigned long long)(block_reads-before));
        assert(block_reads==before);
    }else if(!strcmp(argv[2],"normal")){
        full_mutations(&part,id);
        assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_MKDIR,(uintptr_t)"/persist",0,0,0).status);
        assert(!ext4_fs_surface_write(id,(uintptr_t)"/persist/check.bin",0,(uintptr_t)"CHECK",5,EXT4_WRITE_CREATE|EXT4_WRITE_EXCLUSIVE).status);
        assert(!CALL(ext4_fs_surface_unmount,id,0,0).status && !CALL(ext4_fs_surface_mount,&part,&info,0).status);id=info.cookie;
        char data[5];assert(!ext4_fs_surface_read(id,(uintptr_t)"/persist/check.bin",0,(uintptr_t)data,5,0).status && !memcmp(data,"CHECK",5));
    }
    else if(!strcmp(argv[2],"action") || !strcmp(argv[2],"append") || !strcmp(argv[2],"truncate") || !strcmp(argv[2],"remove") || !strcmp(argv[2],"rename")){
        tracing=1;writes=flushes=0;cut=argc>3?strtoull(argv[3],NULL,10):0;
        const char *path="/system/kmodules/drivers/crash.bin";unsigned char bytes[8192];memset(bytes,0x59,sizeof(bytes));
        if(!strcmp(argv[2],"action")){
            assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_CREATE,(uintptr_t)path,0,0,0).status);puts("ACK CREATE");fflush(stdout);
            assert(!ext4_fs_surface_write(id,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0).status);puts("ACK WRITE");fflush(stdout);
        }else if(!strcmp(argv[2],"append")){assert(!ext4_fs_surface_write(id,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),EXT4_WRITE_APPEND).status);puts("ACK APPEND");fflush(stdout);}
        else if(!strcmp(argv[2],"rename")){assert(!ext4_fs_surface_mutate(id,EXT4_MUTATE_RENAME,(uintptr_t)path,0,(uintptr_t)"/renamed.bin",0).status);puts("ACK RENAME");fflush(stdout);}
        else {unsigned op=!strcmp(argv[2],"remove")?EXT4_MUTATE_REMOVE:EXT4_MUTATE_TRUNCATE;assert(!ext4_fs_surface_mutate(id,op,(uintptr_t)path,137,0,0).status);puts(op==EXT4_MUTATE_REMOVE?"ACK REMOVE":"ACK TRUNCATE");fflush(stdout);}
        tracing=0;cut=0;
    }else if(!strcmp(argv[2],"disconnect")){
        for(unsigned cycle=0;cycle<3;++cycle){
        assert(!CALL(part.control,part.cookie,BLOCK_RETAIN,0).status);
        uint64_t old=id,old_part=part.cookie;writes=flushes=0;disconnect_on_write=1;
        char path[32];snprintf(path,sizeof(path),"/disconnect-%u.bin",cycle);
        assert(ext4_fs_surface_mutate(id,EXT4_MUTATE_CREATE,(uintptr_t)path,0,0,0).status);
        uint64_t before_writes=writes,before_flushes=flushes;
        assert(!ext4_fs_surface_disconnect((uintptr_t)read_block,1,0,0,0,0).status);
        assert(!partition_surface_disconnect((uintptr_t)read_block,1,0,0,0,0).status);
        struct ext4_stat st;assert(CALL(ext4_fs_surface_stat,old,"/",&st).status);
        uint8_t data[512];assert(part.read(old_part,0,1,(uintptr_t)data,sizeof(data),0).status);
        assert(CALL(part.control,old_part,BLOCK_RETAIN,0).status);
        assert(writes==before_writes && flushes==before_flushes && refs==1);
        assert(!CALL(part.control,old_part,BLOCK_RELEASE,0).status && !refs && !live);
        assert(!fclose(image));setup(argv[1],&part);assert(part.cookie!=old_part);
        assert(!CALL(ext4_fs_surface_mount,&part,&info,EXT4_MOUNT_WRITE).status && info.cookie!=old);id=info.cookie;
        assert(CALL(ext4_fs_surface_stat,old,"/",&st).status);
        }
        puts("PASS three in-flight disconnect cycles, no abort writes/flushes, retained stale slice, complete allocation reclaim, fresh journal recovery");
    }else if(!strcmp(argv[2],"fail-write") || !strcmp(argv[2],"fail-flush")){
        writes=flushes=0;if(!strcmp(argv[2],"fail-write"))fail_write=strtoull(argv[3],NULL,10);else fail_flush=strtoull(argv[3],NULL,10);
        assert(ext4_fs_surface_mutate(id,EXT4_MUTATE_CREATE,(uintptr_t)"/failure.bin",0,0,0).status);
        assert(CALL(ext4_fs_surface_unmount,id,0,0).status && CALL(partition_surface_close,part.cookie,0,0).status);
        assert(ext4_fs_surface_write(id,(uintptr_t)"/failure.bin",0,(uintptr_t)"NO",2,0).status);
        puts("PASS failed I/O poisons volume and retains parent");fflush(stdout);_exit(78);
    }else if(!strcmp(argv[2],"recover-rename")){
        struct ext4_stat old,new;int have_old=!CALL(ext4_fs_surface_stat,id,"/system/kmodules/drivers/crash.bin",&old).status,have_new=!CALL(ext4_fs_surface_stat,id,"/renamed.bin",&new).status;assert(have_old!=have_new);
        const char *path=have_old?"/system/kmodules/drivers/crash.bin":"/renamed.bin";unsigned char bytes[8192];struct kernel_surface_result r=ext4_fs_surface_read(id,(uintptr_t)path,0,(uintptr_t)bytes,sizeof(bytes),0);assert(!r.status && r.value==8192);for(unsigned i=0;i<sizeof(bytes);++i)assert(bytes[i]==0x59);puts(have_old?"RECOVERED SOURCE":"RECOVERED DESTINATION");
    }else if(!strcmp(argv[2],"recover")){
        struct ext4_stat stat;struct kernel_surface_result r=CALL(ext4_fs_surface_stat,id,"/system/kmodules/drivers/crash.bin",&stat);
        if(!r.status){assert(stat.bytes==0 || stat.bytes==137 || stat.bytes==8192 || stat.bytes==16384);unsigned char data[16384];r=ext4_fs_surface_read(id,(uintptr_t)"/system/kmodules/drivers/crash.bin",0,(uintptr_t)data,sizeof(data),0);assert(!r.status && r.value==stat.bytes);for(size_t i=0;i<r.value;++i)assert(data[i]==0x59);printf("RECOVERED %llu\n",(unsigned long long)stat.bytes);}else puts("RECOVERED ABSENT");
    }else assert(0);
    close_all(&part,id);puts("PASS writable ext4");return 0;
}
