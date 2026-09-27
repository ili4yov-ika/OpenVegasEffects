import re, os, collections
from PIL import Image

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
OUT = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\icons-VegesEffects"
PNGD = os.path.join(OUT, "png")

data = open(EXE, "rb").read()

# ---- full name inventory from the UTF-16 pool ---------------------------------
names = set()
for m in re.finditer(rb"(?:[\x09\x20-\x7e]\x00){4,260}", data):
    t = m.group().decode("utf-16le")
    if re.search(r"[.](?:png|svg|ico|jpg|jpeg|gif|qss|qrc|webp|tga|bmp)$", t, re.I):
        names.add(t)
names = sorted(names)
with open(os.path.join(OUT, "icons_inventory.txt"), "w", encoding="utf-8") as f:
    for n in names:
        f.write(n + "\n")
print("names:", len(names))

# family histogram (strip @2x, -hover/-checked/-disabled state suffixes)
fam = collections.Counter()
for n in names:
    core = re.sub(r"@2x$", "", n)
    base = re.sub(r"\.\w+$", "", core)
    base = re.sub(r"-(hover|checked|disabled|selected|active)$", "", base, flags=re.I)
    token = re.split(r"[-_0-9]", base)[0]
    fam[token.lower()] += 1
print("\ntop families:")
for k, c in fam.most_common(60):
    print("  %-16s %4d" % (k, c))

# ---- montages -------------------------------------------------------------------
rows = []
for line in open(os.path.join(OUT, "manifest_png.tsv")):
    line = line.strip()
    if not line or line.startswith("sha1"):
        continue
    p = line.split("\t")
    if len(p) != 6:
        continue
    sh, off, ln, w, h, fn = p
    try:
        w, h = int(w), int(h)
    except Exception:
        continue
    if w and h:
        rows.append((w, h, os.path.join(PNGD, fn)))

by_size = collections.defaultdict(list)
for w, h, fn in rows:
    by_size[(w, h)].append(fn)

def montage(items, scale, cols, target_dir, prefix, label_id=False):
    os.makedirs(target_dir, exist_ok=True)
    cell = max(1, max(i[0] for i in items) * scale)
    rows_n = (len(items) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * cell, rows_n * cell), (40, 40, 40, 255))
    n = 0
    for w, h, fn in items:
        im = Image.open(fn).convert("RGBA")
        im = im.resize((w * scale, h * scale), Image.LANCZOS)
        r = n // cols
        c = n % cols
        x = c * cell + (cell - w * scale) // 2
        y = r * cell + (cell - h * scale) // 2
        sheet.paste(im, (x, y), im)
        n += 1
    op = os.path.join(target_dir, "%s.png" % prefix)
    sheet.save(op)
    return op

# 16x16
s16 = sorted([(w, h, f) for w, h, f in rows if (w, h) == (16, 16)], key=lambda t: t[2])
for i in range(0, len(s16), 200):
    montage(s16[i:i + 200], 3, 20, os.path.join(OUT, "contact16"), "sheet16_%02d" % (i // 200))
print("16x16 sheets:", (len(s16) + 199) // 200, "icons:", len(s16))

# 32x32
s32 = sorted([(w, h, f) for w, h, f in rows if (w, h) == (32, 32)], key=lambda t: t[2])
for i in range(0, len(s32), 200):
    montage(s32[i:i + 200], 2, 20, os.path.join(OUT, "contact32"), "sheet32_%02d" % (i // 200))
print("32x32 sheets:", (len(s32) + 199) // 200, "icons:", len(s32))

# 64x64 + rest <= 128
s64 = sorted([(w, h, f) for w, h, f in rows if (w, h) == (64, 64)], key=lambda t: t[2])
montage(s64, 1, 20, os.path.join(OUT, "contact64"), "sheet64_00")
print("64x64:", len(s64))

misc = sorted([(w, h, f) for w, h, f in rows if (w, h) not in [(16, 16), (32, 32), (64, 64)]], key=lambda t: (t[0], t[1]))
for i in range(0, len(misc), 60):
    scale = max(1, 48 // max(misc[i][0], misc[i][1]))
    montage(misc[i:i + 60], scale, 12, os.path.join(OUT, "contact_misc"), "sheetmisc_%02d" % (i // 60))
print("misc:", len(misc), "sheets:", (len(misc) + 59) // 60)