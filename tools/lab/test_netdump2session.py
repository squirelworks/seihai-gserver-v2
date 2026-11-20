#!/usr/bin/env python3
# Seihai project, 2026-11-20: see CHANGES-SEIHAI.md.
"""Tests for netdump2session.py: python3 -m unittest tools/lab/test_netdump2session.py

Uses fixtures/modern-3clients, the same loopback capture as test_labcheck.py: three
seihai-core clients (4.20r8, 5.12 and 6.03r7) logged in as New Client (2.22+).
"""

import io
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import labcheck  # noqa: E402
import netdump2session  # noqa: E402

FIXTURE = Path(__file__).resolve().parent / "fixtures" / "modern-3clients"


class Netdump2SessionTest(unittest.TestCase):
    def setUp(self):
        self.lines = netdump2session.convert(labcheck.load(FIXTURE, None, None))

    def test_logins_are_renamed_and_typed(self):
        logins = [l for l in self.lines if l.startswith("login ")]
        self.assertEqual(logins, [
            "login p1 25 G3D20126",
            "login p2 25 G3D14097",
            "login p3 25 G3D0311C",
        ])
        for account in ("lab420", "lab512", "lab603", "labpw"):
            self.assertFalse(any(account in l for l in self.lines), account)

    def test_packets_are_client_to_server_bodies(self):
        packets = [l.split() for l in self.lines if l.startswith("p")]
        self.assertTrue(packets)
        # PLI_LANGUAGE "English" from the 4.20r8 client, its id byte stripped.
        first = packets[0]
        self.assertEqual(first[:2], ["p1", "37"])
        self.assertEqual(bytes(int(x, 16) for x in first[2:]), b"English")
        for p in packets:
            self.assertIn(p[0], {"p1", "p2", "p3"})
            self.assertTrue(0 <= int(p[1]) < 224)
            self.assertNotEqual(int(p[1]), labcheck.PLI_PONG)

    def test_kind_types(self):
        self.assertEqual(netdump2session.kind_type("Client"), 0x20)
        self.assertEqual(netdump2session.kind_type("RC"), 0x21)
        self.assertEqual(netdump2session.kind_type("NC"), 0x23)
        self.assertEqual(netdump2session.kind_type("New Client (2.19 - 2.21, 3 - 3.01)"), 0x24)
        self.assertEqual(netdump2session.kind_type("New RC (2.22+)"), 0x26)
        self.assertIsNone(netdump2session.kind_type("Something else"))

    def test_main_writes_a_file(self):
        with tempfile.TemporaryDirectory() as out:
            with redirect_stdout(io.StringIO()):
                code = netdump2session.main([str(FIXTURE), out, "--name", "x"])
            self.assertEqual(code, 0)
            text = (Path(out) / "x.session").read_text()
            self.assertEqual(text.splitlines(), self.lines)

    def test_no_clients_is_an_error(self):
        with tempfile.TemporaryDirectory() as empty, tempfile.TemporaryDirectory() as out:
            code = netdump2session.main([empty, out])
            self.assertEqual(code, 1)


if __name__ == "__main__":
    unittest.main()
