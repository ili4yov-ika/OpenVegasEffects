import re, collections

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
data = open(EXE, "rb").read()

names = collections.Counter()
for m in re.finditer(rb"(?:[\x09\x20-\x7e]\x00){4,260}", data):
    s = m.group()
    try:
        t = s.decode("utf-16le")
    except Exception:
        continue
    if re.search(r"[.](?:png|svg|ico|jpg|jpeg|gif|qss|qrc|webp|tga|bmp)$", t, re.I):
        names[t] += 1

print("total utf16 name hits:", len(names))
for t, c in sorted(names.items(), key=lambda kv: -kv[1]):
    print("  %4d  %s" % (c, t))