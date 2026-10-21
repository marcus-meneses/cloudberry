#ifndef CLOUDBERRY_HID_REPORTS_H
#define CLOUDBERRY_HID_REPORTS_H
#include "io.h"
int hid_reports_init(const struct kernel_surfaces *,uintptr_t);
void hid_reports_bind(const char *,uint64_t);
int hid_reports_invoke(struct io_invocation *);
cb_status_t hid_reports_exit(uint64_t);
struct kernel_surface_result hid_surface_report_worker(MODULE_ARGS);
struct kernel_surface_result hid_surface_capture_worker(MODULE_ARGS);
struct kernel_surface_result hid_reports_info(uint64_t,uintptr_t);
#endif
