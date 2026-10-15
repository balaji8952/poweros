# NovaOS App Developer Kit

Make your own apps for NovaOS and install them as `.power` applications.

## 5-minute quick start

You need **Python 3.8+** (nothing else to install) and the `NovaOS.iso` + QEMU you already use.

```
1. python build_all.py                      builds the example apps into examples/dist/
2. python make_disk.py disk.img             creates disk.img and installs the examples into it
3. scripts\run-qemu.bat                     (Windows)   or   sh scripts/run-qemu.sh   (Linux/macOS)
```

NovaOS starts; open the **Start** menu - your apps are listed under **INSTALLED**, and they are in the **dock**
after the divider. Click one to launch it.

> On Windows use `python` (or `py`); on Linux/macOS use `python3`. Wildcards like `*.powor` work on every system.

## Make your own app in three steps

```
1. copy the folder templates\textview to examples\my-app      (any folder name)
2. edit examples\my-app\app.manifest   (id, name, ...)   and   examples\my-app\content.txt   (your text)
3. python build_all.py && python make_disk.py disk.img         then start NovaOS
```

Full walkthrough: **docs/02-your-first-app.md**.

## What is in this kit

| path | what |
|------|------|
| `docs/` | the guide (start with 01 and 02) |
| `tools/mkpkg.py` | builds, checks and inspects packages |
| `tools/novadisk.py` | puts apps and files onto a NovaOS disk image |
| `tools/minimal_pack.py` | 25-line dependency-free package writer (to see how the format works) |
| `templates/` | starting points: a text app and a shortcut app |
| `examples/` | seven finished apps with their sources; `examples/dist/` has the built packages |
| `build_all.py`, `make_disk.py`, `scripts/` | one-command build / install / run helpers |
| `NovaOS.iso` | the NovaOS image to boot (same one as before, with the dock + installer) |

## What kind of apps can I make?

Today NovaOS can run two **application types**. A package is *data* that the kernel interprets - it never
runs machine code from a package (see docs/07-faq.md for why).

* **textview** - a window that shows your text: manuals, recipes, cheat sheets, ASCII art, notes, help pages.
* **builtin** - a shortcut that starts one of the built-in apps (Files, Notepad, Photos, Display, About).

## Names you will see

* `.powor` and `.appd` - a *package file* you can hand around and install (same format, two extensions).
* `.power` - the *installed* form, stored in `/System/Installed Applications/` inside NovaOS.
