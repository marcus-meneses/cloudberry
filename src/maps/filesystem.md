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

Once home provisioning is implemented, non-root accounts use:

```text
/home/<user-name>/
├── _config/
├── _apps/
│   ├── _index
│   ├── <appname>                    # User-installed apps only
│   ├── <appname>.manifest.json      # User-installed apps only
│   ├── <appname>.resources/         # User-installed apps only
│   └── <appname>.data/
├── documents/
├── downloads/
└── desktop/
```

No placeholder user or `/home/root` is created. Root's reserved identity is zero;
its account area is `/system/root`, initial cwd `/`, and it has no own installed
applications. Shared system executables live directly in `/system/apps`; private
state (history, caches, preferences, databases, drafts) belongs in the running
user's `<appname>.data`, including for system-provided applications. Users will
install only for themselves. Account authentication and kernel credentials are implemented; installers,
automatic homes/home grants and per-user catalogs remain subsequent milestones.

Application filenames use catalog names without `.elf`; stable installation IDs
remain in the authoritative catalog. Names are case-sensitive ASCII alphanumeric,
hyphen or underscore, start with an alphanumeric character, and are unique in the
catalog. This avoids both reserved hidden prefixes and sidecar suffix collisions.
Renaming, replacement and reuse of old private data will require installer policy
bound to identity, not filename alone. The current catalog is immutable.

Each `_index` is JSON version 1 with entries containing `app_id`, `name` and
`installation`. The build derives these disposable lookup caches from the
installation catalog; changing an index grants no rights. Runtime application
lookup still uses the immutable catalog, not these JSON caches. A future installer
must update installation files, authoritative records and derived indexes as one
transaction; no boot-time repair scan is introduced. Root's index references
system applications and contains no root-owned installations.

A leading underscore means hidden presentation only. Enumeration retains all
entries; `ls` omits underscore-prefixed names by default and `ls -a` includes
them. `.`/`..` keep path semantics and are not listed by this utility. Future file
pickers/completion should share the underscore rule. No access permission follows
from visibility.

The current filesystem provider protects `/boot` and `/system` against ordinary
mutations, except a catalog-bound app may write descendants of its own
`/system/root/_apps/<appname>.data/`. The provider supplies authenticated caller
installation identity; the IO bridge resolves it against the trusted catalog.
The data directory itself, other apps' state and `_index` cannot be mutated
through that exception. This is the current single boot-domain policy, not a full
multi-user read-isolation implementation. App-data writes require a writable
medium, just like other writes. `/system/root` is built with mode 0700; ext4 modes
alone are not a replacement for future handle authorization policy.

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
