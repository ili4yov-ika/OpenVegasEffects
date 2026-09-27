import re, struct, binascii

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
data = open(EXE, "rb").read()

# fixed: proper rcc name-entry layout detection
total = 0
ranges = []
i = 0
while True:
    i = data.find(b".\x00p\x00n\x00g\x00", i)
    if i < 0:
        break
    # try len field candidates: 4 bytes before string start, where string start = i - (len-4)*2
    hits = []
    for k in range(-16, 0, 1):
        lf = i + k
        try:
            (ln,) = struct.unpack("<I", data[lf:lf + 4])
        except Exception:
            continue
        if not (3 <= ln <= 260):
            continue
        str_begin = lf + 4
        str_end = str_begin + ln * 2
        if str_end <= len(data) and str_end - 8 == i:
            # verify printable
            try:
                nm = data[str_begin:str_end].decode("utf-16le")
            except Exception:
                continue
            if nm.endswith(".png"):
                hits.append((ln, lf, nm))
    total += len(hits)
    i += 4

print("hits:", total)
# Aggregate: count unique (ln, lf) and look for the main cluster
# Re-run, collecting positions
positions = []
i = 0
while True:
    i = data.find(b".\x00p\x00n\x00g\x00", i)
    if i < 0:
        break
    for k in range(-16, 0, 1):
        lf = i + k
        try:
            (ln,) = struct.unpack("<I", data[lf:lf + 4])
        except Exception:
            continue
        if not (3 <= ln <= 260):
            continue
        str_begin = lf + 4
        str_end = str_begin + ln * 2
        if str_end <= len(data) and str_end - 8 == i:
            try:
                nm = data[str_begin:str_end].decode("utf-16le")
            except Exception:
                continue
            if nm.endswith(".png"):
                positions.append(lf)
                break
    i += 4

positions.sort()
print("entries:", len(positions))
clusters = []
prev = None
for p in positions:
    if prev is not None and p - prev <= 2 * 260 + 16:
        clusters[-1].append(p)
    else:
        clusters.append([p])
    prev = p
clusters.sort(key=lambda c: -len(c))
for c in clusters[:6]:
    a, b = c[0], c[-1]
    print("cluster %4d entries  0x%X..0x%X" % (len(c), a, b))
    if len(c) > 2:
        pre = data[a - 96:a]
        print("   pre:", binascii.hexlify(pre).decode())
        for lf in c[:4]:
            (ln,) = struct.unpack("<I", data[lf:lf + 4])
            nm = data[lf + 4:lf + 4 + ln * 2].decode("utf-16le")
            after = data[lf + 4 + ln * 2: lf + 4 + ln * 2 + 6]
            print("   len=%3d %-34r after=%s" % (ln, nm, binascii.hexlify(after).decode()))
        print()