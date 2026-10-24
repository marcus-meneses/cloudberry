# Virtio GPU device module

`gpu-virtio.kmod` is built into the system volume's
`/system/kmodules/drivers`, loaded by `drivers.modules`, and uses the native
kmodule hub contract. It is not coalesced into the kernel or a user application.
The init hook copies discovered PCI identity into owned records, reads modern
virtio capabilities without claiming the device, and publishes no asynchronous
work. After the module is LIVE, `gpu_virtio.discover` publishes
`gpu.virtio.devices`. Normal provider startup then uses the production
qualification contract below; explicit GPU open is performed by the selected
privileged display provider.

## Production boot qualification

The module now publishes `gpu.opengl.probe` and `gpu.opengl.release`. Both
serialize against device open using a nonblocking management token. Qualification
is restricted to modern virtio VGA (1af4:1050, VGA subclass), a single unambiguous
firmware framebuffer memory region at BAR0, a single initialized boot display,
an unclaimed PCI device, cold virtio status and a valid enabled 32-bit linear VGA mode matching the boot display dimensions and pitch. `display.boot_info` copies
only the boot count/geometry, with no framebuffer address. Native non-VGA virtio devices
and physical GPU vendors without a matching implementation use firmware fallback.

Probe reads modern capabilities and maps only the small supervisor MMIO windows
needed for validation through the hub. These views do not size or claim the BAR;
PCI claim later performs its ordinary full sizing/mapping. It queries both actual
device feature banks, saving/restoring the query selector inside one bounded
critical section. It never changes driver features/status, resets, enables DMA,
allocates GPU resources, routes IRQs or changes the display mode. VERSION_1 and
VirGL are required; active Intel/AMD IOMMU operation additionally requires
ACCESS_PLATFORM. Scanout/capset counts must fit the existing transport contract.
A PCI ID alone does not establish acceleration.

The accepted controller and its VGA-register snapshot are retained privately.
`CB_GPU_SELECTED` returns that exact kernel-selected hardware index to the bound
display provider, avoiding accidental takeover of the first enumerated GPU in a
multi-device system. It appends a private operation without changing existing
operation numbers, structure layouts or public graphics/display contracts.

Release does not steal or close an active session. Production process exit/reset
must first clear bridge/PCI ownership, owner pins, resources, contexts,
attachments, DMA and IRQ route, leaving the controller untouched or CLOSED.
Release verifies PCI ownership is still clear, virtio status is zero and all ten
saved VGA mode registers match before authorizing fallback. It is idempotent;
any retained ownership or changed mode denies fallback. It does not program a
replacement VGA mode or treat disabled bus mastering as proof of quiescence.

Diagnostic `gpu_virtio_info.stage` values 100–104 identify identity checks,
firmware matching, MMIO validation, feature inspection and completed qualification.
Open retains its existing configuration stages. The physical-memory/mode
conventions follow QEMU's [virtio-vga implementation](https://github.com/qemu/qemu/blob/v6.2.0/hw/display/virtio-vga.c)
and standard VGA MMIO contract. No PS/2 or IDE code is introduced.

The normal image includes both provider pairs. `make -C src run QEMU_GPU=virgl`
requests the GL-capable virtual device and host GL; bootstrap selects the pair
when qualified. Default VGA and 2D-only virtio devices remain software-rendered.
Host probe/release, transport, installation and ABI regressions pass. Native
four-CPU KVM paired scanout/restoration and all four startup-failure cases also
pass with verified production hooks and matching build hashes; see
[accepted reports](../../tests/README.md#production-rendering-acceptance--2026-10-24).
Earlier acceptance history below applies to its recorded source revisions.

```text
hwenum.hardware + pci firmware/ownership APIs
└── gpu-virtio (disk-loaded device module)
    ├── modern PCI capabilities / split control queue
    ├── owned PCI DMA + IOMMU + MSI-X → irq deferred callback → thread wake
    ├── 2D resources / transfer / scanout / flush
    ├── VirGL capsets / contexts / tracked textures / fenced opaque submissions
    └── future privileged display-opengl + graphics-opengl
        └── unchanged /@display and /@graphics contracts → apps
```

The virtio capability/queue helpers live in `virtio/pci.c/.h`; GPU wire layouts
are in `virtio/gpu-wire.h`. They follow the
[Virtio 1.2 device specification](https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html).
No host Linux DRM or Mesa guest runtime is assumed.

## Surfaces and ownership

[Native interface](gpu-virtio.h), all trusted kernel pointers and BSP foreground:

| Surface | Operation |
| --- | --- |
| gpu_virtio.init | Disk-loader ABI2 init; bind dependencies and copy PCI metadata |
| gpu_virtio.discover | Publish owned inventory after LIVE; no device reset/DMA |
| gpu_virtio.info | Copy one device's state and counters by hardware index |
| gpu_virtio.open | Explicit claim, reset, negotiate, configure queue/IRQ; return instance cookie |
| gpu_virtio.close | Reset/quiesce, release DMA/IRQ/PCI ownership |
| gpu_virtio.display | Copy the device's display-mode response |
| gpu_virtio.create / destroy | Create/drop a tracked 2D CPU-backed BGRX resource |
| gpu_virtio.present | Transfer 2D backing, set scanout and flush a rectangle; textures are not implicitly uploaded |
| gpu_virtio.capset | Query/copy a VirGL capability set |
| gpu_virtio.context | Create/destroy a tracked VirGL context |
| gpu_virtio.texture | Create level-0 2D BGRA/BGRX texture, CPU-backed or host-only |
| gpu_virtio.attach | Track context/resource attachment, with idempotent detach |
| gpu_virtio.upload | Upload a full attached CPU-backed texture or a checked rectangle using the 3D transfer command |
| gpu_virtio.readback | Fenced GPU-resource-to-CPU-backing copy; VirGL only, optional tracked context |
| gpu_virtio.submit | Fenced opaque VirGL command stream for an owned context |
| gpu_virtio.interrupt | Short deferred completion/wakeup callback; no allocation |

Each open instance gets a monotonically assigned cookie and distinct PCI DMA
owner. Resource/context IDs do not wrap/reuse. Hardware ownership takes an explicit
hub owner pin, retained through uncertain cleanup and released only after all
DMA/IRQ/PCI state has been reclaimed; it does not rely on callers first publishing
the inventory table. The metadata arrays are dynamically
allocated from the boot-discovered device count; each descriptor has stable
boot-lifetime storage. The table publication pins the module owner. Zero devices
is valid and does not prevent framebuffer fallback.

An instance has one control request outstanding, using a split queue up to eight
entries and two chained descriptors. Request and response DMA bounce buffers are
16 KiB each, plus one queue page. Resource backing is separately allocated through
PCI/IOMMU ownership; it is not bounded by that message size. Calls block for an IRQ
completion with an absolute 500 ms deadline. Queue indexes, response lengths,
fences and reply types are validated. IOMMU-backed operation requires the
ACCESS_PLATFORM feature; otherwise takeover fails before enabling DMA. Timeout/protocol failures stop further
submissions and retain uncertain DMA until an acknowledged reset allows cleanup.

The DMA transfer completion fence, not merely disabling bus mastering, precedes
unmapping. Resource destruction detaches tracked contexts first; context teardown
detaches its resources. Failed map/command/cleanup records remain owned. Full
reset precedes freeing all remaining backing at close. No unowned buffer or IRQ
is reclaimed. PCI claim 1.1 maps only the control and MSI-X regions, preserving
the firmware framebuffer's existing cache mapping.

## Current boundary

GPU takeover is **explicit**. Resetting firmware queues may invalidate its scanout;
`close` does not claim to restore that scanout. Normal boot therefore performs
read-only discovery and keeps `graphics-vga`/`display-vga`. This module deliberately
supplies no `gpu.opengl.release` or complete renderer-readiness claim yet.

This is a functioning device transport/resource foundation, not an OpenGL API
implementation. The remaining work includes OpenGL/Gallium state/shader translation,
GPU composition/glyph integration, operational
OpenGL handlers, and proven firmware scanout restoration for automatic fallback.
Only initial level-0 2D BGRA/BGRX VirGL textures are exposed; broader formats, mips,
arrays and other resource types are not claimed. Fenced readback into attached
BGRA/BGRX backing is now available; it does not capture the physical scanout. Cursor-queue use, GPU
hotplug/reset recovery, parallel submissions and physical GPU drivers are separate.

## Validation

```
make -C src test-pci test-virtio-gpu-host
make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_ACCEL=tcg
make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_IOMMU=intel
# On a host with /dev/kvm and host EGL/GPU access:
make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_ACCEL=kvm QEMU_GPU=virgl
# Explicit interactive frontend when EGL-headless is unavailable:
make -C src test-virtio-gpu QEMU_CPUS=4 QEMU_ACCEL=kvm QEMU_GPU=virgl GPU_TEST_DISPLAY=gtk
```

The GUI/scanout test copies the boot/system image and substitutes a test-only
module with a synchronous discover hook. It does not edit the candidate image,
attach developer disks, add a production test app or run takeover at normal boot.
The 2D backend checks QMP screen dimensions and four representative pixels.
VirGL additionally erases the guest source buffer and performs a fenced GPU
readback, comparing all 64,000 resource pixels; a context-attached texture gets
another 1,024-pixel readback check. Counters require actual IRQ delivery. Capsets,
contexts, texture attachment/upload, opaque submission and cleanup remain required.

QEMU 6.2 routes VirGL scanout to a GL texture while QMP screendump reads a separate
RAM surface, which can be entirely black. Therefore the VirGL runner records
`pixel_evidence=gpu-resource-readback` and keeps `screen_capture_verified=false`
unless the screen capture itself matches. Only an empty RAM surface or an explicit
QMP no-surface response can be classified as unavailable; other pixel/format
mismatches still fail. A readback result proves GPU resource contents, not visible
frontend output or hardware acceleration performance. See
[QEMU VirGL scanout](https://raw.githubusercontent.com/qemu/qemu/v6.2.0/hw/display/virtio-gpu-virgl.c)
and [QMP capture](https://raw.githubusercontent.com/qemu/qemu/v6.2.0/ui/console.c). Host tests inject bad
fences/completions, DMA-map failure, timeout, invalid geometry, stale cookies,
attachment teardown and missing VirGL.

Current acceptance (four CPUs, TCG):

- 2D scanout, no IOMMU: `bin/tests/gpu-virtio-boot/01bfa3b68d234b52996eae173687bee8/result.json`.
- 2D scanout, Intel IOMMU: `bin/tests/gpu-virtio-boot/0f91e8bc394243ebb27362342f89aeb9/result.json`.
- final driver, AMD IOMMU: `bin/tests/gpu-virtio-boot/7881fa98a0ca492d842e4286b7686503/result.json`.

The final AMD run uses the current driver binary, including explicit owner pins,
ACCESS_PLATFORM gating and tracked VirGL texture APIs. Earlier no-IOMMU/Intel
runs establish the same 2D transport on the earlier driver revision; their binary
and source digests remain in each report. Final host tests cover all new lifetime
and texture behavior. Normal startup/keyboard acceptance also passes with virtio
(`console/87114d0651504cb7b43f5563f02f10c7`) and with the final VGA fallback
(`console/351d3a384d6d426b8ec687a95ec7eec9`). The agent environment lacks /dev/kvm and a GPU render node. The later host run
below now qualifies native KVM/VirGL transport; physical GPU drivers, visible
scanout capture, acceleration speedup and automatic recovery remain unqualified.


First host VirGL attempt: `gpu-virtio-boot/3f18767285c244969930b9499a7c9a3c`
initialized host GL and completed initial scanout commands, then failed the old
QMP pixel assertion with a 320x200 all-black RAM surface. It did not reach the
context/texture tests and remains a failed report. The corrected readback-based
VirGL acceptance passed in the subsequent host run below; the failed report
remains unchanged.

After the capture correction, host evidence-rule/driver tests and the four-CPU
TCG 2D regression pass (`gpu-virtio-boot/acbe39b96c59440f8f8e9e9b8a12c39f`).
This regression keeps QMP pixel matching mandatory; the later native host run
below separately qualifies VirGL readback.


## Native KVM/VirGL transport acceptance

The four-CPU host rerun passed:
[report](../../../bin/tests/gpu-virtio-boot/b2caeb93e73c47adb9896554b750af3e/result.json).
Its driver binary and source digests matched the candidate and isolated
build at that milestone. The guest completed 23 fenced commands and recorded 23 IRQ notifications;
64,000 resource pixels and 1,024 context-attached texture pixels matched after
the guest source backing was erased and repopulated by GPU readback. Capsets,
context creation, texture attachment/upload, opaque submission, resource/context
teardown and device close all completed. Host GL initialized a 4.6 core profile;
this is not a claim that Cloudberry implements an OpenGL 4.6 application API.

The QMP RAM capture remained black and is explicitly unverified visible output.
The result qualifies resource/transport execution through the host VirGL stack,
not physical scanout pixels, GPU-versus-software host rendering, performance,
shader/draw translation or the still-inactive OpenGL UI handlers.


## Privileged GPU bridge

The disk-loaded driver now provides the GPU I/O gateway used by `/@io`'s `gpu`
operation via generic `gpu.invoke` / `gpu.exit` hub surfaces. Only one selected
GPU bridge implementation owns those names at a time; another device backend can
replace it without changing I/O or public application handles. Its contract is [interfaces/gpu.h](../../interfaces/gpu.h); the gateway
implementation lives in `gpu-virtio-io.inc`, alongside the transport's existing
resource/context tracking. It introduces no user mappings of DMA or kernel memory
and no second resource registry.

The kernel supplies the catalog-bound display-provider PID. Manifest permission
alone grants no GPU authority; ordinary apps and the graphics provider cannot
claim the device through this gateway. The staged `display-opengl` manifest asks
for `gpu`; the working VGA provider does not. The display provider owns hardware;
future graphics-to-display command sharing belongs to a private backend contract.
Public `/@graphics` and `/@display` operations are unchanged.

INFO/OPEN return pointer-free device snapshots, with open cookies hidden from a
different owner. CREATE/TEXTURE return only IDs, geometry, format and backing size.
WRITE/READ copy bounded chunks to/from tracked CPU backing; UPLOAD/READBACK perform
explicit fenced GPU transfers. WRITE/UPLOAD flags=1 use an origin byte offset
and bounded width/height. WRITE copies tightly packed input rows to the resource's
full stride; UPLOAD emits a strided rectangle transfer with the matching backing
offset. Bounds, alignment, payload size, context attachment and owner are checked
before mutation. Contexts, attachment, capsets, trusted VirGL
submission and full-resource presentation use the same driver tracking/fences.
The privileged display provider must not expose raw submission to applications.
Unused request fields, malformed sizes, stale cookies and out-of-bounds copies
fail before touching resource memory. Reply capacity is reserved before mutation;
uncertain transport failures carry `CB_OUTCOME_UNKNOWN` through the handle broker.

Calls serialize without spinning and retain the existing BSP/interrupt-enabled
foreground restriction. Provider exit closes every device it owns. Failed
quiescence retains the process binding, DMA and module ownership pin, so a new
provider cannot steal uncertain hardware; cleanup can be retried. Other process
exits remain independent of GPU affinity. This does not establish firmware
scanout restoration, automatic accelerated selection, a general OpenGL API or
working OpenGL UI handlers.

Validation uses the real gateway/driver with the deterministic PCI/DMA/IRQ model
(`tests/gpu-bridge.c`) and the disposable four-CPU GPU boot fixture. The latter
routes through the mediated I/O surface with a test-only kernel-bound principal;
it is not evidence of a completed userland OpenGL provider.


Bridge acceptance: four-CPU TCG
[report](../../../bin/tests/gpu-virtio-boot/8acd034a4a864962a3a2b9822331e9a2/result.json)
passes with `bridge_verified=true`. The fixture uses the generic hub surfaces
through I/O and leaves no live GPU resources after its test-only principal exits.
Ordinary VGA terminal startup/input also passes:
[report](../../../bin/tests/console/bb9433db5e77438faed098d0a3730003/result.json).
Native KVM/VirGL coverage above predates the bridge; no new native bridge result
is claimed.


## Display discovery and drawing consumers

The private bridge appends `NEXT` and `MODES` without renumbering existing GPU
operations. NEXT enumerates hardware indices without claiming devices; MODES
copies the protocol's 16 scanout records for the caller's open cookie. No kernel
address is returned. The experimental display provider uses these to choose an
enabled scanout and derive dimensions rather than assuming a fixed screen size.

GPU rendering code stays in the handler layer: the experimental display engine
and paired VirGL encoder implement clears, fills, uploaded bitmap blits and
scratch-assisted overlap-safe copies. They are not linked into the production
GPU kmodule. The disposable test module links them solely for guest acceptance
through the existing I/O gateway. Host software-GL tests validate actual pixels;
native guest drawing now passes in the report below. Neither the driver nor the
new display executable advertises complete OpenGL frontend readiness/restoration.


Native bridge/drawing acceptance now passes:
[report](../../../bin/tests/gpu-virtio-boot/fbc98226fec0475a93bfe504688342f3/result.json)
from four-CPU KVM/VirGL, matching recorded sources and candidate/isolated GPU
binaries. Fifty fenced commands/fifty IRQs and 3,072 drawing pixels pass, alongside
65,024 resource/texture readback pixels. Userland provider IPC, visible scanout,
full graphics integration, performance and restoration remain unqualified.


Native composition/alpha gate now passes in
[report](../../../bin/tests/gpu-virtio-boot/982b7a1a52364c9d8f2dd0e77001ac9a/result.json):
four-CPU KVM/VirGL, 74 commands/74 IRQs, 3,072 composition pixels alongside
3,072 drawing and 65,024 resource/texture readback pixels. Source and GPU binary
digests match. This qualifies the shared engine/shader through I/O; it does not
qualify paired provider-process IPC, visible scanout, performance or restoration.
Automatic selection still retains VGA until those remaining gates are closed.


## Isolated GPU/broker profiling

`test-gpu-pair-profile` compiles an observer variant and a profiled broker on
copied media. A dedicated 32-edit window disables per-PRESENT diagnostic
readbacks and performs no host screenshots. The driver hook records successful
fenced command count, elapsed completion time, maximum and blocking calls by
command class; its default production macro compiles away. Copied I/O calls
are timed separately. Broker phases and CPU-lock waits/holds use the existing
isolated broker profiler. Clock source/flags/frequency must stay unchanged over
the window. Raw client frame samples and timestamp overhead samples are retained.
These spans overlap: GPU completion time is included in I/O and broker kernel/
wait time, so their totals must not be added or unrelated medians subtracted.
Native data must guide any batching, queue/index or critical-section changes.
No new command queue behavior, fence relaxation or production telemetry is added.
