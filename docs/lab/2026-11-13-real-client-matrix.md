# Real-client lab run, 2026-11-13: ping, weapon cache, post-merge matrix

Loopback only. Everything ran in a Debian lab VM, inside one private network
namespace whose only interface is `lo`. A private `/etc/hosts` pointed the clients'
built-in listserver and login host names at 127.0.0.1. Windows clients ran
under 32-bit Wine and were driven with `xdotool`.
Issues: #25, #26, #34. The #27 run is a separate note.

## Builds

| Part | Commit | Build |
|---|---|---|
| This fork | `seihai` at `e3da7ace` (#60), then `101a2cce` (#62) | GCC 14 + vcpkg `x64-linux`, `-DPACKET_LOGGING=ON` |
| This fork + the gmap-crash fix | `101a2cce` + this branch's `Server.cpp` change | the same, incremental |
| seihai-listserver | `ae213fc` | `cargo build --release` |

The lab world is `bin/servers/` with these changes:
- `login`: a copy of `modern` on port 14900 that serverwarps to "Lab Modern".
- `modern`: `labart.gpake`, the GS2 weapon `-labgs2` and `clientping = 5`, as on
  2026-11-12.
- Every server: `localhostlist = true` and `listport = 14901`.
- `default` and `classic`: a warp room `labroom.nw`, linked at x 37 from the start
  level, and a 2x1 gmap `lab.gmap` (`gm_a.nw`, `gm_b.nw`), linked at x 23.
- `default` renamed "A Lab Default", so it is the first row in every list.

Wine has no HTML control. The "Server Message" window it opens after login is
blank, and while it is open the 2.x and 1.x clients never send a packet. For
the run `servermessage.html` was left empty. This is a lab-only setting; the
server does nothing wrong here.

## Clients

| Client | Token | How |
|---|---|---|
| 1.39 | `GNW07080` | Wine, "One Player Online" to 127.0.0.1:14803 |
| 1.41r1 | `GNW13110` | Wine, list on 14923 |
| 2.05 rev 2 | `GNW03081` | Wine with the GDI renderer, list on 14921 |
| 2.17.1 | `GNW22122` | Wine, list on 14922 |
| 2.22, 2.31 | `GNW03014`, `GNW28015` | Wine, list on 14922 |
| 4.20r8 (unpacked Windows) | `G3D20126` | Wine with libfaketime at 2007-07-01, via the login server |
| 5.12 (Windows) | `G3D14097` | Wine with libfaketime at 2007-11-19, via the login server |
| 6.03r7 (Linux) | `G3D0511C` | native, via the login server |
| 2.22 RC ("Graal Remote Control 2004/01/17") | `GSERV024` | Wine virtual desktop, list on 14922 |

The 4.x and 5.x Windows clients stop working when the clock is far past their
build date (the Win10 lab saw the same). libfaketime in their Wine process was
enough: no client was changed. No 3.153 client was available, so that row was
not run.

## #25 ping, #26 weapon cache (`servers/modern`)

The sequence was 4.20r8, 6.03r7, 5.12, then 4.20r8 again with its weapon cache
kept. It ran on both builds. `tools/lab/labcheck.py` on `101a2cce`:

| Check | Result |
|---|---|
| 4.20r8 and 6.03r7 are sent 108, sub 0 only | PASS (4.20r8: 6 and 7 per session; 6.03r7: 6; e3da run: 9, 6 and 39) |
| each answers 162 = `'!'` + a value we sent | PASS (`c2 21 20 21`, ...) |
| round trips | 0 to 35 ms |
| 5.12 is never sent 108 | PASS, over a 45 s session |
| no "Unknown Player Packet" | PASS |
| 4.20r8 login 1 | `-labgs2` as 193, the client asked for it with 158 once |
| 4.20r8 login 2 | 193 again, **no 158**: the cache was used. `weblevels/weapons/Lab%032Modern/%045labgs2.code` (164 bytes) was not rewritten |
| 5.12 and 6.03r7 | 197 (`PLO_LOADSCRIPT`), never 193 |

Two `labcheck.py` fixes came out of the first pass:
- It still expected 193 from 5.07 and 5.12. Since #58 those builds get 197, so
  they are now checked for 197.
- A 6.03r7 guest still online when 5.12 logged in, also as a guest, was split
  into a session labelled 5.12. A guest's session (`pc:<number>`) no longer ends
  when another guest logs in.

#25 and #26 are closed.

## #34 matrix

"walk" means arrow keys moved the player and the server saw the props. "warp"
means the link to `labroom.nw` and the link back. "gmap" means the link into
`lab.gmap` and a walk across the gm_a/gm_b edge. "chat" means the chat text
reached the server. "relogin" means log out and log in again.

| Client | World | Build | walk | warp | gmap | chat | relogin |
|---|---|---|---|---|---|---|---|
| 1.39 | classic | e3da | pass | pass | n/a (classic has no gmaps) | pass | pass |
| 1.41r1 | classic | e3da | pass | pass | **server crash**, see below | pass | pass |
| 2.05r2 | default | 101a | pass | pass | stays on "loading" (pre-gmap build) | pass | pass |
| 2.17.1 | default | e3da | pass | pass | the link to `lab.gmap` does nothing (pre-gmap build) | pass | pass |
| 2.22 | default | e3da | pass | pass | pass: gm_a, then gm_b across the edge | pass | pass |
| 2.31 | default | e3da | pass | pass | pass: gm_a, then gm_b | pass | pass |
| 4.20r8 | modern | both | login, 104 as packet 6, the `.gpake` image drawn | | | | pass |
| 5.12 | modern | both | login, 104 as packet 6, the `.gpake` image drawn (the tiles are black under Wine's D3D) | | | | pass |
| 6.03r7 | modern | both | login through the 178 warp, 104 as packet 6, the `.gpake` image drawn | | | | |

`labcheck.py matrix` passed every session on `default`, `classic` and `modern`.
Nothing logged "Unknown Player Packet".

### Real 2.22 RC on `default` (101a2cce)

1. **Account edit keeps the admin fields.** `rcv` (`LOADONLY 1`,
   `ADMINLEVEL 2`, `ADMINWORLDS Lab Default,Lab Classic`): Edit Account, Apply.
   The server logged "labrc set the attributes of player rcv". A load-only
   account is never written, so the file stayed byte for byte the same. `rcw`
   (the same, not load-only): Edit Account, Apply. The file was rewritten and
   still has `ADMINLEVEL 2` and `ADMINWORLDS Lab Default,Lab Classic`. **Pass.**
2. **Rights.** Edit Rights on `rcw`, folder rights `rw levels/*`, Apply. The
   server logged "labrc has set the rights of rcw". The file has
   `FOLDERRIGHT rw levels/*`, and the admin level and worlds are kept. **Pass.**
3. **Ban an online player.** 2.22 `x222` was online. Player list, Edit Ban,
   Banned, reason "lab ban c34", Apply. The server logged "labrc has set the ban
   of x222" and disconnected `x222`. The account has `BANNED 1` and the reason. A
   2.31 then logged in at once. **Pass**, no hang.

### `npcdemo` with a real 2.22 (101a2cce)

- #1, the Greeter: "Awake for N seconds" counted up (`timeout`).
- #6, the Listener: the chat `time` was answered with "timevar is 162703632".
- #4, a block: walking into it ran `setlevel2` to `npcdemo_room.nw`.
- The room's Keeper said "You have not touched the counter yet" (`server.`
  flags read back).

**Pass** for the NPCs that were walked to. #2, #3, #5 and #7 to #12 were not
reached in this run.

## Found: a warp to a gmap crashes a classic server

On `servers/classic` (`generation = classic`), a level link to `lab.gmap`, with
`gmaps = lab.gmap` set, took the server down twice with SIGSEGV (exit 139). Both
times it was 1.41r1 walking into the link. The log ended with:

```
[ERROR] Server generation does not support gmaps, refusing to load lab.gmap.
```

`Server::getLoadedLevel` found the gmap's unloaded stub through
`findGmapForLevel`, `loadLevelInto` refused it, and the stub was returned
anyway. `Player::enterLevel` then used it. A client names the level in
`PLI_LEVELWARP`, so any player on a classic server could do this, not only
one following a bad link. This branch returns `nullptr` when the load fails, in
that path and in the hint-level path. With the fix, the same walk gets
`PLO_WARPFAILED` (15) and the server stays up (the client shows "loading").

## Findings, no change made

- 1.39 shows `servermessage.html` as raw HTML source in its message window. The
  classic folder's message is HTML.
- 2.05r2 and 2.17.1 cannot enter a gmap. The link takes 2.05r2 to "loading"; on
  2.17.1 it does nothing. Gmaps start with the 2.2x clients in this run.
- Under load, 2.x clients raise a "game is running at low frame rate" box.
  Closing it through the window manager hangs the client, but its OK button
  does not.
- The 5.24r4 token `G3D15010` is not in the version table: "Client version not
  allowed. (Version: G3D15010 unknown)". This matters for #27.
