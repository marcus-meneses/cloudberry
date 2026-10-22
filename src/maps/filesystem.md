# Filesystem namespace

The image builder prepares this tree in the selected ext4 system volume. Boot
never formats, migrates or creates installation directories. `/boot` is an empty
mountpoint in ext4; startup mounts the verified disk's ISO9660 boot filesystem
there read-only. Foundational modules remain boot-loaded. Future disk-loaded
modules live under `/system/kmodules`, without a legacy boot-completion tree.

```text
/
├── boot/                       # Read-only ISO9660 mount
│   ├── kernel.elf
│   ├── cloudberry.cnf
│   ├── EFI/
│   ├── limine/
│   └── kmodules/
│       ├── drivers/
│       └── helpers/
├── system/
│   ├── apps/
│   │   ├── _index
│   │   ├── <appname>
│   │   ├── <appname>.manifest.json
│   │   └── <appname>.resources/
│   ├── handlers/
│   │   ├── <handlername>
│   │   ├── <handlername>.manifest.json
│   │   ├── <handlername>.provider.cnf
│   │   └── <handlername>.resources/
│   ├── kmodules/
│   │   ├── drivers/
│   │   └── helpers/
│   ├── fonts/
│   ├── config/
│   │   ├── identity/
│   │   │   ├── users.bin
│   │   │   ├── groups.bin
│   │   │   ├── credentials.bin
│   │   │   └── transaction.bin
│   │   ├── installations/
│   │   │   ├── registry.bin
│   │   │   ├── transaction.bin
│   │   │   └── packages/             # Verified signed bundle archives
│   │   └── keyboard/
│   │       ├── config.cnf
│   │       ├── ibm.keymap
│   │       └── abnt2.keymap
│   ├── state/
│   │   └── installed.bin
│   └── root/
│       ├── _config/
│       └── _apps/
│           ├── _index
│           └── <appname>.data/
├── home/
├── volumes/
└── tmp/
```

Explicit `accounts home USER` provisioning creates the non-root account layout:

```text
/home/<user-name>/
├── _config/
├── _apps/
│   ├── _index
│   ├── _<id>.<name>                  # User-installed executable
│   ├── _<id>.<name>.manifest.json
│   ├── _<id>.<name>.resources/
│   ├── _<id>.<name>.data/            # User-installed or global installed app
│   └── <appname>.data/               # Built-in app state
├── documents/
├── downloads/
└── desktop/
```

No placeholder user or `/home/root` is created. Root's reserved identity is zero;
its account area is `/system/root`, initial cwd `/`, and it has no own installed
applications. Shared system executables live directly in `/system/apps`; private
state (history, caches, preferences, databases, drafts) belongs in the running
user's `<appname>.data`, including for system-provided applications. Explicit
home provisioning is separate from login and boot. Users may install only for
themselves; root/admin may install system-wide. Root has no private app scope.

Built-in application filenames use catalog names without `.elf`. Runtime-installed
apps use `_<32-hex-installation-id>.<name>` plus `.manifest.json`, `.resources/`
and `.data` sidecars. Friendly names are case-sensitive ASCII alphanumeric,
hyphen or underscore, start with an alphanumeric character and have at most 207
characters so the longest sidecar fits ext4's 255-byte component limit. Names/IDs
cannot collide with built-ins or visible global installations; separate users may
have different private installations with the same friendly name.

Built-ins live in immutable `installed.bin`; supplemental registrations live in
the protected versioned `registry.bin`. The applications kmodule owns immutable,
refcounted live snapshots. Only the privileged [installer](../handlers/installer/README.md)
may publish a validated replacement. Launch, discovery and private-data policy
use the same registry; admitted installation identity/scope stays stable across
credential changes. Existing names cannot be installed again with `add`; `upgrade` replaces an
inactive app with the same name/ID while retaining private data. Reusing retained data requires explicit
`--reuse-data`; removal retains it unless `--delete-data` is selected. Signed release
revisions and approved permission subsets persist in the registry. The installed
`.manifest.json` remains the signed requested ceiling; the immutable registry
contains the effective policy used at launch. Permission changes require an
inactive app and apply at next launch. Quarantine repair/removal requires trusted
registry ownership; no directory scan creates installation authority.

Each `_index` is a derived JSON version 1 display cache with `app_id`, `name` and
`installation`; editing it grants no rights. The build supplies built-in caches;
installer transactions refresh affected caches and new-home provisioning includes
visible global installations. Registry/WAL recovery runs only when required;
there is no boot-time installation scan, migration or directory provisioning.
Root's cache references global apps, never root-owned installations.

A leading underscore means hidden presentation only. Enumeration retains all
entries; `ls` omits underscore-prefixed names by default and `ls -a` includes
them. `.`/`..` keep path semantics and are not listed by this utility. Future file
pickers/completion should share the underscore rule. No access permission follows
from visibility.

The filesystem manager intersects UID/mode checks with installed-app policy on
every operation, including file streams delegated through stdin/stdout. Authentication
atomically supplies an owned home snapshot; launches copy it independently.
`/@applications.data` resolves the calling installation to its canonical
`<home>/_apps/<appname>.data` without accepting a caller-supplied user/app identity.
Same-user apps cannot read, mutate, inspect or change permissions on another app's
private data, even if mode bits permit it. An app cannot remove its container or
mutate `_apps`/`_index`/installation sidecars. Ordinary document paths keep UID/mode
semantics. Root retains administrative access, while protected identity files
remain exclusive to the installed identity provider. Installation state is
exclusive to the installer provider; it is not exposed to root apps directly. Existing /system and /boot
mutation restrictions remain. Names/caches confer no installation authority.

Closed sessions revoke new resource calls, queued work and blocked stream/FS
operations. Already executing backend work finishes before mappings are reclaimed.
No boot-time provisioning is introduced; explicit offline updates preserve user
state and regenerate catalog caches. See [development images](../tools/development-images.md).

Keyboard `.keymap` files retain their existing JSON format. The build places them
under `/system/config/keyboard`; startup aliases copy these files into the
keyboard handler's existing logical `layouts/*.json` packet entries. Providers
interpret their own configuration. Bootstrap only handles generic
`logical-name=/absolute/source` file aliases and does not parse keyboard layouts.
The compiler's bundle-directory staging remains an intermediate under `bin`; it
is not copied into the guest as legacy bundle directories.

Generated system inodes belong to guest UID/GID 0, independently of host ownership.
Root account/config/data directories use mode 0700; `/tmp` uses 01777. Runtime filesystem calls enforce UID/GID mode bits and directory traversal;
ACLs, setuid/setgid behavior and general POSIX compatibility remain unimplemented.

The root `tmp` directory is reserved for temporary data. Rebuilding an image
starts it empty; automatic cleanup on persistent installations and volume mount
policy are not implemented by the directory's existence.

The ext4 formatter also supplies its filesystem-owned `/lost+found` recovery
directory. It is not an application installation or a user home.

Account metadata is provisioned under /system/config/identity at image-build time.
Only the installed identity handler can access that subtree through public
filesystem handles; management uses /@identity. See [identity](../handlers/identity/README.md).

The public filesystem contract includes empty-directory `rmdir`. Recursive
uninstall traversal belongs to the userland installer; it skips `.`/`..` and
never recursively deletes inside the kernel. Backend ABI 2.1 adds this operation;
2.0 backends remain usable and return UNAVAILABLE for `rmdir`.
