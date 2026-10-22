#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../kmodules/shared/io.c"
static int rtc_available=1;
static int monotonic_available;
static struct kernel_surface_result now_stub(MODULE_ARGS){MODULE_UNUSED();return module_result(0,monotonic_available);}
static struct kernel_surface_result rtc_stub(MODULE_ARGS){MODULE_UNUSED();*(struct clock_calendar *)a=(struct clock_calendar){.year=2024,.month=2,.day=29,.hour=12,.minute=34,.second=56,.flags=CLOCK_RTC_CENTURY_ASSUMED};return module_result(1,rtc_available);}
int main(void){int64_t epoch;assert(cb_calendar_epoch(1970,1,1,0,0,0,&epoch) && epoch==0);assert(cb_calendar_epoch(1969,12,31,23,59,59,&epoch) && epoch==-1);assert(!cb_calendar_epoch(2100,2,29,0,0,0,&epoch));assert(!cb_calendar_epoch(2024,1,1,0,0,60,&epoch));assert(cb_calendar_epoch(2000,2,29,0,0,0,&epoch) && epoch==951782400);
    struct kernel_surfaces fixture={0};struct kernel_surface method={.call=rtc_stub};strcpy(method.name,"clock.rtc");fixture.count=1;fixture.methods_address=(uintptr_t)&method;struct kernel_hub_table table={0};strcpy(table.name,"config.clock.rtc_utc");fixture.tables_address=(uintptr_t)&table;hub=&fixture;
    uint8_t policy=1;struct config_value config={.major=1,.type=CONFIG_BOOL,.bytes=1,.data=&policy};table.address=(uintptr_t)&config;table.bytes=sizeof(config);
    struct cb_io_utc out;struct io_invocation request={.operation=CB_IO_UTC,.output=&out,.capacity=sizeof(out)};
    io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_UNAVAILABLE && !request.actual);
    fixture.table_count=1;policy=0;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_UNAVAILABLE);
    policy=1;rtc_available=0;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_UNAVAILABLE);
    rtc_available=1;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_OK && out.seconds==1709210096 && !out.nanoseconds && out.flags==7);
    request.bytes=1;request.input=&policy;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_BAD_REQUEST && !request.actual);
    clock=now_stub;request.bytes=0;request.input=NULL;request.operation=CB_IO_NOW_NS;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_UNAVAILABLE && !request.actual);monotonic_available=1;io_surface_invoke((uintptr_t)&request,0,0,0,0,0);assert(request.status==CB_OK && request.actual==8 && !*(uint64_t *)&out);
    puts("PASS runtime time: Gregorian/leap/epoch validation, explicit UTC policy, RTC unavailability and resolution/quality flags");}
