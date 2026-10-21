# Account management application

`accounts` is an ordinary installed app, discovered by the terminal catalog.
It uses /@identity, /@keyboard, /@memory, /@applications and standard streams.
Management requires both immutable manifest grants and root/admin credentials;
only root may change root's password. Credential files are inaccessible directly.

```text
accounts self
accounts users
accounts groups
accounts useradd NAME [PRIMARY-GROUP]  # default: users; password initially unset
accounts userdel NAME
accounts enable NAME
accounts disable NAME
accounts rename NAME NEW-NAME
accounts primary NAME GROUP
accounts groupadd NAME
accounts groupdel NAME                 # fails if still referenced
accounts grouprename NAME NEW-NAME
accounts join USER GROUP
accounts leave USER GROUP
accounts home USER                    # explicit root/admin provisioning
accounts passwd USER
accounts login USER [APP [ARGS]]
```

Password entry and confirmation are non-echoed, using exclusive keyboard focus.
Never pass passwords as arguments. Ordinary users can change their own password
with the current password. Root/admin can reset other non-root accounts. Password
length is 1..64 Unicode scalar values. Password changes/account mutations require
a writable volume; QEMU_BOOT_WRITABLE=1 uses a disposable snapshot.

Login authenticates this launcher, opens a session through `/@sessions`, then
starts APP with the verified home as cwd. Omit APP for an interactive terminal.
Exit returns to the invoking terminal with its original identity. Keyboard focus
is nested by verified process ancestry; monitor/password entry restore the nested
terminal. Session close requests cooperative stop of remaining session members.
Run `accounts home USER` before first interactive login; useradd does not provision
storage. Homes and system app data are owned by UID/primary GID, mode 0700.

For persistent state initialize separate media once with `make -C src init-dev-disk`,
then use `make -C src run-dev QEMU_ACCEL=kvm QEMU_CPUS=4`. The normal candidate and
writable snapshots retain their existing semantics.

IDs remain stable across renames and are not reused after deletion. User deletion
removes its credential and supplementary memberships, but retains existing files.
Built-in root/admin/users invariants are checked by the handler. Membership
changes take effect on subsequent authentication; running processes retain their
credential snapshots. Errors print CB status and never retry uncertain commits.

Explicit administrator revocation:

- `accounts end-session SID`: close/retry one session as root/admin; boot SID 0
  is protected, including when retrying a non-boot root session.
- `accounts revoke USER`: revoke current processes/sessions; repeat to retry a
  failed cooperative stop. The account remains enabled.
- `accounts disable USER --end-sessions`: commit the disabled account, then revoke
  its running authority. A close error does not roll back the account transaction;
  retry with `accounts revoke USER`.
- `accounts passwd USER --end-sessions`: update the password, then explicitly revoke
  current authority. Without the option, existing sessions keep their snapshots.

Root/admin credentials are checked by the sessions handler, independently of app
manifest grants. UID 0 is protected. Revoking the caller's own UID also revokes
its stdout; the app exits without attempting another output operation. Group
edits retain snapshots; use `accounts revoke USER` when immediate withdrawal is
required. An unresponsive process may continue executing with its resources
revoked; these commands do not force-kill it.
