# splash

`splash.kmod` owns temporary pre-flight framebuffer validation and rendering.
It embeds the packed Cloudberry logo in its own read-only image. The kernel
only passes the Limine framebuffer response to `splash.show(response)` through
the hub; main does not inspect framebuffer fields or contain drawing loops.

The module requires a valid, mapped RGB framebuffer with 32-bit pixels, aligned
storage/pitch, three non-overlapping eight-bit color channels, and representable
row/address arithmetic. It validates the packed logo dimensions and byte extent
before writing pixels. Unsupported/missing input returns failure. It clears the
visible area, preserves row padding, and centers the logo at native size or a
smaller size fitting approximately 80% of the screen. Tiny framebuffers still
receive a visible pixel. No allocator, display handler or mode setting is used.

Limine protocol prefixes/request objects live in boot/limine.h and boot/limine.c.
The kernel requests the framebuffer and loads the module; splash performs the
resource-specific validation and interpretation. The asset assembly belongs to
splash and is not linked into kernel.elf. Firmware framebuffer use is temporary;
a native display driver remains the intended replacement.

```sh
make -C src test-splash
make -C src test-events-boot
```

The host renderer test checks centering and exact RGB output, row-padding/guard
preservation, tiny-framebuffer behavior and invalid format/size rejection. The
normal disposable modern-bus boot calls the actual splash module through the hub
before hardware initialization and the event loop.
