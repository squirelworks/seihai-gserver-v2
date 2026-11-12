#!/usr/bin/env python3
# Seihai project, 2026-11-12: see CHANGES-SEIHAI.md.
"""Expected-vs-actual checks for the real-client lab runs (issues #25, #26, #27, #34).

Reads what a packet-logging gserver wrote during a run and prints a Markdown
report to paste into a docs/lab/ note. It never talks to a server.

  python3 tools/lab/labcheck.py ITEM SERVERDIR [options]

ITEM is one of:
  ping         #25: PLO_PING (108) only to 4.20r8 and 6.x, PLI_PONG (162) echoes it
  weaponcache  #26: per session, what the server sent for weapons (33/140/193/197)
               and what the client asked back (158)
  milestone    #27: five clients in one level: logins, 60 s, see each other,
               chat tokens, the level NPC
  matrix       #34: one row per session: version, login, levels, files, errors

SERVERDIR is the server folder (servers/<name>): the script reads
logs/networkdump.txt, logs/serverlog.txt and logs/rclog.txt there, and console.log if you
saved the server's standard output to it (that is where "Unknown Player
Packet" goes). Build the server with -DPACKET_LOGGING=ON (or a "Packet
Logging" CMake preset) so networkdump.txt is written.

Options:
  --since "YYYY-MM-DD HH:MM:SS"   ignore log lines before this time
  --accounts a,b,c                 only these accounts (milestone: exactly the five)
  --chat-prefix TEXT               milestone: each client says TEXT<account> (default c27-)
  --npc-marker TEXT                milestone: a string in the test NPC (default lab27npc)

Exit code: 0 when every PASS/FAIL row passed, 1 when any failed, 2 on bad input.
INFO rows never fail a run.

The docs/lab/runbooks/ notes say how to run each item.
"""

from __future__ import annotations

import argparse
import re
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

# Packet ids (gs2lib IEnums.h, upstream names).
PLO_NPCPROPS = 3
PLO_LEVELNAME = 6
PLO_OTHERPLPROPS = 8
PLO_TOALL = 13
PLO_PLAYERWARP = 14
PLO_DISCMESSAGE = 16
PLO_SIGNATURE = 25
PLO_FILESENDFAILED = 30
PLO_NPCWEAPONADD = 33
PLO_PLAYERWARP2 = 49
PLO_FILE = 102
PLO_FILEDECRYPTKEY = 104
PLO_PING = 108
PLO_NPCBYTECODE = 131
PLO_WEAPONBYTECODE2 = 140
PLO_ONLINEMODE = 168
PLO_SERVERWARP = 178
PLO_WEAPONHASH = 193
PLO_LOADSCRIPT = 197
PLI_LEVELWARP = 0
PLI_TOALL = 6
PLI_WANTFILE = 23
PLI_UPDATESCRIPT = 158
PLI_PONG = 162

# Version labels as the server prints them ("[G3D20126 (4.208)]").
PING_LABELS = {"4.208", "6.015", "6.034/6.035", "6.037", "6.037 (Linux)"}
WEAPONCACHE_LABELS = {"4.0211", "4.034", "4.042", "4.110", "4.208", "5.07", "5.12"}

TS = r"\[(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2})\]"
HEADER = re.compile(TS + r" ([<>]) (?:Out Packet to|In Packet from) (.*?) \[ID (\d+)\]: \[(\d+)(?: RAW)?\] (\S+) \((\d+) bytes\)")
HEXLINE = re.compile(TS + r" ((?:[0-9a-f]{2} )+)\s*$")
LOGIN = re.compile(TS + r" :: New login: (\S+) \[([^\]]*)\] \[(.*)\]\s*$")
LOGIN_VERSION = re.compile(r"^(?:SEI00001 \(inner (\S+)\)|(\S+)) \((.*)\)$")
DISCONNECT = re.compile(TS + r" \*\* \[Disconnect\] '?([^':]+)'?: (.*)$")
PINGLOG = re.compile(TS + r" \[Ping\] (\S+): (\d+) ms")


def parse_time(text: str) -> datetime:
    return datetime.strptime(text, "%Y-%m-%d %H:%M:%S")


@dataclass
class Packet:
    time: datetime
    out: bool  # True: server to client.
    account: str
    player_id: int
    pid: int
    name: str
    size: int
    data: bytes = b""  # The whole message as logged, its id byte included.

    @property
    def body(self) -> bytes:
        return self.data[1:]

    def text(self) -> str:
        return self.data.decode("latin-1")


@dataclass
class Login:
    time: datetime
    account: str
    kind: str
    token: str
    label: str
    secure: bool


@dataclass
class Session:
    account: str
    player_id: int
    packets: list[Packet] = field(default_factory=list)
    login: Login | None = None

    @property
    def label(self) -> str:
        return self.login.label if self.login else "?"

    def out(self, pid: int) -> list[Packet]:
        return [p for p in self.packets if p.out and p.pid == pid]

    def inn(self, pid: int) -> list[Packet]:
        return [p for p in self.packets if not p.out and p.pid == pid]

    def span_seconds(self) -> float:
        if not self.packets:
            return 0.0
        return (self.packets[-1].time - self.packets[0].time).total_seconds()


@dataclass
class Logs:
    packets: list[Packet]
    logins: list[Login]
    disconnects: list[tuple[datetime, str, str]]
    pings: list[tuple[datetime, str, int]]
    unknown: list[str]

    def sessions(self) -> list[Session]:
        by_key: dict[tuple[str, int], Session] = {}
        order: list[Session] = []
        for p in self.packets:
            key = (p.account, p.player_id)
            s = by_key.get(key)
            relogged = s is not None and s.packets and any(
                login_account_matches(l.account, p.account) and s.packets[-1].time < l.time <= p.time for l in self.logins)
            if s is None or relogged or (s.packets and (p.time - s.packets[-1].time).total_seconds() > 300):
                s = Session(p.account, p.player_id)
                by_key[key] = s
                order.append(s)
            s.packets.append(p)
        # Each session takes the latest login of its account at or before its first packet.
        for s in order:
            first = s.packets[0].time
            cands = [l for l in self.logins if login_account_matches(l.account, s.account) and l.time <= first]
            s.login = cands[-1] if cands else next((l for l in self.logins if login_account_matches(l.account, s.account)), None)
        return order


def is_guest_account(account: str) -> bool:
    """A guest logs in as "guest" and plays as "pc:<number>", the name the server gives it."""
    return account == "guest" or account.startswith("pc:")


def login_account_matches(login_account: str, session_account: str) -> bool:
    if login_account == session_account:
        return True
    return login_account == "guest" and session_account.startswith("pc:")


def gshort(data: bytes) -> int:
    return ((data[0] - 32) << 7) + (data[1] - 32)


def read_lines(path: Path) -> list[str]:
    if not path.exists():
        return []
    return path.read_bytes().decode("latin-1").splitlines()


def load(serverdir: Path, since: datetime | None, accounts: set[str] | None) -> Logs:
    logdir = serverdir / "logs"
    packets: list[Packet] = []
    current: Packet | None = None
    for line in read_lines(logdir / "networkdump.txt"):
        m = HEADER.match(line)
        if m:
            t, arrow, who, pidnum, pid, name, size = m.groups()
            current = Packet(parse_time(t), arrow == "<", who, int(pidnum), int(pid), name, int(size))
            packets.append(current)
            continue
        h = HEXLINE.match(line)
        if h and current is not None and not current.data:
            current.data = bytes(int(x, 16) for x in h.group(2).split())
            current = None

    logins, disconnects, pings = [], [], []
    # Disconnect reasons go to serverlog.txt or rclog.txt, depending on the reason.
    for line in read_lines(logdir / "serverlog.txt") + read_lines(logdir / "rclog.txt"):
        if m := LOGIN.match(line):
            t, account, kind, rest = m.groups()
            v = LOGIN_VERSION.match(rest)
            if v:
                inner, token, label = v.groups()
                logins.append(Login(parse_time(t), account, kind, inner or token, label, inner is not None))
        elif m := DISCONNECT.match(line):
            disconnects.append((parse_time(m.group(1)), m.group(2), m.group(3)))
        elif m := PINGLOG.match(line):
            pings.append((parse_time(m.group(1)), m.group(2), int(m.group(3))))
    unknown = [l for l in read_lines(serverdir / "console.log") if "Unknown Player Packet" in l]

    def keep(t: datetime, account: str) -> bool:
        if since is not None and t < since:
            return False
        if accounts is None or account in accounts:
            return True
        # "guest" on the command line also picks its pc:<number> session, and the other way round.
        return is_guest_account(account) and any(is_guest_account(a) for a in accounts)

    return Logs(
        [p for p in packets if keep(p.time, p.account)],
        [l for l in logins if keep(l.time, l.account)],
        [d for d in disconnects if keep(d[0], d[1])],
        [p for p in pings if keep(p[0], p[1])],
        unknown,
    )


class Report:
    def __init__(self, title: str):
        self.title = title
        self.rows: list[tuple[str, str, str]] = []

    def check(self, ok: bool, what: str, detail: str = "") -> None:
        self.rows.append(("PASS" if ok else "FAIL", what, detail))

    def info(self, what: str, detail: str = "") -> None:
        self.rows.append(("INFO", what, detail))

    def failed(self) -> bool:
        return any(r[0] == "FAIL" for r in self.rows)

    def render(self) -> str:
        out = [f"### {self.title}", "", "| Result | Check | Detail |", "|---|---|---|"]
        for result, what, detail in self.rows:
            out.append(f"| {result} | {what} | {detail.replace('|', '/')} |")
        return "\n".join(out) + "\n"


def describe(s: Session) -> str:
    return f"{s.account} ({s.label}{', SEI00001' if s.login and s.login.secure else ''})"


# --- #25 ---------------------------------------------------------------------

def check_ping(logs: Logs) -> Report:
    r = Report("#25 PLO_PING / PLI_PONG")
    sessions = logs.sessions()
    if not sessions:
        r.check(False, "packets in networkdump.txt", "none: was the server built with PACKET_LOGGING?")
        return r
    for s in sessions:
        pings = s.out(PLO_PING)
        pongs = s.inn(PLI_PONG)
        if s.label in PING_LABELS:
            r.check(len(pings) > 0, f"{describe(s)} is sent 108", f"{len(pings)} sent")
            bad_sub = [p for p in pings if len(p.body) < 1 or p.body[0] != 0x20]  # GChar 0
            r.check(not bad_sub, f"{describe(s)}: every 108 is sub 0", f"{len(bad_sub)} with another sub")
            sent = {gshort(p.body[1:3]) for p in pings if len(p.body) >= 3}
            game = [p for p in pongs if len(p.body) == 3]
            r.check(len(game) > 0, f"{describe(s)} answers with 162", f"{len(game)} answers, {len(pongs) - len(game)} other 162s")
            echoed = [p for p in game if p.body[0] == 0x21 and gshort(p.body[1:3]) in sent]
            r.check(len(echoed) == len(game) and bool(game), f"{describe(s)}: each 162 is '!' + a value we sent",
                    ", ".join(p.data.hex(" ") for p in game[:3]))
            rtts = [ms for _, a, ms in logs.pings if a == s.account]
            r.info(f"{describe(s)} round trips", f"{len(rtts)} logged" + (f", {min(rtts)} to {max(rtts)} ms" if rtts else ""))
        else:
            r.check(not pings, f"{describe(s)} is never sent 108", f"{len(pings)} sent")
            if pongs:
                r.info(f"{describe(s)} sent 162 anyway", f"{len(pongs)}: " + ", ".join(p.data.hex(" ") for p in pongs[:3]))
    r.check(not logs.unknown, "no 'Unknown Player Packet' in console.log", "; ".join(logs.unknown[:3]))
    return r


# --- #26 ---------------------------------------------------------------------

def weapon_name_of(p: Packet) -> str:
    # PLO_WEAPONHASH {193}{i5 crc}{name}; PLO_NPCWEAPONADD {33}{c len}{name}...
    body = p.body
    if p.pid == PLO_WEAPONHASH and len(body) > 5:
        return body[5:].decode("latin-1")
    if p.pid == PLO_NPCWEAPONADD and body:
        n = body[0] - 32
        return body[1:1 + n].decode("latin-1")
    if p.pid == PLI_UPDATESCRIPT:
        return body.decode("latin-1").strip()
    return ""


def check_weaponcache(logs: Logs) -> Report:
    r = Report("#26 weapon script cache (193 / 158)")
    by_account: dict[str, list[Session]] = defaultdict(list)
    for s in logs.sessions():
        by_account[s.account].append(s)
    if not by_account:
        r.check(False, "packets in networkdump.txt", "none: was the server built with PACKET_LOGGING?")
    for account, sessions in by_account.items():
        for n, s in enumerate(sessions, 1):
            adds = s.out(PLO_NPCWEAPONADD)
            hashes = s.out(PLO_WEAPONHASH)
            asks = s.inn(PLI_UPDATESCRIPT)
            r.info(f"{describe(s)} login {n}",
                   f"33 x{len(adds)} ({sum(p.size for p in adds)} bytes), 140 x{len(s.out(PLO_WEAPONBYTECODE2))}, "
                   f"193 x{len(hashes)} [{', '.join(weapon_name_of(p) for p in hashes[:6])}], 197 x{len(s.out(PLO_LOADSCRIPT))}, "
                   f"158 asked x{len(asks)} [{', '.join(weapon_name_of(p) for p in asks[:6])}]")
            if s.label in WEAPONCACHE_LABELS:
                r.check(len(hashes) > 0, f"{describe(s)} login {n}: weapons go out as 193", f"{len(hashes)}")
        if len(sessions) >= 2 and sessions[-1].label in WEAPONCACHE_LABELS:
            last = sessions[-1]
            r.check(not last.inn(PLI_UPDATESCRIPT), f"{account}: the last login asks no script back (cache used)",
                    f"{len(last.inn(PLI_UPDATESCRIPT))} 158s")
    return r


# --- #27 ---------------------------------------------------------------------

def check_milestone(logs: Logs, accounts: list[str], chat_prefix: str, npc_marker: str) -> Report:
    r = Report("#27 five clients, one level, one GS1 NPC")
    sessions = {s.account: s for s in logs.sessions()}  # The last session of each account.
    ids = {a: s.player_id for a, s in sessions.items()}
    for a in accounts:
        s = sessions.get(a)
        r.check(s is not None and s.login is not None, f"{a} logged in", describe(s) if s else "no packets")
        if s is None:
            continue
        r.check(s.span_seconds() >= 60, f"{a} stayed 60 s", f"{s.span_seconds():.0f} s of traffic")
        disc = [d for d in logs.disconnects if d[1] == a]
        if disc or s.out(PLO_DISCMESSAGE):
            r.check(False, f"{a} not disconnected", "; ".join(d[2] for d in disc) or s.out(PLO_DISCMESSAGE)[0].text()[1:])

        seen = defaultdict(int)
        for p in s.out(PLO_OTHERPLPROPS):
            if len(p.body) >= 2:
                seen[gshort(p.body[:2])] += 1
        for b in accounts:
            if b == a or b not in ids:
                continue
            n = seen.get(ids[b], 0)
            r.check(n >= 2, f"{a} sees {b} and its movement", f"{n} prop updates for id {ids[b]}")
            token = f"{chat_prefix}{b}"
            heard = any(token in p.text() for p in s.packets if p.out)
            r.check(heard, f"{a} gets {b}'s chat '{token}'")

        npc = s.out(PLO_NPCPROPS)
        text = any(npc_marker in p.text() for p in npc)
        code = s.out(PLO_NPCBYTECODE)
        if text:
            r.check(True, f"{a} gets the NPC", "as GS1 text")
        elif code:
            r.info(f"{a} gets the NPC", f"as bytecode (131 x{len(code)}); no GS1 text with '{npc_marker}'")
        else:
            r.info(f"{a} gets the NPC", f"FINDING: no NPC text with '{npc_marker}' and no bytecode ({len(npc)} NPC prop messages)")

        failed = [p.text()[1:] for p in s.out(PLO_FILESENDFAILED)]
        if failed:
            r.info(f"{a} files the server could not send", ", ".join(failed[:12]))
        if s.label.startswith("6."):
            warps = s.out(PLO_SERVERWARP)
            levels = [p.text()[1:] for p in s.out(PLO_LEVELNAME)]
            r.info(f"{a} level reached", f"{', '.join(levels[:3]) or 'none'}; 178 from this server x{len(warps)} (the lobby's 178 is not in this log)")
    return r


# --- #34 ---------------------------------------------------------------------

def check_matrix(logs: Logs) -> Report:
    r = Report("#34 post-merge client matrix")
    sessions = logs.sessions()
    if not sessions:
        r.check(False, "packets in networkdump.txt", "none: was the server built with PACKET_LOGGING?")
    for s in sessions:
        outs = [p for p in s.packets if p.out]
        disc = s.out(PLO_DISCMESSAGE)
        online = s.out(PLO_ONLINEMODE) or s.out(PLO_SIGNATURE)
        detail = [
            f"first out: {outs[0].name if outs else '-'}",
            f"levels: {', '.join(dict.fromkeys(p.text()[1:] for p in s.out(PLO_LEVELNAME))) or '-'}",
            f"warps asked: {len(s.inn(PLI_LEVELWARP))}",
            f"files sent: {len(s.out(PLO_FILE))}, failed: {', '.join(p.text()[1:] for p in s.out(PLO_FILESENDFAILED))[:200] or '-'}",
            f"{s.span_seconds():.0f} s",
        ]
        if s.out(PLO_FILEDECRYPTKEY):
            first104 = outs.index(s.out(PLO_FILEDECRYPTKEY)[0])
            detail.append(f"104 at out #{first104 + 1}")
        r.check(bool(online) and not disc, f"{describe(s)} logged in",
                "; ".join(detail) + (f"; DISCONNECTED: {disc[0].text()[1:]}" if disc else ""))
    for t, account, why in logs.disconnects:
        r.info(f"server disconnect {account}", f"{t:%H:%M:%S} {why}")
    r.check(not logs.unknown, "no 'Unknown Player Packet' in console.log", "; ".join(logs.unknown[:5]))
    return r


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("item", choices=["ping", "weaponcache", "milestone", "matrix"])
    ap.add_argument("serverdir", type=Path)
    ap.add_argument("--since")
    ap.add_argument("--accounts")
    ap.add_argument("--chat-prefix", default="c27-")
    ap.add_argument("--npc-marker", default="lab27npc")
    args = ap.parse_args(argv)

    if not (args.serverdir / "logs").is_dir():
        print(f"{args.serverdir}/logs not found", file=sys.stderr)
        return 2
    accounts = [a for a in (args.accounts or "").split(",") if a]
    if args.item == "milestone" and len(accounts) != 5:
        print("milestone needs --accounts with the five accounts", file=sys.stderr)
        return 2
    logs = load(args.serverdir, parse_time(args.since) if args.since else None, set(accounts) or None)

    if args.item == "ping":
        report = check_ping(logs)
    elif args.item == "weaponcache":
        report = check_weaponcache(logs)
    elif args.item == "milestone":
        report = check_milestone(logs, accounts, args.chat_prefix, args.npc_marker)
    else:
        report = check_matrix(logs)
    print(report.render())
    return 1 if report.failed() else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
