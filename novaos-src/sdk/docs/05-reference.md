# 5. Reference

## app.manifest keys (project file)

| key | applies to | required | rule |
|-----|-----------|----------|------|
| `id` | all | yes | 3-24 chars `a-z 0-9 . _ -`; first char a letter; unique among installed apps |
| `name` | all | yes | 1-24 printable ASCII chars; no `/` or `\\`; no leading/trailing space |
| `version` | all | yes | `N.N.N`; each number 0-65535, no leading zeros (`1.0.0`, not `01.0.0` or `1.0`) |
| `author` | all | no | up to 32 chars |
| `description` | all | no | up to 80 chars |
| `type` | all | yes | `textview` or `builtin` |
| `accent` | all | no | exactly 6 hex digits, no `#` (default `4F7BE8`) |
| `title` | textview | no | window title, up to 32 chars (default: `name`) |
| `wrap` | textview | no | `1` word-wrap (default), `0` show lines as written |
| `data` | textview | yes | path of the text file, relative to the project folder |
| `target` | builtin | yes | `files`, `notepad`, `photos`, `display`, `about` |
| `output` | all | no | `powor` (default) or `appd` (project-only: chooses the file extension) |

Unknown keys, duplicate keys and bad values are errors - the tools and the kernel both refuse them.

## Limits

| what | limit |
|------|-------|
| package file | 256 KB |
| textview text | 65,535 bytes of ASCII (tab = 4 spaces when shown) |
| installed apps | 32 |
| text-viewer windows open at once | 4 (NovaOS also allows 8 windows in total) |
| apps in the dock | first 8 installed (sorted by id); the rest are in the Start menu, which lists 8 and then **More...** |

## Error messages and what to do

| message (from NovaOS or the tools) | meaning / fix |
|-----------------------------------|---------------|
| not a NovaOS package / missing NPKG signature | the file is not a package. Build it with `mkpkg.py pack`. |
| checksum mismatch / package is corrupted | the file was modified after building (or damaged). Rebuild it. |
| unsupported package format version | the package is from a newer/older format than the OS understands. |
| damaged package header / layout | the file was truncated or hand-edited. Rebuild it. |
| meta: required key 'x' is missing | add `id`, `name` and `version`. |
| meta: id must be 3-24 characters ... | fix the `id` (lowercase a-z, digits, `. _ -`, start with a letter). |
| meta: name must be ... | 1-24 chars; no `/` or `\\`; no space at the start/end. |
| meta: version must look like 1.0.0 | three numbers separated by dots. |
| unknown key 'x' | a typo, or a key that belongs to another type (e.g. `target` on a textview app). |
| data: byte 0xNN at line L, column C is not plain ASCII text | remove the character, or build with `--fix-text`. |
| data: a textview app needs between 1 and 65535 bytes of text | empty or too long text file. |
| data: a builtin shortcut must not have any data | remove the `data=` line from a `type=builtin` project. |
| type: builtin needs target=... | add `target=notepad` (or another built-in). |
| unsupported application type | only `textview` and `builtin` exist. |
| An application with this ID is already installed | uninstall the old one first (Start menu x), or use `novadisk.py install --update`. |
| A file with this application's name already exists | something else is called `<id>.power` in the apps folder; remove it. |
| Too many applications are installed | 32 maximum; uninstall some. |
| No space left on the disk | free some space or use a bigger disk image. |
| The installed copy did not verify - installation was rolled back | the disk misbehaved while writing; nothing was installed. Try again. |
| No free application window slots | 4 text apps (or 8 windows) already open; close one. |

## Tool reference

```
mkpkg.py pack PROJECT [-o OUT] [--fix-text]        PROJECT = folder with app.manifest, or the manifest file
mkpkg.py build OUT --id --name --version --type [--data F] [--author A] [--description D] [--title T] [--wrap 0|1] [--accent RRGGBB] [--target T]
mkpkg.py check FILE...                              exit status 1 if any file is invalid
mkpkg.py inspect FILE
novadisk.py create|install|uninstall|apps|ls|put|get|mkdir|rm|check|info   (run with -h for each)
```
