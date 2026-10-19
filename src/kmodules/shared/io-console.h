#ifndef CLOUDBERRY_IO_CONSOLE_H
#define CLOUDBERRY_IO_CONSOLE_H
#include "io.h"
#include "../../boot/limine.h"
void io_console_hub(const struct kernel_surfaces *);
int io_console_init(const struct framebuffer_response *);
void io_console_bind(const char *,uint64_t,uint64_t);
int io_installation_data(const unsigned char *,const char *);
int io_filesystem_invoke(struct io_invocation *);
int io_console_invoke(struct io_invocation *);
#endif
