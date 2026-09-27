import re, struct, binascii

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
data = open(EXE, "rb").read()
N = len(data)

name_positions = []
i = 0
while True:
    i = data.find(b".\x00p\x00n\x00g\x00", i)
    if i < 0:
        break
    # candidate name entry: pos = length field end -> string start at pos+4
    str_start = i  # start of utf16 string is position of '.'
    # length field typically 2 bytes before string start... actually len field is right before the string
    len_field = str_start - 4
    if len_field >= 0:
        (ln,) = struct.unpack("<I", data[len_field:len_field + 4])
        # the utf16 string should run from str_start back: the string begins at str_start - (ln-1)*2
        str_begin = str_start - (ln - 1) * 2
        if str_begin == len_field + 4:
            name_positions.append((len_field, str_begin, ln))
    i += 4

print("candidate name entries:", len(name_positions))

name_positions.sort()
prev = None
clusters = []
for p in name_positions:
    if prev is not None and p[0] - prev <= 4 * 300:
        clusters[-1].append(p)
    else:
        clusters.append([p])
    prev = p[0]

clusters.sort(key=lambda c: -len(c))
for c in clusters[:10]:
    first = c[0][0]
    last = c[-1][0] + c[-1][2] * 2 + 1
    print("cluster: %d entries, start=0x%X end=0x%X (%d bytes)" % (len(c), first, last, last - first))
    # dump 48 bytes before cluster
    pre = data[first - 64:first]
    print("   before:", binascii.hexlify(pre).decode())
    # first 3 entries raw
    for (lf, sb, ln) in c[:3]:
        nm = data[sb:sb + ln * 2].decode("utf-16le")
        after = data[sb + ln * 2: sb + ln * 2 + 8]
        print("   entry len=%d name=%r after=%s" % (ln, nm, binascii.hexlify(after).decode()))
    print()