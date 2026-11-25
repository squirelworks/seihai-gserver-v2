#!/usr/bin/env python3
# Seihai project, 2026-11-25: see CHANGES-SEIHAI.md.
"""Tests for g3dlabworlds.py: python3 -m unittest tools/lab/test_g3dlabworlds.py

The pack here is made up: one hand-built .3ds cube with a material, two
textures and a scene.json, so no game content is needed.
"""

import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import g3dlabworlds as g  # noqa: E402


def chunk(cid, body):
    return struct.pack("<HI", cid, len(body) + 6) + body


def cube():
    mat = chunk(0xAFFF, chunk(0xA000, b"m\0") + chunk(0xA020, chunk(0x0011, bytes([200, 10, 10]))))
    pts = [(x, y, z) for x in (0, 1) for y in (0, 1) for z in (0, 1)]
    plist = struct.pack("<H", len(pts)) + b"".join(struct.pack("<3f", *p) for p in pts)
    faces = [(0, 1, 2), (1, 3, 2)]
    group = chunk(0x4130, b"m\0" + struct.pack("<H", 2) + struct.pack("<2H", 0, 1))
    flist = struct.pack("<H", len(faces)) + b"".join(struct.pack("<4H", *f, 7) for f in faces) + group
    tri = chunk(0x4110, plist) + chunk(0x4120, flist)
    obj = chunk(0x4000, b"box\0" + chunk(0x4100, tri))
    return chunk(0x4D4D, chunk(0x0002, struct.pack("<I", 3)) + chunk(0x3D3D, mat + obj))


def walk(data, start=0, end=None):
    """Every chunk id in the tree; fails if any length does not nest."""
    ids = []
    for cid, s, e in g.chunks(data, start, end):
        ids.append(cid)
        if cid in g.CONTAINERS:
            body = s + 6
            if cid == 0x4000:
                body = data.index(b"\0", body, e) + 1
            ids += walk(data, body, e)
    covered = sum(e - s for _, s, e in g.chunks(data, start, end))
    assert covered == (len(data) if end is None else end) - start, "chunks fill their parent"
    return ids


class G3DLabWorlds(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        root = Path(self.tmp.name)
        self.pack = root / "pack"
        self.pack.mkdir()
        (self.pack / "ground.3ds").write_bytes(cube())
        (self.pack / "lamp.3ds").write_bytes(cube())
        (self.pack / "sky0.png").write_bytes(b"png")
        (self.pack / "sky0").write_bytes(b"png")
        (self.pack / "scene.json").write_text(json.dumps({
            "Model": "ground.3ds",
            "Objects": [{"Name": "Lamp", "Model": "lamp.3ds", "X": 1, "Y": -1, "Z": 0}],
            "SkyMaterials": "sky0.png,sky0.png",
        }))
        self.mbg = root / "mbg"
        self.mbg.mkdir()
        (self.mbg / "a.dts").write_bytes(struct.pack("<I", 24) + b"x" * 20)
        (self.mbg / "b.dif").write_bytes(struct.pack("<I", 44) + b"y" * 20)
        self.out = root / "out"

    def tearDown(self):
        self.tmp.cleanup()

    def test_the_cube_is_well_formed(self):
        self.assertIn(0xAFFF, walk(cube()))

    def test_dropping_materials_keeps_every_length_right(self):
        stripped = g.drop_chunks(cube(), {0xAFFF, 0x4130})
        ids = walk(stripped)
        self.assertNotIn(0xAFFF, ids)
        self.assertNotIn(0x4130, ids)
        self.assertIn(0x4110, ids)

    def test_every_variant_changes_one_thing(self):
        made = dict(g.build(self.out, self.pack, self.mbg))
        for vid in ("C0", "T1", "T2", "S1", "S2", "S3", "S4", "O1", "O2", "G1", "G2", "G3",
                    "Z-nomat", "Z-half", "Z-mainlen", "Z-ptcount", "Z-faceidx", "F-dts00", "F-dif00",
                    "V-dts16", "V-dts25", "V-dif43", "V-dif45"):
            self.assertIn(vid, made)
        control = (self.out / "C0/config/graal3dworld.txt").read_text().splitlines()[1:]
        self.assertIn("object Ground ground.3ds 0 0 0", control)
        self.assertIn("sky sky0,sky0 0 0.78 0.79 0.63", control)

        def diff(vid):
            lines = (self.out / vid / "config/graal3dworld.txt").read_text().splitlines()[1:]
            return [l for l in lines if l not in control], [l for l in control if l not in lines]

        self.assertEqual(diff("S1"), ([], ["sky sky0,sky0 0 0.78 0.79 0.63"]))
        added, removed = diff("Z-nomat")
        self.assertEqual(removed, ["object Lamp lamp.3ds 1 -1 0"])
        self.assertEqual(added, ["object Lamp lamp_nomat.3ds 1 -1 0"])
        self.assertTrue((self.out / "Z-nomat/world/g3dlab/lamp_nomat.3ds").exists())
        self.assertEqual(diff("T1"), ([], []))
        self.assertFalse((self.out / "T1/world/g3dlab/sky0.png").exists())
        self.assertTrue((self.out / "T1/world/g3dlab/sky0").exists())
        self.assertFalse((self.out / "T2/world/g3dlab/sky0").exists())

        v = (self.out / "V-dts16/world/g3dlab/a_v16.dts").read_bytes()
        self.assertEqual(struct.unpack_from("<H", v)[0], 16)
        self.assertTrue((self.out / "RUNSHEET.md").read_text().count("| V-") == 7)

    def test_section_2_probe_writes_all_thirteen_sections(self):
        line = g.raw_gameobject("P", "lamp.3ds", (1, 2, 3), (2, 2, 2))
        words = line.split()[2:]
        # 1: f1 str, 2: f1 + 3 floats, 3: f1 str, 4: f0, 5: f1 + 7 floats, 6-13: f0 each.
        self.assertEqual(len(words), 2 + 4 + 2 + 1 + 8 + 8)
        self.assertEqual(words[2:6], ["f1", "f32:2", "f32:2", "f32:2"])

    def test_only_builds_the_named_variants(self):
        made = g.build(self.out, self.pack, None, {"C0", "S1"})
        self.assertEqual([v for v, _ in made], ["C0", "S1"])
        self.assertFalse((self.out / "RUNSHEET.md").exists())


if __name__ == "__main__":
    unittest.main()
