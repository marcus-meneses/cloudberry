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
accounts passwd USER
accounts login USER APP [ARGS]
```

Password entry and confirmation are non-echoed, using exclusive keyboard focus.
Never pass passwords as arguments. Ordinary users can change their own password
with the current password. Root/admin can reset other non-root accounts. Password
length is 1..64 Unicode scalar values. Password changes/account mutations require
a writable volume; QEMU_BOOT_WRITABLE=1 uses a disposable snapshot.

Login authenticates this launcher before starting APP, which inherits its kernel
credentials. The invoking terminal keeps its existing identity. For example:
`accounts login alice accounts self`. This supplies authenticated app execution,
not a complete login-session manager. Home creation, user installation policy and
nested interactive terminal focus routing are not provided by this app.

IDs remain stable across renames and are not reused after deletion. User deletion
removes its credential and supplementary memberships, but retains existing files.
Built-in root/admin/users invariants are checked by the handler. Membership
changes take effect on subsequent authentication; running processes retain their
credential snapshots. Errors print CB status and never retry uncertain commits.
