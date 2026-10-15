# 1. How NovaOS apps work

```
  your project folder          package file             installed app              what you see
  -------------------        ---------------          ---------------------      ---------------------
  app.manifest      \         hello.powor    install   /System/Installed       Start menu: INSTALLED
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
