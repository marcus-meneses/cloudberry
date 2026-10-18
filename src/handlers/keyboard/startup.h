#ifndef CLOUDBERRY_KEYBOARD_STARTUP_H
#define CLOUDBERRY_KEYBOARD_STARTUP_H
#include "../../interfaces/handle.h"
/* Read-only startup packet: this prefix followed by the selected layout JSON. */
struct keyboard_startup {struct cb_version version;uint64_t layout_bytes;};
#endif
