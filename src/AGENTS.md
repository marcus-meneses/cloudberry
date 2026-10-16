# Cloudberry v2

- This is the active source tree. Add only requested scope.
- The superseded implementation is archived in ../.src_old; keep it local and
  excluded from Git and active builds.
- Build artifacts belong directly in ../bin. Do not import v1 system components.
- Use UEFI, PCIe NVMe, AHCI SATA and xHCI USB for development; do not add PS/2
  input or legacy IDE/PATA drivers.
- Keep the directory tree in README.md current.
- Prepare images at build time; never add migrations or formatting to boot.
- Use focused build/boot validation and disposable disk paths for automated
  tests. Do not automatically attach the developer's persistent disk images.
