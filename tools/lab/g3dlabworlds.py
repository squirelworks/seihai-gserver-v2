#!/usr/bin/env python3
# Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
"""Build Graal 3D lab worlds that each change one thing from a control world.

The real client is the only reference for what Graal 3D content is. This tool
builds the worlds a lab serves to it from this fork's gserver (graal3d = true),
so that a rejected or misdrawn variant points at the one thing that changed.
Nothing here contains game content: you pass the folders, and the output stays
on the lab machines (never commit it).

    g3dlabworlds.py OUT --pack PACKDIR [--ground NAME] [--extra DIR ...] [--mbg MBGDIR] [--only ID,ID]

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

def build(out, pack, mbg=None, only=None, seed=2026, extra=(), ground=None):
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
            if isinstance(f, tuple):
                (dest / f[0]).write_bytes(f[1])
            else:
                shutil.copyfile(f, dest / f.name)
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

    probe = next((o for o in control.objects if o[1] != control.ground), None)
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

    sheet = ["# Graal 3D lab worlds", "",
             "Each variant changes one thing from C0. Run each on both routes",
             "(graal3droute = mission, then sharp3d), clearing the client's download",
             "cache between runs. Results: drawn / drawn differently / not drawn / login",
             "stuck at step N / disconnected / client crashed, plus the server log lines.", "",
             "| Variant | Changed from C0 | mission | sharp3d |", "|---|---|---|---|"]
    sheet += [f"| {vid} | {what} | | |" for vid, what in variants]
    if not only:
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
    a = p.parse_args(argv)
    only = set(a.only.split(",")) if a.only else None
    made = build(a.out, a.pack, a.mbg, only, a.seed, a.extra, a.ground)
    print(f"{len(made)} variants in {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
