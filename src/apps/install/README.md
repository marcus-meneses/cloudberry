# Local application installer

`install` is an ordinary catalog application using `/@installer` and standard
streams. Installation policy and filesystem work belong to the privileged provider.

```text
install inspect /path/application.cbp
install add /path/application.cbp [--system|--user] [--reuse-data] [--yes]
install upgrade /path/replacement.cbp [--system|--user] [--allow-downgrade] [--yes]
install repair /path/replacement.cbp [--system|--user] [--yes]
install permissions NAME [--system|--user]
install approve NAME --permissions /path/approved.json [--system|--user] [--yes]
install remove NAME [--system|--user] [--delete-data] [--yes]
install list
install status
```

Root defaults to system-wide scope. Normal users default to their own installation;
only root/admin may select `--system`. The app cannot select a different user.
Without `--yes`, adding shows the verified manifest and asks for consent; removal
asks for consent and states whether private data is retained. Filesystem paths are
resolved against the current directory. Normal users may source bundles from their
home or `/system/packages`. Source bytes are signature-checked again at install.

Only inactive apps can be removed. Stop them first if BUSY is returned. Retained
data requires `--reuse-data` on a subsequent install; no silent discard occurs.
Use upgrade to replace an inactive app with the same name/installation ID. The
transaction retains the old signed revision until commit and preserves private
data. No private-data schema migration or post-success rollback history is added.
Uncertain storage errors require checking provider health and recovery, not blind
retry. STATUS exposes generation/health and the last diagnostic phase/status.

Build a signed bundle on the host with `src/tools/build-app-bundle.py build`.
The first build creates the excluded development signing key; explicit keys can be
selected through `INSTALL_SIGNING_KEY`. See the [provider contract](../../handlers/installer/README.md)
for format, trust limits and durability. Signer keys never enter guest images.


`--permissions FILE` also selects a subset during inspect/add/upgrade/repair.
Use manifest JSON, for example:

```json
{"version":1,"resources":[{"name":"/@streams","operations":["read","write"]}]}
```

Every grant must fit the signed request, including resource scope/publication.
`{"version":1,"resources":[]}` denies all resource requests. Review shows signed
requests and approved grants separately; consent binds both. `--yes` skips the
interactive review, not validation. The file path is resolved against the CWD and
read with the original caller's access rights.

Upgrades require increasing signed numeric revisions (`--revision N` when building);
equal revisions are refused. Downgrades require `--allow-downgrade` and retain data
without schema conversion. Upgrade defaults preserve prior denials and do not
automatically approve new operations. Explicit approval changes require the app
inactive and apply on next launch; they do not revoke running handles.

A quarantined app can be repaired from a verified same-or-newer revision, retaining
its identity/data and previous permission subset. An unavailable saved archive does
not prevent removal when its registry is trustworthy. An untrusted registry remains
restricted to diagnostics; neither command discovers installations from loose files.
