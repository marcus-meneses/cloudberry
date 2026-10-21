#ifndef CLOUDBERRY_INTERFACE_STREAM_H
#define CLOUDBERRY_INTERFACE_STREAM_H
#include "handle.h"
#define CB_STDIN_NAME "/@stdin"
#define CB_STDOUT_NAME "/@stdout"
#define CB_STDERR_NAME "/@stderr"
#define CB_STREAM_NAME "/@streams"
#define CB_STREAM_INTERFACE_ID {{0x43,0x42,0x53,0x54,0x52,0x31,0x46,0x3b,0x91,0xe1,0x40,0x0a,0x1c,0x87,0xc0,0x0a}}
enum {CB_STREAM_PIPE=1,CB_STREAM_READ,CB_STREAM_WRITE};
enum {CB_STREAM_NONBLOCK=1,CB_STREAM_CHUNK=4096,CB_PIPE_CAPACITY=16384};
struct cb_stream_pair {cb_handle_t reader,writer;};
struct cb_stream_write {uint64_t flags;};
struct cb_stream_read {uint64_t bytes,flags;};
/* WRITE: cb_stream_write then raw bytes, reply uint64_t accepted bytes. READ reply raw bytes; zero
 * means EOF. Both may return a prefix. Release with cb_system_release.
 * Pipe endpoint capabilities are process-local, directional and transferable
 * only through launch. Standard names resolve the calling process's binding,
 * never a global pipe. Each named acquisition returns an independent endpoint. */
#endif
