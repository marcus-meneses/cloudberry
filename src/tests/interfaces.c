#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../interfaces/keyboard.h"
#include "../interfaces/startup.h"
#include "../interfaces/installations.h"
#include "../interfaces/admission.h"
#include "../interfaces/display.h"
#include "../interfaces/graphics.h"
#include "../interfaces/text.h"
#include "../interfaces/stream.h"
#include "../interfaces/filesystem.h"
#include "../sdk/application.h"
#ifdef __cplusplus
#define ABI_ASSERT static_assert
#else
#define ABI_ASSERT _Static_assert
#endif
ABI_ASSERT(sizeof(struct cb_application_launch_streams)==64,"stdio launch");
ABI_ASSERT(sizeof(struct cb_stream_read)==16,"stream read");
ABI_ASSERT(sizeof(struct cb_graphics_draw)==48,"graphics draw");
ABI_ASSERT(sizeof(struct cb_graphics_state)==56,"graphics state");
ABI_ASSERT(sizeof(struct cb_graphics_surface_info)==48,"graphics damage");
ABI_ASSERT(sizeof(struct cb_graphics_blit)==48,"graphics blit");
ABI_ASSERT(sizeof(struct cb_filesystem_request)==16,"filesystem request");
ABI_ASSERT(sizeof(struct cb_install_catalog)==24,"installation catalog");
ABI_ASSERT(sizeof(struct cb_install_record)==64,"installation entry");
ABI_ASSERT(sizeof(struct cb_application_launch)==40,"application launch");
ABI_ASSERT(sizeof(struct cb_application_status)==32,"application status");
ABI_ASSERT(sizeof(struct cb_startup)==24,"startup prefix");
ABI_ASSERT(sizeof(struct cb_startup_file)==16,"startup file prefix");
ABI_ASSERT(sizeof(struct cb_version)==8,"version prefix");
ABI_ASSERT(sizeof(struct cb_call_header)==80,"call frame");
ABI_ASSERT(sizeof(struct cb_delivery_header)==96,"provider delivery");
ABI_ASSERT(offsetof(struct cb_call_header,payload_bytes)==72,"call field offsets");
ABI_ASSERT(sizeof(struct cb_reply_header)==56,"reply frame");
ABI_ASSERT(sizeof(struct cb_error_header)==48,"error frame");
ABI_ASSERT(sizeof(struct cb_trace_frame)==128,"trace frame");
ABI_ASSERT(sizeof(struct cb_description)==104,"description header");
ABI_ASSERT(sizeof(struct cb_operation_description)==40,"operation header");
ABI_ASSERT(sizeof(struct cb_publication)==48,"publication header");
ABI_ASSERT(sizeof(struct cb_keyboard_event)==64,"keyboard event");
ABI_ASSERT(offsetof(struct cb_keyboard_event,codepoint)==52,"keyboard field offsets");
ABI_ASSERT(sizeof(struct cb_keyboard_read)==32,"keyboard read");
ABI_ASSERT(sizeof(struct cb_keyboard_batch)==32,"keyboard batch");
ABI_ASSERT(sizeof(struct cb_keyboard_state)==32,"keyboard state");
ABI_ASSERT(sizeof(struct cb_keyboard_pressed)==16,"pressed key");
int main(void){
    const struct cb_interface_id keyboard=CB_KEYBOARD_INTERFACE_ID,base=CB_HANDLE_INTERFACE_ID,vow=CB_VOW_INTERFACE_ID;
    assert(memcmp(&keyboard,&base,sizeof(base)) && memcmp(&keyboard,&vow,sizeof(vow)) && !strcmp(CB_KEYBOARD_NAME,"/@keyboard"));
    assert(cb_frame_fits(80,64,0,144) && !cb_frame_fits(80,64,1,144));
    assert(!cb_frame_fits(UINT64_MAX,1,0,UINT64_MAX) && !cb_frame_fits(80,UINT64_MAX,1,144));
    struct cb_keyboard_event event;memset(&event,0,sizeof(event));
    event.version.bytes=sizeof(event);event.version.major=1;event.sequence=1;event.device_id=1;
    event.kind=CB_KEY_DOWN;event.usage_page=7;event.usage=0x52;
    assert(cb_keyboard_event_valid(&event));event.kind=CB_KEY_UP;assert(cb_keyboard_event_valid(&event));
    event.kind=CB_KEY_TEXT;event.codepoint=0;assert(cb_keyboard_event_valid(&event));
    event.codepoint=0x1f600;assert(cb_keyboard_event_valid(&event));
    event.codepoint=0xd800;assert(!cb_keyboard_event_valid(&event));
    event.codepoint=0x110000;assert(!cb_keyboard_event_valid(&event));
    event.kind=CB_KEY_RESET;event.codepoint=0;event.device_id=0;assert(cb_keyboard_event_valid(&event));
    event.kind=CB_KEY_DOWN;assert(!cb_keyboard_event_valid(&event));event.device_id=1;
    event.version.major=2;assert(!cb_keyboard_event_valid(&event));event.version.major=1;
    event.version.bytes=63;assert(!cb_keyboard_event_valid(&event));
    puts("PASS handle/keyboard contracts: C/C++ layouts, interface identities, framing overflow, structured keys and Unicode validation");
}
