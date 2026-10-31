# Built-in NPC-server (GS1) lab run, 2026-10-31

What the fork's built-in NPC-server (`serverside = true`) does today, tested on a
loopback lab, and what GS1 it lacks for the sample world and for the levels
that shipped with the 1.x clients.

- Server: `seihai-gserver-v2`, branch `seihai` at `083306a`, Linux release build
  (GCC 14), reports `4.0.0-beta`.
- Worlds: `bin/servers/npcdemo` (in this repo) and a lab-only copy of it with
  the 690 `.graal` levels from the 1.x client archive (`15thgraal`), not in
  the repo.
- Client: `tools/lab/npcprobe`, a small headless client built on
  `seihai-core` (the Seihai client's protocol code). It logs in, walks to
  given spots, chats, sends `triggeraction`s and warps, and prints every
  server message. Versions used: 2.31 for everything, 2.05, 2.17 and 2.22 for
  the sample world.
- No real Graal client was run in this session. Real 2.x clients under Wine
  are the next check (see "Next steps").

## How to repeat

```
# build the fork (see the handoff notes or the README), then:
cd bin && ./gs2emu --server npcdemo &
cd ../tools/lab/npcprobe && cargo build --release
./target/release/npcprobe 127.0.0.1:14804 2.31 labtest ../steps/npcdemo.txt | less
# script errors go to bin/servers/npcdemo/logs/npclog.txt and to NC
```

The classic sweep: copy the `.graal` files into a second server folder (not
in the repo), write a steps file that warps into each level and walks into
every NPC that has `playertouchsme`, and read `logs/npclog.txt` and the
console. `tools/lab/graalnpcs.py` lists a level's NPC scripts and
`tools/lab/scan.py` does the static scan below.

## Sample world: results

All on 2.31; 2.05, 2.17 and 2.22 gave the same results for the touch, sign,
counter, weapon, split-script and warp NPCs. No disconnects.

| NPC | Works | Notes |
|---|---|---|
| Greeter: `created`, `showcharacter`, `playerenters`, `timeout` | yes | `#n` inside `setcharprop` is the NPC's nick, see bug 3 |
| Sign: `playertouchsme`, `say2` | yes | needs `setshape`, see gap 1 |
| Counter: `this.`, `server.` flags, `message` | partly | `setstring server.x,#a` deletes the flag, see bug 2 |
| Warp: `setlevel2` | yes | also `playerx = ..; playery = ..; setlevel ..;` |
| Weapon giver: `addweapon`, `hasweapon` | yes | weapon and its client half arrive |
| Listener: `playerchats`, `strequals`, `timevar` | yes | |
| Trigger target: `if (actiondemo)` | yes | only with `setshape` |
| Split script (`//#CLIENTSIDE`) | yes | server half runs, client half is sent in the NPC props |
| Spawner: `putnpc2` + `destroy` | yes | NPC appears, deleted after its 5 s timeout |
| Walker: `move` + `movementfinished` | yes | walks back and forth |
| Blinker: `hide`, `show`, `visible` | no | hides once, never shows again, see bug 1 |
| Strings: `setstring`, `addstring`, `strlen`, `tokenize`, `#t` | yes | |
| Weapon server half: `triggeraction 0,0,serverside,Demo Wand,zap` | yes | |
| Control-NPC: `playerlogin` (`sendpm`) | yes | |

Also checked with chat-driven micro tests (`tools/lab/microtests.gs1`):
arrays (`{1,2,3}`, `arraylen`), `for` loops, `sleep`, `random`, `int`, `abs`,
`sin`, `sarraylen`, `#I`, `#P1`, `client.` strings, unprefixed player flags,
`say 0` (sign text sent), `lay greenrupee` (item appears), `playerrupees`
assignment. All worked. `play` and `toinventory` are refused as client-side
and the rest of the block still runs.

## Bugs found (in priority order)

1. **Crash when an idle level with items is unloaded.** Fixed in the
   `seihai-fix-level-unload-crash` PR, see `2026-10-31-level-unload-crash.md`. The level destructor
   removes its items with "inform clients", which asks for a shared pointer to
   the level that is being destroyed. The server stops with an unhandled
   `bad_weak_ptr` (console: `Level::removeItem` called from the level's
   destructor, from the timed level-unload event). Seen once, 15 minutes into
   the classic sweep, after many levels with dropped items had gone idle.
   Fix: in `Level::~Level` remove items without informing clients (nobody is
   in the level), or just clear `m_items`. Needs its own PR and a lab run that
   lays an item, leaves the level and waits past `unloadinactiveleveltime`.
2. **`#a` (and `#g`) are empty in level NPC events.** `mc_a` and the guild code
   look up the player from the current source, which in an NPC event is the
   NPC. `#n`, `#c` and the other character codes use the nearest player on
   the stack and work. Effect: `setstring server.x,#a;` deletes the flag,
   `sendtorc #a has logged in!` prints an empty name.
3. **`#n` inside `setcharprop` is the NPC's nick.** `setcharprop` pushes the
   NPC as the current source before reading its arguments, so
   `setcharprop #c,Welcome #n!` greets the NPC by its own name. In `message`
   `#n` is the player. GS1 levels from the 1.x era use the `setcharprop #c,..#n`
   form to greet the player, so this should resolve the value arguments
   against the player first. Needs a check of what the original did.
4. **`visible` is always true after `created`.** It tests
   `visFlags != HIDDEN`, but `visFlags` also carries the `CREATED` bit, so it
   never equals `HIDDEN` again. `if (visible) hide; else show;` hides once and
   then hides forever. Fix: test the `VISIBLE` bit.
5. **`message X=..` with a one-letter word before `=` sends nothing.**
   `message P=1;`, `message H=#v(playerhearts);` and `message Q=2;` send no
   NPC chat; `message A P=1;` and `message hp #v(playerhearts);` work. Odd:
   `message R=#v(playerrupees);` works. Probably the parser reads `X=...` as
   an assignment. Low priority.
6. `hurt 1;` from a level NPC sends nothing to the player (no hit, no hearts
   change) on 2.31. Not looked into.

## Gaps (missing GS1 or behaviour)

1. **NPC size comes only from `setshape`.** The server never reads image
   sizes, so a level NPC with `block.png` or any other image is 0x0 on the
   server: `playertouchsme`, `triggeraction` at its position, and hits never
   reach it. Almost no 1.x level calls `setshape`. Options: read GIF/PNG/MNG
   sizes from the world folder when an NPC's image is set (the server has the
   files), with a small cache; fall back to 32x32 for the built-in images the
   server does not have (`block.png`, `door.gif` and the like).
2. **Scripts without `//#CLIENTSIDE` run only on the server.** None of the
   1,648 NPC scripts in the 690 1.x levels has the marker, so every one runs
   entirely server-side and the client gets none of it. Any client-side
   command or flag in them fails (see the sweep). A compatibility switch is
   needed for classic worlds, for example `classicnpcscripts = true`: send
   the whole script to the client and run only the server-meaningful events
   on the server, or run nothing server-side for levels that match a glob.
   This needs a design decision (owner).
3. **`shootball;` without a direction is refused** ("invalid arguments:
   shootball dir") when `shootball=true` in `scriptengine-gs1.txt`, the
   default. 1.x levels use the bare form (and it fires from `timeout`
   loops, 2,292 log lines from 14 levels in the sweep). Accept 0 arguments and shoot toward
   the nearest player, or the NPC's `dir`.
4. **Client-side-only commands used by 1.x levels**, refused server-side:
   `play` (125 uses in 67 levels), `toinventory` (16 in 16), `putleaps` (11 in
   4), `openurl` (3), `stopmidi` (3), `noplayerkilling` (3), `followplayer`
   (2), `seturllevel` (1). With gap 2 solved these would run on the client.
   Without it, `play` could be forwarded to the players in the level.
5. **Older GS1 syntax the parser rejects.** The sweep logged 12 scripts in
   11 levels that do not parse (missing `;` before `else` or `}`, `playerx
   45.5` without `=`, and the like; list in the sweep section) and 129
   scripts in 20 levels (GR-V1.01 to 1.03) that do not lex, at line 1
   column 1 on control bytes or `\`. The lexer failures may be damaged
   levels or the `.graal` loader reading the NPC section wrongly; compare
   with Seihai's own GR-V1.0x loader before changing the grammar. 1.x levels
   also glue commands to their first argument (`hurt1`, `say0;`,
   `putcomplizardon,30,44;`, `setskincolororange`; about 190 uses in the
   static scan). These logged no error, so the lexer seems to split them,
   but nobody checked that they do the right thing.
6. Not implemented server-side (they throw an error): `saveinfo`,
   `showstats`, the function `findnearestplayers`.
7. `localhost = true` still tries to connect to the listserver every few
   seconds and logs an error each time. Harmless; it should not try at all.

## Static scan of the 1.x levels

`tools/lab/scan.py` over the 690 levels: 1,648 NPC scripts in 449 levels.
Most-used commands, all supported by the fork: `say` 774, `hide` 633, `set`
458, `unset` 353, `putcomp` 195, `take` 137, `setgif` 122, `hurt` 78, `show`
75, `setlevel` 60, `dontblock` 52, `drawunderplayer` 50, `shootarrow` 48,
`lay` 47, `shootball` 47, `removecompus` 38, `setplayerdir` 27, the `set*color`
commands about 20 each, `drawoverplayer` 19, `putexplosion` 19, `say2` 13.
Every function the levels call (`onwall`, `random`, `strequals`, ...) exists.
The gaps are the client-side commands in gap 4 and the syntax in gap 5.

## Classic sweep (runtime)

Probe as 2.31 on a lab-only copy of `npcdemo` holding the 690 `.graal` levels.
It warped into each of the 449 levels that have NPC scripts, waited for
`created`, `playerenters` and the first timeouts, and walked into every NPC
with `playertouchsme`. Two passes, because the server crashed after level 341
(bug 1); the second pass did the remaining 109 levels without a crash.

| Result | Count |
|---|---|
| Levels entered | 449 of 449 |
| Server crashes | 1 (bug 1, level unload) |
| Disconnects of the probe | 1 (the crash) |
| `npclog.txt` lines | 2,380 |
| `invalid arguments: shootball dir` (gap 3) | 2,292 lines, 14 levels |
| `Clientside only command: 'play'` (gap 4) | 81 lines, 69 levels |
| `Clientside only command: 'noplayerkilling'` | 5 lines, 3 levels |
| `Clientside only command: 'stopmidi'` | 2 lines, 2 levels |
| GS1 parse failures (gap 5) | 12 scripts in 11 levels |
| GS1 lexer failures (gap 5) | 129 scripts in 20 levels |

Parse failures, by error: `=` or `,` where the grammar wants `;` (3),
`missing '(' at 'got'` (2), `no viable alternative at input 'x 0'` (2),
`playerx 45.5` without `=` (1), `else` after a statement with no `;` (1),
`timeout` where `}` was expected (1), extra `if`/`show` (2). Levels:
`DW19`, `DW28`, `DW5`, `GerudoFort_Inn`, `ICEP24`, `palace_level4_house2_1`,
`palace_level4_house2_2`, `palace_shadoworb`, `skytf1r4`, `skytf1r6`, `zgrave`.

Not covered by the sweep: events that need fighting or items (`washit`,
`wasshooted`, `exploded`, `waspelt`, `playerlaysitem`), chests, and anything
after a `setlevel` an NPC does on touch. Commands that ran without an error
may still do the wrong thing: the sweep only reads errors, it does not check
what the client sees.

## Next steps (one branch and PR each)

1. Fix the level-unload crash (bug 1), with a lab run.
2. Fix `#a`/`#g` and `visible` (bugs 2 and 4), with the sample world as the test.
3. Decide the classic-script model (gap 2) with the owner, then implement it.
4. Image sizes for NPC shapes (gap 1).
5. Bare `shootball;` (gap 3) and the older syntax in gap 5, with a parser
   test per form, after checking the lexer failures against Seihai's loader.
6. `#n` in `setcharprop` (bug 3) once the original behaviour is confirmed.
7. Rerun this sample world and the sweep with real 2.05, 2.17, 2.22 and 2.31
   clients under Wine; record before and after per fix.
8. Turn the probe's steps into CI: start `gs2emu --server npcdemo` in the
   Linux job and fail on missing replies or on new `npclog.txt` lines.
