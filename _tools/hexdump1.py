data = open(r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\VEGAS_Effects\VegasEffects.exe", "rb").read()

def hexdump(start, end):
    for off in range(start, end, 16):
        chunk = data[off:off + 16]
        hexs = " ".join("%02x" % b for b in chunk)
        asc = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
        print("0x%08X  %-47s  %s" % (off, hexs, asc))

print("=== cluster 1 (watermarks/templates) 0xC4EA40-0xC4EBC0 ===")
hexdump(0xC4EA40, 0xC4EBC0)
print()
print("=== cluster 2 (icons) 0x128AA00-0x128AB20 ===")
hexdump(0x128AA00, 0x128AB20)