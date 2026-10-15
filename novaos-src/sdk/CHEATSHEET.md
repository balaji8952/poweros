# NovaOS apps - one-page cheat sheet

```
python tools/mkpkg.py pack  examples/my-app            build -> examples/my-app/<id>.powor
python tools/mkpkg.py pack  examples/my-app --fix-text same, converting curly quotes/dashes to ASCII
python tools/mkpkg.py check  my.powor                  validate (same rules as the kernel)
python tools/mkpkg.py inspect my.powor                 show everything inside

python tools/novadisk.py create  disk.img --size 64M   new disk image (formats it)
python tools/novadisk.py install disk.img my.powor     install   (add --update to replace an older version)
python tools/novadisk.py apps    disk.img              list installed apps
python tools/novadisk.py uninstall disk.img my.app     remove by id
python tools/novadisk.py put disk.img notes.txt /Documents      copy any file into NovaOS
python tools/novadisk.py ls  disk.img /System/Installed\ Applications
python tools/novadisk.py check disk.img                verify the file system
```

**app.manifest keys**

| key | required | rule |
|-----|----------|------|
| `id` | yes | 3-24 chars `a-z 0-9 . _ -`, starts with a letter, unique |
| `name` | yes | 1-24 printable chars, no `/` or `\\`, no space at the ends |
| `version` | yes | `N.N.N`, each 0-65535 |
| `author` | no | up to 32 chars |
| `description` | no | up to 80 chars |
| `type` | yes | `textview` or `builtin` |
| `title` | no (textview) | window title, up to 32 chars |
| `wrap` | no (textview) | `1` word-wrap (default) / `0` never re-flow |
| `accent` | no | 6 hex digits, e.g. `2F6FED` |
| `target` | yes (builtin) | `files` `notepad` `photos` `display` `about` |
| `data` | yes (textview) | text file, plain ASCII, 1-65535 bytes |
| `output` | no | `powor` (default) or `appd` |

**Limits:** package <= 256 KB - 32 installed apps - 4 text-viewer windows open at once.
Dock shows the first 8 installed apps (A-Z by id); the Start menu lists them all.
