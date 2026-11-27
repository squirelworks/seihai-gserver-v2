# Graal 3D (Seihai)

<!-- Modified by the Seihai project, 2026-11-14: reconcile recorded lab coverage; 2026-11-19: protocol 19 Sky and Player; 2026-11-20: default Camera facing the spawn; 2026-11-25: lab raw objects and events, the Sharp3D route; 2026-11-27: big worlds (distance scoping, ghost order, large terrains); the add-on as a separate module. -->

This file is part of the Seihai fork (GPL-3.0, see `LICENSE.md` and `CHANGES-SEIHAI.md`).

The Graal clients from 3.151 (Linux) and 3.153 to 5.x include a Torque engine
3D mode (`Graal3DEngine.dll` in 4.x and 5.x), separate from the February 2000
Graal3D program. Graal 6 removed it. Server support and real-client results vary
by protocol: the 2026-11-14 lab reached the control object on 3.151 and 3.153,
but both crashed on their first frame; 4.20r8 played a full 3D session. See
"Protocol 19" and "Lab coverage and remaining checks" below.

## How a client joins a 3D server

1. The server list marks a 3D server with a `3 ` in front of its name.
2. The client opens two connections to the same host at once:
   - the normal Graal TCP login on the listed port `P`, exactly as for a 2D server;
   - a Torque UDP connection on port `P + 1`.
3. While in 3D mode the client ignores the TCP level, warp and flag packets. Flags,
   weapon pick-ups and serverside triggers go over the UDP side instead. Chat, PMs,
   the player list and file downloads stay on TCP.
4. If the TCP session ends, the client drops the 3D side too.

## Turning it on

In `config/serveroptions.txt`:

```
graal3d = true
# graal3dport = 0      (0 or unset: serverport + 1, the port the clients use)
# graal3dstringprefix = true
# graal3droute = mission   (or sharp3d: see "The Sharp3D route")
# graal3dshapecheck = skip  (or warn, or off: see "Shipping a 3D world")
# graal3dscope = off        (or on, or auto: see "Big worlds")
# graal3dsendms = 50        (least ms between two data packets to a client, 10 to 1000)
```

The 3D side is built into this fork's server by default (`-DGRAAL3D=ON`). It can
also be built on its own as the module `seihai-graal3d`
(`libseihai-graal3d.so`, `seihai-graal3d.dll`) and dropped beside a server built
with `-DGRAAL3D=OFF`, which then loads it when `graal3d = true` (`graal3dmodule =
auto`, or a path). Everything in this file applies to both. See `docs/addons.md`,
"Add-on modules", and `addons/graal3d/README.md`.

`graal3dstringprefix` says whether strings inside game packets start with Torque's
"shares a prefix with the last string" flag. The 5.00r7 `Graal3DEngine.dll` gives
every game packet a string buffer, as stock TGE 1.x does, so the default is `true`.
If a lab client's events log `does not fill its length with packet strings on; it
does with them off`, set it to `false`.

To list the server as a 3D server, set `hq_level = 4` in `config/adminconfig.txt`.
seihai-listserver tags HQ level 4 as `3 ` on the gen 4 and gen 5 lists.

Open UDP `serverport + 1` in the firewall as well as the TCP port. The client always
sends its 3D traffic to the listed port + 1; nothing the server sends changes that
(Graal_analysis `torque/3d-server-entry-and-udp-port.md`). So set `graal3dport` only
when something in between maps listed port + 1 to another local port. The client's
own UDP port is chosen by its OS unless the player turns on `fixedport`
(`fixedudpport`, default 14899). The server answers every packet at its source
address and port, so either works.

## The world

`config/graal3dworld.txt` lists what every 3D client is sent. One entry per line,
`#` starts a comment:

```
# object <name> <shape> <x> <y> <z> [<axis x> <axis y> <axis z> <angle in radians> [<sx> <sy> <sz>]]
object Ground ground.dts 0 0 0
object Tower tower.dif 20 5 0  0 0 1 1.5708
object BigTree tree.3ds 8 -4 0  0 0 1 0  2 2 3

# sun <direction x y z> <colour r g b a> <ambient r g b a>
sun 0.57 0.57 -0.57  0.8 0.8 0.8 1  0.3 0.3 0.35 1

# the shape of every player's own object, and where it starts
playershape player.dts
spawn 0 0 1

# optional: where each client's Camera sits from the spawn point (default
# 0 -5 2), and an axis and angle to turn it; without one it faces the spawn
camera 0 -5 2

# optional, protocol 19 (3.153, 4.04r2) only: the control object, a stock TGE
# Player that walks (the default) or the Camera of the 2026-11-14 labs
tgecontrol player
# the TGE .dts the client loads for that Player; without it the Camera is used,
# since 4.04r2 drops a Player whose PlayerData has no loadable shape
tgeplayershape player.dts

# optional: a Sky: six sky-box textures (front, right, back, left, top, bottom),
# comma separated ("-" for none), then the visible distance and fog colour
sky sky_fr,sky_rt,sky_bk,sky_lf,sky_up,sky_dn 500 0.5 0.6 0.8
# the Sky's three unnamed bools and its solid colour (see below)
skyoptions 1 0 0 0.4 0.6 0.9

# optional: scripts the client runs in 3D mode
weapon3d -Move3D config/move3d.gs2
weapon3d -System
```

`weapon3d` sends a script over the 3D text channel (`addweapon`, then
`weaponscript`) when the mission starts: the text of the named file, or, without
a file, the server weapon of that name (its client bytecode, or its clientside
source when it has none). A file is compiled to GS2 bytecode when the NPC-server
is on (`serverside = true`), as GS2 unless the file starts with its own `//#`
engine line: in the lab the 5.12 client took source text this way but never ran it.
A server weapon (`weapon3d <name>` with no file) needs a `//#GS2` line after
`//#CLIENTSIDE` to get bytecode. The 5.x client does not move its player by itself: in 3D mode
its key bindings call `moveForward2`, `moveBackward2`, `moveLeft2`, `moveRight2`
and `jump`, which only such a script defines.

In the lab, a 5.12 client script read the arrow keys with `keydown2` but could not
reach its own 3D object (`findobject` returns nothing for it), so the gserver moves
the object instead. In 3D mode the client sends a script's `triggerserver` and
`client.` flag writes over the 3D text channel, as `t <args>` and `s <flag>=<value>`
lines of an "events" text. `triggerserver(<kind>, <weapon>, "g3dpos", x, y, z)` moves
the player's object there and ghosts it to every 3D client, its own included. For
two seconds after that, the client's own reports of its position are ignored, so
an older report does not put the object back. At login the gserver sets
`clientr.g3dpos` to the spawn point, so a script can start from there. Other
`triggerserver` calls from a 3D script go to the NPC-server as weapon triggers
(kind `weapon`) or are logged as ignored.

The `Sky` layout is read from the 5.12 client (`Sky::packUpdate`). The client's
GS2 property names for Sky include `useskytextures`, `renderbottomtexture`,
`norenderbans` and `skysolidcolor`, but the read does not tie the three bools and
the colour in the update to those names, so `skyoptions` sets them by position.
The 5.12 client splits the Sky's texture field on commas and loads one texture
per entry, the first six as the sky box. It asks the gserver for each one without
an extension (`sky_fr`), so for these clients the gserver also tries `.png`,
`.jpg`, `.jpeg`, `.bmp`, `.gif` and `.dds` when a requested name has none. The
client stores them under `levels3d/images/sky/`, and loads the textures only when
it unpacks the Sky, so the server sends the Sky whole again once the mission
start is done, by when the downloads are in. A real 5.12 client drew the sky box
this way in the lab. `skyoptions 1 0 0` and `1 1 1` gave a light box fading into
the fog; `0 0 0` a more saturated one.

Each 4.20r6 and 5.x client also gets its own `Camera` ghost
(`G3DCamera_<account>`), and the server names it as the client's camera
(`ServerConnection.camera` in the client's scripts) once the client has it. The
5.x client draws its 3D view only through a Camera: with just the control object
the window stays black (lab 2026-11-20, real 5.12 on Windows 10). It sits 0 -5 2
from the spawn point, or where the `camera` line says, and faces the spawn point
unless that line gives a rotation. The server sets no camera mode and no followed
object: the client's scripts may set `cameramode`, `position` and `eulerrotation`.

Rotations are an axis and an angle in radians, and the clients turn the opposite
way from the right-hand rule: a positive angle about +x tilts the view down, one
about +z turns it right. For `spawn 0 0 1` the default camera is
`camera 0 -5 2 1 0 0 0.38`. A `.3ds` shape needs a material: one with only
vertices and faces loads without an error but is not drawn (5.12 and 4.20r6,
lab 2026-11-20).

A shape is a file name the client loads: `.dts`, `.dif`, `.3ds` or `.ft`. A client
asks the gserver over TCP for any file it does not have, and stays at login step 5
until every one of those requests has been answered (with the file or with "file not
found"), so put the shapes and their textures in the server's file folders.

Each linked player also gets a `GameObject` named `G3DPlayer_<account>`, with the
player's nickname, on every 3D client. A player's own object is that client's control
object. Each client reports its position in every packet, and the server moves the
player's object on the other clients. Chat said on the object (`chat`) is passed on
too. The server never sends a player's position back to that player.

## Several clients in one world

What the server does when more than one 3D client is on at once:

| | 4.20r6 and 5.x (protocols 21 and 24) | 3.153 and 4.04r2 (protocol 19) |
|---|---|---|
| Joining | Each client gets its own 3D link, matched to its own TCP login by account and address, so several clients on one machine are fine. | the same |
| Seeing the others | Every other linked player appears as a `GameObject` named `G3DPlayer_<account>`, with the world's player shape and the player's nickname. Every other linked player appears as a stock TGE `Player` on the world's `tgeplayershape`, at that player's position and facing. This needs `tgecontrol player` and a `tgeplayershape`; with a Camera as the control object, these clients get no other players. |
| What is passed on | What each client reports for its own object: position, rotation, scale, `chat`, shape, skin, attributes, and nickname with its colour. Move type, velocity and attachment are not sent yet. On the Sharp3D route, a player moves only through `g3dpos`. | The position and facing of the client's Player, as the server runs its moves. The other builds see this as that player's `GameObject`. |
| Starting point | The first player starts on the world's `spawn`. Each later one takes the first free point of 8 around it, 2 units out, then 16 at 4 units, and so on, so two players never stand on one point. | the same |
| Moving with `g3dpos` | The server moves the object, passes it on and traces it. The client goes on reporting where it was before; those reports are set aside until it reports somewhere else. | the same |
| Leaving | The player's object is deleted on the other clients, and the trace says on how many. | the same |
| 2D players | A TCP-only player in a 3D player's level appears on its screen as a `GameObject` named `G2DPlayer_<account>`, at its tile position as Torque (x, -y), at the spawn's height, with its nickname and chat. It goes when the player leaves the level. Checked 5 times a second. | as a stock TGE `Player`, under the same conditions as other 3D players |
| TCP side | Every 3D client is also an ordinary TCP player, so the gserver sends the usual other-player props, chat and PMs, as it would to 2D clients in the same level. A 3D player's own `chat` becomes its chat prop, so 2D clients see it. | the same |
| Position on the TCP side | The player's TCP position follows its 3D object: Torque (x, y) is map tile (x, -y). Serverside scripts see where the player is; without this, the script `player` stayed at the spawn while a 4.04r2 Player walked (lab, 2026-11-25). Positions off the map are left alone. | the same, from the Player the server runs |

**Texture names:** a 3D client's request for an image with the wrong image extension also finds the same name with another one. 3.153 asks for Sky textures as `.gif`, never `.png` (lab, 2026-11-25), so a `.png` texture answers it. Whether 3.153 draws a PNG sent under a `.gif` name is not tested yet; ship `.gif` copies to be sure.

**Accounts:** with `localhost = true` (the lab default) the gserver accepts any account
name and password and creates the account from `(defaultaccount)` on its first login.
Each test client can use its own made-up name, such as `lab1` or `lab2`, and each keeps
its own saved state. Use only made-up names on a lab server.

**Trace:** `graal3dtrace = <file>` appends one tab-separated line to the file for each
of these:

- each 3D link and each close;
- each event a client sends, decoded where the server can decode it and as hex where
  it cannot;
- each change in a client's control-object report;
- each ghost the server writes to that client;
- each time a player's object is passed on to the others;
- each TCP position set from a 3D object, or skipped as off the map;
- each 2D player shown, updated or gone on a 3D client;
- each leave, with the number of clients the object was deleted on.

Each line holds: milliseconds, account (or object name), protocol (0 for the server's
own relay lines), then what happened. Leave the setting empty to turn the trace off.

## What the server does

| Step | State |
|---|---|
| Torque connect challenge and connect request | done. The request must name `GameConnection` and `Graal3D`, with protocol 19 (3.153, 4.04r2), 21 (4.20r6) or 24 (5.x). |
| Who may connect | The first argument of the request is the account. It must be logged in over TCP, from the same IP address, with a 3.153 to 5.x client. A 5.x guest sends `guest`, while the gserver names guests `pc:<number>`, so `guest` matches the guest client logged in from that address. Until the TCP login is done the request is not answered (the client repeats it), and the log says once per address that no TCP client matched. |
| Connect accept | done, with Graal's class-name table for that protocol. |
| Keep-alive, acks, time-out (30 s) | done for every protocol. |
| Mission start (`MissionStartPhase1`, `2`) | 5.x and 4.20r6. The client answers each phase and raises `onGraal3DLoginStep`. A 5.x client lights the mission when `MissionStartPhase2` arrives, from the TerrainBlock, WaterBlock, Sun and terrain chunks it has by then: a TerrainBlock created later draws with no slope shading all session, and chunks that come later are lit from the old heights, a hard-edged dark patch until relog (Graal_analysis T3D-162, `torque/terrain-512.md`). So phase 2 waits until the client has acked the create of each of those ghosts and, with a TerrainBlock, every `TerrainChunkEvent` the server sent in answer to its terrain request (an answer of no chunks counts). The wait is capped at 15 s; past that, phase 2 goes anyway and the log says what was missing (`phase 2 sent with ...`). Creates go out with the client's own Camera and control object first, then the lighting ghosts, then the rest, so a big world does not hold phase 2 up. Ported from seihai-gserver #223, which was checked on a real 5.12 with a cold and a warm terrain cache. |
| Flags | sent at link time as one `events` text (`s name=value` lines), then every flag change as `set` / `unset`. |
| From the client: `events` text | `s name=value` sets a flag or client variable (as `PLI_FLAGSET`), `w name` gives a weapon, `t weapon,name,params` runs a serverside triggeraction on that weapon. |
| Ghosting (4.20r6, 5.x) | done. Objects are created, updated (only what changed) and deleted in the ghost section of each packet, and what a lost packet carried is sent again. `GameObject` (name, shape, skin, position and rotation, scale, chat, `attr[1..8]`, nickname and colour, the solid/shadow/collision flags) and `Sun`. |
| Control object (4.20r6, 5.x) | done. Once the client has its player object and has acked both mission phases, the server makes that object the control object, which is login step 5. It must come after the mission start, because `MissionStartPhase2` sets the 5.x client's login step back to 3 and step 5 is only set by the first control object. Step 6 follows once every TCP file request is answered. With `graal3d` on, a missing file gets `PLO_FILESENDFAILED` for these clients, so it does not hold the login up. |
| From the client: its own object | read in every packet: the position, and the client's changes to the object (moves, chat, shape). Other players see them. |
| "Connecting..." window | The 5.x client builds its 3D view itself (`GUIContainer`, `GraalControl3D`, from GS2 built into the client) and shows it at login step 6. Its "Connecting..." window stays on top until the TCP side sends `PLO_SERVERLISTCONNECTED`, so with `graal3d` on, every 3.153 to 5.x client gets that packet at login, with or without a listserver. |
| Camera | one `Camera` per client, facing the spawn point unless the `camera` line says otherwise, named as its camera in every packet once its ghost is in |
| `Sky` | with a `sky` line: init and material list, the later sections (storm, wind, fog changes) left clear. Layout from the 5.12 client; a real 5.12 client drew the sky box in the lab (see "The world"). |
| Scripts in 3D mode | with `weapon3d` lines: `addweapon` and `weaponscript` over the 3D text channel. A 5.12 lab client compiled and ran one, and its `keydown2` saw the arrow keys. |
| Walking | a script's `triggerserver(..., "g3dpos", x, y, z)` and the gserver moves the player's object. Lab 15 showed the trigger reaching the gserver; the move is not yet run with a real client. |
| `TerrainBlock`, `WaterBlock` | **no layout yet.** The lab's `rawobject` and `rawevent` lines (see "Lab: raw objects and events") send them with hand-written fields. |
| `movetype` | **not sent.** |
| From the client: `TerrainRequestUpdatesEvent` | logged: its length, how many of its 256 stamps are set, the first eight `(x,y)=stamp`, and the payload as hex. Other events the server does not decode are logged with their length and first 64 bytes. |
| 3.15x and 4.04r2 game packets | done up to a walking `Player` control object and a `Sky`. A real 4.04r2 runs a `Camera` session with the `Sky`, and a `Player` session once `tgeplayershape` names a loadable TGE `.dts` (lab 2026-11-19). See "Protocol 19" below. |

## Hostile and broken clients

The 3D port is open to anyone who can reach it, so the server guards it
(board rows T3D-142 to T3D-146):

- **Floods and probes.** Each address may send 256 datagrams a second once it
  has a 3D link and 32 before. A linked 5.12 sends a few a second, and a
  handshake only a handful. Past the budget, the rest of that second is
  dropped unread and logged once per address. Junk, cut-short connect requests
  and data packets from an address with no link are ignored. A connect request
  must carry the challenge digest for its address before the server does any
  work for it.
- **Reconnects.** A new connect request from a linked address closes the old
  link first. A player gets one 3D link: a second one closes the first. Closing
  a link removes the player's object from every other client and clears its
  per-player state.
- **Impossible positions.** Clients own their objects' positions and the server
  passes them on to everyone else (T3D-128). A report with a NaN, an infinity
  or a coordinate beyond 1,000,000, or a rotation that is not finite, is
  dropped, so it never reaches another client. It is logged once per player.

A lab run (2026-11-26) checked each of these against a server with two real
5.12 clients linked, one of them walking the whole time:

- 1000 random datagrams of 0 to 1500 bytes, cut-short connect requests at every
  length, and datagrams up to 65,507 bytes: nothing logged, CPU at most 2%,
  memory flat.
- 5000 datagrams a second for 20 seconds from one address: one warning, exactly
  32 replies a second to the flooder, and every probe from another address
  answered. CPU peaked at 1.9%.
- 500 rounds of bad handshakes: a bad digest or too many arguments gets no
  reply, and a bad protocol, game string or class name gets the matching refusal.
- 60 link-and-drop cycles on one account: the session count never grew, and
  memory rose by 0.2 MB and then stayed flat. The walking client stayed
  connected, kept drawing terrain and grass, and kept seeing the other player.
- A `.3ds` naming a PNG that claims to be 100000x100000 is refused at start-up
  with an error. The client draws that shape white and keeps running.

A TCP login from 6.037 (the 6.03r7 token, Windows or Linux) may also join a 3D link.
Stock 6.03r7 is a 2D client and never opens one, so for it nothing changes. The
Frankenstein 6.037 lab build logs in with that token and opens the 5.12 protocol-24
link beside it. It is linked with the 5.12 row's capabilities. The 3D login needs
(answered checksum requests, bare-name textures, login files) still go only to the
builds listed below.

## What each client build is offered

When a 3D connect request is accepted, the server picks the client's capability row by its login token and Torque protocol (`server/include/graal3d/Graal3DCapabilities.h`; an unknown token gets the protocol's default build: 19 4.04r2, 21 4.20r6, 24 5.12). The rows mirror Seihai's version profile (`VersionProfile::three_d` in seihai-core), whose source is Graal_analysis `version-profiles/three-d.toml`. The log line `Graal3D: <account> is a <build> client: ...` names the row, and says when the protocol or every value is assumed rather than read.

| Build | Game layer | Streamed terrain and water lines | TorqueScript | Notes |
|---|---|---|---|---|
| 3.153 | TGE | not sent (its TerrainBlock and WaterBlock are stock TGE) | runs its own only | crashes without a Sky (the protocol 19 path always sends one); asks for sky textures as `<stem>.gif`, so keep a `.gif` of each; never asks for `.cs`, `.dso` or `.ter`; reaches its sky view once the `.gif` requests are answered with the PNGs (lab M3, 2026-11-25) |
| 4.04r2 (Win, Mac) | TGE | not sent | none | never downloads `.dso`, `.cs`, `.ter`; under Wine drew only the Sky gradient (lab 2026-11-25); crashes on an update to a ghost that was never created (fuzz tier 1), so the server must only update created ghosts; Mac row all assumed |
| 4.20r6 Mac | GameObject | sent | none | protocol 21 |
| 4.20r8 Win | GameObject | sent; it draws the terrain but never fetches `heights.info` or terrain textures, so the ground is untextured (lab T3D-117, Mesa). Its earlier crash near the camera was the lab VM's GDI Generic OpenGL 1.1 | none | links as protocol 24 (lab) |
| 5.00r7 | GameObject | sent | none | class set and file rules assumed from 5.12 |
| 5.12 (Win, Mac) | GameObject | sent | none | strips `.png` from texture names: serve textures under their bare name too; Mac row all assumed |
| 4.20 Linux (`G3D22126`) | GameObject | sent; it fetches terrain textures (lab) | none | links as protocol 24; 5.12's row except what a 2026-11-25 lab read. A DTS 17 shape froze it: serve DTS 24. It crashes on a broken `.3ds`. |

2D and 3D clients in one level (lab pair H1, 2026-11-25): no read build moves its 2D player when it walks in 3D, and 5.12 draws no 2D players, though 2D chat reaches it as `onRemotePlayerChats`. So a 2D client sees a 3D player only if the server moves that player's 2D position itself, and the reverse needs a server-made ghost. The rows carry this as `mirrors2dPosition` (`needs2dPositionRelay()`), `sees2dPlayers` and `takes2dChat`; `drawn` records what each build has drawn here so far.

Only the objects the `terrain` and `water` lines build are gated; lab `rawobject` lines still go to every client. A `TerrainRequestUpdatesEvent` from a build without the 5.x terrain layout is logged and not answered.

## The Sharp3D route

`graal3droute = sharp3d` in `serveroptions.txt` (default `mission`) gives protocol
24 clients (5.00r7, 5.12) a second way in, for comparison. It copies an
unofficial, LLM-written third-party 3D server that a real 5.12 client joined and
drew (capture of 2026-11-24, read by graal-client-repro's `g3d-replay`). That
server is not a reference: it only shows one layout the client accepts. What the
client itself reads (static and live analysis, and fuzzing it against this
server) is the authority, and the default route stays the one built from that.

- The connect accept's class table is objects `Sun`, `GameObject`, `Camera`, `Sky`
  and events `BigDataStartEvent`, `BigDataChunkEvent`, `ConnectionMessageEvent`,
  `GhostAlwaysObjectEvent`, in that id order. There is no `RemoteCommandEvent` or
  `NetStringEvent`, so no mission-start commands.
- Every object is created by a `GhostAlwaysObjectEvent`: ghost index (12 bits), a
  flag (set), class (12), the update's length (14), then the update. Later
  changes (other players moving, chat) go in the normal ghost section; the
  captured server's world was static, so that part is this server's choice.
- The mission counts as started once the client has acked every one of those
  events. Then the client's `Camera` becomes its control object, named in every
  packet with an empty Camera update (13 GameObject flags and 2 Camera flags), and
  as its camera.
- Flags and `weapon3d` scripts still go over the BigData text channel.
- The client's reports for its Camera do not move its player object, which moves
  with the `g3dpos` trigger only. The reader assumes the client writes a Camera
  update as it reads one (the two extra flags); not yet checked live.
- `TerrainBlock`, `WaterBlock` and the lab's raw objects are not in this class
  table, so they are not sent on this route. The captured server also sent a
  field of view of 60 in every packet; this server sends none.

Catch tests check the table, the event layout and the control object. No real
client has run this route on our server yet.

## Shipping a 3D world

A 3D playerworld is an ordinary gserver folder, with every file under its real name
and extension:

```
config/graal3dworld.txt          the world file: names files as they are on disk
world/3d/                        any folder the file folders cover
  ground.3ds  tree.dts  hall.dif   shapes
  grass.png   sky_fr.png ...       textures: 8-bit PNG, JPEG or GIF
  island_terrain/                  streamed terrain (see Terrain)
    heights0-0.dat ... heights15-15.dat
    heights.info                   one texture name a line, with .png
    g3d_grass.png ...
```

- **Name files as they are on disk.** The world file names shapes and Sky textures by their real file names: `object Hall hall.dif ...`, `sky sky_fr.png,sky_rt.png,...`. Before it sends a Sky, the server drops any image extension from the texture names.
- **Bare-name requests.** The 3D clients ask for every texture by a bare, lower-cased name and store it under that name in their own cache. The server answers a bare request with `<name>.png`, `.jpg`, `.jpeg`, `.gif`, `.bmp`, `.tga` or `.dds`, tried in that order and matched case-insensitively. It answers the clients' "is my copy current?" checks the same way, so nothing needs an extension-less copy on disk. Shapes keep their extension in requests.
- **Texture formats.** Only 8-bit PNG, JPEG and GIF draw on 5.12. BMP, TGA, DDS and 16-bit PNG draw white.
- **Lower-case names** are safest, because the clients lower-case what they ask for. The server's lookup ignores case, so mixed case still works.
- **Start-up check.** At start-up the server looks up every shape and Sky texture the world file names, and with a `terrain` line, `heights.info` and every texture it lists (a client that cannot fetch them draws the terrain white). It logs any that are missing, and any found only without an extension.
- **Unsafe shapes and textures.** Every `.3ds` and `.dts` shape is checked for the faults that crashed a real client. For a `.3ds` these are: no materials, a cut file, a chunk longer than its parent, a vertex or face list claiming more entries than it holds, or a face using a vertex the mesh does not have. A `.dts` must be version 24, the only version the real 5.12 drew (it trusts the version word: other versions were refused, hung, crashed or drew nothing), and the memory buffer its header claims must fit the file; a cut file or a huge size word crashed 5.12. Its node, object, decal and sub-shape counts must not exceed the buffer. A `.dif` interior whose first word (the InteriorResource version) is not 44, the only version any Graal 3D build was read to take, gets a warning; it is still sent, because a real 5.12 refuses it safely (lab, 2026-11-26): versions 43, 45 and 0xFFFFFFFF gave "InteriorResource::read: incompatible file version found", a file cut to 4 bytes, 50% or 90% gave "Unable to read detail level 0", and in every case the client kept the link and drew the rest. A file short by only its last 16 bytes is dropped with no console line, and the server does not catch a cut file with a good version word. The player shapes (`playershape`, `tgeplayershape`) are checked too: a bad one crashed 4.04r2 at login, so with `skip` it is not used (protocol 19 then controls a Camera). Sky, terrain, grass and `.3ds` material textures (chunk `0xA300`) that are PNGs must claim a width and height from 1 to 8192; one claiming 30000x30000 crashed 5.12 (content fuzzing, lab, 2026-11-25). A bad texture is logged as an error, since it cannot be left out. With `skip`, every file the check finds unsafe (a shape, a player shape, a replicator shape or a texture) is also refused when a 3D client asks for it by name: the client is told the server does not have it, which it survives, where the file itself crashed 5.12 (an oversized terrain PNG fetched by name, lab 2026-11-25). A 16-bit PNG, a BMP, an empty file, or an image over 1024 a side gets a warning too, since 5.12 draws none of them (it drew 1024; 2048 and 4096 came out white). 8-bit PNG, JPEG and GIF draw. Terrain textures should be 256x256 24-bit; the client draws its default texture for any other size, and the server warns about it. With `graal3dshapecheck = skip` (the default), an object with such a shape is left out of the world and logged. Labs that send broken shapes on purpose set `warn`, which logs and sends them anyway; `off` skips the check.

- **Folders config.** A file folder covers only its own files, not its subfolders. `config/foldersconfig.txt` needs `file 3d/*` and a line for each streamed terrain folder, such as `file 3d/island_terrain/*`; without them the world's files are not found (lab, 2026-11-25). The default and modern servers have `file 3d/*`.

`seihai-servertools gen terrain DIR --world NAME --stream --playerworld` writes a whole
world in this layout from a generated height field.

## Big worlds

Ported from seihai-gserver #226 and #227 (checked there on a real 5.12, lab 2026-11-27); the
planner and the window are in `Graal3DBigWorld.h`.

**Ghost order.** A GameObject-era client (4.20r6, 4.20r8, 5.x) has a ghost table of 4096
entries, and the first ghosts set take the first entries. The server now ghosts the player's
own object and its Camera first, then the rest of the world but its objects (the
TerrainBlock, Sun, Sky, water, ground cover and raw objects), then the other players, and
the world objects last. With the objects first, a world of 4094 or more objects filled the
table before the own object and Camera, and a real 5.12 drew a black view all session.

**Distance scoping** (off by default):

```
graal3dscope = auto            # off, on (every world) or auto
graal3dscopeauto = 2000        # auto: only a world with more objects than this
graal3dscoperadius = 1100      # world units
graal3dscopehysteresis = 200   # world units
```

- A world `object` is ghosted only while it is within the radius of the player's own object
  (3D distance), and deleted once it is farther than radius + hysteresis.
- Each client's scope is looked at 5 times a second. With `graal3dtrace` on, each look that
  changed something adds a `scope` line.
- Only `object` lines are scoped; everything else always goes.
- A scoped client holds at most 3584 world objects (the table less 512), nearest first.
- On a real 5.12, 4000 unscoped objects took 31 s to come in at the default pacing. With
  20,164 objects and the defaults, the join took about 3 s and the peak was about 1400
  table entries while walking.

`graal3dsendms` is the least time between two data packets (default 50 ms, at most 400
bytes each). A real 5.12 took 10 ms on loopback with no loss (terrain 4x faster). That is
untried over a real network, so the default stays.

**Large terrains** (a sliding window):

```
terrain world/continent 8 repeat 48x48
```

- `<width>x<height>` chunks, each even and 16 to 256. `repeat` is required, and `16x16` is
  the plain terrain.
- The folder holds `heights<X>-<Y>.dat` for X < width and Y < height.
- The terrain is centred on the origin: world chunk X covers x from (X - width / 2) x span
  to (X - width / 2 + 1) x span, where span is 16 squares.
- The client tiles its 16 x 16 grid, so world chunk X sits in slot (X - width / 2 + 8) mod
  16 (X mod 16 for a width of 48). Each slot holds the chunk that falls in it among the 16
  round the player's own chunk.
- The terrain request is answered for the window where the player is now.
- Each chunk crossing sends the slots that changed (16 straight, 31 diagonal). Once the
  client has acked them all, `MissionStartPhase2` goes again so it lights them. A real 5.12
  drew such chunks flat-lit until then; after it, they matched a static terrain pixel for
  pixel. The ack of that phase 2 is taken quietly.
- Past the edge, a slot keeps the nearest chunk of the terrain that falls in it.

## Lab: ground cover (`replicator`)

```
# replicator <class> <x> <y> <z> <radius> <min> <max> <shapes> [<seed> [<x0> <y0> <x1> <y1>]] [scale=<min>[,<max>]]
replicator DynamicShapeReplicator 10 -20 1.5 4 5 5 labcube.3ds scale=3
replicator DynamicShapeReplicator 0 0 0 8 2 6 a.dts,b.dts 1 -512 -512 512 512 scale=1,3
replicator DynamicGrass 0 0 0 8 30 50 lp_white.png
```

Ghosts a `DynamicShapeReplicator`, `DynamicGrass` or `DynamicGrassReplicator`: ground
cover scattered around the point on a grid of 8-unit cells. `<radius>` is in cells
(1 to 64), `<min>` and `<max>` are objects per cell (0 to 20; 0 to 100 for the grass
classes), `scale=` sets the random scale range of each copy (default 1), and `<shapes>` is the
shape list as one string; 5.12 splits it at commas, so write several shapes as
`a.dts,b.dts` with no spaces. The box is the world x/y region cover may grow in: 5.12
grows an 8-unit cell only if its corner lies in `[x0, x1)` and `[y0, y1)`. Without one,
cover may grow anywhere. The box's z range is always -32767 to 32767: 5.12 places each
copy by a ray cast down from z 2000 to -2000 and keeps it only if the hit height lies
in the box's z range and the surface is no steeper than the allowed slope (90 degrees
here). The grid follows the client's control object, not the
replicator's position, and cover is placed by casting rays, so it needs real ground
(streamed terrain or solid objects). On the real 5.12 a replicator over streamed terrain
grew a carpet of scaled, randomly rotated shapes on the slopes around the player (lab,
2026-11-25). The start-up check looks up each replicator shape or texture and checks
it like a world object's or a terrain texture's. Live results so far (lab, 2026-11-25): two shapes in the list are both
used, picked per copy; two 5.12 clients see identical cover for the same seed;
`DynamicGrass` and `DynamicGrassReplicator` draw nothing with shapes, but with a texture
(e.g. `lp_white.png`) as `<shapes>` they draw many small flat pieces on the ground,
denser at 50 a cell, so the server refuses a grass line whose list is not all images
(`.png`, `.jpg`, `.jpeg`, `.bmp`) and a `DynamicShapeReplicator` line whose list is
not all `.3ds`/`.dts`; a nearly transparent texture showed nothing under GDI. 5.12
never asks the server for a grass texture, so it must already have it (lab, Mesa,
2026-11-26): the server sends every grass texture the replicator lines name over TCP
right after the 3D link, once per account per server run, unless the start-up check
refused it (`graal3dpushgrass = false` turns this off). On a real 5.12 (lab, 2026-11-26)
the push arrived at link time ("File download: lp_white.png done") and filled the cache,
but grass still did not draw on that first visit, only on later ones. The lab switch
`graal3dgrassdelay` held the grass ghosts back 0, 3 and 10 s: the first frame was the
same each time (lab, 2026-11-26), so 5.12 seems to build its grass once, at mission
load. The textures therefore also go to every 3D-capable client at its TCP login, after
the `gpaks` packages, before the 3D mission starts, and that works: on a real 5.12 with
an empty cache, grass drew on the very first frame (lab, 2026-11-26). Only native `replicator` lines are pushed: a `rawobject`
grass line names its texture in raw fields the server does not read. Cover grew only
around the spawn in that run because only the camera moved: 5.12 re-centres the grid
on the control object every tick (static read, Graal_analysis `torque/replicators-512.md`).
Grass and radius (lab, 2026-11-26, still open): grass lines at radius 4 with 5 or 10
a cell drew nothing on a real 5.12 while the same lines at radius 8 drew, but a
conformance case at radius 4 with 40 to 60 a cell drew as much as radius 8
(Graal_analysis `torque/conformance`, C035 to C037). So radius alone does not decide
whether grass draws, and the cause is not pinned down yet. This server sends the radius
as written; if a sparse grass line draws nothing, a larger radius or count is the first
thing to try. Seen live (lab, 2026-11-26): with the avatar itself walking 1700 units across a
2048-unit streamed terrain, at 10 and 40 units a second, a radius-8 `DynamicGrass`
stayed centred on it the whole way. Cells came and went one at a time at the rim of
the patch, the ground by the avatar was never bare, and neither side logged anything. Every name on the line must be
a bare file name: one with `/`, `\`, `:` or `..` fails the line, since the client
stores a file under the name it is sent. 4.20r8 takes the ghost
without a crash but grows no cover, under GDI or Mesa, even with the camera pitched
down over the cells (lab, Graal_analysis PR #951), so its row has `groundCover` false
and it gets no replicator and no grass push. The layout is from a static read of the real 5.12 client
(Graal_analysis `torque/replicators-512.md`); 5.12 has drawn it live (above). The other
fields are fixed: any rotation about z, terrain only, slopes up to 90 degrees,
no alignment, shown. Only clients that take streamed
terrain and grow ground cover (`groundCover`) get it.

## Terrain (4.20r6 and 5.x)

```
# terrain <folder> [<square size>] [repeat]
terrain world/terrain
```

The 5.12 client gets its terrain in two parts. A `TerrainBlock` ghost carries only
settings, and the heights, materials and holes follow as up to 256
`TerrainChunkEvent`s of 16 x 16 squares. The terrain is 256 x 256 squares, centred
on the origin, 8 m a square by default. Send one TerrainBlock a world: with two, 5.12 asks for chunks for both but lays them only on the last (the server warns at start-up). Hole flags cut out exactly the flagged squares, with no slope at the edge. The square size may be 1 to 64; a huge one killed a live 5.12 right after "Terrain lit" (lab, 2026-11-25). The layouts come from a static read of the
real 5.12 client (Graal_analysis `torque/terrain-512.md`). The 2026-11-25 terrain
lab confirmed them live on a real 5.12, by sending them as raw lines from this
server. The native `terrain` line has not yet run against a real client.

`<folder>` (from the server folder) holds `heights<x>-<y>.dat`, x and y 0 to 15, with
no zero padding. Each file is one chunk's zlib-compressed data, the same file the
client keeps in its own cache (`levels3d/heights/<server>/`). A missing file leaves
that chunk out, and a file over 1023 bytes or one that does not inflate is logged
at start-up. Put `heights.info` (one texture name a line, in material order, each
256 x 256 24-bit) and its textures in the file folders: the client asks for
`heights.info` over TCP when it has none, then fetches each texture. On a first visit (empty cache) 5.12 asks only for a bare `heights.info`, saves it in
`levels3d/images/`, and draws the terrain white; on the next visit it logs "Missing
terrain texture" for each line, asks for each texture by its listed name, and draws
them (live, 2026-11-25). So test textures on a return visit. Write the
names with their extension (`g3d_grass.png`): the Linux 4.206 client given bare
names drew the terrain white, then saved the textures without an extension, could
not load them and drew no terrain at all; with `.png` names it drew them textured
(lab, 2026-11-25).

`heights.info` is newline-separated. A script that sets terrain materials through
`setmaterials` instead must separate the names with commas, because the 5.12 client
reads a newline-separated list as one name (lab, 2026-11-25). The native
`terrain` line does not send `setmaterials`.

When the block arrives, the client sends a `TerrainRequestUpdatesEvent` with the
stamp of every chunk it already holds. The server answers with every chunk whose
stamp differs, so a returning client downloads only what changed. The stamp is
Torque's CRC of the inflated bytes. The client stamps its own re-encoding of each
chunk (mode 2, the material ids it uses in ascending order, its own deltas), so
write chunks in that form, or every chunk is sent again on every visit.
`seihai-servertools gen terrain --stream` writes them that way. A damaged zlib
stream is saved by the client as a flat chunk. The 5.12 client does not clamp heights: keep the first
height and every running sum in 0 to 16383, or they wrap. It ignores trailing
bytes, and a second copy of a chunk overwrites the first (lab, 2026-11-25). Protocol 19 clients do not get the block,
because their stock TGE terrain is a different layout.

### Water

```
# water <x> <y> <z> <sx> <sy> <sz> [<surface texture> [<liquid type>]]
water -1024 -1024 -19.5 2048 2048 20 water_surface 0
```

A `WaterBlock` at that corner, unrotated, of that size (the client takes the size
only when x and y are both at least 1024). The surface is at z plus the z size, not
at z (live 5.12, 2026-11-25): for water at height h and depth d, write z = h - d and
a z size of d. The example's surface is at 0.5, and `liquidheight` reads that with a
little wave motion. Liquid types follow stock TGE (0 water,
1 ocean, 2 river, 3 stagnant, 4 lava, ...), and the rest of its look takes stock TGE
1.x's defaults. The layout is from a static read of the 5.12 client (Graal_analysis
`torque/terrain-512.md` section 5); live 5.12 reads its `liquidheight` as above.

## Lab: raw objects and events

For classes whose update layout has not been read (`TerrainBlock`, `WaterBlock`,
the replicators) and events such as `TerrainChunkEvent`, the world file can
describe the bits by hand, so a lab can try a layout by editing the file and
restarting, without a new build:

```
# rawobject <class> <fields...>: a ghost of <class> whose update is the fields
rawobject TerrainBlock f1 str:lab.ter u32:256 f32:8.0
# rawevent [late] <class> <fields...>: one guaranteed event; `late` waits until
# the client has finished the mission start
rawevent late TerrainChunkEvent i4:0 i4:0 u32:1 zfile@10:world3d/chunk_0_0.bin
```

| Field | Writes |
|---|---|
| `f0`, `f1` | a flag |
| `i<N>:<v>` | an N-bit int (1 to 32); decimal, `0x` hex, or negative (two's complement) |
| `u8:`, `u16:`, `u32:` | `i8`, `i16`, `i32` |
| `f32:<v>` | a raw 32-bit float |
| `str:<text>` | a string, in the packet's string mode (`graal3dstringprefix`); `%20` for a space, `%23` for `#`, `-` for an empty string |
| `hex:<bytes>` | bytes given as hex |
| `file:<path>` | a file's bytes (the path from the server folder) |
| `zfile:<path>` | a file's bytes, zlib-compressed with the zlib header |

`hex`, `file` and `zfile` take a length prefix: `file@10:x.bin` writes the byte
count as an int10 first. A file too long for its prefix, a missing file, or an
update or payload over 16383 bits (an int14 length) is logged at start-up.

The client reads every update and event with its length, so a wrong guess is
skipped rather than misreading the rest of the packet. A raw object goes to every
client whose class table has its class: 4.20r6 and 5.x list `TerrainBlock` and
`WaterBlock`, and protocol 19 (3.153, 4.04r2) now lists them too, after `Camera`,
`Player` and `Sky` so those keep their ids. Raw objects on protocol 19 go out with
the other ghosts, after the datablocks. Nothing here has been run with a real
client yet; the 2026-11-25 terrain lab is the first user.

## Terrain switches

```
graal3dterrainall = true    # lab: the 5.x TerrainBlock, WaterBlock and chunks to every 4.20r6 and 5.x client
graal3dpushterrain = false  # stop sending heights.info and its textures at login and on link (on by default)
graal3dpushgrass = false    # stop sending grass replicator textures on link (on by default)
graal3dgrassdelay = 3000    # lab: ghost grass replicators this many ms after the rest of the world (0 to 60000)
```

`graal3dterrainall` ignores the capability rows that keep streamed terrain from
builds such as 4.20r8, so a lab can see what those builds do with it. Real
players' servers leave it off.

`graal3dpushterrain` (on by default since 2026-11-26) answers 5.12's first visit,
which fetches only `heights.info` and draws the terrain white; its textures come on
the next visit. With it on, the
server sends `heights.info` and every texture it lists (as found at start-up) to the
client unasked at its TCP login, before the 3D mission loads (the timing that made
first-visit grass draw), and again when its 3D link opens, once per account per server
run. On a real 5.12 with an empty cache (lab, 2026-11-26, Mesa) the terrain was then
textured on the very first frame, with `heights.info` and all four textures logged
before `MissionStartPhase1`; with it off, the first frame was white. A build
whose row has `terrainTextures` false (Windows 4.20r8, which never fetches them)
gets neither push. Under Mesa it takes the 256 chunks and the WaterBlock and still
never applies a terrain or water texture (lab, 2026-11-26), so nothing a server
sends fixes that. On a real
5.12 (lab, 2026-11-25) the first visit then had `heights.info` and all four
textures before the terrain loaded, with no "Missing terrain texture" line; whether
it draws them on that visit still needs a run under Mesa. The client stores the
textures in `levels3d/images` without their extensions; the server's copies keep
theirs.

`graal3dterrainall` on 4.20r8 ran 110 s with a high spawn, and crashed about 1 s in
with a spawn just above the ground (z 17.5 on a native terrain): the same
near-terrain crash as before, under GDI GL. A TerrainBlock alone is enough, with no
chunks. A height sweep on the lab VM's software GL found the line: 4.20r8 crashes
when its camera starts 150 units or less above the ground, and survives from 180 up,
on a hilly and on a flat block. That crash is the GDI driver's: under Mesa, 4.20r8
ran fine 80 units above the ground (Graal_analysis PR #951), so its row now gets
terrain, and only a client on software GL (GDI Generic OpenGL 1.1) still needs a
high spawn.

The push sends each texture name once, however often `heights.info` lists it, never a
`heights.info` line with `/`, `\`, `:` or `..` in it (the client stores a pushed file under
the name it is sent, so only bare file names go out), and
never a texture the start-up check found unsafe (a PNG header of 30000x30000 or
65536x1 crashed 5.12). `heights.info` takes one texture name per line: 5.12 asks for a
whole line as one file name, so the server warns about a line with spaces or commas.

Clients ask for `heights.info` by its bare name, so only one file of that name may be
in the file folders. Another one (a stray `world/global/heights.info` in that lab)
wins, and the terrain then asks for that file's textures. The server logs an error at
start-up when the `heights.info` clients get is not the terrain folder's.

## Lab: mutations (`graal3dmutate`)

**Lab only. Never set this on a server real players use.** `graal3dmutate =
<kind>[:<seed>]` changes one thing in what the server sends each 3D client, so a lab
can watch how a real client in a VM copes. The seed (default 1) picks the details, so
a run can be repeated. Each link gets the mutation once, and the server log and
`graal3dtrace` both get a `mutation <kind>:<seed>` line with what changed and the
packet's exact bytes in hex.

| Kind | What is sent |
|---|---|
| `accept-protocol` | the connect accept carries a wrong protocol number (one off, 0, `0xFFFFFFFF` or 100 more) |
| `accept-classtable` | the accept's class table leaves out the last 1 to 3 event classes, adds an event class `SeihaiLabEvent`, or renames one object class `SeihaiLabObject` |
| `event-unknown-class` | an unguaranteed event whose 12-bit class id is past the table, with 0 to 63 random bits |
| `event-bad-length` | an unguaranteed event of a real class whose int14 length is 1 or 8 more, 1 less, or 16383, against its 0 to 63 bits |
| `ghost-uncreated` | an update for a ghost index that holds no ghost, with an existing ghost's class id and random bits |
| `ghost-double-delete` | two deletes of an index that holds no ghost, in one ghost section |
| `ghost-index-width` | a full update of an existing ghost, in a ghost section whose 4-bit index width is wrong |
| `control-wrong-class` | the control object names a ghost of another class (a Sky, Sun, ...) |
| `control-deleted` | the control object names an index that holds no ghost |

The accept mutations go out with the accept. The others go out 2 s after the mission
start is done, in one packet. A mutation that finds nothing to change (no ghost of
another class, say) logs that instead. The server's own state is not changed by a
mutation: its ghosts and control object stay as they were.

## Protocol 19 (3.151 Linux, 3.153, 4.04r2)

These clients run the stock Torque game layer: datablocks, `Camera` / `Player`
ghosts and moves, with a three-phase mission start. The layouts come from a static
read of the 4.04r2 `Graal3DEngine.dll` (Graal_analysis `torque/protocol19-server.md`);
3.153 is assumed to read the same. What the server does with them:

| Step | What goes out |
|---|---|
| First client packet | `MissionStartPhase1` with the mission sequence (`1`) and the mission name (`Graal3D`). 3.153's script sends the sequence back in its ack, 4.04r2 sends the ack bare; both are taken. |
| Phase 1 ack | One datablock, a `PlayerData` with the `tgeplayershape` shape and the 4.04r2 constructor's values (without a shape, or with `tgecontrol camera`, a `CameraData` with no shape and every optional field clear), as a `SimDataBlockEvent`, then the connection message `DataBlocksDone` (5). |
| `DataBlocksDownloadDone` (6) from the client | `MissionStartPhase2`, the connection message `GhostAlwaysStarting` (3) with the ghost count, and from now on ghosts: the world's `Sky` (or, without a `sky` line, a plain one in its solid colour) and one stock `Player` (or `Camera`) per client, at the world's `spawn` point. No ghost goes out before this, since a ghost naming a datablock the client has not loaded drops the connection. |
| Phase 2 ack, and every create acked | `GhostAlwaysDone` (0), then `MissionStartPhase3`. The client lights the scene and acks. |
| Phase 3 ack | The `Player` (or the `Camera`, in fly mode) becomes the control object, with its packet data. 4.04r2 then raises login step 5. |
| Every client packet after that | The client's moves are read and acknowledged in the server's move ack. A `Player`'s are applied as a plain walk (turn with the move's yaw and pitch, walk at 10 units a second, no gravity or collision, since the server has no terrain), and its packet data (position, velocity, head and rotation) goes out again in the next packet. A `Camera`'s are not applied: the client moves it by prediction. |

The `Sky` update is the 5.12 one: 4.04r2's `Sky::unpackUpdate` reads the same
layout. 4.04r2 takes the material list's seventh entry as its environment map and
crashes without one, so a protocol 19 `Sky`'s list is padded to seven entries
with its first name (`g3d_sky` for the plain one). The `Player`, `PlayerData` and `Player` packet data layouts were read from
the 4.04r2 `Graal3DEngine.dll` on 2026-11-19; they are stock TGE 1.x, with Graal's
md3 model name in `PlayerData`. 4.04r2's `Player::onNewDataBlock` fails without a
shape it can load, so the `Player` is only sent with `tgeplayershape`. 4.04r2 loads
`.dts` version 24 and older; a shape with no sequences is accepted (the lab used a
version 24 `octahedron.dts`). The shape can sit next to the client, or in the
server's world folder for the client to download over TCP. No `.dts` with the real
player sequences is held yet, so the ghost has no walk animation.

Not done for these clients: other players' `Player`s, the world's objects (they are
`GameObject`s, a 4.20r6 and later class), `Sun`, terrain, and damage flash or
white-out. Flags and `weapon3d` scripts go over the 3D text channel as for 5.x.

Labs 2026-11-14 and 2026-11-19, with real clients:

| Client | How far it gets |
|---|---|
| Linux 3.151, Mac 3.153 | Every step above with a `Camera` and no `Sky`, up to the control object, and their moves are acked. Then each crashes on its first frame (a null read while drawing). Likely the missing `Sky`, which 4.04r2 also needs (2026-11-19); not rerun yet. |
| Windows 4.04r2 | 2026-11-14: crashes while starting its own 3D engine, before it opens the UDP port (on Windows 10, and under Wine, where a 2D server works). 2026-11-19, under Wine with no memory patch: with a `Camera` and the `Sky` (padded to seven materials by the server) it draws the sky and fog and holds the link; with `tgeplayershape` naming a loadable version 24 `.dts`, the `Player` ghost is accepted and walks (193 move packets, the position tracked from spawn), whether the shape sits next to the client or is downloaded from the server's world folder. A `Player` with no reachable shape drops the link in under a second. |
| Seihai's Ato client, as 4.04r2 and 3.153 | Every step, and the server reads its moves. |
| 4.20r8 | Not protocol 19: it speaks protocol 24 and plays a full 3D session. |

3.15x clients join on a `newmain` world (`default` or `classic`), whose allowed versions
already cover them; `modern` starts at 4.04r2.

The server log shows each link: `Graal3D: <account> linked from <ip> (protocol 24, 5.x ...)`.

## Which client each finding comes from

The 3D clients are not one protocol. Each layout here was read from one build, and
nothing says the others match.

| Client | Protocol | What is known |
| --- | --- | --- |
| 3.151 (Linux), 3.153, 4.04r2 (the first Graal 3D) | 19 | Stock TGE underneath: datablocks, moves, TGE object classes and a three-phase mission start, with no `GameObject`. Its layouts were read from the 4.04r2 `Graal3DEngine.dll`. The real 3.151 and 3.153 clients follow them up to the control object (see "Protocol 19"). The `GameObject` ghosting, control-object and camera work above does not apply to it. |
| 4.20r6 | 21 | Its class table (read from 4.20r6) has `GameObject`, `Camera` and `Sun`, so the server ghosts to it. The layouts of those classes were not read from 4.20r6: they are assumed to match 5.00r7. |
| 5.00r7, 5.12 | 24 | The `GameObject`, `Camera`, `Sun`, control-object and ghost layouts were read from the 5.00r7 dll. The handshake was checked against a real 5.12 client. |

The camera is off unless the world file has a `camera` line, so a client whose
`Camera` layout differs is only affected when it is turned on.

## Lab coverage and remaining checks

The handshake bytes are tested against the packets a real 5.12 client sent in a
2026-10-30 lab (`Catch_tests/Graal3D`). Later lab results recorded above include a
5.12 client running a 3D weapon script and drawing the sky box, and the
2026-11-14 protocol 19 run reaching the control object on real Linux 3.151 and
Mac 3.153 clients before their first-frame crashes. That run also records a
full 3D session on 4.20r8 (protocol 24). These results supersede the earlier
blanket statement that the game layer had never run against a real client.

They do not establish parity across all builds. The protocol 21 object layouts
are still assumed to match 5.00r7, the Walking row above still lacks a real-client
move check, and protocol 19 still lacks the objects listed in its section.
The first-frame crash cause remains unknown; Windows 4.04r2 crashed before UDP
connection. Keep those limits separate from the successful individual checks.

2026-11-25, real Linux 4.206 client (it links as protocol 24, G3D22126), mission
route, the `tools/lab/g3dlabworlds.py` round 1 worlds:

- **What draws:** the control scene drew in 3 of 3 runs. Its `.3ds` models were white, without textures, on that VM's software OpenGL; the textures did download.
- **No Sky:** the background clears to dark red (128, 0, 0).
- **Fog:** a fog colour change shows at the horizon.
- **Object counts:** 64 and 512 extra objects drew.
- **GameObject section 2 is the scale.** (2, 2, 2) drew an object twice the size and (0.5, 0.5, 0.5) half, so the server now calls it scale (default 1, 1, 1). An object line can set it.
- **Malformed `.3ds` crash the client** (segfault) rather than being skipped:
  - materials removed;
  - cut at half its length;
  - the main chunk length past the end;
  - two of three 0.5% bit-flip passes.

  A point count larger than the points present, or a face index of 65535, drew.
- **`.dts` versions:**
  - 16 and 25 are rejected with a console error;
  - 17 hangs the client (the link times out after about 30 s);
  - 24 draws.
- **`.dif` versions:** 43 and 45 are rejected, and 44 draws.
