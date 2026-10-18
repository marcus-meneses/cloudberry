# Built-in ordinary applications

`installed.json` is the build-owned source of installation bindings. The builder
packages paths, manifest bytes, identities and execution settings into the binary
catalog; launch callers cannot override these values. It also records the
privileged keyboard and application providers so bootstrap can validate their
bindings. The generated catalog lives at the root of the current read-only
ext4 bundle directory, outside each application's own directory.

- `launch`: accepts an application startup packet whose argv is
  `launch INSTALLATION [ARGS...]`. It acquires `/@applications`, launches the
  named ordinary installation, waits, releases its result and returns the child's
  exit status (or 128 + fault vector). The launcher role is explicit in the catalog.
- `true`: a minimal ordinary native executable returning zero. Its manifest has
  no resource grants. It receives the same read-only startup format as other apps.

`../sdk/application.h` validates the startup envelope and exposes argument and cwd
spans without raw kernel pointers. A cwd span is length-delimited, not NUL-terminated;
argument strings are NUL-terminated and preserve case and whitespace.

There is no terminal in this active implementation yet. The applications are
installed and callable through the lifecycle handle; they are not new kernel or
terminal built-ins. Ordinary candidate boot starts the two resource providers,
with no demo or test application automatically launched.
