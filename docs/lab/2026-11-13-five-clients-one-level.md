# #27: five client builds in one level with one GS1 NPC, 2026-11-13

Loopback only, in the Debian lab VM and network namespace used for
`2026-11-13-real-client-matrix.md`. Server: `101a2cce` plus the gmap-warp fix
from #65, with packet logging. The run follows
`runbooks/27-five-clients-one-level.md`.

## Setup

- `servers/milestone`: a copy of the lab `default` folder.
  - Port 14806, name "Lab Milestone".
  - `allowedversions.txt` newmain = `GNW07080, GNW03014, GNW28015, G3D14097:G3D0511C, G3D15010`, with the `modern` line commented out.
  - `(defaultaccount)` starts in `milestone.nw`: the stock start board plus
    one NPC at 30,26, `// lab27npc`, `message lab27npc` on `playerenters`,
    `say2 ... touched by #a` on `playertouchsme`.
- `servers/login` (port 14900, for the 5.x and 6.x clients' built-in login
  host) serverwarps to "Lab Milestone".
- Clients:
  - 1.39, direct to 14806;
  - 2.22 and 2.31, through the listserver;
  - 5.12, via the login server (Windows, Wine, libfaketime 2007-11-19);
  - 6.03r7, via the login server (Linux, native).

  Accounts: `m139`, `m222`, `m231`; 5.12 and 6.03r7 came in as guests
  (`pc:1109225`, `pc:7459860`).

## 5.24r4 could not join

5.24r4 (Windows, libfaketime 2010-02-15) sends `G3D15010`. The version table
does not know it, and the login server refused it: "Client version not allowed.
(Version: G3D15010 unknown)". With `G3D15010` added to the login server's
`allowedversions.txt`, the login was accepted, but the client was never sent
the 178 server warp, so it stayed on the login server. 5.12 took its place, as
the runbook allows. To fix this, the version table (gs2lib) needs a 5.24 entry.

## Results

All five were in `milestone.nw` at once for over 70 s.
`labcheck.py milestone ... --accounts m139,m222,m231,pc:1109225,pc:7459860`:

| Criterion | Result |
|---|---|
| All log in and stay 60 s | **pass** for 1.39, 2.22, 2.31, 5.12 and 6.03r7 (126 to 1006 s of traffic each). 5.24r4: see above |
| Each sees the other four and their movement | **pass on the wire** for all 20 pairs (18 to 37 prop updates each). On screen, 2.22 and 2.31 drew every other player with head and body. **1.39 drew none of the others**, although their props reached it. 5.12 and 6.03r7 drew only black under Wine (names only on 5.12) |
| Chat from any client shows on the others | `c27-m139`, `c27-m222` and `c27-m231` reached all four other clients, and were drawn on 2.22 and 2.31. Chat from 5.12 and 6.03r7 was not typed (no input path under Wine) |
| The GS1 NPC runs on all five | all five got it **as GS1 text**, 5.12 and 6.03r7 too: the NPC is a level NPC and this world has `serverside = false`. Its `message lab27npc` was not seen on the 2.22 screen. Not shown to run |
| Player images and heads on 1.39 | its own player drew with a head. `sword1.png` and `shield1.png` could not be sent (no GIF copies), and the other players were not drawn |
| 6.03r7 reaches the level through the login redirect | **pass**: the login server's 178, then `milestone.nw` |

## Findings

1. **5.24r4 (`G3D15010`) is unknown to the version table.** It is refused
   unless listed by token, and even then it gets no 178. The fix is a gs2lib
   table entry.
2. **1.39 does not draw other players** on this server. Seihai-gserver's
   2026-11-12 run saw 1.39 draw them as a name with no body. Here not even the
   name showed.
3. **The GS1 level NPC goes to 5.12 and 6.03r7 as GS1 text.** With
   `serverside = false` the server compiles no GS2 for them. Whether those
   builds run it was not visible (black screen under Wine).
4. **PNG-only art gaps on 1.39:** `sword1.png` and `shield1.png` (plus
   `wbomb1.png`) were asked for and not sent.
