# Installation provider

`installer.elf` is a continuously running privileged userland handler publishing
`/@installer`. It starts after identity/sessions and before application launch.
It owns installation transactions; the ordinary `install` app requests operations
through handles and never edits the registry itself.

## Trust and scope

Local `.cbp` bundles carry an Ed25519 signature, issuer public key and stable
16-byte application identity. The build pins one issuer key in the installer's
startup resources. Unknown issuers, invalid signatures, malformed resource paths,
invalid executable images and excessive manifest authority are refused before
writing files. The initial issuer is a host development key, generated once at
`.toolchain/signing/installer-development.pem` with mode 0600, excluded from Git
and images. Only its 32-byte public key ships. `INSTALL_SIGNING_KEY` selects an
explicit host key. This is a pinned local issuer, not a public trust ecosystem,
Secure Boot chain or an independently audited package system. Key rotation is
not supported; the offline updater refuses a changed key when apps are installed.

Ed25519 verification uses the unmodified, hash-pinned Monocypher 4.0.2 sources in
`vendor/monocypher/`, under BSD-2-Clause/CC0. The optional implementation uses
SHA-512. See upstream [verification contract](https://monocypher.org/manual/ed25519).
The guest contains no signing key. SHA-512 checksums detect registry/WAL corruption;
they do not replace signatures or filesystem access control.

Caller PID comes from broker admission. Kernel-owned UID/GID/home/admin facts
select the scope: normal users install only for themselves; root defaults to
system-wide installation, and root/admin may explicitly select system scope.
Root has no personal installation scope. No request can nominate another UID.
Package source reads are checked against the original caller's filesystem policy
and mode bits, so installer privileges cannot copy another app's private bundle.
Runtime bundles may contain ordinary apps or launchers, never providers/managers,
OS resource publications or private IO authority. The signed manifest is the
requested permission ceiling. The registry holds the approved subset used at launch.
The CLI displays both and asks for consent unless `--yes` is supplied; this does not implement a general interactive permission-prompt broker.

## Layout and interfaces

Built-in registrations remain immutable. Installed registrations are supplemental,
versioned snapshots owned by the applications kmodule. Only this provider may
stage/prepare/publish them through its installation-bound IO operation. Readers
pin a snapshot; commit swaps the publisher reference and frees old storage only
after the last reader exits. Launch/discovery/private-data policy use the same
registry. Built-in names/identities cannot be replaced. New launches of dynamic
apps pause during prepared publication; built-ins remain available. Active apps
prevent replacement/removal. No forced kill or running-image upgrade is provided.

Physical application files use `_` + 32 lowercase identity hex digits + `.` + the
friendly name. This bounds dynamic names to 207 bytes so the longest sidecar fits
the filesystem's 255-byte component limit. Programs remain callable by friendly
name in the terminal. System installations live in `/system/apps`; per-user
installations live in `/home/<name>/_apps`. Sidecars are `.manifest.json`,
`.resources/` and `.data/`. Per-user code/resources are root-owned and read-only;
private data is owned by the account, mode 0700. System apps have separate private
data in each provisioned home and `/system/root/_apps`. Explicit home provisioning
also includes already registered system apps. `applications.resources` resolves
an app's resource directory; `applications.data` resolves its private directory.
JSON `_index` files are derived listing caches and convey no execution authority.

INSPECT returns paged requested or approved JSON and a SHA-512 fingerprint of
the signed-bundle fingerprint plus canonical approved JSON. Mutations present that
review fingerprint, binding consent to both package bytes and effective grants.
METADATA returns signed/installed revision, scope owner and issuer public key. Existing retained data requires explicit `--reuse-data`. REMOVE keeps data
unless `--delete-data` is requested. LIST shows visible dynamic registrations.
STATUS reports generation, recovery-required state and last operation phase/status.
Phases: 1 verify, 2 private-data planning, 3 registry construction, 4 prepare,
5 durable intent, 60 executable, 61 manifest, 62 resource directory, 63 resource
file, 64 signed archive, 7 commit marker, 8 registry storage, 9 publication,
10 removal cleanup, 11 derived indexes. A successful operation clears the phase.
A successful removal or upgrade requires the process to have stopped; BUSY does not kill it.
Application renames, semantic version/dependency management, network fetching,
new trust-key enrollment and provider installation are later milestones.

## Durable publication and recovery

Protected `/system/config/installations/` contains:

- `registry.bin`: explicit little-endian supplemental records plus SHA-512.
- `transaction.bin`: a checksummed intent and optional `COMMIT01` footer.
- `packages/<owner-hex>-<identity-hex>.cbp`: verified signed originals for recovery.

The registry header is four u64s: magic `0x324745524243`, total payload bytes,
generation and count. Each record is owner UID (UINT64_MAX for system-wide), a
nonzero signed release revision, then the documented 64-byte installation record
and name/path/approved JSON bytes. Legacy `0x314745524243` records with reserved
zero remain readable as revision 1. Explicit mutations write the new format; boot
does not migrate it. The physical `.manifest.json` stays the signed requested ceiling. All fields have explicit sizes/byte order; no pointers persist.
The public ABI is in `interfaces/installer.h` and `interfaces/registry.h`.

The provider validates and prepares a new kernel snapshot before filesystem work.
It durably writes the full intent, writes new code/resources/data, then appends the
commit footer. It writes the checksummed registry, publishes the prepared snapshot,
performs uninstall cleanup and refreshes indexes, then clears the journal. Existing
executables are never overwritten by an install. No rename primitive or kernel
package policy is needed. Empty-directory removal is explicit `filesystem.rmdir`;
recursive traversal remains userland work.

An uncommitted intent rolls back newly created code/resources/data; a committed
intent finishes publication/removal. Boot only reads registrations, verifies signed
originals/installed bytes and replays an outstanding transaction. It does not scan
for packages, provision accounts, migrate an image or install ordinary new apps.
Failed writes after a possible commit poison further mutations rather than guess
or retry a disk operation. Reboot performs recovery; an unresolved or corrupt WAL
must not be imported by the offline updater. Provider death discards staging; if
publication was prepared, dynamic launches remain gated pending recovery.

Tests use disposable files/images. Host tests cover signature/format refusals,
scope/role policy, reader retirement, active removal, and install/remove crashes at
six transaction boundaries. Guest tests exercise actual ext4, pipes, registration,
resources, private data, per-user visibility and reboot/offline-update persistence.

The committed intent also repairs a registry file interrupted during its rewrite;
a damaged registry without such an intent fails closed. Host crash tests cover
both cases. ext4 indexed-directory initialization clears recycled blocks before
calculating their checksums, so uninstall/reinstall cannot inherit stale data in
new directory metadata.

Focused validation commands (all disposable):

```sh
make -C src test-installation-registry test-installer-store test-bundles
python3 src/tests/installation-state.py bin
python3 src/tests/persistent-installations.py bin --cpus 4
```

The persistence runner installs and removes apps through the real terminal,
checks active-removal refusal and explicit data reuse, installs private/global
apps, performs an offline update and reboots to verify discovery/resources/data.
It removes successful disposable media while retaining reports/logs. Failed media
is retained for diagnosis. Candidate builds contain neither sample apps nor test
packages; fixtures enter only copied test images.

Startup containment: an invalid optional app is omitted from live discovery while
verified apps remain usable. Its durable registration/files remain unchanged.
`install status` lists unavailable apps and their errors; `install list` marks
them unavailable. When the full registry is trustworthy, explicit owner-authorized
repair/removal of quarantined entries is allowed; ordinary mutations stay blocked
until quarantine is cleared.
An untrusted registry or unresolved recovery failure leaves the installer available
for status and starts immutable built-ins only. No disk state is guessed, erased
or automatically reinstalled. Four-CPU TCG corruption checks passed in
`console/aa1cae264e794094867a5dfc357646a0` (registry) and
`console/c1b9e67eba73411abf4f1ab444258c2c` (one bad app plus one valid app).

Inactive upgrades use `install upgrade PATH [--system|--user] [--yes]`. The bundle
must retain the registered application name, installation ID and pinned issuer;
the reviewed fingerprint binds the new executable, manifest and resources. The
kernel's guarded PREPARE checks activity even if only code/resources change and
the registry record is otherwise identical. New dynamic launches remain gated
until publication or rollback. Private data is retained; no data migration runs.

Upgrade intents use `CBINS002`: the existing 64-byte header plus an eight-byte
previous-bundle length, followed by base path, plans, next registry, new signed
bundle, previous signed bundle and checksum. Existing install/remove intents keep
`CBINS001` and remain readable. Current bundle/registry revisions are described below.
Before commit, recovery restores the previous signed executable/manifest/resources
and archive. After commit, it finishes the new revision and archive. The old
revision is retained in the WAL until completion; successful upgrades do not keep
a historical rollback catalogue. This protects launch/publication and crash
recovery; raw administrative filesystem reads are not snapshot-isolated during
replacement. Both revisions must tolerate the retained data schema.

`make -C src test-installer-crash` builds fault executables only under `bin/tests`,
then exercises six durable cut/reboot cases on copied images. The marker and exit
code are compiled out of candidate installer code. Independent ext4 checks and
signed-content/data assertions run after recovery; successful media is cleaned.
The model is abrupt provider exit at completed durable boundaries followed by VM
restart, not arbitrary torn writes or physical controller power-loss acceptance.
Four-CPU reports are recorded in the roadmap. A failed test preserves its media.


## Signed revisions and approval maintenance

New bundles use `CBPKG002`: the original 112-byte header followed by a signed,
nonzero little-endian u64 release revision at offset 112, then name, requested
manifest, executable, resources and Ed25519 signature. The host builder accepts
`--revision N`, default 1. Legacy `CBPKG001` bundles remain readable as revision 1.
Identity belongs to the pinned issuer; upgrades keep issuer, ID, name and scope.
The CLI prints the issuer public key and signed/installed revision. Enrollment,
issuer substitution and key rotation remain unsupported, rather than silently
reassigning an identity. Offline updates refuse a changed pinned key with installed apps.

Upgrade requires a greater revision. Equal revisions are rejected; an explicit
`--allow-downgrade` permits a lower revision and warns that retained private data
may have an incompatible schema. This is a numeric release counter, not semantic
versioning, dependency resolution, anti-rollback hardware or private-data migration.

Install defaults to the requested policy after installation consent. Upgrade and
repair default to the intersection of previous approvals and new requests: removed
operations disappear, newly requested and previously denied operations stay denied.
`install permissions NAME` shows signed requests and current grants.
`install approve NAME --permissions PATH` reviews and transactionally replaces the
grants with a JSON subset of the signed ceiling. The same `--permissions PATH`
option selects grants during install/upgrade/repair. Use manifest JSON syntax;
resource scope and publication metadata must match the ceiling, and unknown or
duplicate resources/operations are refused. An empty resources array denies all.
The source file is read under the original caller's access policy, copied and
validated; a changed policy between review and mutation fails the review hash.

The approved manifest in the immutable kernel registry is the admission policy
used by handle acquisition/reflection. Changing grants requires an inactive app;
the new snapshot applies at next launch. This does not implement live revocation
of an already acquired handle or an automatic authorization dialog.
Metadata-only approval transactions use CBINS001 action 6 and never rewrite signed
artifacts or private data.

## Explicit quarantine repair and removal

`install repair PATH` requires a quarantined entry in a fully validated registry,
the same owner/name/ID/pinned issuer and a revision at least the installed revision.
It replaces executable, requested manifest, resources and archive with verified
bytes, preserves existing private data and retains the approval intersection unless
an explicit subset is selected. Other quarantined entries stay unpublished; healthy
apps stay available. No loose executable is scanned for identity or trusted as a
repair source. A healthy app uses upgrade instead of same-revision repair.

Repair uses action 4 with CBINS001: before commit recovery removes replacement
artifacts/archive and newly provisioned data, leaving the app unavailable; after
commit it finishes publication. Existing private data survives either outcome.
Explicit repair can replace damaged evidence; it is never attempted automatically.

Removal of a quarantined app does not require a readable saved package. CBINS003
uses a 72-byte header (previous-registry length at offset 64), action 5, no bundle,
and the trusted previous registry after the next registry. Recovery validates the
exact single-record removal and canonical owner/ID-derived paths before cleanup.
The journal retains that authority through registry overwrite. Default removal
retains data; `--delete-data` explicitly removes it. This grants no authority to
remove unrelated sidecars or another account's installation.

STATUS distinguishes quarantine-only state 1 from untrusted/uncertain state 2.
State 2 permits diagnostics/listing but no repair/removal; no registry is inferred
from disk contents. Successful repair/removal clears only the target diagnostic.
Host UBSan checks cover public request authorization/review, revision policy,
permission subsets and six interruption boundaries each for repair, quarantined
removal and approval, in addition to existing install/remove/upgrade recovery.


Focused guest maintenance checks (isolated fixtures, four CPUs):

```sh
python3 src/tests/console-boot.py bin --fixtures --installer-phase policy --cpus 4
python3 src/tests/console-boot.py bin --fixtures --installer-phase repair --cpus 4
python3 src/tests/console-boot.py bin --fixtures --installer-phase quarantine-remove --cpus 4
```

These pass on TCG; report IDs are recorded in the roadmap. The runner synchronizes
host fixture writes before launching the VM so preparing a test image does not
leave host writeback outstanding when guest flush deadlines start. This is separate
from filesystem/controller power-loss validation. Successful media is removed.


Containment readiness: startup validates the full registry, verifies signed
artifacts, prepares the healthy snapshot, repairs derived indexes and clears the
recovered WAL **before** the final kernel publication. COMMIT is the last step;
no fallible filesystem operation follows it. Index rebuilding uses the prepared
snapshot plus immutable built-ins, with the same per-user visibility as LIST.
Indexes remain presentation caches and never authorize execution.

An initialization failure aborts staging and publishes a verified empty snapshot
at the current kernel generation. The kernel continues to reject restore BEGIN
over an ordinary live registry; the installer does not bypass that gate. Failure
to obtain the generation or publish containment prevents provider readiness.
STATUS retains the original storage failure. Outstanding recovery intent is kept
when index rebuilding or WAL cleanup fails, so a later boot can retry safely.
Host tests route actual installer transport requests through the kernel registry
implementation, including both failure sites and VERSION/COMMIT refusal.

`make -C src test-installer-maintenance-crash` runs only the six maintenance
interruption cases: repair (action 4), quarantine removal without a usable archive
(action 5), and approval-only publication (action 6), each before and after the
commit footer. Fault ELFs live under `bin/tests`; candidate executables contain no
cut code. Each case reboots the copied disk, checks registry visibility and real
permission enforcement through the terminal, then independently replays/checks
ext4 and verifies registry, artifacts, grants and retained private data. Successful
media is removed after each case. The older `test-installer-crash` target retains
its original six install/remove/upgrade cases. This is provider interruption at
durable boundaries, not arbitrary controller/sector power-loss acceptance.


`make -C src test-installer-user-lifecycle` checks a normal user's signed V2
installation, denied operations, inactive upgrade, reboot and offline candidate
update. It checks effective handle authorization, scope isolation, signed revision,
approved JSON, inode ownership/modes and exact private-data preservation on copied
media. Test applications and packages never enter the candidate image.
