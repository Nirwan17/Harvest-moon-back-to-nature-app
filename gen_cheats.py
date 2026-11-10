#!/usr/bin/env python3
"""cheats.txt (format GameShark) -> cheats_data.h (kategori, grup, op, konflik)."""
import re, sys, collections

SRC = sys.argv[1] if len(sys.argv) > 1 else "cheats.txt"
DST = sys.argv[2] if len(sys.argv) > 2 else "cheats_data.h"

CODE = re.compile(r"^([0-9A-Fa-f]{8})\s+([0-9A-Fa-f]{4})$")
BANNER = re.compile(r"^#\s*={3,}\s*(.*?)\s*=*\s*$")

# kata kunci banner -> nama kategori (None = abaikan, tetap di kategori sebelumnya)
RULES = [
    ("calon istri", None),
    ("waktu", "Waktu"),
    ("pasti", "Favorit (Sering Dipakai)"),
    ("kesukaan", "Kesukaan Penduduk"),
    ("instan panen", "Instan Panen"),
    ("area baris", "Instan Panen"),
    ("auto watered", "Siram Otomatis"),
    ("cuaca", "Cuaca & Musim"),
    ("status", "Status & Ekonomi"),
    ("berkebun", "Berkebun & Beternak"),
    ("biji mineral", "Bijih Mineral (999)"),
    ("item & tool", "Item & Alat"),
    ("hewan", "Hewan & Event"),
    ("kurcaci", "Kurcaci"),
    ("khusus penduduk", "Penduduk & Hati"),
    ("modif kebun", "Modif Kebun"),
    ("tanam rumput", "Tanam Rumput"),
    ("buat pagar", "Buat Pagar"),
    ("tanaman custom", "Tanaman Custom"),
]
EXP_CAT = "Eksperimental (kemungkinan tidak jalan)"
DEFAULT_CAT = "Umum"

def sanitize(s):
    return re.sub(r"[^\x20-\x7E]", "", s).rstrip()

def pretty(t):
    t = re.sub(r"\s+", " ", t).strip(" =-")
    t = re.sub(r"^(?i:cheat)\s+", "", t)
    if t.isupper():
        t = t.title()
    elif t[:1].islower():
        t = t[:1].upper() + t[1:]
    return t[:90]

def cat_for(banner):
    low = banner.lower()
    for key, name in RULES:
        if key in low:
            return name, True
    return pretty(banner), False

cats, groups = [], []
def cat_id(name):
    if name not in cats:
        cats.append(name)
    return cats.index(name)

cur_cat = DEFAULT_CAT
pend_titles, pend_banners, cur = [], [], None

def finish():
    global cur
    if cur is not None:
        groups.append(cur); cur = None

for raw in open(SRC, "rb").read().decode("latin1").replace("\r", "").split("\n"):
    s = sanitize(raw)
    m = CODE.match(s)
    if m:
        if cur is None:
            if pend_banners:
                nm, known = cat_for(pend_banners[0])
                if nm is not None:
                    cur_cat = nm
                elif len(pend_banners) > 1:
                    nm2, _ = cat_for(pend_banners[1])
                    cur_cat = nm2 or cur_cat
            if pend_titles:
                title = " - ".join(pretty(t.lstrip("# ")) for t in pend_titles)
            elif pend_banners:
                title = pretty(pend_banners[-1])
            else:
                title = "(tanpa judul)"
            cur = {"title": title[:100], "cat": cur_cat, "ops": []}
            pend_titles, pend_banners = [], []
        t = m.group(1)[:2].upper()
        typ = {"80": 0, "30": 1, "D0": 2}.get(t)
        if typ is None:
            sys.exit("tipe kode tidak didukung: " + s)
        cur["ops"].append((int(m.group(1)[2:], 16), int(m.group(2), 16), typ))
    elif s.startswith("#"):
        finish()
        b = BANNER.match(s)
        if b and b.group(1):
            pend_banners.append(b.group(1))
        elif s.strip("# ="):
            pend_titles.append(s)
finish()

# tandai eksperimental / belum pasti
for g in groups:
    writes = [o for o in g["ops"] if o[2] != 2]
    g["exp"] = any(o[2] == 2 for o in g["ops"])
    g["odd"] = any(not (0x70000 <= (o[0] & 0x1FFFFF) < 0x80000) for o in writes)
    if g["exp"]:
        g["cat"] = EXP_CAT
    elif g["odd"]:
        g["title"] += " [?]"

for g in groups:
    cat_id(g["cat"])
if EXP_CAT in cats:                      # eksperimental selalu terakhir
    cats.remove(EXP_CAT); cats.append(EXP_CAT)
order = sorted(range(len(groups)), key=lambda i: (cats.index(groups[i]["cat"]), i))
groups = [groups[i] for i in order]

# konflik: dua grup menulis alamat yang sama (>=50% dari yang lebih kecil)
def bytes_of(g):
    s = set()
    for a, v, t in g["ops"]:
        if t == 0: s.update((a, a + 1))
        elif t == 1: s.add(a)
    return s
sets = [bytes_of(g) if not g["exp"] else set() for g in groups]
conf = [[] for _ in groups]
for i in range(len(groups)):
    for j in range(i + 1, len(groups)):
        if sets[i] and sets[j]:
            inter = len(sets[i] & sets[j])
            if inter and inter / min(len(sets[i]), len(sets[j])) >= 0.5:
                conf[i].append(j); conf[j].append(i)

def cs(t): return '"' + t.replace("\\", "\\\\").replace('"', '\\"') + '"'

ops_flat, out = [], []
for g in groups:
    g["start"] = len(ops_flat); g["count"] = len(g["ops"]); ops_flat += g["ops"]
cat_lists = [[i for i, g in enumerate(groups) if g["cat"] == c] for c in cats]
conf_flat, conf_idx = [], []
for c in conf:
    conf_idx.append((len(conf_flat), len(c))); conf_flat += c

out.append("// DIHASILKAN OTOMATIS oleh gen_cheats.py dari cheats.txt - jangan diedit")
out.append("struct Op { uint32_t addr; uint16_t val; uint8_t type; };  // type 0=tulis16 (80), 1=tulis8 (30), 2=jika16==val (D0)")
out.append("struct Group { const char* name; uint16_t start; uint16_t count; uint16_t conflStart; uint16_t conflCount; uint8_t exp; };")
out.append("static const Op kOps[] = {")
line = []
for a, v, t in ops_flat:
    line.append("{0x%06X,0x%04X,%d}" % (a, v, t))
    if len(line) == 8: out.append("  " + ",".join(line) + ","); line = []
if line: out.append("  " + ",".join(line) + ",")
out.append("};")
out.append("static const int kOpCount = %d;" % len(ops_flat))
out.append("static const Group kGroups[] = {")
for g, (cs0, cn) in zip(groups, conf_idx):
    out.append("  {%s,%d,%d,%d,%d,%d}," % (cs(g["title"]), g["start"], g["count"], cs0, cn, 1 if g["exp"] else 0))
out.append("};")
out.append("static const int kGroupCount = %d;" % len(groups))
out.append("static const uint16_t kConfl[] = {%s};" % (",".join(map(str, conf_flat)) or "0"))
out.append("static const char* const kCats[] = {%s};" % ",".join(cs(c) for c in cats))
out.append("static const int kCatCount = %d;" % len(cats))
cl_flat, cl_idx = [], []
for l in cat_lists:
    cl_idx.append((len(cl_flat), len(l))); cl_flat += l
out.append("static const uint16_t kCatGroups[] = {%s};" % ",".join(map(str, cl_flat)))
out.append("static const uint16_t kCatIdx[][2] = {%s};" % ",".join("{%d,%d}" % x for x in cl_idx))
open(DST, "w").write("\n".join(out) + "\n")

# ringkasan
print("kategori: %d | grup: %d | op: %d" % (len(cats), len(groups), len(ops_flat)))
cnt = collections.Counter(t for _, _, t in ops_flat); print("tipe op:", dict(cnt))
for ci, c in enumerate(cats):
    gl = cat_lists[ci]
    print("%2d %-42s %3d grup" % (ci, c, len(gl)))
print("grup eksperimental:", sum(g["exp"] for g in groups), "| bertanda [?]:", sum(1 for g in groups if g["title"].endswith("[?]")))
print("grup dengan konflik:", sum(1 for c in conf if c), "| pasangan konflik:", sum(len(c) for c in conf) // 2)
