import os, re, collections

OUT = r"D:\Devs\Cpp\OpenVegas_Effects\SAMPLES\icons-VegesEffects"
names = [l.rstrip("\n") for l in open(os.path.join(OUT, "icons_inventory.txt"), encoding="utf-8") if l.strip()]

STATE = ("hover", "checked", "disabled", "selected", "active", "menu", "invalid")

themes = collections.Counter()
sizes = collections.Counter()
states = collections.Counter()
exts = collections.Counter()
pairs = []

for n in names:
    m = re.match(r"^(G|')?(.*)$", n)
    pref = m.group(1) or "base"
    rest = m.group(2)
    exts[os.path.splitext(n)[1]] += 1
    if "@2x" in rest:
        sizes["@2x"] += 1
        rest = rest.replace("@2x", "")
    else:
        sizes["base"] += 1
    core = re.sub(r"\.\w+$", "", rest)
    st = None
    for s in STATE:
        if core.endswith("-%s" % s):
            st = s
            core = core[: -(len(s) + 1)]
            break
    states[st or "none"] += 1
    themes[pref] += 1
    pairs.append((pref, core, st))

print("extensions:", dict(exts))
print("sizes: base=%d @2x=%d" % (sizes["base"], sizes["@2x"]))
print("themes:", dict(themes))
print("states:", dict(states))

# how many base 'core' names exist per theme prefix
cores = collections.Counter()
for pref, core, st in pairs:
    cores[(pref, core)] += 1
print("unique (theme, core):", len(cores))

# overlap between themes on the same core
base_cores = {c for (p, c) in cores if p == "base"}
g_cores = {c for (p, c) in cores if p == "G"}
ap_cores = {c for (p, c) in cores if p == "'"}
print("base-only cores:", len(base_cores), "G-only:", len(g_cores), "'-only:", len(ap_cores))
print("base & G:", len(base_cores & g_cores), "| base & ':", len(base_cores & ap_cores), "| G & ':", len(g_cores & ap_cores), "| all:", len(base_cores & g_cores & ap_cores))

# sample icon cores (base theme), no state, 20 examples
ex = sorted(base_cores)[:0]
samp = sorted(base_cores)
print("\nsample base cores (40):")
print("  " + ", ".join(samp[:40]))
print("\nall-base total:", len(samp))