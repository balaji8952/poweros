#!/usr/bin/env python3
"""NovaOS asset generator.

 * build/font.h     8x16 bitmap font (ASCII 32..126)
 * build/aN.bin     every file that gets embedded into the kernel image
 * build/assets.s   assembly table (name, folder, data, size) read by fs_init()

Built-in demo pictures (BMP, PNG and JPEG, so every decoder gets exercised) are
generated here.  Anything you drop into ./assets is embedded too:

    images (png jpg bmp gif webp tiff ico ...)  ->  /Pictures
    everything else                             ->  /Documents

Images are normalised so the kernel's small decoders can always read them:
baseline JPEGs are kept as-is, everything else becomes an 8-bit PNG, and
anything over MAX_PIXELS is scaled down.
"""
import io, os, re, struct, sys
import numpy as np
from PIL import Image, ImageDraw, ImageFont

MAX_PIXELS = 3_000_000          # keep in sync with MAX_IMG_PIXELS in image.h
HERE = os.path.dirname(os.path.abspath(__file__))
os.chdir(HERE)
os.makedirs("build", exist_ok=True)

# ---------------------------------------------------------------- font
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/TTF/DejaVuSansMono.ttf",
    "/Library/Fonts/DejaVuSansMono.ttf",
]
fpath = next((p for p in FONT_CANDIDATES if os.path.exists(p)), None)
if not fpath:
    sys.exit("DejaVuSansMono.ttf not found - install fonts-dejavu-core")
f = ImageFont.truetype(fpath, 13)
rows = []
for code in range(128):
    glyph = [0] * 16
    if 32 < code < 127:
        im = Image.new("L", (8, 16), 0)
        ImageDraw.Draw(im).text((0, 0), chr(code), font=f, fill=255)
        for y in range(16):
            b = 0
            for x in range(8):
                if im.getpixel((x, y)) > 100:
                    b |= 0x80 >> x
            glyph[y] = b
    rows.append("  {" + ",".join("0x%02x" % v for v in glyph) + "}")
with open("build/font.h", "w") as fh:
    fh.write("static const unsigned char font[128][16] = {\n" + ",\n".join(rows) + "\n};\n")


# ------------------------------------------------------- demo pictures
def bmp_bytes(arr):
    h, w, _ = arr.shape
    stride = (w * 3 + 3) & ~3
    pad = stride - w * 3
    data = bytearray()
    for y in range(h - 1, -1, -1):
        row = arr[y][:, ::-1].astype(np.uint8)  # RGB -> BGR
        data += row.tobytes() + b"\0" * pad
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(data), 0, 0, 54)
    dib = struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(data), 2835, 2835, 0, 0)
    return hdr + dib + bytes(data)


W, H = 640, 400
yy, xx = np.mgrid[0:H, 0:W].astype(np.float64)


def sunset():
    t = yy / H
    sky = np.stack([255 * (0.25 + 0.75 * t ** 0.8),
                    255 * (0.10 + 0.55 * t ** 1.4),
                    255 * (0.45 - 0.25 * t)], axis=-1)
    sx, sy = W * 0.62, H * 0.58
    d = np.hypot(xx - sx, yy - sy)
    sun = np.clip((70 - d) / 6, 0, 1)[..., None]
    glow = np.clip(1 - d / 260, 0, 1)[..., None] ** 2
    sky = sky + glow * np.array([120, 70, 10])
    sky = sky * (1 - sun) + sun * np.array([255, 236, 150])
    img = np.clip(sky, 0, 255)
    for k, (col, base, amp, fr) in enumerate([((90, 40, 80), 0.68, 38, 0.011),
                                              ((50, 24, 60), 0.76, 30, 0.017),
                                              ((22, 12, 34), 0.86, 22, 0.027)]):
        ridge = H * base + amp * np.sin(xx[0] * fr + k * 2.1) + amp * 0.5 * np.sin(xx[0] * fr * 2.7 + k)
        mask = (yy >= ridge[None, :])[..., None]
        img = np.where(mask, np.array(col, dtype=float), img)
    return img


def fractal():
    cre = -2.2 + xx / W * 3.2
    cim = -1.0 + yy / H * 2.0
    z = np.zeros_like(cre, dtype=np.complex128)
    c = cre + 1j * cim
    it = np.zeros(cre.shape)
    alive = np.ones(cre.shape, bool)
    for n in range(90):
        z[alive] = z[alive] ** 2 + c[alive]
        esc = alive & (np.abs(z) > 2)
        it[esc] = n + 1 - np.log2(np.log2(np.abs(z[esc]) + 1e-9) + 1e-9)
        alive &= ~esc
    v = np.where(alive, 0, it / 90.0)
    return np.stack([255 * np.clip(v * 3.0, 0, 1) ** 0.8,
                     255 * np.clip(v * 2.0 - 0.15, 0, 1),
                     255 * (0.25 + 0.75 * np.sin(v * 9.0) ** 2) * (v > 0)], axis=-1)


def plasma():
    p = (np.sin(xx / 37.0) + np.sin(yy / 23.0) + np.sin((xx + yy) / 41.0)
         + np.sin(np.hypot(xx - W / 2, yy - H / 2) / 29.0))
    return np.stack([127.5 * (1 + np.sin(p * 1.2)),
                     127.5 * (1 + np.sin(p * 1.2 + 2.1)),
                     127.5 * (1 + np.sin(p * 1.2 + 4.2))], axis=-1)


def aurora():
    w, h = 800, 500
    y, x = np.mgrid[0:h, 0:w].astype(np.float64)
    t = y / h
    img = np.stack([8 + 18 * t, 12 + 26 * t, 30 + 50 * t], axis=-1)
    rng = np.random.default_rng(7)
    stars = rng.random((h, w)) > 0.9985
    img[stars] = 235
    for k, (col, base, amp, fr, wd) in enumerate([((40, 255, 150), 0.38, 40, 0.012, 55),
                                                  ((150, 80, 255), 0.30, 30, 0.017, 40),
                                                  ((60, 220, 255), 0.46, 26, 0.009, 36)]):
        ridge = h * base + amp * np.sin(x * fr + k * 1.7) + amp * 0.6 * np.sin(x * fr * 2.3 + k)
        g = np.exp(-((y - ridge) / wd) ** 2) * (0.55 + 0.45 * np.sin(x * 0.07 + k))
        img = img + g[..., None] * np.array(col) * 0.9
    ground = y > h * 0.86 + 12 * np.sin(x * 0.02)
    img[ground] = (6, 8, 12)
    return np.clip(img, 0, 255)


def png_bytes(arr):
    b = io.BytesIO()
    Image.fromarray(arr.astype(np.uint8)).save(b, "PNG")
    return b.getvalue()


def jpg_bytes(arr, q=88):
    b = io.BytesIO()
    Image.fromarray(arr.astype(np.uint8)).save(b, "JPEG", quality=q)
    return b.getvalue()


# ------------------------------------------------------ asset collection
assets = []          # (name, folder, bytes)
used = set()


def add(name, folder, data):
    base, ext = os.path.splitext(name)
    n, cand = 2, name
    while (folder, cand.lower()) in used:
        cand = "%s %d%s" % (base, n, ext)
        n += 1
    used.add((folder, cand.lower()))
    assets.append((cand, folder, data))


def clean(name):
    name = re.sub(r"[^A-Za-z0-9 ._()\-]", "_", name).strip()
    stem, ext = os.path.splitext(name)
    return stem[: 31 - len(ext)] + ext


add("Sunset.bmp", "Pictures", bmp_bytes(sunset()))
add("Fractal.bmp", "Pictures", bmp_bytes(fractal()))
add("Plasma.png", "Pictures", png_bytes(plasma()))
add("Aurora.jpg", "Pictures", jpg_bytes(aurora()))

# ---- sample NovaOS packages (try: open one in Files -> Install) ----
sys.path.insert(0, os.path.join(HERE, "tools"))
import mkpkg
HELLO = (b"Hello from a NovaOS package!\n\nThis window is a 'textview' application. Its text is the DATA section\n"
         b"of /System/Installed Applications/hello.text.power.\n\nNothing in a package is ever executed - the kernel only\n"
         b"interprets it, so installing one cannot run arbitrary code.\n\nScroll with the mouse wheel, arrows or PgUp/PgDn.\n")
MANUAL = (b"NovaOS package format (NPKG v1)\n\n  header   48 bytes: magic, version, section table, CRC-32\n"
          b"  META     id, name, version, author, description\n  TYPE     the application type and its settings\n"
          b"  DATA     the application's payload\n\nSupported types: textview (this viewer) and builtin (a shortcut to a built-in app).\n"
          b"Install a .powor or .appd by opening it in Files. Remove an app with the x button in the Start menu.\n"
          b"\nFull spec: docs/NOVAPKG_FORMAT.md. Build packages with tools/mkpkg.py.\n")
add("Hello.powor", "Documents", mkpkg.build({"id": "hello.text", "name": "Hello NovaOS", "version": "1.0.0", "author": "NovaOS",
    "description": "Sample text viewer app"}, {"type": "textview", "title": "Hello NovaOS", "accent": "2F6FED"}, HELLO))
add("Manual.appd", "Documents", mkpkg.build({"id": "novaos.manual", "name": "NovaOS Manual", "version": "0.1.0", "author": "NovaOS"},
    {"type": "textview", "accent": "28A745", "wrap": "1"}, MANUAL))
add("Photos Shortcut.appd", "Documents", mkpkg.build({"id": "photos.link", "name": "Photo Album", "version": "1.0.0"},
    {"type": "builtin", "target": "photos", "accent": "E5484D"}, b""))

IMG_EXT = {".png", ".jpg", ".jpeg", ".jpe", ".bmp", ".gif", ".webp", ".tif", ".tiff", ".ico", ".ppm", ".pgm"}
if os.path.isdir("assets"):
    for fn in sorted(os.listdir("assets")):
        p = os.path.join("assets", fn)
        if fn.startswith(".") or not os.path.isfile(p):
            continue
        stem, ext = os.path.splitext(fn)
        if ext.lower() in IMG_EXT:
            try:
                im = Image.open(p)
                im.load()
            except Exception as e:                       # noqa
                print("  skip %s (%s)" % (fn, e))
                continue
            keep_jpeg = (im.format == "JPEG" and not im.info.get("progressive")
                         and im.mode in ("RGB", "L") and im.width * im.height <= MAX_PIXELS)
            if keep_jpeg:
                add(clean(stem + ".jpg"), "Pictures", open(p, "rb").read())
            else:
                if im.mode not in ("RGB", "RGBA", "L"):
                    im = im.convert("RGBA" if "A" in im.getbands() or im.mode == "P" else "RGB")
                if im.width * im.height > MAX_PIXELS:
                    im.thumbnail((1920, 1200))
                b = io.BytesIO()
                im.save(b, "PNG")
                add(clean(stem + ".png"), "Pictures", b.getvalue())
        else:
            add(clean(fn), "Documents", open(p, "rb").read())

# ------------------------------------------------------------ emit asm
out = ['/* generated by gen_assets.py */', '.section .rodata', '.align 4']
for i, (name, folder, data) in enumerate(assets):
    with open("build/a%d.bin" % i, "wb") as fh:
        fh.write(data)
    out += ['.align 4', 'a%d_data: .incbin "build/a%d.bin"' % (i, i), 'a%d_end:' % i,
            'a%d_name: .asciz "%s"' % (i, name), 'a%d_dir: .asciz "%s"' % (i, folder)]
out += ['.align 4', '.global asset_count', 'asset_count: .long %d' % len(assets),
        '.global asset_table', 'asset_table:']
for i in range(len(assets)):
    out.append('  .long a%d_name, a%d_dir, a%d_data, a%d_end - a%d_data' % (i, i, i, i, i))
out.append('.section .note.GNU-stack,"",@progbits')
open("build/assets.s", "w").write("\n".join(out) + "\n")
open("build/assets.txt", "w").write("".join("%s\t%s\tbuild/a%d.bin\n" % (a[0], a[1], i) for i, a in enumerate(assets)))
print("assets ok: %d embedded (%d KB)" % (len(assets), sum(len(a[2]) for a in assets) // 1024))
for a in assets:
    print("  /%s/%s  %d bytes" % (a[1], a[0], len(a[2])))
