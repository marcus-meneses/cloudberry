# Filesystem ownership utility

`fsctl` is an ordinary installed app using `/@filesystem` and standard streams.
Paths resolve relative to the inherited cwd.

```text
fsctl stat PATH
fsctl chmod OCTAL PATH
fsctl chown UID GID PATH
```

Stat returns byte length, UID, GID, permission bits and directory status. Chmod
accepts 000..777; owners and administrators may change permissions when ancestor
search and namespace policy allow. Chown requires root/admin and 32-bit IDs.
Numeric IDs are intentional; name resolution belongs to identity clients.
Protected `/system` and `/boot` policy still applies even to administrators.
Operations use format backend metadata mutations; ext4 persists them through its
journal and barriers. No recursive mutation, ACL, setuid/setgid or retry policy is
introduced. Failed writes may have an uncertain on-disk outcome.
