# #27 rerun: five clients in one level after #68, 2026-11-13

Loopback only, in the Debian lab VM and network namespace used for
`2026-11-13-five-clients-one-level.md`. Server: `9e114583` (PR #68's head,
gs2lib at `65e0e09`), the same code as `seihai` at `980574a7`, built with
packet logging. The run follows `runbooks/27-five-clients-one-level.md`.

## Setup

- **World:** `servers/milestone` and `servers/login`, as in the first run.
  - The hand-added `G3D15010` entries were taken out of both
    `allowedversions.txt` files, so 5.24r4 is let in by the version range
    alone (`G3D14097:G3D0511C`).
  - The milestone accounts were removed first, so every account was new.
- **Clients:**
  - 1.39 (`GNW07080`), direct to 14806;
  - 2.22 (`GNW03014`) and 2.31 (`GNW28015`), through the listserver;
  - 5.24r4 (`G3D15010`, Windows, Wine, libfaketime 2010-02-15) and 6.03r7
    (`G3D0511C`, Linux), through the login server on 14900.

  5.12 was left out.
- **Accounts:** `m139`, `m222`, `m231`. 5.24r4 and 6.03r7 came in as guests
  (`pc:2747842`, `pc:4077956`).
- **Load:** the VM's load average stayed near 15 while all five clients ran.

## Results

| Criterion | Result |
|---|---|
| (a) 5.24r4 logs in, gets the 178, reaches `milestone.nw`, stays 60 s | **pass**. The login server logged `G3D15010 (5.24)` and sent it on; the milestone server logged `New login: guest ... [G3D15010 (5.24)]`. 915 s of traffic, and it saw the other four move |
| (b) On 1.39, the other four players are drawn and move | **partly**. 1.39 now draws the other four, with nickname and chat, where the first run drew none. They show as **heads with no body**. On the wire, 1.39 got 11–54 prop updates per player. Movement was not caught on the 1.39 screen: in the frames taken, the others stood still |
| (c) 1.39 draws its own sword and shield; no failed `sword1` / `shield1` sends | **partly**. 1.39 asked for no `sword1` or `shield1` it could not get; its only unsent file was `wbomb1.png`. Its own shield draws. A sword swing was not caught on screen |
| (d) `labcheck.py milestone` passes all pairs | **pass for every login, stay, sight and movement pair** (20 pairs, 47 PASS rows). The 10 FAIL rows are the chat tokens of 5.24r4 and 6.03r7, which did not chat (no input path for them under Wine). Chat from 1.39, 2.22 and 2.31 reached every other client |
| (e) 2.22 and 2.31 still draw 1.39 normally | **pass**: head, body and shield, with its chat |
| GS1 NPC | unchanged: all five got it as GS1 text, 5.24r4 and 6.03r7 included |

## Left

- 1.39 draws the other players' heads but not their bodies.
- A screen capture of the others moving on 1.39, and of 1.39's own sword swing.
- Chat from 5.24r4 and 6.03r7.
