import re, binascii

EXE = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe"
data = open(EXE, "rb").read()

starts = []
for m in re.finditer(rb"(?:[\x09\x20-\x7e]\x00){4,260}", data):
    t = m.group().decode("utf-16le")
    if t.endswith(".png"):
        starts.append(m.start())

print("occurrences:", len(starts))
mono = []
for s in starts[:40]:
    # find actual string start in bytes (may be part of longer run) - just show region
    run_start = s
    while run_start >= 2 and data[run_start - 2: run_start] in (b" \x00", b"a\x00", b"A\x00", b"!\x00", b"/\x00", b"-\x00", b"'\x00"):
        run_start -= 2
    ctx = data[run_start - 24:run_start]
    seg = data[run_start:run_start + 48]
    print("0x%08X runstart-24d --- %s | %s" % (
        s,
        binascii.hexlify(ctx).decode(),
        binascii.hexlify(seg).decode(),
    ))