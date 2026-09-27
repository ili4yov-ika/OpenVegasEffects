import hashlib, os, re, struct, sys

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
OUT = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\icons-VegesEffects"

PNG_SIG = b"\x89PNG\r\n\x1a\n"
IEND = b"IEND"

data = open(EXE, "rb").read()
print("exe size:", len(data))

def parse_png(start):
    if data[start:start + 8] != PNG_SIG:
        return None
    pos = start + 8
    w = h = None
    while pos + 12 <= len(data):
        try:
            (length,) = struct.unpack(">I", data[pos:pos + 4])
        except struct.error:
            return None
        ctype = data[pos + 4:pos + 8]
        # sanity: length reasonable
        if length > 0x4000000:
            return None
        if ctype == b"IHDR" and length >= 13:
            w, h = struct.unpack(">II", data[pos + 8:pos + 16])
        if ctype == IEND:
            end = pos + 12 + length if length else pos + 12
            return start, end, w, h
        pos += 12 + length
        if pos > start + 4 * 1024 * 1024:
            return None
    return None

pngs = {}
i = 0
while True:
    i = data.find(PNG_SIG, i)
    if i < 0:
        break
    r = parse_png(i)
    if r:
        s, e, w, h = r
        blob = data[s:e]
        hsh = hashlib.sha1(blob).hexdigest()
        if hsh not in pngs:
            pngs[hsh] = (s, e, w, h, blob)
    i += 8

print("unique PNG found:", len(pngs))

png_dir = os.path.join(OUT, "png")
os.makedirs(png_dir, exist_ok=True)
manifest = []
for hsh, (s, e, w, h, blob) in sorted(pngs.items(), key=lambda kv: kv[1][0]):
    fn = "png_%08X_%dx%d.png" % (s, w or 0, h or 0)
    with open(os.path.join(png_dir, fn), "wb") as f:
        f.write(blob)
    manifest.append((hsh, s, e - s, w, h, fn))

with open(os.path.join(OUT, "manifest_png.tsv"), "w") as f:
    f.write("sha1\toffset\tlength\twidth\theight\tfile\n")
    for row in manifest:
        f.write("\t".join(str(x) for x in row) + "\n")

# --- SVG blobs ---------------------------------------------------------
svg_pat = re.compile(rb"<svg\b[^>]{0,400}?>", re.I)
svg_hits = []
m = svg_pat.search(data)
c = 0
while True:
    m = svg_pat.search(data, m.end() if m else 0)
    if not m:
        break
    start = max(0, m.start() - 512)
    # find plausible end: closing </svg>
    end = data.find(b"</svg>", m.end())
    if end >= 0 and end - start < 2 * 1024 * 1024:
        blob = data[m.start():end + 6]
        hsh = hashlib.sha1(blob).hexdigest()
        if hsh not in pngs and hsh not in [x[0] for x in svg_hits]:
            svg_hits.append((hsh, m.start(), len(blob), blob))
    c += 1
    if c > 200000:
        break

print("candidate SVG:", len(svg_hits))

svg_dir = os.path.join(OUT, "svg")
os.makedirs(svg_dir, exist_ok=True)
svg_manifest = []
for hsh, off, ln, blob in svg_hits:
    fn = "svg_%08X.svg" % off
    with open(os.path.join(svg_dir, fn), "wb") as f:
        f.write(blob)
    svg_manifest.append((hsh, off, ln, fn))

with open(os.path.join(OUT, "manifest_svg.tsv"), "w") as f:
    f.write("sha1\toffset\tlength\tfile\n")
    for row in svg_manifest:
        f.write("\t".join(str(x) for x in row) + "\n")

print("done. png:", len(manifest), "svg:", len(svg_manifest))