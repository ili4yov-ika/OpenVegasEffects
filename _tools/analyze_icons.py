import re, struct, collections

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
data = open(EXE, "rb").read()

# --- UTF-16LE path strings ending in .png / .svg / .ico / qrc-like ---------
# Qt stores resource paths ("/icons/foo.png") as UTF-16LE in rcc tables and
# as QString literals in code. Scan for printable UTF-16 runs containing '/',
# '.' and a known image extension.
REPOPP = re.compile(rb"(?:[^\x00-\x1f\x7f]{1,200}\x00)[.]\x00p\x00n\x00g\x00", re.I)
found = set()
# improved: capture runs of 2-byte chars (ASCII) then see if decoded endswith image ext
for m in re.finditer(rb"(?:[\x20-\x7e]\x00){6,300}", data):
    s = m.group()
    try:
        t = s.decode("utf-16le")
    except Exception:
        continue
    if not t.startswith(("/", ">", ":")):
        continue
    if re.search(r"[.]png$|[.]svg$|[.]ico$|[.]jpg$|[.]jpeg$|[.]gif$|[.]qrc$|[.]qss$", t, re.I):
        found.add(t)
found = sorted(found)
print("resource-name-like strings:", len(found))
for t in found:
    print(" ", t)

# --- dimension histogram ------------------------------------------------------
import os
hist = collections.Counter()
dims = collections.defaultdict(list)
for line in open(r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\icons-VegesEffects\manifest_png.tsv"):
    line = line.strip()
    if not line or line.startswith("sha1"):
        continue
    parts = line.split("\t")
    if len(parts) != 6:
        continue
    sh, off, ln, w, h, fn = parts
    try:
        w, h = int(w), int(h)
    except Exception:
        continue
    if w and h:
        hist[(w, h)] += 1
        dims.get((w, h), []).append(fn)
print("\ndimension histogram (w,h):count")
for k in sorted(hist, key=lambda x: (x[0] * x[1])):
    if hist[k] >= 1:
        print("  %4dx%-4d : %3d" % (k[0], k[1], hist[k]))