# 2. Your first app (textview)

We will make **"Study Notes"**: a window with some notes in it.

## Step 1 - create the project folder

Copy `templates/textview` to `examples/study-notes` (any name works; `build_all.py` builds every folder under
`examples/`). You now have two files:

```
examples/study-notes/app.manifest
examples/study-notes/content.txt
```

## Step 2 - edit `app.manifest`

```
id=study.notes
name=Study Notes
version=1.0.0
author=Your Name
description=My revision notes
type=textview
accent=E5484D
data=content.txt
```

* `id` is the app's permanent identity: lowercase letters, digits, `.` `_` `-`, 3-24 characters, starting with a
  letter. Pick something unlikely to clash (`yourname.notes`). It also becomes the file name `study.notes.power`.
* `name` is what people see in the Start menu, the dock tooltip and the menu bar.
* `accent` is a colour (6 hex digits, no `#`) used for the app's tile in the dock/Start menu and the stripe down the
  left of its window. Try `2F6FED` (blue), `28A745` (green), `F59E0B` (orange), `8B5CF6` (purple), `E5484D` (red).
* Lines starting with `#` are comments. A value runs to the end of its line, so put comments on their own line.

## Step 3 - write `content.txt`

Just text. The rules:

* **Plain ASCII only** - letters, digits and ordinary punctuation. No accents (e, a), emoji, curly quotes or long dashes.
  If you paste some by accident, build with `--fix-text` and they are converted (curly quotes become `"`, `--` for
  a long dash, anything else becomes `?`).
* Up to **65,535 bytes**.
* Tabs are shown as four spaces. Windows line endings (CRLF) are fine.
* With `wrap=1` (the default) lines are word-wrapped to the window width - write paragraphs as single long lines.
  With `wrap=0` lines are shown exactly as written and anything past the window's right edge is cut off, so keep
  lines under about **60 characters** (the default window is ~64 columns x 21 lines; the user can resize it).

## Step 4 - build it

```
python build_all.py
```

You should see `built examples/dist/study-notes.powor ... id=study.notes`. To build just one project:

```
python tools/mkpkg.py pack examples/study-notes -o study-notes.powor
python tools/mkpkg.py inspect study-notes.powor        # shows what is inside
```

If something is wrong the tool tells you exactly which rule failed, for example
`meta: id must be 3-24 characters of a-z 0-9 . _ - and start with a letter`.

## Step 5 - put it on a NovaOS disk and run it

Close NovaOS first (never edit a disk image while it is running), then:

```
python make_disk.py disk.img            # installs everything in examples/dist/ (updating older versions)
scripts\run-qemu.bat                    # Windows  (Linux/macOS: sh scripts/run-qemu.sh)
```

In NovaOS: **Start -> INSTALLED -> Study Notes**, or click its tile in the dock.

### Prefer installing from inside NovaOS?

Put the package into the disk as a normal file, then install it with the OS's own installer:

```
python tools/novadisk.py put disk.img examples/dist/study-notes.powor /Documents
```

Start NovaOS, open **Files -> Documents**, double-click `study-notes.powor`, press **Install**.

## Step 6 - improve and re-install

Edit, run `python build_all.py`, close NovaOS, run `python make_disk.py disk.img` again. It replaces the older
version automatically. Bump `version=` so you can see which one is installed (the window's status bar shows
`Name vX.Y.Z`).

`make_disk.py` only installs and updates - it never removes. To remove an app use
`python tools/novadisk.py uninstall disk.img study.notes`, or delete `disk.img` and create it again.

## Troubleshooting

See the error table in `05-reference.md`.
