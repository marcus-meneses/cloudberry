# OpenGL display provider

`display-opengl` ships in the normal catalog alongside `display-vga`. Bootstrap
selects the pair only when `gpu.opengl.probe` qualifies the firmware-owned
VGA-compatible virtio GPU and both provider installations exist. Other devices
use the VGA pair. Apps keep the same `/@display` interface (info/fill/blit/copy).

The provider obtains the exact kernel-selected device through private
`CB_GPU_SELECTED` over `/@io`, opens it and queries the enabled scanout geometry.
It publishes `/@display.render` before `/@display`, so public readiness includes
the private dependency used by graphics. No DMA address, kernel pointer or raw
GPU resource ID is exposed to ordinary applications.

## Rendering and ownership

`render.c/.h` and `render-surfaces.inc` adapt copied GPU requests to VirGL.
The private interface in `interfaces/render-backend.h` provides info, ordered
fill/copy batches and create/release/write/patch/compose. Only the kernel-bound graphics
PID is authorized. It uses opaque process-owned tokens with monotonic IDs across
reopen, complete bounded uploads and explicit commit before sampling. The display
watches its graphics client and releases that client's cached resources on exit.

Composition draws into a separate target. BEGIN supplies the opaque base;
continued chunks append clipped layers; END swaps and presents. An unfinished
frame leaves scanout intact, and public mutations are busy until it ends.
Transparent layers use the explicit source-over shaders, including on hosts
whose BLIT alpha flag alone does not blend. Initial uploads initialize the full
backing. PATCH then accepts a bounded rectangle as contiguous packed row groups,
only for initialized textures; it blocks sampling until the whole patch commits.
Malformed chunks fail before mutation. CPU writes honor backing pitch and GPU
uploads transfer only that rectangle, retaining the same fenced ownership path.
Failed transport poisons the session; partial writes are never replayed blindly.
A BEGIN|DAMAGE frame preserves the previous complete frame in the back target
and rebuilds only intersecting layers over the damaged opaque base. END remains
the sole frame swap/presentation boundary.

GPU transport failures poison the session, retain UNKNOWN outcomes and never
replay a partly executed batch. Close/reset must quiesce the device before
backing is freed. Bootstrap reaps failed providers before calling the production
release hook, which verifies cleared ownership/resources and the saved firmware
VGA mode before permitting fallback. Uncertain cleanup fails startup.

## Validation

`test-virgl-render` and `test-graphics-opengl` check the production renderer,
FreeType, surfaces/composition and failures against real host VirGL/software GL.
Native four-CPU paired scanout/restoration and all four startup-failure cases
pass with production qualification/release evidence and matching build hashes.
Reports are recorded in [the test documentation](../../tests/README.md#production-rendering-acceptance--2026-10-24).

Production feature probing, release hooks and catalog activation are now
implemented. Host policy/transport/ABI checks and 2D-only production fallback
pass, as does native acceptance. The regression command requires
production-probe/release evidence:

```sh
make -C src test-gpu-pair-screen test-gpu-pair-failures QEMU_CPUS=4 QEMU_ACCEL=kvm
```

The tests use copied media and EGL-headless scanout readback. Current qualification
is specific to firmware-owned virtio-vga/VirGL; other GPU drivers require their
own capability and restoration implementation. See [the device contract](../../kmodules/shared/gpu-virtio.md).
