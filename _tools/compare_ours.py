import os, re

OURS = r"D:\Devs\Cpp\OpenVegas_Effects\resources\icons"
INV = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\icons-VegesEffects\icons_inventory.txt"

ours = set()
for root, _, files in os.walk(OURS):
    for f in files:
        if f.endswith(".svg"):
            ours.add(f[:-4].lower())
print("our svg icons:", len(ours))

names = [l.strip() for l in open(INV, encoding="utf-8") if l.strip()]
ref_cores = set()
for n in names:
    m = re.match(r"^(G|')?(.*)$", n)
    rest = m.group(2).replace("@2x", "")
    core = re.sub(r"\.\w+$", "", rest)
    core = re.sub(r"-(hover|checked|disabled|selected|active|menu|invalid)$", "", core)
    ref_cores.add(core.lower())
print("reference base cores:", len(ref_cores))

overlap = sorted(ours & ref_cores)
print("overlap:", len(overlap))
print("ours not in ref:", len(ours - ref_cores))
for k in sorted(ours - ref_cores):
    print("   ours-only:", k)
print("ref-only (sample 30):")
for k in sorted(ref_cores - ours)[:30]:
    print("   " + k)