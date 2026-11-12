#!/usr/bin/env python3
# Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
"""Tests for labcheck.py: python3 -m unittest tools/lab/test_labcheck.py

fixtures/modern-3clients is a real capture (2026-11-12, loopback): this fork
built with PACKET_LOGGING, a copy of servers/modern with clientping = 2, and
three scripted seihai-core clients at once: 4.20r8 and 5.12 (log in, read
3 s) and 6.03r7 (seihai-suite-stress probe: chat, walk). They do not answer
108, so the ping answer checks fail there by design. The other cases add
hand-made lines in the same format.
"""

import io
import shutil
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import labcheck  # noqa: E402

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "modern-3clients"


def run(*args):
    out = io.StringIO()
    with redirect_stdout(out):
        code = labcheck.main([str(a) for a in args])
    return code, out.getvalue()


def row(report, text):
    return next(line for line in report.splitlines() if text in line)


def out_packet(t, who, pid, pidnum, data):
    name = f"ID{pidnum}"
    return (f"[{t}] < Out Packet to {who} [ID {pid}]: [{pidnum}] {name} ({len(data)} bytes)\n"
            f"[{t}] {data.decode('latin-1')}\n[{t}] {' '.join(f'{b:02x}' for b in data)} \n\n")


def in_packet(t, who, pid, pidnum, data):
    name = f"ID{pidnum}"
    return (f"[{t}] > In Packet from {who} [ID {pid}]: [{pidnum}] {name} ({len(data)} bytes)\n"
            f"[{t}] {data.decode('latin-1')}\n[{t}] {' '.join(f'{b:02x}' for b in data)} \n\n")


def gshort(v):
    return bytes([(v >> 7) + 32, (v & 127) + 32])


def gchar(v):
    return bytes([v + 32])


class FixtureTests(unittest.TestCase):
    def test_parse(self):
        logs = labcheck.load(FIXTURE, None, None)
        self.assertGreater(len(logs.packets), 100)
        self.assertTrue(all(p.data for p in logs.packets))
        labels = {s.account: s.label for s in logs.sessions()}
        self.assertEqual(labels, {"lab420": "4.208", "lab512": "5.12", "lab603": "6.037"})

    def test_ping_on_scripted_clients(self):
        code, report = run("ping", FIXTURE)
        self.assertEqual(code, 1)  # Scripted clients never answer.
        self.assertIn("PASS", row(report, "lab420 (4.208) is sent 108"))
        self.assertIn("PASS", row(report, "lab603 (6.037): every 108 is sub 0"))
        self.assertIn("PASS", row(report, "lab512 (5.12) is never sent 108"))
        self.assertIn("FAIL", row(report, "lab603 (6.037) answers with 162"))

    def test_matrix(self):
        code, report = run("matrix", FIXTURE)
        self.assertEqual(code, 0)
        self.assertIn("onlinestartlocal.nw", row(report, "lab603 (6.037) logged in"))

    def test_weaponcache_baseline(self):
        code, report = run("weaponcache", FIXTURE)
        self.assertEqual(code, 1)  # No 193 yet for 4.20r8 / 5.12 GS1 weapons (#26 not built).
        self.assertIn("33 x1", row(report, "lab420 (4.208) login 1 |"))


class SyntheticTests(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        (self.dir / "logs").mkdir()

    def tearDown(self):
        shutil.rmtree(self.dir)

    def write(self, dump, serverlog, console=""):
        (self.dir / "logs" / "networkdump.txt").write_bytes(dump.encode("latin-1"))
        (self.dir / "logs" / "serverlog.txt").write_text(serverlog)
        if console:
            (self.dir / "console.log").write_text(console)

    def test_ping_answered(self):
        t = "2026-11-12 03:00:00"
        dump = (out_packet(t, "six", 2, 108, gchar(108) + gchar(0) + gshort(7))
                + in_packet(t, "six", 2, 162, gchar(162) + b"!" + gshort(7)))
        log = f"[{t}] :: New login: six [New Client (2.22+)] [G3D0311C (6.037)]\n[{t}] [Ping] six: 12 ms (6.037)\n"
        self.write(dump, log)
        code, report = run("ping", self.dir)
        self.assertEqual(code, 0, report)
        self.assertIn("12 to 12 ms", row(report, "round trips"))

    def test_ping_wrong_echo_and_unknown_packet(self):
        t = "2026-11-12 03:00:00"
        dump = (out_packet(t, "six", 2, 108, gchar(108) + gchar(0) + gshort(7))
                + in_packet(t, "six", 2, 162, gchar(162) + b"!" + gshort(8)))
        log = f"[{t}] :: New login: six [New Client (2.22+)] [G3D0311C (6.037)]\n"
        self.write(dump, log, "Unknown Player Packet: 162 (!'(')\n")
        code, report = run("ping", self.dir)
        self.assertEqual(code, 1)
        self.assertIn("FAIL", row(report, "each 162 is"))
        self.assertIn("FAIL", row(report, "Unknown Player Packet"))

    def test_secure_login_label(self):
        t = "2026-11-12 03:00:00"
        dump = out_packet(t, "sec", 4, 25, gchar(25) + gchar(73))
        log = f"[{t}] :: New login: sec [New Client (2.22+)] [SEI00001 (inner GNW28015) (2.31)]\n"
        self.write(dump, log)
        code, report = run("matrix", self.dir)
        self.assertEqual(code, 0)
        self.assertIn("sec (2.31, SEI00001) logged in", report)

    def test_relogin_splits_sessions(self):
        t1, t2 = "2026-11-12 03:00:00", "2026-11-12 03:00:30"
        name = b"-Wbow"
        dump = (out_packet(t1, "w", 2, 33, gchar(33) + gchar(len(name)) + name + b"script")
                + in_packet(t1, "w", 2, 158, gchar(158) + name)
                + out_packet(t2, "w", 2, 193, gchar(193) + b"\x20" * 5 + name))
        log = (f"[{t1}] :: New login: w [New Client (2.22+)] [G3D20126 (4.208)]\n"
               f"[{t2}] :: New login: w [New Client (2.22+)] [G3D20126 (4.208)]\n")
        self.write(dump, log)
        code, report = run("weaponcache", self.dir)
        self.assertIn("158 asked x1 [-Wbow]", row(report, "login 1 |"))
        self.assertIn("193 x1 [-Wbow]", row(report, "login 2 |"))
        self.assertIn("PASS", row(report, "the last login asks no script back"))

    def test_milestone(self):
        names = ["a1", "b2", "c3", "d4", "e5"]
        start, end = "2026-11-12 03:00:00", "2026-11-12 03:01:05"
        dump, log = "", ""
        for i, me in enumerate(names):
            pid = i + 1
            log += f"[{start}] :: New login: {me} [New Client (2.22+)] [GNW03014 (2.22)]\n"
            dump += out_packet(start, me, pid, 3, gchar(3) + b"script lab27npc")
            for j, other in enumerate(names):
                if other == me:
                    continue
                for _ in range(2):
                    dump += out_packet(start, me, pid, 8, gchar(8) + gshort(j + 1) + b"x")
                dump += out_packet(start, me, pid, 8, gchar(8) + gshort(j + 1) + f"c27-{other}".encode())
            dump += in_packet(end, me, pid, 2, gchar(2) + b"x")
        self.write(dump, log)
        code, report = run("milestone", self.dir, "--accounts", ",".join(names))
        self.assertEqual(code, 0, report)
        self.assertIn("as GS1 text", row(report, "a1 gets the NPC"))

        code, report = run("milestone", self.dir, "--accounts", ",".join(names), "--chat-prefix", "zz-")
        self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
