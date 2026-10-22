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
separate from the guest installer and does not perform in-place app upgrades.

The updater validates GPTs, format/identity records and the candidate's build
manifest. It takes nonblocking byte-range image locks to refuse an active writer.
It imports data only from a disposable filesystem copy; required ext4 journal
replay runs with `e2fsck -p -E journal_only` on that copy. Other filesystem errors
are refused. A nonempty identity or installation transaction requires recovery by the old guest;
it is never guessed/replayed by the host updater. Unsupported inode types,
hardlinks and control-character filenames fail explicitly; guest APIs currently
provide regular files/directories. Capacity failures preserve both inputs.

Preserved paths are `/home`, `/volumes`, `/system/root`, `/system/config/identity`
`/system/config/installations`, validated dynamically installed system apps,
and additional top-level user files/directories outside boot/system/tmp/recovery.
UID/GID/mode metadata, filesystem UUID and GPT disk/partition identity are retained.
Built-in executables, providers, catalogs, modules and other system configuration
come from the candidate. Supplemental registrations, signed archives, code,
resources and per-user private data survive together. The updater verifies saved
Ed25519 bundles against the pinned issuer and checks actual installed bytes; it
rejects unregistered system app files, candidate name/ID conflicts and issuer-key
changes while apps are installed. `_index` caches are not trust records. Temporary and lost+found trees are fresh. `_index` caches
are regenerated and new system-app data containers are created offline; retained
retired app data grants no installation entitlement. User data is never imported
into managed code/configuration paths. Source images remain unchanged.

A SHA-256 content manifest identifies the candidate kernel, foundational modules,
provider/app code, manifests, resources and managed configuration. It excludes
mutable credentials, installation transactions/registrations, derived indexes
and app/user data. Signed runtime code is verified separately, not incorporated
into the candidate build hash. The updater rejects modified/mixed managed
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

`INSTALL_SIGNING_KEY=/absolute/host/key.pem` pins a local Ed25519 issuer at build
time. The default `.toolchain/signing/installer-development.pem` is generated once
and retained outside Git/images; back it up separately if retaining installed
apps. A rebuilt candidate with a different key cannot silently inherit those apps.
Only the raw public key is included in the installer startup resources. See the
[installer](../handlers/installer/README.md) for bundle creation and guest commands.

Guest inactive upgrades retain the same installation ID/name and private-data
location. Their CBINS002 journal includes old/new signed bundles until commit
completion; interrupted transactions require guest recovery before offline update,
just like CBINS001 install/remove transactions. A quarantined or corrupt installed
application is not silently repaired by the offline updater. The guest can still
start built-in recovery tools. A trusted registry permits explicit quarantine repair
or removal through the guest installer; an untrusted registry remains read-only.
Offline preservation validates signed release revisions and approved permission
subsets, retaining the signed requested manifest separately. No boot-time migration
or offline automatic repair is added.


`make -C src test-installer-user-lifecycle` exercises signed per-user revision and
approval preservation across reboot and this offline updater. It verifies effective
denials, registry/signed artifacts, UID/GID/modes and private bytes on disposable
media, then removes successful images while keeping reports. Startup recovery
finishes index/WAL disk work before the final kernel registry publication; no
ordinary image update or layout migration occurs during boot.
