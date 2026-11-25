#!/usr/bin/env python3
# Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
"""Build Graal 3D lab worlds that each change one thing from a control world.

The real client is the only reference for what Graal 3D content is. This tool
builds the worlds a lab serves to it from this fork's gserver (graal3d = true),
so that a rejected or misdrawn variant points at the one thing that changed.
Nothing here contains game content: you pass the folders, and the output stays
on the lab machines (never commit it).

    g3dlabworlds.py OUT --pack PACKDIR [--ground NAME] [--extra DIR ...] [--mbg MBGDIR]
                    [--round 1|2] [--levels DIR ...] [--only ID,ID]

PACKDIR is a scene folder of .3ds models and textures with a scene.json
("Model", "Objects", "SkyMaterials", ...), such as the kokiri scene of the
2026-11-24 Graal3D pack. MBGDIR, if given, is a folder of version-24 .dts and
version-44 .dif files. Each --extra DIR adds its files to every variant (for
models the scene names from another folder, such as the childlink scene's
player model). OUT gets one folder per variant:

    OUT/<id>/config/graal3dworld.txt
    OUT/<id>/world/g3dlab/<files>      the files the client downloads
    OUT/<id>/VARIANT.md                what changed from the control
    OUT/RUNSHEET.md                    every variant, with blanks for results

To run one: copy OUT/<id>/config and OUT/<id>/world into a lab server folder
with graal3d = true, start it, connect a real client in a lab VM, and note
what it does. Clear the client's levels3d download cache between variants;
changed files also get new names, so a stale cached copy cannot stand in.

Round 1 (the default) varies textures, sky, object count, GameObject section
2, .3ds damage and .dts/.dif versions. Round 2 varies placement (far, below,
rotated, a zero rotation axis), model name case, .3ds structure (no UVs, no
smoothing groups, no local axes, no version chunk, an odd version, the mesh
twice, an empty mesh, a 65535-point mesh), 2048 objects, the ground alone, and
a bare ghost of each networked class this server has no writer for. Round 3
(`--count N`, `--seed S`) is seeded fuzz: each world is the control plus one
fuzzed GameObject (every section on or off, edge floats, long, empty, missing
or odd strings), or one fuzzed sun, sky or object line. Each
--levels DIR adds one variant per NAME_graal3dworld.txt in it (as written by
`seihai-servertools gen terrain DIR --world NAME --3d`), served as it is.
Each --cases DIR (a graal-client-repro g3d-fuzzcases set) adds one world per
.3ds/.dts/.dif in it: C0 plus that shape, holding only the world file and
the case, to copy over a server that already has C0's files.
Run each variant on both routes (graal3droute = mission, then sharp3d).

Tests: python3 -m unittest tools/lab/test_g3dlabworlds.py
"""

import argparse
import json
import random
import shutil
import struct
import sys
from pathlib import Path

LAB = "g3dlab"


# --------------------------------------------------------------- .3ds chunks

def chunks(data, start=0, end=None):
    """The chunks directly inside data[start:end]: (id, start, end) each."""
    end = len(data) if end is None else end
    out = []
    i = start
    while i + 6 <= end:
        cid, length = struct.unpack_from("<HI", data, i)
        if length < 6 or i + length > end:
            break
        out.append((cid, i, i + length))
        i += length
    return out


CONTAINERS = {0x4D4D, 0x3D3D, 0x4000, 0x4100, 0xAFFF}


def drop_chunks(data, ids):
    """A copy of a .3ds with every chunk whose id is in `ids` removed, and the
    lengths of the chunks around them fixed."""

    def rebuild(start, end, cid):
        body_start = start + 6
        if cid == 0x4000:
            # A named object: a C string before its sub-chunks.
            nul = data.index(b"\0", body_start, end)
            prefix = data[body_start:nul + 1]
            body_start = nul + 1
        else:
            prefix = b""
        if cid not in CONTAINERS:
            return data[start:end]
        body = bytearray(prefix)
        for sub, s, e in chunks(data, body_start, end):
            if sub in ids:
                continue
            body += rebuild(s, e, sub)
        return struct.pack("<HI", cid, len(body) + 6) + bytes(body)

    top = chunks(data)
    return b"".join(rebuild(s, e, c) for c, s, e in top if c not in ids)


def replace_chunk(data, cid, new_body):
    """A copy of a .3ds with the first chunk `cid` given a new body, and the
    lengths of the chunks around it fixed."""

    def rebuild(start, end, cur):
        if cur == cid and not done[0]:
            done[0] = True
            return struct.pack("<HI", cur, len(new_body) + 6) + new_body
        if cur not in CONTAINERS:
            return data[start:end]
        body_start = start + 6
        body = bytearray()
        if cur == 0x4000:
            nul = data.index(b"\0", body_start, end)
            body += data[body_start:nul + 1]
            body_start = nul + 1
        for sub, s, e in chunks(data, body_start, end):
            body += rebuild(s, e, sub)
        return struct.pack("<HI", cur, len(body) + 6) + bytes(body)

    done = [False]
    return b"".join(rebuild(s, e, c) for c, s, e in chunks(data))


def find_chunk(data, cid, start=0, end=None):
    """The first chunk `cid` anywhere in the tree: (start, end) or None."""
    for sub, s, e in chunks(data, start, end):
        if sub == cid:
            return s, e
        if sub in CONTAINERS:
            body = s + 6
            if sub == 0x4000:
                body = data.index(b"\0", body, e) + 1
            hit = find_chunk(data, cid, body, e)
            if hit:
                return hit
    return None


def mutations(data, seed):
    """Named malformed copies of one .3ds: (suffix, bytes, what)."""
    out = [
        ("nomat", drop_chunks(data, {0xAFFF, 0x4130}), "materials and face-material groups removed"),
        ("half", data[: len(data) // 2], "cut at half its length"),
    ]
    longer = bytearray(data)
    struct.pack_into("<I", longer, 2, len(data) + 1000)
    out.append(("mainlen", bytes(longer), "main chunk length 1000 bytes past the end of the file"))
    pts = find_chunk(data, 0x4110)
    if pts:
        more = bytearray(data)
        count = struct.unpack_from("<H", data, pts[0] + 6)[0]
        struct.pack_into("<H", more, pts[0] + 6, min(0xFFFF, count * 2 + 1))
        out.append(("ptcount", bytes(more), f"first point list claims {min(0xFFFF, count * 2 + 1)} points, holds {count}"))
    faces = find_chunk(data, 0x4120)
    if faces:
        bad = bytearray(data)
        n = struct.unpack_from("<H", data, faces[0] + 6)[0]
        if n:
            struct.pack_into("<H", bad, faces[0] + 8, 0xFFFF)
            out.append(("faceidx", bytes(bad), "first face's first point index 65535"))
    rng = random.Random(seed)
    for k in range(3):
        flipped = bytearray(data)
        for _ in range(max(1, len(data) // 200)):
            i = rng.randrange(len(flipped))
            flipped[i] ^= 1 << rng.randrange(8)
        out.append((f"flip{k}", bytes(flipped), f"about 0.5% of bytes with one bit flipped (seed {seed}, pass {k})"))
    return out


def structure_variants(data):
    """Well-formed .3ds files that differ in one structural way: (suffix, bytes, what)."""
    out = []
    for cid, suffix, what in ((0x4140, "nouv", "texture coordinates (0x4140) removed"),
                              (0x4150, "nosmooth", "smoothing groups (0x4150) removed"),
                              (0x4160, "noaxes", "local axes (0x4160) removed"),
                              (0x0002, "nover", "the version chunk (0x0002) removed")):
        if find_chunk(data, cid):
            out.append((suffix, drop_chunks(data, {cid}), what))
    if find_chunk(data, 0x0002):
        out.append(("ver99", replace_chunk(data, 0x0002, struct.pack("<I", 99)), "file version 99"))
    edit = find_chunk(data, 0x3D3D)
    obj = find_chunk(data, 0x4000)
    if edit and obj:
        twice = bytearray(data[: edit[1]]) + data[obj[0]: obj[1]] + data[edit[1]:]
        for at, extra in ((2, obj[1] - obj[0]), (edit[0] + 2, obj[1] - obj[0])):
            struct.pack_into("<I", twice, at, struct.unpack_from("<I", twice, at)[0] + extra)
        out.append(("twice", bytes(twice), "its first mesh object a second time, same name"))
    if find_chunk(data, 0x4110) and find_chunk(data, 0x4120):
        empty = replace_chunk(replace_chunk(data, 0x4110, struct.pack("<H", 0)), 0x4120, struct.pack("<H", 0))
        out.append(("empty", empty, "first mesh with no points and no faces"))
        n = 0xFFFF
        pts = struct.pack("<H", n) + b"".join(struct.pack("<3f", (i % 256) * 0.01, (i // 256) * 0.01, 0) for i in range(n))
        big = replace_chunk(data, 0x4110, pts)
        if find_chunk(big, 0x4140):
            big = drop_chunks(big, {0x4140})
        out.append(("pts65535", big, "first mesh with 65535 points (its faces unchanged, UVs removed)"))
    return out


# Networked classes this server has no writer for, by protocol (the class
# tables in Graal3DProtocol.cpp). Round 2 sends each one bare.
BARE_CLASSES = ["WaterBlock", "DynamicGrass", "DynamicGrassReplicator", "DynamicShapeReplicator",
                "fxSunLight", "SimpleNetObject", "twSurfaceReference"]


# ------------------------------------------------------------- world files

class World:
    def __init__(self, scene):
        self.ground = scene["Model"]
        self.objects = [(o["Name"], o["Model"], o["X"], o["Y"], o["Z"]) for o in scene.get("Objects", [])]
        sky = [s.rsplit(".", 1)[0] for s in scene.get("SkyMaterials", "").split(",") if s]
        self.sky = sky
        self.sky_distance = 0.0
        self.fog = (0.78, 0.79, 0.63)
        self.sun = "sun 0 0.707 -0.707  0.7 0.7 0.7 1  0.3 0.3 0.3 1"
        self.spawn = (2.0, -2.0, 1.0)
        self.camera = "camera -5 6 1.8  0 0 1 2.44"
        self.extra = []
        self.texture_names = "both"  # both, bare, png

    def text(self, note):
        lines = [f"# g3dlabworlds: {note}", self.sun, f"object Ground {self.ground} 0 0 0"]
        for name, model, x, y, z in self.objects:
            lines.append(f"object {name} {model} {x:g} {y:g} {z:g}")
        if self.sky:
            fog = " ".join(f"{v:g}" for v in self.fog)
            lines.append(f"sky {','.join(self.sky)} {self.sky_distance:g} {fog}")
        lines += self.extra
        lines.append("spawn {:g} {:g} {:g}".format(*self.spawn))
        lines.append(self.camera)
        return "\n".join(lines) + "\n"


def raw_gameobject(name, shape, pos, section2):
    """A GameObject ghost written field by field, to probe section 2 (read as
    velocity by this server, as (1, 1, 1) scale by the 2026-11-24 replay)."""
    f = []
    f += ["f1", f"str:{name}"]                                    # 1 name
    f += ["f1"] + [f"f32:{v:g}" for v in section2]                 # 2 velocity or scale
    f += ["f1", f"str:{shape}"]                                   # 3 shape
    f += ["f0"]                                                   # 4 skin
    f += ["f1"] + [f"f32:{v:g}" for v in pos] + ["f32:0", "f32:0", "f32:1", "f32:0"]  # 5 transform
    f += ["f0"] * 8                                               # 6 to 13: none
    return "rawobject GameObject " + " ".join(f)


# --------------------------------------------------------------- variants

def build(out, pack, mbg=None, only=None, seed=2026, extra=(), ground=None, round_=1, levels=(), count=40, cases=()):
    pack, out = Path(pack), Path(out)
    scene = json.loads((pack / "scene.json").read_text())
    if ground:
        scene["Model"] = ground
    def listed(d):
        return [p for p in Path(d).iterdir() if p.is_file() and p.suffix.lower() not in (".json", ".gs2", ".txt") and not p.name.startswith(".")]

    have = {p.name for p in listed(pack)}
    base_files = listed(pack)
    for d in extra:
        base_files += [p for p in listed(d) if p.name not in have]
        have |= {p.name for p in listed(d)}
    base_files.sort(key=lambda p: p.name)
    missing = sorted(({scene["Model"]} | {o["Model"] for o in scene.get("Objects", [])}) - have)
    if missing:
        print("warning: the scene names files no folder holds: " + ", ".join(missing), file=sys.stderr)
    variants = []

    def variant(vid, what, world, files, renames=None):
        if only and vid not in only:
            return
        d = out / vid
        if d.exists():
            shutil.rmtree(d)
        (d / "config").mkdir(parents=True)
        dest = d / "world" / LAB
        dest.mkdir(parents=True)
        for f in files:
            if isinstance(f, tuple) and f[0] == "sub":
                continue
            if isinstance(f, tuple):
                (dest / f[0]).write_bytes(f[1])
            else:
                shutil.copyfile(f, dest / f.name)
        for n, m, *_ in world.objects:
            if "/" in m:
                (dest / Path(m).parent).mkdir(parents=True, exist_ok=True)
                shutil.copyfile(next(p for p in base_files if p.name == Path(m).name), dest / m)
        (d / "config" / "graal3dworld.txt").write_text(world.text(f"{vid}: {what}"))
        (d / "VARIANT.md").write_text(f"# {vid}\n\nChanged from C0: {what}\n")
        variants.append((vid, what))

    def textures(kind):
        keep = []
        for p in base_files:
            if p.suffix.lower() == ".3ds":
                keep.append(p)
            elif kind == "both":
                keep.append(p)
            elif kind == "bare" and p.suffix == "":
                keep.append(p)
            elif kind == "png" and p.suffix.lower() == ".png":
                keep.append(p)
        return keep

    control = World(scene)
    variant("C0", "nothing: the control world (scene ground, its objects, its sky)", control, textures("both"))
    probe = next((o for o in control.objects if o[1] != control.ground), None)
    if round_ == 2:
        round2(scene, control, probe, base_files, textures, variant)
    if round_ == 3:
        round3(scene, control, probe, textures, variant, seed, count)
    for d in levels:
        for txt in sorted(Path(d).glob("*_graal3dworld.txt")):
            name = txt.name[: -len("_graal3dworld.txt")]
            vid = f"L-{name}"
            if only and vid not in only:
                continue
            files = [p for p in Path(d).glob(f"{name}*.3ds")]
            dest = out / vid
            if dest.exists():
                shutil.rmtree(dest)
            (dest / "config").mkdir(parents=True)
            (dest / "world" / LAB).mkdir(parents=True)
            for f in files:
                shutil.copyfile(f, dest / "world" / LAB / f.name.lower())
            text = txt.read_text()
            for f in files:
                text = text.replace(f.name, f.name.lower())
            (dest / "config" / "graal3dworld.txt").write_text(f"# g3dlabworlds: {vid}: generated level {name}\n" + text)
            what = f"generated level {name} ({len(files)} models), served as written"
            (dest / "VARIANT.md").write_text(f"# {vid}\n\nIn place of C0: {what}\n")
            variants.append((vid, what))
    for d in cases:
        # One world per malformed shape: config and the one case file only. The
        # server keeps C0's world/g3dlab files; copy each case's over them.
        for f in sorted(Path(d).iterdir()):
            if f.suffix.lower() not in (".3ds", ".dts", ".dif"):
                continue
            vid = f"K-{Path(d).name}-{f.stem}"
            if only and vid not in only:
                continue
            w = World(scene)
            w.extra = [f"object FuzzCase {f.name} {control.spawn[0] + 3:g} {control.spawn[1]:g} 0"]
            dest = out / vid
            if dest.exists():
                shutil.rmtree(dest)
            (dest / "config").mkdir(parents=True)
            (dest / "world" / LAB).mkdir(parents=True)
            shutil.copyfile(f, dest / "world" / LAB / f.name)
            what = f"C0 plus one {f.suffix[1:]} shape: fuzz case {f.name} from {Path(d).name} (its manifest.tsv says what changed)"
            (dest / "config" / "graal3dworld.txt").write_text(w.text(f"{vid}: {what}"))
            (dest / "VARIANT.md").write_text(f"# {vid}\n\n{what}\nOnly the case file is here: keep C0's world/g3dlab files on the server.\n")
            variants.append((vid, what))
    if round_ in (2, 3) or cases:
        return finish(out, variants, only, round_)

    w = World(scene)
    variant("T1", "textures only as extension-less copies", w, textures("bare"))
    variant("T2", "textures only as .png files", w, textures("png"))

    w = World(scene); w.sky = []
    variant("S1", "no Sky", w, textures("both"))
    w = World(scene); w.sky_distance = 2000
    variant("S2", "Sky visible distance 2000 (control 0)", w, textures("both"))
    w = World(scene); w.fog = (1.0, 0.2, 0.2)
    variant("S3", "fog colour red (1 0.2 0.2)", w, textures("both"))
    w = World(scene); w.sky = control.sky + control.sky[:1] if len(control.sky) < 6 else control.sky[:5]
    variant("S4", f"Sky with {len(w.sky)} texture names (control {len(control.sky)})", w, textures("both"))

    if probe:
        _, model, x, y, z = probe
        for vid, count in (("O1", 64), ("O2", 512)):
            w = World(scene)
            side = int(count ** 0.5)
            w.objects = control.objects + [
                (f"Probe{i}", model, x + (i % side) * 0.8, y - (i // side) * 0.8, z) for i in range(count)
            ]
            variant(vid, f"{count} more copies of {model} in a grid", w, textures("both"))
        for vid, s2 in (("G1", (1, 1, 1)), ("G2", (2, 2, 2)), ("G3", (0.5, 0.5, 0.5))):
            w = World(scene)
            w.extra = [raw_gameobject("ProbeScale", model, (x + 1.5, y, z), s2)]
            variant(vid, f"one more {model}, sent field by field with GameObject section 2 = {s2}", w, textures("both"))

        source = next((p for p in base_files if p.name == model), None)
        data = source.read_bytes() if source else b""
        stem = Path(model).stem
        for suffix, bytes_, what in (mutations(data, seed) if data else []):
            w = World(scene)
            name = f"{stem}_{suffix}.3ds"
            w.objects = [(n, name if m == model else m, ox, oy, oz) for n, m, ox, oy, oz in control.objects]
            variant(f"Z-{suffix}", f"{model} served as {name}: {what}", w, textures("both") + [(name, bytes_)])

    if mbg:
        mbg = Path(mbg)
        for kind in ("dts", "dif"):
            found = sorted(mbg.glob(f"*.{kind}"))
            for i, f in enumerate(found):
                w = World(scene)
                w.extra = [f"object Probe{kind.upper()} {f.name} {control.spawn[0] + 3:g} {control.spawn[1]:g} 0"]
                variant(f"F-{kind}{i:02d}", f"one {kind} shape added: {f.name}", w, textures("both") + [f])
            if found:
                data = found[0].read_bytes()
                tests = (16, 17, 24, 25) if kind == "dts" else (43, 44, 45)
                for v in tests:
                    patched = bytearray(data)
                    if kind == "dts":
                        # Low 16 bits of the first 32-bit word: the shape version.
                        struct.pack_into("<H", patched, 0, v)
                    else:
                        struct.pack_into("<I", patched, 0, v)
                    name = f"{found[0].stem}_v{v}.{kind}"
                    w = World(scene)
                    w.extra = [f"object Probe{kind.upper()} {name} {control.spawn[0] + 3:g} {control.spawn[1]:g} 0"]
                    variant(f"V-{kind}{v}", f"{found[0].name} with its version field set to {v}, as {name}", w,
                            textures("both") + [(name, bytes(patched))])

    return finish(out, variants, only, round_)


# Values that break naive readers: zeroes of both signs, tiny and huge
# magnitudes, infinities and NaN (f32 tokens take "inf" and "nan").
EDGE_FLOATS = ["0", "-0", "1e-38", "1e-45", "1e30", "-1e30", "3.4e38", "inf", "-inf", "nan", "-1", "1", "65536"]


def fuzz_string(rng, model):
    kind = rng.randrange(7)
    if kind == 0:
        return "-"                                         # empty
    if kind == 1:
        return "x" * rng.choice((200, 255))                 # long (the server takes up to 255)
    if kind == 2:
        return model                                       # a real model
    if kind == 3:
        return "missing_" + str(rng.randrange(1000)) + ".3ds"
    if kind == 4:
        return "%25s%25n%25d"                              # printf directives, percent-escaped
    if kind == 5:
        return "../" * rng.randrange(1, 4) + model         # path walk
    return "%c3%a9t%c3%a9"                                 # UTF-8


def fuzz_gameobject(rng, model, pos):
    """A GameObject update whose 13 sections are each on or off at random,
    with edge values; the transform keeps a sane position half the time."""
    f = []
    fl = lambda: "f32:" + (rng.choice(EDGE_FLOATS) if rng.random() < 0.7 else f"{rng.uniform(-100, 100):.3f}")
    s = lambda: "str:" + fuzz_string(rng, model)
    sec = lambda: rng.random() < 0.5
    f += ["f1", s()] if sec() else ["f0"]                                   # 1 name
    f += ["f1", fl(), fl(), fl()] if sec() else ["f0"]                      # 2 velocity or scale
    f += ["f1", s()] if sec() else ["f0"]                                   # 3 shape
    f += ["f1", s()] if sec() else ["f0"]                                   # 4 skin
    if sec():                                                               # 5 transform
        p = [f"f32:{v:g}" for v in pos] if rng.random() < 0.5 else [fl(), fl(), fl()]
        f += ["f1"] + p + [fl(), fl(), fl(), fl()]
    else:
        f += ["f0"]
    f += ["f1", "str:" + rng.choice(["interpolate", "list", "rigid", "-", "bogus"])] if sec() else ["f0"]  # 6 movetype
    f += ["f0"]                                                             # 7 movement data: never (no length)
    if sec():                                                               # 8 shape data, int14 length + bytes
        n = rng.choice((0, 1, 16, 255))
        f += ["f1", f"hex@14:{''.join(f'{rng.randrange(256):02x}' for _ in range(n))}"] if n else ["f1", "i14:0"]
    else:
        f += ["f0"]
    f += ["f1", s()] if sec() else ["f0"]                                   # 9 chat
    f += ["f1", s()] if sec() else ["f0"]                                   # 10 script classes
    if sec():                                                               # 11 attr[1..8]
        f += ["f1"] + sum((["f1", s()] if sec() else ["f0"] for _ in range(8)), [])
    else:
        f += ["f0"]
    if sec():                                                               # 12 nickname + RGBA
        f += ["f1", s()] + [f"u8:{rng.randrange(256)}" for _ in range(4)]
    else:
        f += ["f0"]
    f += ["f1", f"u16:{rng.randrange(65536)}"] if sec() else ["f0"]         # 13 flags
    return "rawobject GameObject " + " ".join(f)


def round3(scene, control, probe, textures, variant, seed, count):
    """Seeded fuzz: each variant is C0 plus one fuzzed GameObject (FZ-g), or C0
    with one fuzzed sun (FZ-s), sky (FZ-k) or object line (FZ-o)."""
    rng = random.Random(seed)
    model = probe[1] if probe else control.ground
    x, y, z = (probe[2], probe[3], probe[4]) if probe else (0, 0, 0)
    e = lambda: rng.choice(EDGE_FLOATS)
    for i in range(count):
        kind = "gsko"[i % 4]
        w = World(scene)
        if kind == "g":
            line = fuzz_gameobject(rng, model, (x + 1.5, y, z))
            w.extra = [line]
            what = "one fuzzed GameObject: " + line[len("rawobject GameObject "):][:160]
        elif kind == "s":
            w.sun = "sun " + " ".join(e() for _ in range(11))
            what = "fuzzed sun line: " + w.sun
        elif kind == "k":
            w.sky_distance = float(e())
            w.fog = tuple(float(e()) for _ in range(3))
            what = f"fuzzed sky: distance {w.sky_distance}, fog {w.fog}"
        else:
            vals = [e() for _ in range(7)]
            w.extra = [f"object FuzzObj {model} " + " ".join(vals)]
            what = "fuzzed object line: " + w.extra[0]
        variant(f"FZ-{kind}{i:03d}", what, w, textures("both"))


def round2(scene, control, probe, base_files, textures, variant):
    if probe:
        _, model, x, y, z = probe
        for vid, pos, what in (("P1", (x + 5000, y - 5000, z), "5000 units away on x and y"),
                               ("P2", (x, y, z - 500), "500 units below the ground"),
                               ("P3", (x, y, z + 1e7), "at height 10 000 000")):
            w = World(scene)
            w.objects = [(n, m, *(pos if (n, m) == probe[:2] else (ox, oy, oz))) for n, m, ox, oy, oz in control.objects]
            variant(vid, f"{probe[0]} ({model}) placed {what}", w, textures("both"))
        for vid, rot, what in (("P4", "0 0 1 1.5708", "turned 90 degrees about z"),
                               ("P5", "1 0 0 3.1416", "turned upside down (180 degrees about x)"),
                               ("P6", "0 0 0 1", "a zero rotation axis")):
            w = World(scene)
            w.extra = [f"object ProbeRot {model} {x + 1.5:g} {y:g} {z:g} {rot}"]
            variant(vid, f"one more {model} {what}", w, textures("both"))
        source = next((p for p in base_files if p.name == model), None)
        for vid, served, files_name, what in (
                ("N1", model.upper(), model, f"the world names {model.upper()}, the file is {model}"),
                ("N2", model, model.upper(), f"the world names {model}, the file is {model.upper()}"),
                ("N3", "sub/" + model, None, f"the world names sub/{model}, the file is in a subfolder")):
            w = World(scene)
            w.objects = [(n, served if m == model else m, ox, oy, oz) for n, m, ox, oy, oz in control.objects]
            files = [p for p in textures("both") if p.name != model]
            if files_name:
                files.append((files_name, source.read_bytes()))
            else:
                files.append(("sub", None))
            variant(vid, what, w, files)
        data = source.read_bytes() if source else b""
        stem = Path(model).stem
        for suffix, bytes_, what in structure_variants(data):
            w = World(scene)
            name = f"{stem}_{suffix}.3ds"
            w.objects = [(n, name if m == model else m, ox, oy, oz) for n, m, ox, oy, oz in control.objects]
            variant(f"X-{suffix}", f"{model} served as {name}: {what}", w, textures("both") + [(name, bytes_)])
        w = World(scene)
        w.objects = control.objects + [(f"Probe{i}", model, x + (i % 45) * 0.8, y - (i // 45) * 0.8, z) for i in range(2048)]
        variant("O3", f"2048 more copies of {model} in a grid", w, textures("both"))
    w = World(scene)
    w.objects = []
    variant("O4", "the ground alone: no other objects", w, textures("both"))
    for cls in BARE_CLASSES:
        w = World(scene)
        w.extra = [f"rawobject {cls} f0"]
        variant(f"R-{cls}", f"one {cls} ghost whose update is a single 0 bit", w, textures("both"))
        w = World(scene)
        w.extra = [f"rawobject {cls} f1 hex:{'00' * 32}"]
        variant(f"R-{cls}-z", f"one {cls} ghost whose update is a 1 bit, then 256 zero bits", w, textures("both"))


def finish(out, variants, only, round_):
    sheet = [f"# Graal 3D lab worlds, round {round_}", "",
             "Each variant changes one thing from C0. Run each on both routes",
             "(graal3droute = mission, then sharp3d), clearing the client's download",
             "cache between runs. Results: drawn / drawn differently / not drawn / login",
             "stuck at step N / disconnected / client crashed, plus the server log lines.", "",
             "| Variant | Changed from C0 | mission | sharp3d |", "|---|---|---|---|"]
    sheet += [f"| {vid} | {what} | | |" for vid, what in variants]
    if not only:
        out.mkdir(parents=True, exist_ok=True)
        (out / "RUNSHEET.md").write_text("\n".join(sheet) + "\n")
    return variants


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("out")
    p.add_argument("--pack", required=True)
    p.add_argument("--mbg")
    p.add_argument("--ground", help="the ground model, in place of the scene's \"Model\"")
    p.add_argument("--extra", action="append", default=[], help="another folder of files every variant gets")
    p.add_argument("--only", help="comma-separated variant ids")
    p.add_argument("--seed", type=int, default=2026)
    p.add_argument("--round", type=int, choices=(1, 2, 3), default=1)
    p.add_argument("--count", type=int, default=40, help="round 3: how many fuzz variants")
    p.add_argument("--cases", action="append", default=[], help="a folder of fuzz-case shapes (g3d-fuzzcases): one world each")
    p.add_argument("--levels", action="append", default=[], help="a folder of generated NAME_graal3dworld.txt worlds")
    a = p.parse_args(argv)
    only = set(a.only.split(",")) if a.only else None
    made = build(a.out, a.pack, a.mbg, only, a.seed, a.extra, a.ground, a.round, a.levels, a.count, a.cases)
    print(f"{len(made)} variants in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
