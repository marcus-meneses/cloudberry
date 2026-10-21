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
