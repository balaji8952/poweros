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
