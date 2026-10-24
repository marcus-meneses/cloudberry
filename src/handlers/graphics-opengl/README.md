# OpenGL graphics provider

`graphics-opengl` ships in the normal catalog. Bootstrap chooses it together
with `display-opengl` for a qualified virtio-vga/VirGL device; other devices use
the VGA pair. Applications keep `/@graphics` and never choose a backend.

The public decoder in `graphics-common/provider.inc` is shared with graphics-vga.
It preserves owned surfaces, hierarchy, clipping, transformations, primitives,
TrueType text, damage generations, authorization and mouse overlays. Shared
raster/font/scene helpers remain source libraries under graphics-vga; this provider
does not call a running VGA provider. Overlay readiness precedes public graphics
readiness, so mouse/text startup cannot race its private dependency.

## GPU presentation

`present.inc` caches GPU copies of bitmap surfaces, retained text rows and cursor
images through private `/@display.render`. First use uploads the full backing;
subsequent content changes upload the accumulated dirty rectangle in bounded,
packed row groups. Damage for uploads is tracked separately from the public
surface-damage observations, including edits while hidden. Placement, scrolling
and cursor movement reuse unchanged textures. Containers supply coordinates and clipping without pixel backing.
The base is opaque and other layers use source-over. Complex vector primitives
and FreeType glyph coverage still rasterize on the CPU, then compose on the GPU.
There is no application OpenGL API or claim of GPU vector rasterization.

`virgl.c/.h` encodes bounded commands. `virgl-alpha.inc` provides TGSI shaders,
sampler views and explicit blending: older VirGL hosts decode the BLIT alpha
flag without performing the required blend. The display composes into a separate
GPU target and presents only after END. A partial frame first copies the previous
complete front buffer into that target, restores the damaged rectangle from the
opaque base and replays only intersecting layers. This preserves alpha ordering
and untouched pixels; it still performs a full GPU copy and full scanout present.
Large/full damage uses the original complete-frame path. Partial failures preserve UNKNOWN outcome
and are not replayed. Surface/overlay deletion releases cached textures, and the
display also sweeps them if graphics exits. Resource IDs never become app pointers.

## Qualification and validation

The production GPU module verifies firmware framebuffer identity, VGA mode and
actual offered VirGL/version/DMA features. It selects the exact device and
verifies cleanup/restoration before authorizing failed-startup fallback. See
[the device contract](../../kmodules/shared/gpu-virtio.md) for kernel qualification and acceptance boundaries.

`test-graphics-opengl` drives this decoder, hierarchy, FreeType and overlays
against real host GL. It covers clipped Unicode text, cached moves/scroll without
uploads, transparent cursor restoration, ownership and no-op frames.
`test-console-host` retains the software-backend contract checks.

Native four-CPU paired execution, visible scanout/restoration and all four
startup-failure cases pass with the production hooks and matching build hashes.
See [accepted reports](../../tests/README.md#production-rendering-acceptance--2026-10-24).
The regression command is:

```sh
make -C src test-gpu-pair-screen test-gpu-pair-failures QEMU_CPUS=4 QEMU_ACCEL=kvm
```

Ordinary images now contain both provider pairs. Native GPU support currently
means qualified virtio-vga/VirGL, with VGA fallback on unsupported hardware.

## Partial redraw validation — 2026-10-24

Host real VirGL/software-GL checks compare every resulting pixel for repeated
typing, edits and cached scroll, plus multi-chunk patches, hidden edits, hierarchy,
alpha/cursor restoration and no-op frames. For 32 edits to a 1024×768 cached
bitmap (96×64 diagnostic scanout), CPU-backing upload bytes fell from 100,663,296
to 10,352 for typing and 49,152 for rectangle edits. GPU transport calls fell
from 8,384 to 224 for both. Cached scrolling retains zero uploads. These are
work reductions, not native frame-rate claims. The raw comparison is
`bin/tests/graphics-redraw-comparison.json` at the workspace root.

The candidate build, host renderer/transport/ABI/software-console checks and
four-CPU TCG scrolling at the requested 1024×768 startup mode pass. Four-CPU
native KVM paired-screen validation of this redraw revision also passes, including
six matching scanout frames, keyboard/mouse, client cleanup, provider restart and
VGA restoration. Source, module and provider hashes match the accepted build.
Report: `bin/tests/gpu-pair/9d8a3f857b89400bbba136f7ef118895/result.json` at the
workspace root. The command above remains the regression check; this run is a
correctness check rather than a native performance benchmark.
