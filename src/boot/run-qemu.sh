#!/usr/bin/env bash
set -euo pipefail

# Snap editor terminals may inject GTK/GIO modules built against Snap's glibc.
# Clear library/module overrides for this process; keep display/session access.
exec env -u LD_LIBRARY_PATH -u LD_PRELOAD \
    -u GTK_PATH -u GTK_EXE_PREFIX -u GTK_DATA_PREFIX \
    -u GTK_MODULES -u GTK3_MODULES \
    -u GIO_MODULE_DIR -u GIO_EXTRA_MODULES \
    -u GDK_PIXBUF_MODULE_FILE -u GDK_PIXBUF_MODULEDIR \
    "$@"
