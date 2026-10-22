#include "libc.h"
#include <stdlib.h>
#include <malloc.h>
#include <errno.h>
#include <reent.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <string.h>
#include <limits.h>
#include <assert.h>
static struct cb_runtime runtime;
static int initialized;
static char cwd[4096]="/";
struct descriptor {struct descriptor *next;int fd;cb_handle_t cap;};
static struct descriptor *descriptors;
static unsigned standard_opened;
struct cb_runtime *cb_libc_runtime(void){if(!initialized){initialized=1;cb_runtime_init(&runtime);}return &runtime;}
int cb_libc_set_cwd(const char *s){if(!s || s[0]!='/')return 0;char p[sizeof(cwd)];if(!cb_path(p,sizeof(p),"/",s))return 0;memcpy(cwd,p,strlen(p)+1);return 1;}
static int error(cb_status_t s){switch(s){case CB_DENIED:return EACCES;case CB_NOT_FOUND:return ENOENT;case CB_RESOURCE_FAILURE:return ENOMEM;case CB_WOULD_BLOCK:case CB_BUSY:return EAGAIN;case CB_BROKEN_PIPE:return EPIPE;case CB_READ_ONLY:return EROFS;case CB_BAD_HANDLE:return EBADF;case CB_TIMEOUT:return ETIMEDOUT;case CB_UNAVAILABLE:return ENOSYS;case CB_BAD_REQUEST:return EINVAL;default:return EIO;}}
void *malloc(size_t n){void *p=cb_heap_alloc(&cb_libc_runtime()->heap,n);if(n && !p)errno=ENOMEM;return p;}
void free(void *p){if(!cb_heap_free(&cb_libc_runtime()->heap,p))cb_exit(134);}
void *calloc(size_t n,size_t size){void *p=cb_heap_calloc(&cb_libc_runtime()->heap,n,size);if(n && size && !p)errno=ENOMEM;return p;}
void *realloc(void *p,size_t n){void *q=cb_heap_realloc(&cb_libc_runtime()->heap,p,n);if(n && !q)errno=ENOMEM;return q;}
size_t malloc_usable_size(void *p){return cb_heap_usable(&cb_libc_runtime()->heap,p);}
void *_malloc_r(struct _reent *r,size_t n){void *p=malloc(n);if(n && !p)r->_errno=ENOMEM;return p;}
void *_calloc_r(struct _reent *r,size_t n,size_t size){void *p=calloc(n,size);if(n && size && !p)r->_errno=ENOMEM;return p;}
void *_realloc_r(struct _reent *r,void *p,size_t n){void *q=realloc(p,n);if(n && !q)r->_errno=ENOMEM;return q;}
void _free_r(struct _reent *r,void *p){(void)r;free(p);}
size_t _malloc_usable_size_r(struct _reent *r,void *p){(void)r;return malloc_usable_size(p);}
void *_memalign_r(struct _reent *r,size_t alignment,size_t n){if(!alignment || (alignment&(alignment-1))){r->_errno=EINVAL;return NULL;}void *p=cb_heap_aligned(&cb_libc_runtime()->heap,alignment,n);if(n && !p)r->_errno=ENOMEM;return p;}
void *aligned_alloc(size_t alignment,size_t n){if(!alignment || (alignment&(alignment-1)) || n%alignment){errno=EINVAL;return NULL;}return _memalign_r(_REENT,alignment,n);}
static struct descriptor *descriptor(int fd){for(struct descriptor *d=descriptors;d;d=d->next)if(d->fd==fd)return d;return NULL;}
static struct descriptor *bind_fd(int fd,cb_handle_t cap){struct descriptor *d=malloc(sizeof(*d));if(!d)return NULL;struct descriptor **at=&descriptors;while(*at && (*at)->fd<fd)at=&(*at)->next;*d=(struct descriptor){.next=*at,.fd=fd,.cap=cap};*at=d;return d;}
static struct descriptor *get_fd(int fd){struct descriptor *d=descriptor(fd);if(d || fd<0 || fd>2 || (standard_opened&(1u<<fd)))return d;cb_handle_t cap=cb_standard(fd);if(!cap)return NULL;d=bind_fd(fd,cap);if(!d)cb_system_release(cap);else standard_opened|=1u<<fd;return d;}
_ssize_t _write_r(struct _reent *r,int fd,const void *p,size_t n){struct descriptor *d=get_fd(fd);size_t written;if(!d){r->_errno=EBADF;return -1;}if(n>(size_t)LONG_MAX){r->_errno=EINVAL;return -1;}cb_status_t s=cb_runtime_write(cb_libc_runtime(),d->cap,p,n,&written,0);if(s){r->_errno=error(s);return -1;}return written;}
_ssize_t _read_r(struct _reent *r,int fd,void *p,size_t n){struct descriptor *d=get_fd(fd);size_t read;if(!d){r->_errno=EBADF;return -1;}if(n>(size_t)LONG_MAX){r->_errno=EINVAL;return -1;}cb_status_t s=cb_runtime_read(cb_libc_runtime(),d->cap,p,n,&read,0);if(s){r->_errno=error(s);return -1;}return read;}
int _close_r(struct _reent *r,int fd){struct descriptor *d=get_fd(fd);if(!d){r->_errno=EBADF;return -1;}cb_status_t s=cb_runtime_close(cb_libc_runtime(),d->cap);if(s){r->_errno=error(s);return -1;}struct descriptor **at=&descriptors;while(*at!=d)at=&(*at)->next;*at=d->next;free(d);return 0;}
int _open_r(struct _reent *r,const char *path,int flags,int mode){(void)mode;unsigned access=flags&O_ACCMODE;unsigned supported=O_ACCMODE|O_CREAT|O_TRUNC|O_APPEND;unsigned operation;
    if((unsigned)flags&~supported){r->_errno=ENOTSUP;return -1;}
    if(access==O_RDONLY && !(flags&(O_CREAT|O_TRUNC|O_APPEND)))operation=CB_FILE_READ;
    else if(access==O_WRONLY && (flags&O_TRUNC) && !(flags&O_APPEND))operation=CB_FILE_RECREATE;
    else if(access==O_WRONLY && (flags&O_APPEND) && !(flags&O_TRUNC))operation=CB_FILE_APPEND;
    else {r->_errno=ENOTSUP;return -1;}
    /* Public stream OPEN has no atomic must-exist output mode. */
    if(operation!=CB_FILE_READ && !(flags&O_CREAT)){r->_errno=ENOTSUP;return -1;}
    struct cb_runtime *rt=cb_libc_runtime();
    int fd=3;for(struct descriptor *d=descriptors;d;d=d->next){if(d->fd<fd)continue;if(d->fd>fd)break;if(fd==INT_MAX){r->_errno=EMFILE;return -1;}++fd;}
    cb_handle_t cap;cb_status_t s=cb_runtime_open(rt,cwd,path,operation,&cap);if(s){r->_errno=error(s);return -1;}if(!bind_fd(fd,cap)){cb_system_release(cap);r->_errno=ENOMEM;return -1;}return fd;
}
int _fstat_r(struct _reent *r,int fd,struct stat *out){if(!get_fd(fd)){r->_errno=EBADF;return -1;}memset(out,0,sizeof(*out));out->st_mode=S_IFIFO|0600;out->st_blksize=4096;return 0;}
int _isatty_r(struct _reent *r,int fd){r->_errno=get_fd(fd)?ENOTTY:EBADF;return 0;}
_off_t _lseek_r(struct _reent *r,int fd,_off_t offset,int whence){(void)offset;(void)whence;r->_errno=get_fd(fd)?ESPIPE:EBADF;return -1;}
void *_sbrk_r(struct _reent *r,ptrdiff_t size){(void)size;r->_errno=ENOSYS;return (void *)-1;}
int _gettimeofday_r(struct _reent *r,struct timeval *tv,void *zone){(void)zone;if(!tv){r->_errno=EINVAL;return -1;}struct cb_io_utc utc;cb_status_t s=cb_runtime_utc(cb_libc_runtime(),&utc);if(s){r->_errno=error(s);return -1;}tv->tv_sec=utc.seconds;tv->tv_usec=utc.nanoseconds/1000;return 0;}
int gettimeofday(struct timeval *tv,void *zone){return _gettimeofday_r(_REENT,tv,zone);}
int _getpid_r(struct _reent *r){r->_errno=ENOSYS;return -1;}
int _kill_r(struct _reent *r,int pid,int signal){(void)pid;(void)signal;r->_errno=ENOSYS;return -1;}
int _unlink_r(struct _reent *r,const char *path){(void)path;r->_errno=ENOTSUP;return -1;}
int _link_r(struct _reent *r,const char *a,const char *b){(void)a;(void)b;r->_errno=ENOTSUP;return -1;}
int _stat_r(struct _reent *r,const char *path,struct stat *out){(void)path;(void)out;r->_errno=ENOTSUP;return -1;}
__attribute__((noreturn)) void _exit(int status){cb_exit((unsigned)status);}
__attribute__((noreturn)) void abort(void){cb_exit(134);}
/* The host-header audit names this assertion entry; target assert.h uses
 * Newlib's __assert_func. Both terminate through the owned process boundary. */
__attribute__((noreturn)) void __assert_fail(const char *expression,const char *file,unsigned line,const char *function){__assert_func(file,(int)line,function,expression);}
