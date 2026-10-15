#!/usr/bin/env python3
"""Regenerates the developer-kit sources in ./sdk (docs, examples, templates, scripts) from this script. Run: python3 tools/make_sdk.py"""
import os, shutil, sys, textwrap
SRC = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))     # repository root
OUT = os.path.join(SRC, "sdk")
shutil.rmtree(OUT, ignore_errors=True)
for d in ("docs", "tools", "examples", "templates/textview", "templates/shortcut", "scripts"):
    os.makedirs(os.path.join(OUT, d), exist_ok=True)

def w(path, text):
    p = os.path.join(OUT, path); os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, "w", newline="\n").write(textwrap.dedent(text).lstrip("\n"))

# ------------------------------------------------------------------ tools
shutil.copy(os.path.join(SRC, "tools", "minimal_pack.py"), os.path.join(OUT, "tools", "minimal_pack.py")) if os.path.exists(os.path.join(SRC, "tools", "minimal_pack.py")) else None

w("tools/minimal_pack.py", '''\
    #!/usr/bin/env python3
    """The smallest possible NovaOS package writer: no dependencies, ~25 lines.
    Shows exactly what mkpkg.py does, so you can generate packages from any language."""
    import struct, zlib, sys

    def make_package(meta: dict, typ: dict, data: bytes) -> bytes:
        m = "".join("%s=%s\\n" % kv for kv in meta.items()).encode("ascii")     # META section
        t = "".join("%s=%s\\n" % kv for kv in typ.items()).encode("ascii")      # TYPE section
        total = 48 + len(m) + len(t) + len(data)
        header = (b"NPKG" + struct.pack("<HHII", 1, 48, total, 0)               # magic, version, header size, total, crc(0 for now)
                  + struct.pack("<IIIIII", 48, len(m), 48 + len(m), len(t), 48 + len(m) + len(t), len(data))
                  + b"\\0" * 8)                                                  # reserved
        blob = header + m + t + data
        crc = zlib.crc32(blob) & 0xFFFFFFFF                                      # CRC-32 with the crc field still zero
        return blob[:12] + struct.pack("<I", crc) + blob[16:]

    if __name__ == "__main__":
        pkg = make_package({"id": "tiny.app", "name": "Tiny App", "version": "1.0.0"}, {"type": "textview"}, b"Made without mkpkg!\\n")
        open(sys.argv[1] if len(sys.argv) > 1 else "tiny.powor", "wb").write(pkg)
        print("wrote", len(pkg), "bytes")
''')

# ---------------------------------------------------------------- examples
def example(name, manifest, content=None, cname="content.txt"):
    w("examples/%s/app.manifest" % name, manifest)
    if content is not None: w("examples/%s/%s" % (name, cname), content)

example("hello-world", '''\
    # Hello World - the smallest useful NovaOS app.
    id=hello.world
    name=Hello World
    version=1.0.0
    author=You
    description=My first NovaOS app
    type=textview
    accent=2F6FED
    data=content.txt
''', '''\
    Hello, NovaOS!

    This window is an application you installed.
    Its text lives in a package file (hello-world.powor) that
    the installer validated and copied to
    /System/Installed Applications/hello.world.power

    Try this:
      * scroll with the mouse wheel, arrow keys, PgUp/PgDn
      * resize the window - the text re-wraps
      * open the Start menu: your app is listed under INSTALLED
      * look at the dock: it has its own icon, with the "H" tile
        coloured by the accent= line in app.manifest
''')

example("daily-tips", '''\
    id=daily.tips
    name=Daily Tips
    version=1.2.0
    author=NovaOS SDK
    description=Short tips for getting around NovaOS
    type=textview
    title=Daily Tips
    wrap=1
    accent=28A745
    data=tips.txt
''', '''\
    DAILY TIPS

    1. Double-click a title bar to zoom a window to full size, and double-click again to restore it. Drag the bottom-right corner to resize.

    2. Files remembers where you were: press Backspace to go up one folder, F2 to rename the selected item, Delete to remove it (you are asked first).

    3. Notepad saves with Ctrl+S. If the file has no name yet you are asked for one. Ctrl+Shift+S always asks for a new name (Save As).

    4. The Start menu lists every installed application. Hover one and press the x button to uninstall it. The first eight installed apps also live in the dock.

    5. Click the date in the menu bar to open the calendar. Use the arrow buttons to look at other months.

    6. Photos opens BMP, PNG and baseline JPEG pictures. Use the left and right arrow keys to flip through a folder, and "Set wallpaper" to use the current picture as your desktop.

    7. Everything you save goes to the hard disk (when one is attached), so it is still there next time you start NovaOS.

    8. Want your own app? Write a few lines in app.manifest, run mkpkg.py, then install it with novadisk.py. The SDK documents every step.

    9. Because this app uses wrap=1, long lines like this one are word-wrapped to the width of the window. Resize the window and watch them flow.

    10. Keep your text plain ASCII: letters, digits and normal punctuation. mkpkg.py --fix-text turns curly quotes and dashes into plain ones for you.
''', "tips.txt")

example("keyboard-shortcuts", '''\
    id=keys.reference
    name=Keyboard Shortcuts
    version=1.0.0
    author=NovaOS SDK
    description=Cheat sheet of NovaOS keys
    type=textview
    wrap=0
    accent=F59E0B
    data=keys.txt
''', '''\
    KEYBOARD SHORTCUTS    (wrap=0: lines are not re-flowed)
    ============================================================

    FILES
      Arrows, PgUp/PgDn, Home/End   move the selection
      Enter                         open the selected item
      Backspace                     go up one folder
      F2                            rename
      Delete                        delete (asks first)
      Ctrl+N                        new file
      Ctrl+Shift+N                  new folder

    NOTEPAD
      Ctrl+S                        save
      Ctrl+Shift+S                  save as...
      Ctrl+N                        new document
      Ctrl+O                        open Files to pick a document
      Tab                           inserts four spaces

    PHOTOS
      Left / Right                  previous / next picture

    TEXT VIEWER APPS (like this one)
      Up / Down                     scroll one line
      PgUp, PgDn, Space             scroll ten lines
      Home / End                    jump to top / bottom
      Mouse wheel                   scroll

    EVERYWHERE
      Esc                           close menus and dialogs
      Enter                         confirm the dialog
      Double-click a title bar      zoom / restore the window
''', "keys.txt")

example("ascii-gallery", '''\
    id=ascii.gallery
    name=ASCII Gallery
    version=0.9.0
    author=NovaOS SDK
    description=A few pictures made of characters
    type=textview
    wrap=0
    accent=8B5CF6
    data=art.txt
''', r'''
     ASCII GALLERY  (wrap=0 keeps the pictures intact)

        /\_/\          ___________
       ( o.o )         |  _   _  |         |
        > ^ <          | | | | | |        /_\
       /|   |\         | |_| |_| |       /___\
      (_|   |_)        |  _____  |      |  _  |
                       | |     | |      | |_| |
         |\__/,|   (`\ |_|_____|_|      |_____|
       _.|o o  |_   ) )   |_|_|_|
      ((   ((  ^  ,-'  /_____________\
       ``-'(((/  (((/

        .-.
       (o o)   A friendly robot
       | O |
        `~'
'''.lstrip("\n"), "art.txt")

example("pancakes", '''\
    id=pancake.recipe
    name=Pancakes
    version=1.0.0
    author=Grandma
    description=Fluffy pancakes in 20 minutes
    type=textview
    accent=E5484D
    data=recipe.txt
''', '''\
    FLUFFY PANCAKES            serves 4, 20 minutes

    INGREDIENTS
      - 200 g plain flour
      - 2 tsp baking powder
      - 1 tbsp sugar
      - a pinch of salt
      - 2 eggs
      - 300 ml milk
      - 30 g melted butter, plus a little for the pan

    METHOD
      1. Whisk the flour, baking powder, sugar and salt in a large bowl.
      2. In a jug, beat the eggs with the milk and the melted butter.
      3. Pour the wet mixture into the dry one and stir until just combined. A few lumps are fine - over-mixing makes tough pancakes.
      4. Rest the batter for five minutes while the pan heats over a medium flame.
      5. Melt a little butter, pour in a ladle of batter and cook until bubbles appear on top (about two minutes).
      6. Flip and cook for one more minute until golden.
      7. Keep the finished pancakes warm under a clean tea towel while you cook the rest.

    TOPPINGS
      Maple syrup, berries, sliced banana, a spoon of yoghurt, or just a squeeze of lemon and a sprinkle of sugar.
''', "recipe.txt")

example("notepad-link", '''\
    # A "builtin" app: no text, it just starts a built-in NovaOS app.
    # Saved as .appd to show that .appd and .powor are the same format.
    id=quick.notes
    name=Quick Notes
    version=1.0.0
    description=One click to Notepad
    type=builtin
    target=notepad
    accent=F5B400
    output=appd
''')
example("files-link", '''\
    id=my.files
    name=My Files
    version=1.0.0
    type=builtin
    target=files
    accent=1F6FE5
''')

# --------------------------------------------------------------- templates
w("templates/textview/app.manifest", '''\
    # ===== NovaOS app project (type: textview) =====
    # Build:    python tools/mkpkg.py pack templates/textview -o my-app.powor
    # Comments must be on their own line (they start with #). Every key is explained in docs/05-reference.md.

    # ---- package metadata ----
    # id:          REQUIRED  3-24 chars of a-z 0-9 . _ -  (start with a letter). Must be unique.
    id=my.app
    # name:        REQUIRED  shown in the Start menu, dock and menu bar (1-24 chars)
    name=My App
    # version:     REQUIRED  three numbers, e.g. 1.0.0 (each 0-65535)
    version=1.0.0
    # author / description: optional (up to 32 / 80 chars)
    author=Your Name
    description=What my app does

    # ---- application type ----
    # type: textview = a window that shows text
    type=textview
    # title: optional window title (default: name), up to 32 chars
    title=My App
    # wrap: optional. 1 = word-wrap long lines (default), 0 = never re-flow
    wrap=1
    # accent: optional. 6 hex digits, no '#': colour of the dock/Start tile and the window stripe
    accent=2F6FED

    # ---- application data ----
    # data: the text file to show (plain ASCII, up to 65535 bytes). Path is relative to this folder.
    data=content.txt

    # ---- output ----
    # output: optional. powor (default) or appd - identical format, just a different extension
    # output=powor
''')
w("templates/textview/content.txt", '''\
    Replace this text with your app's content.

    Plain ASCII only: letters, digits and normal punctuation.
    (If you paste curly quotes or long dashes, build with:
       python tools/mkpkg.py pack templates/textview --fix-text)
''')
w("templates/shortcut/app.manifest", '''\
    # ===== NovaOS app project (type: builtin) - a shortcut to a built-in app =====
    # Build: python tools/mkpkg.py pack templates/shortcut -o my-shortcut.appd
    id=my.shortcut
    name=My Shortcut
    version=1.0.0
    type=builtin
    # target: files | notepad | photos | display | about
    target=notepad
    accent=F5B400
    # builtin apps have no data= line
''')

# ----------------------------------------------------------------- scripts
w("build_all.py", '''\
    #!/usr/bin/env python3
    """Builds every project in examples/ (and any folder you add there) into examples/dist/, then validates the results
    with the same rules the NovaOS kernel uses.     Usage:  python build_all.py"""
    import glob, os, sys
    HERE = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(HERE, "tools") if os.path.isdir(os.path.join(HERE, "tools", "mkpkg.py")) or os.path.exists(os.path.join(HERE, "tools", "mkpkg.py")) else os.path.join(HERE, "..", "tools"))
    import mkpkg

    dist = os.path.join(HERE, "examples", "dist")
    os.makedirs(dist, exist_ok=True)
    for old in glob.glob(os.path.join(dist, "*.pow*")) + glob.glob(os.path.join(dist, "*.appd")):
        os.remove(old)
    failed = 0
    for manifest in sorted(glob.glob(os.path.join(HERE, "examples", "*", "app.manifest"))):
        proj = os.path.dirname(manifest); name = os.path.basename(proj)
        try:
            blob, app_id, ext = mkpkg.pack_project(proj, fix=True)
            out = os.path.join(dist, "%s.%s" % (name, ext))
            open(out, "wb").write(blob)
            print("built  %-34s %5d bytes   id=%s" % (os.path.relpath(out, HERE), len(blob), app_id))
        except (mkpkg.PkgError, OSError) as e:
            print("FAILED %s: %s" % (name, e)); failed += 1
    sys.exit(1 if failed else 0)
''')
w("make_disk.py", '''\
    #!/usr/bin/env python3
    """Creates (or updates) a NovaOS disk image and installs every package from examples/dist/ into it.
    Close NovaOS / QEMU first - never edit an image while it is running.
    Usage:  python make_disk.py [disk.img] [--size 64M] [--dir some/folder/with/packages]"""
    import argparse, glob, os, sys
    HERE = os.path.dirname(os.path.abspath(__file__))
    sys.path.insert(0, os.path.join(HERE, "tools") if os.path.isdir(os.path.join(HERE, "tools", "mkpkg.py")) or os.path.exists(os.path.join(HERE, "tools", "mkpkg.py")) else os.path.join(HERE, "..", "tools"))
    import mkpkg
    from novadisk import NovaDisk, DiskError, parse_size

    ap = argparse.ArgumentParser()
    ap.add_argument("image", nargs="?", default="disk.img")
    ap.add_argument("--size", default="64M")
    ap.add_argument("--dir", default=os.path.join(HERE, "examples", "dist"))
    a = ap.parse_args()
    try:
        d = NovaDisk(a.image, create=True, size=parse_size(a.size))
        files = sorted(glob.glob(os.path.join(a.dir, "*.powor")) + glob.glob(os.path.join(a.dir, "*.appd")))
        if not files: sys.exit("no packages found in %s (run: python build_all.py)" % a.dir)
        for f in files:
            blob = open(f, "rb").read()
            new_id = mkpkg.validate(blob)[0]["id"]
            for k, m, _, _ in d.installed():                   # updating: replace an older copy of the same app
                if m and m["id"] == new_id: d.delete(k)
            meta = d.install(blob)
            print("installed %-18s %-22s v%s" % (meta["id"], meta["name"], meta["version"]))
        d.commit()
        problems = d.check()
        if problems: sys.exit("file system check failed: %s" % problems)
        print("\\n%s is ready: %d apps installed, %d KB free. Boot NovaOS with it (see scripts/run-qemu.*)." % (a.image, len(d.installed()), d.free_clusters() * 4))
    except (DiskError, mkpkg.PkgError) as e:
        sys.exit("error: %s" % e)
''')
w("scripts/make.bat", '''\
    @echo off
    rem Builds all example apps and installs them into disk.img  (needs Python 3.8+)
    cd /d "%~dp0\\.."
    python build_all.py || py build_all.py
    python make_disk.py disk.img || py make_disk.py disk.img
    pause
''')
w("scripts/run-qemu.bat", '''\
    @echo off
    rem Starts NovaOS with disk.img attached.  Adjust the QEMU path if it is not on your PATH.
    cd /d "%~dp0\\.."
    rem Add  -accel whpx  for much better speed if "Windows Hypervisor Platform" is enabled in Windows Features.
    qemu-system-i386.exe -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime
''')
w("scripts/make.sh", '''\
    #!/bin/sh
    # Builds all example apps and installs them into disk.img  (needs Python 3.8+)
    cd "$(dirname "$0")/.." || exit 1
    python3 build_all.py && python3 make_disk.py disk.img
''')
w("scripts/run-qemu.sh", '''\
    #!/bin/sh
    cd "$(dirname "$0")/.." || exit 1
    exec qemu-system-i386 -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime "$@"
''')
for f in ("scripts/make.sh", "scripts/run-qemu.sh", "build_all.py", "make_disk.py", "tools/mkpkg.py", "tools/novadisk.py", "tools/minimal_pack.py"):
    if os.path.exists(os.path.join(OUT, f)): os.chmod(os.path.join(OUT, f), 0o755)

# ------------------------------------------------------------------- docs
sys.path.insert(0, os.path.join(SRC, "tools"))
import mkpkg, struct

w("README.md", """\
    # NovaOS App Developer Kit

    Make your own apps for NovaOS and install them as `.power` applications.

    ## 5-minute quick start

    You need **Python 3.8+** (nothing else to install) and the `NovaOS.iso` + QEMU you already use.

    ```
    1. python build_all.py                      builds the example apps into examples/dist/
    2. python make_disk.py disk.img             creates disk.img and installs the examples into it
    3. scripts\\run-qemu.bat                     (Windows)   or   sh scripts/run-qemu.sh   (Linux/macOS)
    ```

    NovaOS starts; open the **Start** menu - your apps are listed under **INSTALLED**, and they are in the **dock**
    after the divider. Click one to launch it.

    > On Windows use `python` (or `py`); on Linux/macOS use `python3`. Wildcards like `*.powor` work on every system.

    ## Make your own app in three steps

    ```
    1. copy the folder templates\\textview to examples\\my-app      (any folder name)
    2. edit examples\\my-app\\app.manifest   (id, name, ...)   and   examples\\my-app\\content.txt   (your text)
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
""")

w("CHEATSHEET.md", """\
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
    python tools/novadisk.py ls  disk.img /System/Installed\\ Applications
    python tools/novadisk.py check disk.img                verify the file system
    ```

    **app.manifest keys**

    | key | required | rule |
    |-----|----------|------|
    | `id` | yes | 3-24 chars `a-z 0-9 . _ -`, starts with a letter, unique |
    | `name` | yes | 1-24 printable chars, no `/` or `\\\\`, no space at the ends |
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
""")

w("docs/01-how-it-works.md", """\
    # 1. How NovaOS apps work

    ```
      your project folder          package file             installed app              what you see
      -------------------        ---------------          ---------------------      ---------------------
      app.manifest      \\         hello.powor    install   /System/Installed       Start menu: INSTALLED
      content.txt        >--pack-> (or .appd)  -------->   Applications/          + a dock icon
                        /          validated               hello.world.power        + a window when launched
    ```

    ## The three parts of every app

    A NovaOS app package keeps three things strictly apart:

    1. **Package metadata (META)** - who/what the app is: `id`, `name`, `version`, `author`, `description`.
    2. **Application type (TYPE)** - *how* NovaOS should run it: `type=textview` or `type=builtin`, plus the
       settings of that type (`wrap`, `accent`, `target`, ...).
    3. **Application data (DATA)** - the payload that the type works on: for `textview`, your text.

    `app.manifest` in your project holds parts 1 and 2; `data=` points at part 3. The `pack` command glues them into
    one checked file.

    ## Install, launch, uninstall

    * **Install** - inside NovaOS open the `.powor`/`.appd` in **Files** (double-click it). A dialog shows the name,
      version, author, type and size; press **Install**. NovaOS validates every field, copies the package to
      `/System/Installed Applications/<id>.power`, reads it back from the disk to verify it, and adds it to the registry.
      If anything fails, nothing is left behind.
    * **Launch** - click the app in the **Start menu** (INSTALLED section) or in the **dock**, or open its `.power`
      file in Files. NovaOS re-checks the file every time it launches.
    * **Uninstall** - hover the app in the Start menu and press the red **x** (you are asked to confirm). Its windows
      are closed and its file is deleted. (Selecting a `.power` file in Files and pressing Delete does the same.)
    * Installing an app whose `id` is already installed is refused. To update: uninstall, then install the new version
      (or use `novadisk.py install --update` while NovaOS is off).

    ## Where things are

    * `/System/Installed Applications/` - the installed apps. The folder is protected in Files (it can't be
      renamed or deleted by accident).
    * Your packages are normal files: keep them in `/Documents` or anywhere else until you install them.

    ## Why packages are data, not programs

    NovaOS runs everything in one address space with full privileges. If it executed code from a package, any package
    could take over the machine. So packages are *interpreted*: the kernel reads your manifest and your text and
    decides what to do with them, and the worst a bad package can do is be rejected. See `07-faq.md`.
""")

w("docs/02-your-first-app.md", """\
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
    scripts\\run-qemu.bat                    # Windows  (Linux/macOS: sh scripts/run-qemu.sh)
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
""")

w("docs/03-shortcut-apps.md", """\
    # 3. Shortcut apps (type `builtin`)

    A `builtin` app has **no data**. It is a named, coloured launcher for one of NovaOS's built-in apps - handy to put
    "Notes" or "Photos" in the dock with your own name and colour.

    ```
    id=quick.notes
    name=Quick Notes
    version=1.0.0
    type=builtin
    target=notepad
    accent=F5B400
    ```

    `target` must be one of `files`, `notepad`, `photos`, `display`, `about`. There is no `data=` line (a builtin
    package with data is rejected). Build it like any other app; examples: `examples/notepad-link`, `examples/files-link`.

    Notes
    * Launching starts that built-in app, or brings its window to the front if it is already open.
    * A shortcut can only start built-in apps - it cannot start another installed app.
    * `.appd` and `.powor` are the same format. `output=appd` in the project manifest just chooses the extension
      (see `examples/notepad-link`).
""")

w("docs/04-getting-apps-into-novaos.md", """\
    # 4. Getting apps into NovaOS

    NovaOS has no network, USB or clipboard yet, so files reach it through its **disk image**. `tools/novadisk.py` reads
    and writes that image from your PC.

    > **Always close NovaOS/QEMU before running novadisk.** Editing an image that is in use will corrupt it.

    ## Make a disk

    ```
    python tools/novadisk.py create disk.img --size 64M
    ```

    Any size from 1 MB works; NovaOS uses at most the first 256 MB. A blank file that NovaOS has never seen is also fine:
    NovaOS formats a completely blank disk itself on first boot (and asks before touching a disk that holds other data).
    novadisk refuses to touch images that contain unknown data.

    ## Install apps straight into the disk

    ```
    python tools/novadisk.py install disk.img examples/dist/*.powor examples/dist/*.appd
    python tools/novadisk.py install disk.img my-app.powor --update      # replace an older version
    python tools/novadisk.py apps disk.img                                 # list what is installed
    python tools/novadisk.py uninstall disk.img my.app
    ```

    These validate the package with the kernel's rules, refuse duplicates, write
    `/System/Installed Applications/<id>.power`, read it back to check it, and verify the whole file system.

    ## Or hand NovaOS the package and let it install it

    ```
    python tools/novadisk.py put disk.img my-app.powor /Documents
    ```

    Then in NovaOS: Files -> Documents -> double-click `my-app.powor` -> Install. (This is the same path a user takes.)

    ## Other file commands

    ```
    python tools/novadisk.py ls disk.img /Documents
    python tools/novadisk.py put disk.img photo.png /Pictures          # PNG/JPEG/BMP show up in Photos
    python tools/novadisk.py get disk.img /Documents/notes.txt notes.txt
    python tools/novadisk.py mkdir disk.img /Documents/Projects
    python tools/novadisk.py rm disk.img /Documents/old.txt
    python tools/novadisk.py check disk.img
    ```

    ## Starting NovaOS with the disk

    ```
    qemu-system-i386 -cdrom NovaOS.iso -hda disk.img -boot d -m 128 -vga std -rtc base=localtime
    ```

    (`scripts/run-qemu.bat` / `.sh` run exactly this.) Add `-accel whpx` on Windows for speed if the Windows Hypervisor
    Platform feature is on. NovaOS reads IDE disks only: in VirtualBox attach the image to the **IDE (PIIX4)** controller.

    ## Baking apps into the OS image itself (advanced)

    If you build NovaOS from source, drop `.powor`/`.appd` files into the source tree's `assets/` folder before running
    `build.sh`; they appear in `/Documents` on a freshly formatted disk. For everyone else the disk-image route above is
    simpler and doesn't need a rebuild.
""")

w("docs/05-reference.md", """\
    # 5. Reference

    ## app.manifest keys (project file)

    | key | applies to | required | rule |
    |-----|-----------|----------|------|
    | `id` | all | yes | 3-24 chars `a-z 0-9 . _ -`; first char a letter; unique among installed apps |
    | `name` | all | yes | 1-24 printable ASCII chars; no `/` or `\\\\`; no leading/trailing space |
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
    | meta: name must be ... | 1-24 chars; no `/` or `\\\\`; no space at the start/end. |
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
""")

# --- binary format chapter: spec + a worked example generated from a real package
spec = open(os.path.join(SRC, "docs", "NOVAPKG_FORMAT.md")).read()
pk = mkpkg.build({"id": "tiny.app", "name": "Tiny App", "version": "1.0.0"}, {"type": "textview"}, b"Hi!\n")
rows = []
for off in range(0, len(pk), 16):
    chunk = pk[off:off + 16]
    rows.append("%04X  %-47s  %s" % (off, " ".join("%02X" % b for b in chunk), "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)))
ml = len(b"id=tiny.app\\nname=Tiny App\\nversion=1.0.0\\n")
hdr = struct.unpack_from("<HHIIIIIIII", pk, 4)
example = """

## Worked example

`tools/minimal_pack.py` builds this 107-byte package (`id=tiny.app`, a textview app whose text is `Hi!`):

```
""" + "\\n".join(rows) + """
```

Header fields (little-endian):

| offset | value | meaning |
|--------|-------|---------|
| 0x00 | `4E 50 4B 47` | magic "NPKG" |
| 0x04 | %d | format_version |
| 0x06 | %d | header_size |
| 0x08 | %d | total_size (= file size) |
| 0x0C | `%08X` | CRC-32 of the file with this field zeroed |
| 0x10 | %d | meta_off |
| 0x14 | %d | meta_len |
| 0x18 | %d | type_off |
| 0x1C | %d | type_len |
| 0x20 | %d | data_off |
| 0x24 | %d | data_len |
| 0x28 | 0 | 8 reserved bytes |

To write packages from another language: build the three sections, fill the header, compute CRC-32 (the usual
zlib/IEEE CRC-32) over the whole file while the crc field is zero, then store it at offset 12.
`tools/minimal_pack.py` does exactly that in about 25 lines of Python.
""" % (hdr[0], hdr[1], hdr[2], hdr[3], hdr[4], hdr[5], hdr[6], hdr[7], hdr[8], hdr[9])
w("docs/06-binary-format.md", spec.replace("# NovaOS package format", "# 6. NovaOS package format", 1) + example)

w("docs/07-faq.md", """\
    # 7. FAQ and limits

    **Can I write an app with real code (C, assembly, a script)?**
    Not yet - and that is deliberate. NovaOS runs everything in a single address space at the highest privilege level
    (ring 0) with no memory protection, no process model and no system-call layer. A "native app" would be able to
    overwrite the kernel, the disk driver or other apps, and a malicious package would own the machine. Doing it
    properly needs: user-mode (ring 3) segments and a task-state segment, an interrupt table with exception and
    system-call gates, paging with per-app address spaces, an executable loader, a defined system-call interface and a
    scheduler. NovaOS has none of those yet, so the format only allows *interpreted* application types.

    **How could code come later?**
    The format already separates *type* from *data*. A future type (for example a small, sandboxed script or bytecode
    interpreter running inside the kernel with strict limits) can be added without changing existing packages: old
    NovaOS versions simply reject the unknown type, new ones run it.

    **What's the difference between .powor, .appd and .power?**
    `.powor` and `.appd` are interchangeable names for a package you can share and install. `.power` is the installed
    copy in `/System/Installed Applications/`. The kernel checks the *contents* (signature + checksum), never the extension.

    **Why does my package say it is corrupted?**
    Any change to the file after `mkpkg.py` built it breaks the checksum - including opening it in an editor. Always
    rebuild instead of editing a package.

    **Why plain ASCII only?**
    NovaOS's built-in font covers printable ASCII. Accepting other bytes would show garbage, so the validator refuses
    them (and `--fix-text` converts the common ones).

    **Can an app have its own icon?**
    Not yet; every app gets a tile in its `accent` colour with the first letter of its name.

    **Can apps talk to each other / read files / use the network?**
    No. A textview app is a viewer for its own text. That is what keeps installing a package safe.

    **What happens if I delete the `.power` file in Files?**
    NovaOS notices and removes the app from the Start menu and dock (use the Start menu x for the proper uninstall
    with confirmation and window cleanup).

    **Is a disk image safe to edit while NovaOS runs?**
    No. Always shut NovaOS down first.

    **Where is the file-system format described?**
    `tools/novadisk.py` is a complete, readable implementation (about 300 lines). The package format is in
    `06-binary-format.md`.
""")
print("docs written")

