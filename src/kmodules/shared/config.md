# Boot configuration

`config.kmod` parses [boot/cloudberry.cnf](../../boot/cloudberry.cnf). Limine
loads the file as a data module tagged `cloudberry-config`, separately from the
executable module tagged `config`. The kernel locates exactly one data module
and passes its reported address/size to `config.init(hub,heap,text,bytes)` after
heap initialization, before hardware initialization or AP/thread startup. There
is no fixed physical load address and no kernel filesystem access is needed.
Missing or duplicate data modules fail startup. The core does not parse values.

## Text and schema

Each entry is `key=value` on its own line. LF/CRLF and a final line without a
newline are accepted. Blank lines and lines whose first non-space character is
`#` are ignored. Leading/trailing spaces/tabs are trimmed. Keys are case-sensitive;
unknown or duplicate keys are errors. Values have no quoting, escaping, inline
comments, arrays, nesting or inferred types. Strings contain UTF-8 without ASCII
control characters; embedded NUL bytes anywhere in the file are invalid. No BOM
is accepted. Type definitions, defaults and ranges belong to the module schema.

| Key | Type | Rule/default |
| --- | --- | --- |
| `version` | unsigned integer | Required; exactly `1` |
| `boot.show_inventory` | boolean | `true`; controls the core's hardware inventory dump |
| `drivers.directory` | string | `/system/drivers`; metadata for future disk loading |
| `storage.timeout_ms` | unsigned integer | `1000`, range 1–4294967295; metadata for future storage consumers |
| `drivers.partition_guid` | GUID | Optional; absent until image preparation assigns the partition |

Booleans are strictly `true`/`false`. Integers are unsigned decimal with overflow
and schema-range checks. GUIDs use exact `8-4-4-4-12` hex groups, accepting either
hex letter case. The driver directory and timeout do not yet configure a storage
stack; in particular they do not change the existing NVMe timeout. There is no
fabricated partition GUID or automatic selection of the first ext4 partition.

## Hub references and ownership

Each available item is a table reference named `config.<key>`, pointing to a
`struct config_value` from [config.h](config.h). Its ABI version, type, byte size
and data pointer describe the owned value. Strings include a terminating NUL
in their byte size; bool uses `uint8_t`, integer uses `uint64_t`, and GUID uses
`struct config_guid`, with native integer fields matching GPT's x86 layout.

```c
const struct kernel_hub_table *ref =
    kernel_hub_find_table(hub, "config.drivers.directory");
const struct config_value *value = ref ? (const void *)ref->address : NULL;
/* Check value->major/type/bytes before interpreting value->data. */
```

Parsing and allocation finish before `hub.publish_tables(refs,count)` publishes
the batch. This kernel mechanism validates capacity, descriptors and all name
collisions under the CPU lock before changing the hub. Existing single-reference
publication remains available. Failed parsing/allocation/publication frees all
temporary owned storage and leaves no partial configuration references.

Published descriptors and data live until reboot. Source text is never retained.
They are immutable by trusted-kernel contract; heap pages are not separately
write-protected and this is not a userland isolation boundary. Reacquire hub
reference pointers after subsequent export registration moves the reference area;
descriptor/data pointers stay stable. The current schema retains five 56-byte
item slots plus string backing (16 bytes for the default directory), excluding
allocator metadata. Temporary publication references are freed after init.

`config.info(out)` reports readiness, published count, error code and source line.
Errors without an associated source line report zero. Failed init may be retried;
successful init is one-shot and rejects replacement. No mutation/reload interface
is provided. The core prints error code/line on startup failure, not values.

## Validation

`make -C src test-config` runs production parser/init code under UBSan with
modeled allocation/publication: all types, defaults, owned lifetime after source
mutation, malformed UTF-8/GUID/integers, ranges, duplicate/unknown/missing keys,
NULs, LF/CRLF, allocation rollback, collisions and publication failure.

`make -C src test-config-boot` uses disposable images. Positive boot verifies the
Limine data handoff, typed hub references and actual batch-publication rollback.
Negative boots reject missing/duplicate data modules and an invalid config
version with a source-line diagnostic. One-CPU and four-CPU Intel/IOMMU TCG
acceptance pass; no candidate VM or persistent development disk is used.
