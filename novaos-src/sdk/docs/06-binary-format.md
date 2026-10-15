# 6. NovaOS package format "NPKG" v1

A package is **data**, never code. NovaOS never executes package bytes; an in-kernel handler for the
package's *application type* interprets the payload. Unknown types are rejected.

Extensions
- `.powor`, `.appd` - install sources (identical container; detected by magic + CRC, not by extension).
- `.power`          - installed form, stored as `/System/Installed Applications/<id>.power`.

All integers are little-endian. A package is exactly three sections, back to back, no gaps, no trailing bytes:

```
off size field
  0   4  magic            "NPKG"
  4   2  format_version   1         (anything else is rejected)
  6   2  header_size      48
  8   4  total_size       == file size
 12   4  crc32            CRC-32/IEEE of the whole file with this field zeroed
 16   4  meta_off         == 48
 20   4  meta_len         1..2048
 24   4  type_off         == meta_off + meta_len
 28   4  type_len         1..1024
 32   4  data_off         == type_off + type_len
 36   4  data_len         0..196608 (type-specific, see below)
 40   8  reserved         must be zero
 48  ...  META | TYPE | DATA            data_off + data_len == total_size
```
Overflow-safe checks are used everywhere; any violation is a hard reject.

## META - package metadata (text)
`key=value` lines separated by `\n` (max 16 lines, 256 bytes/line). Bytes must be printable ASCII (0x20-0x7E)
plus `\n`. No duplicate keys, no unknown keys, no empty lines.

| key         | req | rule |
|-------------|-----|------|
| id          | yes | 3..24 chars, `[a-z0-9._-]`, starts with a letter. Unique key; installed as `<id>.power` |
| name        | yes | 1..24 printable ASCII, no `/ \`, no leading/trailing space |
| version     | yes | `N.N.N`, each N = 0..65535 (decimal, no leading zeros except "0") |
| author      | no  | 0..32 printable ASCII |
| description | no  | 0..80 printable ASCII |

## TYPE - application type (text, same line rules)
`type=` is required and selects the handler; the remaining keys are validated by that handler.

| type     | keys | DATA |
|----------|------|------|
| textview | `title` (opt, <=32), `wrap` (opt, 0/1, default 1), `accent` (opt, 6 hex digits) | 1..65535 bytes of text: printable ASCII, `\n`, `\t`, `\r` |
| builtin  | `target` (req: files, notepad, photos, display, about), `accent` (opt) | must be empty |

## DATA - application data
Opaque to the installer; its meaning and limits belong to the type handler.

## Limits
Package <= 256 KiB. At most 32 installed applications. At most 4 text-viewer windows. A viewer holds one heap copy
of its text (<= 64 KiB). Every launch re-reads and re-validates the file.

## Install / uninstall guarantees
Install = validate -> check duplicates (by id, and by file name) -> create file -> write -> evict + re-read from
disk and compare (read-after-write) -> registry refresh. Any failure rolls back the created file.
Uninstall = confirmation -> close the app's windows -> delete the file (frees its clusters) -> registry refresh.

## Why there is no native-code type
Executing machine code safely needs ring-3 segments + TSS, an IDT with exception/syscall gates, paging with
per-process address spaces, an executable loader (ELF32), a syscall ABI and a scheduler. NovaOS 0.x has none of these
(single flat ring-0 address space, polling loop, interrupts disabled), so a "loader" would execute untrusted bytes with
full kernel privileges. It is intentionally not provided.


## Worked example

`tools/minimal_pack.py` builds this 107-byte package (`id=tiny.app`, a textview app whose text is `Hi!`):

```
0000  4E 50 4B 47 01 00 30 00 6A 00 00 00 C9 00 A8 DE  NPKG..0.j.......\n0010  30 00 00 00 28 00 00 00 58 00 00 00 0E 00 00 00  0...(...X.......\n0020  66 00 00 00 04 00 00 00 00 00 00 00 00 00 00 00  f...............\n0030  69 64 3D 74 69 6E 79 2E 61 70 70 0A 6E 61 6D 65  id=tiny.app.name\n0040  3D 54 69 6E 79 20 41 70 70 0A 76 65 72 73 69 6F  =Tiny App.versio\n0050  6E 3D 31 2E 30 2E 30 0A 74 79 70 65 3D 74 65 78  n=1.0.0.type=tex\n0060  74 76 69 65 77 0A 48 69 21 0A                    tview.Hi!.
```

Header fields (little-endian):

| offset | value | meaning |
|--------|-------|---------|
| 0x00 | `4E 50 4B 47` | magic "NPKG" |
| 0x04 | 1 | format_version |
| 0x06 | 48 | header_size |
| 0x08 | 106 | total_size (= file size) |
| 0x0C | `DEA800C9` | CRC-32 of the file with this field zeroed |
| 0x10 | 48 | meta_off |
| 0x14 | 40 | meta_len |
| 0x18 | 88 | type_off |
| 0x1C | 14 | type_len |
| 0x20 | 102 | data_off |
| 0x24 | 4 | data_len |
| 0x28 | 0 | 8 reserved bytes |

To write packages from another language: build the three sections, fill the header, compute CRC-32 (the usual
zlib/IEEE CRC-32) over the whole file while the crc field is zero, then store it at offset 12.
`tools/minimal_pack.py` does exactly that in about 25 lines of Python.
