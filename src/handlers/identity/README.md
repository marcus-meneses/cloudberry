# Identity provider

`identity.elf` is a continuously running privileged userland handler publishing
`/@identity` after filesystem readiness and before the initial app launcher.
It acquires filesystem, memory and installation-bound owner-exit notification
handles. Clients release capabilities without stopping the provider.

The image builder provisions `/system/config/identity/` (0700) and these files
(0600, root-owned in ext4):

- `users.bin`: UID, account status, primary GID, username.
- `groups.bin`: GID, built-in flag, group name, supplementary member UIDs.
- `credentials.bin`: UID, algorithm, work factor, salt and derived hash.
- `transaction.bin`: private three-file replay journal, empty between commits.

Initial identities are root UID 0, root GID 0, admin GID 1, users GID 2. Root and
these groups cannot be deleted or renamed; root stays active with primary GID 0.
Other IDs are unsigned 64-bit and allocated below persistent next-ID high-water
marks. Deleted IDs and skipped IDs below those marks cannot be introduced again.
Usernames/group names may otherwise change without changing IDs. Account metadata
does not create, move or migrate home directories or app data.

Provisioning runs during image creation, never guest boot. Root receives a random
boot-only secret; only its PBKDF2-HMAC-SHA256 derived value is stored (32-byte
random salt, 32-byte hash, 600,000 rounds). The secret is neither printed nor
persisted. Existing complete databases are preserved by the provisioning helper;
partial databases are rejected rather than replaced. The format records KDF
parameters explicitly. The low-level CREDENTIAL operation accepts derived hashes. The PASSWORD and
AUTHENTICATE operations accept secrets only through private copied handle
messages; no stored hash is returned.
The work factor follows the [OWASP PBKDF2 guidance](https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html).

## Binary format

All integers in files are little endian and encoded explicitly. No native C
layout, pointer, padding or compiler ABI is persisted. Public declarations and
codecs live in `interfaces/identity.h` and `interfaces/identity-format.h`.
Each file starts with a 64-byte header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | 8 bytes | CBUSER01 / CBGRUP01 / CBCRED01 |
| 8 | u16 | Major 1 |
| 10 | u16 | Minor 0 |
| 12 | u32 | Header bytes: 64 |
| 16 | u64 | Total file bytes |
| 24 | u64 | Common nonzero transaction generation |
| 32 | u64 | Record count |
| 40 | u64 | Next UID/GID; zero in credentials |
| 48 | u32 | CRC32 of whole file with these four bytes zeroed |
| 52 | 12 bytes | Reserved zero |

Records are sorted by numeric ID, with no duplicate IDs or public names. Names
are nonempty ASCII identifiers: first character lower-case letter/underscore,
subsequent characters may also be digits/hyphens. All records have a 32-byte
prefix and an explicit u32 record length at offset 0.

- User: status u32 at 4 (0 disabled, 1 active), UID u64 at 8, primary GID u64 at
  16, name length u32 at 24, reserved u32 zero at 28, then name bytes without NUL.
- Group: built-in flag u32 at 4, GID u64 at 8, name length u32 at 16, member count
  u32 at 20, reserved u64 zero at 24, then name bytes and sorted unique member UIDs.
- Credential: algorithm u32 at 4 (1 PBKDF2-HMAC-SHA256), UID u64 at 8, rounds u32
  at 16 (at least 600000), salt/hash lengths u32 at 20/24 (32 each), reserved u32
  zero at 28, then salt/hash. Record size is 96 bytes. Referenced users/groups
  must exist. Root must retain a credential.

CRC detects malformed/torn data; it is not a cryptographic authenticator. Access
control is enforced at the filesystem boundary using the authenticated installed
identity provider's catalog identity. All direct public filesystem operations in
this subtree are denied to other installations, even if their filesystem handle
allows reading the rest of `/system`. Users/groups are listed through identity
handles. Credential bytes are never returned by the public identity interface.

## Management contract

Manifest operations are individually granted. At every management dispatch the
handler also queries kernel-owned process credentials through its installation-
bound IO authority. UID 0 or primary/supplementary admin GID 1 may manage users
and groups; only UID 0 may replace root's credential. Supplying a UID, PID or
admin flag in a client payload grants no authority. ABORT permits owner cleanup
even after privilege loss. Queued mutations are checked when dispatched; already
dispatched synchronous operations finish.

SELF returns the caller's UID, primary GID, credential generation and supplementary
GIDs. AUTHENTICATE verifies an active account password and replaces only the
requesting process's credentials. Future children inherit independent copies;
parents, siblings and already running children remain unchanged. Boot processes
explicitly start as root. Group edits/account disable/delete govern subsequent
authentication; existing credential snapshots remain until replacement or exit.
There is no implicit session-wide identity switch or forced session termination.
The public credential snapshot and authentication group list must fit a copied
handle frame; oversized results fail explicitly. Kernel group storage is dynamic.

INFO returns generation, next IDs, counts and health. USERS/GROUPS return up to
4096 encoded file bytes per request with a next-byte cursor; start at zero, EOF
returns zero result bytes. Listing does not freeze a snapshot: compare generations
and retry if another management client committed while downloading it.

BEGIN checks the expected generation and reserves one transaction per caller.
A zero file size preserves its existing contents and advances its generation;
credentials size must always be zero. Nonzero users/groups sizes allocate dynamic
staging storage. STAGE appends bounded chunks at exact offsets; there is no fixed
account/group database capacity. CREDENTIAL replaces/adds one salt/hash record,
or removes a non-root credential with algorithm zero. It cannot export other
credentials. COMMIT validates complete uploads, formats, references, protected
identities and monotonic allocation, then checks the generation again. Concurrent
stale transactions return BUSY. ABORT discards staging. Caller death discards its
staging through owner-exit notifications.

Commit writes a complete checksummed `transaction.bin`, including all three file
images and COMMIT01 footer, before changing any database file. Existing filesystem
writes synchronously flush their ext4 journal/storage barriers. Then it applies
all images and clears the replay journal. Startup replays a valid committed
journal idempotently. An incomplete journal with a valid old database is ignored;
inconsistent/corrupt database sets fail closed. An older journal never rolls back
a newer coherent database. Recovery requires writable storage when replay is due.

After an uncertain storage failure the provider rejects further data/mutation
calls until recovery/restart; the operation may have committed. Do not blindly
retry it. Read-only media returns READ_ONLY without poisoning the healthy view.
Normal QEMU boot can read a read-only database; management commits require writable
media. A writable QEMU snapshot is disposable, not persistent installation data.

## Passwords and management app

`accounts` is a catalog-installed ordinary launcher app. It lists accounts/groups,
creates/deletes/renames them, changes primary/supplementary groups, enables or
disables accounts, changes passwords and authenticates before launching an app.
See [commands](../../apps/accounts/README.md). It never edits identity files.

PASSWORD derives PBKDF2-HMAC-SHA256 inside the provider (600000 rounds, 32-byte
salt and digest). Salt comes from checked RDRAND through IO; unavailable hardware
entropy fails explicitly, with no clock-based substitute. SHA-256 is a pinned,
BSD-licensed TinyCrypt subset; the archived upstream and maintenance ownership
are recorded under crypto/vendor. Host tests compare the complete PBKDF2 path
against Python hashlib, including long UTF-8 keys. This is functional validation,
not an independent cryptographic audit.

Secrets contain 1..64 Unicode scalar values, at most 256 UTF-8 bytes. The app
claims keyboard focus for non-echoed entry and confirmation; other readers park
and non-owner pressed-key snapshots are denied while focus is held; secrets never enter
argv or terminal command history. Non-admin self changes require the current
password. Failed verification delays subsequent verification admission globally
for one second; there is no persistent account lockout. Provider work is
preemptible userland work but requests to this provider are serialized.
The prompt belongs to the installed management app; this is not a secure-attention
or compositor-isolated trusted UI. Client/provider secret buffers are scrubbed;
normal broker request lifetime ends on SDK release. Root's unknown build-generated
boot secret can be replaced by root with `accounts passwd root`.

Account updates do not implicitly create homes. Explicit home provisioning and
interactive login are described below and in [sessions](../sessions/README.md).
The default root boot workflow is unchanged.

## Explicit home provisioning

`accounts home USER` invokes HOME with a UID. Root/admin may create the active
account's `/home/<username>` with `_config`, `_apps`, documents, downloads and
desktop, owned by its UID/primary GID with mode 0700. System applications receive
`_apps/<name>.data` and a derived `_index` from the immutable startup catalog.
No executable is installed in that directory by this operation. Root resolves to
`/system/root`; it gets neither a home nor personal installed apps.

Provisioning keeps the parent root-owned 0700 until children are assigned. It can
resume its private staging tree after failure, rejects unrelated owners/types,
and never takes over an existing user directory. A matching existing home is
returned unchanged. Ordinary callers may resolve their own existing home, but
cannot create one. This is an explicit administrative operation, separate from
the binary account transaction: a failure may leave an account with no home or a
private staging tree. No provisioning, migration or repair scan runs during boot.
Renames/deletions preserve existing directories; no automatic file movement or
ownership reuse occurs. Per-user installation and app-specific home data grants
remain future policy work; UID/mode protection isolates different users now.
