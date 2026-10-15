#!/usr/bin/env python3
"""Developer-kit tests: follows the SDK README Quick Start in a staged copy (like the zip a user receives) and checks every promise."""
import glob, os, shutil, subprocess, sys
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
STAGE = os.path.join(ROOT, "build", "sdk_stage")
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkpkg
fails = 0
def check(cond, msg):
    global fails
    print(("  ok   " if cond else "  FAIL ") + msg)
    if not cond: fails += 1
def run(*args, cwd=STAGE, expect=0):
    r = subprocess.run([sys.executable] + list(args), cwd=cwd, capture_output=True, text=True)
    if r.returncode != expect: print(r.stdout, r.stderr)
    return r

# stage exactly what the zip contains: sdk/ + tools/*.py
shutil.rmtree(STAGE, ignore_errors=True)
shutil.copytree(os.path.join(ROOT, "sdk"), STAGE)
os.makedirs(os.path.join(STAGE, "tools"), exist_ok=True)
for f in ("mkpkg.py", "novadisk.py", "minimal_pack.py"): shutil.copy(os.path.join(ROOT, "tools", f), os.path.join(STAGE, "tools", f))

print("[Quick start step 1: python build_all.py]")
r = run("build_all.py"); check(r.returncode == 0 and r.stdout.count("built ") == 7, "builds all 7 examples")
dist = sorted(os.listdir(os.path.join(STAGE, "examples", "dist")))
check(dist == ["ascii-gallery.powor", "daily-tips.powor", "files-link.powor", "hello-world.powor", "keyboard-shortcuts.powor", "notepad-link.appd", "pancakes.powor"], "dist/ has the expected packages (one .appd, rest .powor): %s" % dist)
r = run("tools/mkpkg.py", "check", "examples/dist/*"); check(r.returncode == 0 and r.stdout.count("OK") == 7, "mkpkg check (with a wildcard, as on Windows) accepts all")

print("[Quick start step 2: python make_disk.py disk.img]")
r = run("make_disk.py", "disk.img", "--size", "16M"); check(r.returncode == 0 and "7 apps installed" in r.stdout, "disk created and 7 apps installed")
r = run("tools/novadisk.py", "apps", "disk.img"); check(r.stdout.count("textview") == 5 and r.stdout.count("builtin") == 2, "apps lists 5 textview + 2 builtin")
r = run("make_disk.py", "disk.img", "--size", "16M"); check(r.returncode == 0 and "7 apps installed" in r.stdout, "re-running make_disk updates in place (still 7, no duplicates)")
r = run("tools/novadisk.py", "check", "disk.img"); check(r.returncode == 0, "novadisk check: file system consistent")

print("[docs/02: the three-step custom app flow]")
shutil.copytree(os.path.join(STAGE, "templates", "textview"), os.path.join(STAGE, "examples", "study-notes"))
mf = os.path.join(STAGE, "examples", "study-notes", "app.manifest")
t = open(mf).read().replace("id=my.app", "id=study.notes").replace("name=My App", "name=Study Notes").replace("title=My App", "title=Study Notes").replace("accent=2F6FED", "accent=E5484D")
open(mf, "w").write(t)
r = run("build_all.py"); check(r.returncode == 0 and "study.notes" in r.stdout, "the TEMPLATE builds unchanged-in-structure after editing id/name (docs/02)")
r = run("make_disk.py", "disk.img", "--size", "16M"); check("8 apps installed" in r.stdout, "custom app installed alongside the examples")
r = run("tools/mkpkg.py", "inspect", "examples/dist/study-notes.powor"); check("Study Notes" in r.stdout and "E5484D" in r.stdout, "inspect shows the manifest values")
r = run("tools/novadisk.py", "uninstall", "disk.img", "study.notes"); check(r.returncode == 0, "documented removal of the custom app: novadisk uninstall")
r = run("tools/novadisk.py", "apps", "disk.img"); check(r.stdout.count("v") >= 7 and "study.notes" not in r.stdout, "custom app is gone, examples remain")
shutil.rmtree(os.path.join(STAGE, "examples", "study-notes")); run("build_all.py")

print("[templates build as shipped]")
for tpl, out in (("templates/textview", "t1.powor"), ("templates/shortcut", "t2.appd")):
    r = run("tools/mkpkg.py", "pack", tpl, "-o", out); check(r.returncode == 0, "pack %s" % tpl)
    r = run("tools/mkpkg.py", "check", out); check(r.returncode == 0, "  result validates")

print("[docs claims about text rules]")
bad = os.path.join(STAGE, "badproj"); os.makedirs(bad)
open(os.path.join(bad, "app.manifest"), "w").write("id=bad.curly\nname=Curly\nversion=1.0.0\ntype=textview\ndata=c.txt\n")
open(os.path.join(bad, "c.txt"), "wb").write("It\u2019s \u201cfancy\u201d \u2014 caf\u00e9\r\nsecond".encode("utf-8"))
r = run("tools/mkpkg.py", "pack", "badproj", expect=1); check(r.returncode == 1 and "line 1, column 3" in (r.stdout + r.stderr) and "--fix-text" in (r.stdout + r.stderr), "non-ASCII text is refused with line/column and a --fix-text hint")
r = run("tools/mkpkg.py", "pack", "badproj", "--fix-text", "-o", "fixed.powor"); check(r.returncode == 0, "--fix-text converts it")
m, ty, d = mkpkg.validate(open(os.path.join(STAGE, "fixed.powor"), "rb").read())
check(d == b"It's \"fancy\" -- caf?\nsecond", "converted text is exactly as documented: %r" % d)
for body, msg in (("id=Bad\nname=X\nversion=1.0.0\ntype=builtin\ntarget=files\n", "id must be"), ("id=ok.id\nname=X\nversion=1.0\ntype=builtin\ntarget=files\n", "version must look like"),
                  ("id=ok.id\nname=X\nversion=1.0.0\ntype=builtin\n", "needs target"), ("id=ok.id\nname=X\nversion=1.0.0\ntype=textview\ndata=nofile\n", "nofile"),
                  ("id=ok.id\nname=X\nversion=1.0.0\ntype=elf\n", "unsupported application type"), ("id=ok.id\nname=X\nversion=1.0.0\nflavour=x\ntype=builtin\ntarget=files\n", "unknown key")):
    open(os.path.join(bad, "app.manifest"), "w").write(body)
    r = run("tools/mkpkg.py", "pack", "badproj", expect=1); check(r.returncode != 0 and msg in (r.stdout + r.stderr), "error mentions %r" % msg)

print("[wrap=0 examples keep lines within the default window (documented: under ~60 columns)]")
for ex in ("keyboard-shortcuts", "ascii-gallery"):
    pk = open(os.path.join(STAGE, "examples", "dist", ex + ".powor"), "rb").read()
    d = mkpkg.validate(pk)[2].decode().splitlines()
    check(max(map(len, d)) <= 62, "%s: longest line %d chars" % (ex, max(map(len, d))))

print("[minimal_pack.py: the dependency-free writer produces a valid package]")
r = run("tools/minimal_pack.py", "tiny.powor"); check(r.returncode == 0 and mkpkg.validate(open(os.path.join(STAGE, "tiny.powor"), "rb").read())[0]["id"] == "tiny.app", "valid package from 25 lines of Python")

print("[docs: every command in the cheat sheet that we can run offline works]")
r = run("tools/novadisk.py", "put", "disk.img", "tiny.powor", "/Documents"); check(r.returncode == 0, "put")
r = run("tools/novadisk.py", "ls", "disk.img", "/Documents"); check("tiny.powor" in r.stdout, "ls")
r = run("tools/novadisk.py", "get", "disk.img", "/Documents/tiny.powor", "back.powor"); check(open(os.path.join(STAGE, "back.powor"), "rb").read() == open(os.path.join(STAGE, "tiny.powor"), "rb").read(), "get returns identical bytes")
r = run("tools/novadisk.py", "mkdir", "disk.img", "/Documents/Projects"); check(r.returncode == 0, "mkdir")
r = run("tools/novadisk.py", "rm", "disk.img", "/Documents/tiny.powor"); check(r.returncode == 0, "rm")
r = run("tools/novadisk.py", "uninstall", "disk.img", "pancake.recipe"); check(r.returncode == 0, "uninstall by id")
r = run("tools/novadisk.py", "uninstall", "disk.img", "no.such.app", expect=1); check(r.returncode == 1 and "no installed app" in r.stderr, "uninstall of unknown id is a clear error")
r = run("tools/novadisk.py", "install", "disk.img", "examples/dist/hello-world.powor", expect=1); check(r.returncode == 1 and "already installed" in r.stderr, "duplicate install refused with an explanation")
r = run("tools/novadisk.py", "install", "disk.img", "examples/dist/pancakes.powor"); check(r.returncode == 0, "reinstall after uninstall")
r = run("tools/novadisk.py", "check", "disk.img"); check(r.returncode == 0, "file system still consistent")
r = run("tools/novadisk.py", "info", "disk.img"); check("7 apps installed" in r.stdout, "info: " + r.stdout.strip())
# docs/02 step 6 and the docs' disk size claim: sizes from 1 MB work
r = run("tools/novadisk.py", "create", "small.img", "--size", "1M"); check(r.returncode == 0, "1 MB image can be created")
r = run("tools/novadisk.py", "install", "small.img", "examples/dist/hello-world.powor"); check(r.returncode == 0, "...and used")
# the kernel test mounts this fresh 7-app image
shutil.copy(os.path.join(STAGE, "disk.img"), os.path.join(ROOT, "build", "sdkdisk.img"))
print("\n%s (%d failures)" % ("SDK TESTS PASSED" if not fails else "SDK TESTS FAILED", fails))
sys.exit(1 if fails else 0)
