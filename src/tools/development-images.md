# Persistent development media

All initialization and updates run on the host, outside guest startup. Normal
`build` creates a fresh candidate. `init-dev-disk` clones it once and refuses an
existing destination. `run-dev` launches the selected initialized disk as-is.

```sh
make -C src init-dev-disk
make -C src run-dev QEMU_ACCEL=kvm QEMU_CPUS=4
# Stop the VM, build the new system, and produce a separate updated image:
make -C src update-dev-disk
make -C src run-dev DEV_DISK=../bin/cloudberry-dev-next.img QEMU_ACCEL=kvm QEMU_CPUS=4
```

`DEV_DISK` defaults to `../bin/cloudberry-dev.img`. `UPDATED_DEV_DISK` defaults to
`../bin/cloudberry-dev-next.img`. An existing output is never replaced. Keep the
previous image for rollback; select the new path explicitly. Clean/test cleanup
never removes developer persistent disks. This is an offline development updater,
not a guest installer, package manager or in-place application update facility.

The updater validates GPTs, format/identity records and the candidate's build
manifest. It takes nonblocking byte-range image locks to refuse an active writer.
It imports data only from a disposable filesystem copy; required ext4 journal
replay runs with `e2fsck -p -E journal_only` on that copy. Other filesystem errors
are refused. A nonempty identity transaction requires recovery by the old guest;
it is never guessed/replayed by the host updater. Unsupported inode types,
hardlinks and control-character filenames fail explicitly; guest APIs currently
provide regular files/directories. Capacity failures preserve both inputs.

Preserved paths are `/home`, `/volumes`, `/system/root`, `/system/config/identity`
and additional top-level user files/directories outside boot/system/tmp/recovery.
UID/GID/mode metadata, filesystem UUID and GPT disk/partition identity are retained.
System executables, providers, catalogs, modules and other system configuration
come from the candidate. Temporary and lost+found trees are fresh. `_index` caches
are regenerated and new system-app data containers are created offline; retained
retired app data grants no installation entitlement. User data is never imported
into managed code/configuration paths. Source images remain unchanged.

A SHA-256 content manifest identifies the candidate kernel, foundational modules,
provider/app code, manifests, resources and managed configuration. It excludes
mutable credentials and app/user data. The updater rejects modified/mixed managed
files or an inconsistent build ID. Boot compares the Limine-loaded config ID with
`/system/state/build.id` before loading disk modules/providers: one small read,
with no scan, repair, update or provisioning. This is coherence checking under
the existing build-owned trust model; it is not signature verification.

Pre-versioned development media can be imported once explicitly:

```sh
python3 src/tools/update-dev-disk.py bin/cloudberry.iso OLD.img NEW.img --allow-unversioned-input
```

The flag permits data import only; it never retains old executable components or
adds a legacy boot path. Default updates require versioned inputs.

`python3 src/tests/offline-update.py bin` exercises new-build preservation,
UID/GID/modes, credentials, overwrite refusal, active-file locks and mixed-build
rejection on disposable media. Reports remain after media cleanup. Guest console
checks separately boot an updated copy and authenticate/read persisted data.
