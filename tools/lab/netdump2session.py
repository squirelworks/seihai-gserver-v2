#!/usr/bin/env python3
# Seihai project, 2026-11-20: see CHANGES-SEIHAI.md.
"""Turn a PACKET_LOGGING server's logs into .session files for replay_handlers (#87).

    python3 tools/lab/netdump2session.py SERVERDIR OUTDIR [--since "YYYY-MM-DD HH:MM:SS"]

SERVERDIR is the server folder whose logs/ holds networkdump.txt and serverlog.txt
(our own loopback lab runs only). Each run of the server becomes one .session file
with every client's packets to the server, in log order, so players that were online
together meet again in the replay. Players are named p1, p2 and so on, not by account,
and passwords never reach the file: the login line keeps only the client type and
version token. See fuzz/handlers/replay_handlers.cpp for the format.
"""

from __future__ import annotations

import argparse
import sys
from datetime import datetime
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import labcheck  # noqa: E402

# The login kind the server prints, to the type byte the client sent (PLTYPE bit + 32).
KIND_TYPE = {
    "Client": 0x20,
    "RC": 0x21,
    "NC": 0x23,
    "New Client (2.19 - 2.21, 3 - 3.01)": 0x24,
    "New Client (2.22+)": 0x25,
    "New RC (2.22+)": 0x26,
}
# Packets that make no sense without a socket or would only repeat the login.
SKIP_IDS = {
    labcheck.PLI_PONG,
}


def kind_type(kind: str) -> int | None:
    if kind in KIND_TYPE:
        return KIND_TYPE[kind]
    if kind.startswith("New Client (2.19"):
        return 0x24
    return None


def convert(logs: labcheck.Logs) -> list[str]:
    """One session text for the whole log: logins in time order, then packets."""
    lines = ["# replay_handlers session, from netdump2session.py"]
    names: dict[tuple[str, int], str] = {}
    for s in logs.sessions():
        if s.login is None:
            continue
        t = kind_type(s.login.kind)
        if t is None:
            continue
        name = f"p{len(names) + 1}"
        names[(s.account, s.player_id)] = name
        lines.append(f"login {name} {t:02x} {s.login.token}")
    for p in logs.packets:
        if p.out or p.pid in SKIP_IDS:
            continue
        name = names.get((p.account, p.player_id))
        if name is None or not p.data:
            continue
        body = " ".join(f"{b:02x}" for b in p.body)
        lines.append(f"{name} {p.pid} {body}".rstrip())
    return lines


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("serverdir", type=Path)
    ap.add_argument("outdir", type=Path)
    ap.add_argument("--since", help='only packets at or after "YYYY-MM-DD HH:MM:SS"')
    ap.add_argument("--name", default=None, help="file stem (default: the server folder's name)")
    args = ap.parse_args(argv)

    since = datetime.strptime(args.since, "%Y-%m-%d %H:%M:%S") if args.since else None
    logs = labcheck.load(args.serverdir, since, None)
    lines = convert(logs)
    players = sum(1 for l in lines if l.startswith("login "))
    if players == 0:
        print("no logged-in clients in the logs", file=sys.stderr)
        return 1
    args.outdir.mkdir(parents=True, exist_ok=True)
    out = args.outdir / f"{args.name or args.serverdir.resolve().name}.session"
    out.write_text("\n".join(lines) + "\n", encoding="ascii")
    print(f"{out}: {players} players, {len(lines) - 1 - players} packets")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
