# Local application installer

`install` is an ordinary catalog application using `/@installer` and standard
streams. Installation policy and filesystem work belong to the privileged provider.

```text
install inspect /path/application.cbp
install add /path/application.cbp [--system|--user] [--reuse-data] [--yes]
install upgrade /path/replacement.cbp [--system|--user] [--yes]
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
