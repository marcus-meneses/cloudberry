#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../interfaces/keyboard.h"
#include "../interfaces/mouse.h"
#include "../interfaces/io.h"
#include "../interfaces/system.h"
#include "../interfaces/memory.h"
#include "../interfaces/startup.h"
#include "../interfaces/installations.h"
#include "../interfaces/installer.h"
#include "../interfaces/admission.h"
#include "../interfaces/display.h"
#include "../interfaces/graphics.h"
#include "../interfaces/text.h"
#include "../interfaces/stream.h"
#include "../interfaces/filesystem.h"
#include "../interfaces/identity.h"
#include "../sdk/application.h"
#ifdef __cplusplus
#define ABI_ASSERT static_assert
#else
#define ABI_ASSERT _Static_assert
#endif
ABI_ASSERT(sizeof(struct cb_installer_request)==96 && sizeof(struct cb_bundle_info)==136 && sizeof(struct cb_release_info)==120 && sizeof(struct cb_registry_entry)==56,"installer/registry frames");
ABI_ASSERT(CB_FILESYSTEM_RMDIR==9 && CB_APPLICATION_RESOURCES==9,"appended filesystem/resources operations");
ABI_ASSERT(sizeof(struct cb_identity_home)==40 && sizeof(struct cb_session_status)==64 && CB_APPLICATION_DATA==8,"identity home/session drain/appdata ABI");
ABI_ASSERT(sizeof(struct cb_session)==32 && sizeof(struct cb_io_session)==24 && sizeof(struct cb_io_input_focus)==32,"session/focus ABI");
ABI_ASSERT(sizeof(struct cb_filesystem_stat)==16 && sizeof(struct cb_filesystem_metadata)==32 && sizeof(struct cb_filesystem_owner)==16,"filesystem metadata ABI preserves STAT");
ABI_ASSERT(CB_FILESYSTEM_MKDIR==5 && CB_FILESYSTEM_METADATA==6 && CB_FILESYSTEM_CHOWN==8 && CB_IDENTITY_HOME==12,"appended ownership/home operations");
ABI_ASSERT(sizeof(struct cb_credentials)==32 && sizeof(struct cb_identity_password)==16,"credential/password ABI");
ABI_ASSERT(sizeof(struct cb_identity_info)==48 && sizeof(struct cb_identity_begin)==32,"identity metadata/transaction ABI");
ABI_ASSERT(sizeof(struct cb_identity_stage)==24 && sizeof(struct cb_identity_credential)==88,"identity staging/credential ABI");
ABI_ASSERT(sizeof(struct cb_process_query)==16 && sizeof(struct cb_process_sample)==336,"process samples");
ABI_ASSERT(CB_APPLICATION_PROCESSES==7 && CB_IO_PROCESS_SAMPLE==25,"appended process monitoring operations");
ABI_ASSERT(sizeof(struct cb_abandon_result)==8 && CB_SYSTEM_ABANDON==12,"abandon contract");
ABI_ASSERT(sizeof(struct cb_mouse_event)==120,"mouse event");
ABI_ASSERT(sizeof(struct cb_mouse_read)==32 && sizeof(struct cb_mouse_batch)==32,"mouse read");
ABI_ASSERT(sizeof(struct cb_mouse_device)==56 && sizeof(struct cb_mouse_state)==32,"mouse state");
ABI_ASSERT(sizeof(struct cb_io_hid_capture)==16,"HID report capture size");
ABI_ASSERT(sizeof(struct cb_io_hid_event)==56 && sizeof(struct cb_io_hid_device)==32,"HID transport");
ABI_ASSERT(CB_IO_DISPLAY_COPY==15 && CB_IO_HID_SUBSCRIBE==16 && CB_IO_HID_SET_FEATURE==24,"appended HID IO");
ABI_ASSERT(sizeof(struct cb_text_cursor)==8 && CB_TEXT_CURSOR==5,"appended text cursor");
ABI_ASSERT(sizeof(struct cb_wait_item)==32,"wait item");
ABI_ASSERT(CB_SYSTEM_STANDARD==8 && CB_SYSTEM_WAIT==11,"appended gate operations");
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
ABI_ASSERT(CB_IO_UTC==38 && CB_IO_PROCESS_TERMINATE==37 && sizeof(struct cb_io_utc)==16,"append-only UTC ABI");
ABI_ASSERT(CB_MEMORY_ALLOCATE==1 && CB_MEMORY_FREE==2 && sizeof(struct cb_memory_request)==16 && sizeof(struct cb_memory_result)==16,"implemented memory ABI");
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
ABI_ASSERT(sizeof(struct cb_keyboard_read_focus)==40 && sizeof(struct cb_keyboard_batch_focus)==40 && sizeof(struct cb_keyboard_focus)==32,"keyboard focus extensions");
ABI_ASSERT(CB_KEYBOARD_FOCUS_CLAIM==3 && CB_KEYBOARD_FOCUS_RELEASE==4,"appended keyboard focus operations");
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
