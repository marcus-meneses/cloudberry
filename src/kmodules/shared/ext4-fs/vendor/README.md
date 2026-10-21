# lwext4

Upstream: https://github.com/gkostka/lwext4
Pinned commit: 58bcf89a121b72d4fb66334f1693d3b30e4cb9c5
License: BSD-3-Clause, retained in lwext4/LICENSE and source headers.

Fresh upstream sources; no superseded Cloudberry components were imported.
Local changes are recorded in cloudberry.patch: dynamic registries, normal dirty
mount state, integrity/transaction failure hooks, registry-only disconnect
unlinking, recovery corrections and initialization of recycled indexed-directory
blocks before checksum calculation, as
validated by the isolated tests. Formatting/MBR/mkfs/xattr API sources are not
linked. The Cloudberry adapter supplies allocation, byte primitives and durable
block I/O through the hub-selected transport.
