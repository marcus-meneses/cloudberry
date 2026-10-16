# USB hub class

`usb-hub.kmod` owns external hub class policy above [USB core](usb.md).
It consumes named/versioned hub surfaces for memory, clock, threads and USB;
it neither programs PCI registers nor duplicates xHCI DMA or interrupt routing.
Kmodules remain trusted kernel components, distinct from application handles.

`usb_hub.init(hub,heap)` publishes `usb.hubs`. `start()` launches a preemptible
BSP manager that subscribes to `usb.watch` before inspecting the live device list.
Normal boot starts this manager before USB enumeration. Class-9 interfaces with
a supported interrupt-IN endpoint get a separate BSP worker and interface owner.
The manager sleeps between USB inventory notifications.

Each worker validates the USB2/USB3 hub descriptor, configures the host's hub/TT
context through USB core, sets SuperSpeed hub depth when applicable, powers ports
and waits the advertised power-good interval. Port state is allocated from the
advertised count, without a separate fixed hub or port-count ceiling. USB2's
descriptor field allows up to 255 ports; interrupt bitmaps need at most 32 bytes.
An explicit VID/PID/USB-version quirk accepts QEMU 6.2's incomplete obsolete
power-control-mask tail; unrelated malformed descriptors still fail validation.

Connected ports debounce for 100 ms, reset with a bounded one-second deadline,
wait 10 ms for recovery and submit their child to `usb.downstream`. SuperSpeed
Inactive/Compliance ports use warm reset. Route strings and nearest high-speed
transaction-translator ancestry are transport concerns, supplied by xHCI.

After the initial scan, workers block on the hub's interrupt endpoint; they do
not periodically poll ports. Change bitmaps trigger class GET_STATUS and feature
acknowledgements. Connection edges are acknowledged before slow enumeration so
a later unplug/replug cannot disappear into the earlier acknowledgement.
Overcurrent requests port power removal and fails the worker safely; automatic
power-fault recovery is not implemented. An invalid child descriptor is recorded
on that child rather than taking its upstream hub offline.

USB core serializes root and downstream topology changes per controller using
blocked waiters. xHCI also serializes command mutation between class/topology
workers. A disconnect invalidates the subtree first, waking pending I/O as
disconnected, then waits for active callers and disables slots deepest first.
DMA remains retained until verified controller quiescence. Fresh attachment
IDs/generations prevent old clients from binding to a replacement device.

`usb_hub.stop()` requests asynchronous shutdown, cancels pending hub interrupt
reads and wakes workers. Workers remove children and release their interfaces;
the manager reaps their stacks and unsubscribes. Observe `info(out).manager`
until zero, then call `stop()` again to reap the manager. Stop the class manager
before stopping USB core. `info` reports retained connector records, active
workers and failed hubs; table records expose state, source-line error,
port/interface/endpoint metadata and processed change count. `manager` and
`worker` exports are internal thread entry points.

The table has a static 32-byte header and dynamic 56-byte public records.
Connector records are reused across reattachment; each active worker has a
32 KiB guarded stack and a dynamic port array (16 bytes per advertised port).
Raw table traversal requires CPU-lock synchronization. Image unloading,
alternate-setting selection for high-speed multi-TT negotiation, suspend/resume,
and automatic failed-hub restart remain deferred.

## Restart and descriptor consistency

An explicit class-only stop/start reclaims already known root hubs even when
USB core and their attachment IDs stay unchanged. The manager waits for exited
workers to become reapable before reporting its stop complete; callers then
repeat `stop()` to reap the manager, or let `start()` reap it. Rebinding can briefly
wait for a returning worker to leave its stack. This retry uses a one-millisecond
thread deadline only while reaping is pending, not a periodic discovery poll.
Failed attachments are not automatically retried. This is cooperative class
restart, not arbitrary image unloading or general fault recovery.

The worker validates the first descriptor prefix and the complete descriptor,
requiring every prefix byte to agree before allocation or host/port configuration.
Changed lengths/types/port counts/characteristics/power timing and short reads
are rejected. A SuperSpeed descriptor already complete in its first 12-byte
read needs no second read. The narrow QEMU legacy tail quirk remains supported.

Focused validation:

```sh
make -C src test-usb test-usb-hub test-xhci
make -C src test-usb-hub-boot
make -C src test-usb-hub-boot QEMU_CPUS=4 QEMU_IOMMU=intel QEMU_CPU=max,vendor=GenuineIntel
make -C src test-usb-hub-boot QEMU_CPUS=4 QEMU_IOMMU=amd QEMU_CPU=max,vendor=AuthenticAMD
```

Host tests use production policy/transport code under UBSan, including malformed
descriptors, inconsistent second reads and short responses, late connection edges, warm reset, overcurrent, route saturation,
multi-TT inheritance and parent-first removal refusal. Isolated boot tests use
disposable media and nested QEMU hubs, hotplug a child, remove an upstream hub
while a grandchild interrupt read is blocked, verify stale references, reattach
the topology and stop it. Two additional class-only stop/start cycles leave USB
core and the root attachment active, fail pending grandchild reads, create fresh
descendant identities and verify all three manager/worker stacks are reclaimed
and restored without growth. QEMU 6.2's hub is full-speed: these tests do not establish
real high-speed TT or SuperSpeed behavior. Native KVM and physical hub acceptance
remain pending. Candidate images contain no test probes or injected topology.

References: [USB specifications](https://www.usb.org/document-library/usb-20-specification),
[xHCI requirements](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf).
