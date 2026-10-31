# npcdemo: sample world for the built-in NPC-server

A small world that only works when the fork's built-in NPC-server runs
(`serverside = true`). Every NPC script runs on the server; the client gets
NPC props and, for one NPC, the part after `//#CLIENTSIDE`.

Its level NPC scripts have no `//#CLIENTSIDE` line, so the world sets
`serversidescriptlevels = npcdemo_*.nw` to run them on the server (without it
they would go to the client, which is the default since the classic-scripts
change).

It is set up for a loopback lab: `localhost = true` (no listserver, any
account and password are accepted), `127.0.0.1`, port 14804, UPnP off.
Do not open it to the internet as it is.

## Start

```
cd bin
./gs2emu --server npcdemo
```

Then log in with any 2.x client or Seihai to `127.0.0.1:14804`. New accounts
start in `npcdemo_start.nw` at 30,40.

## What is in it

`npcdemo_start.nw`

| # | Where | Tests |
|---|---|---|
| 1 | Greeter, 30,30 | `created`, `showcharacter`, `playerenters`, `timeout`, `setcharprop` |
| 2 | block, 20,36 | `playertouchsme`, `say2` |
| 3 | block, 40,36 | `this.` variables, `server.` flags, `setstring`, `message` |
| 4 | block, 30,50 | `setlevel2` to `npcdemo_room.nw` |
| 5 | block, 45,45 | `addweapon`, `hasweapon` (gives the Demo Wand) |
| 6 | Listener, 15,45 | `playerchats`, `strequals`, `timevar`: say `hello` or `time` |
| 7 | block, 50,30 | `triggeraction` from the client: `if (actiondemo)` |
| 8 | block, 10,30 | a split script: server half on touch, client half after `//#CLIENTSIDE` |
| 9 | block, 50,45 | `putnpc2` with a script that destroys itself after 5 s |
| 10 | Walker, 30,20 | `move` with the `movementfinished` option |
| 11 | block, 20,50 | `hide`, `show`, `visible` |
| 12 | block, 10,50 | `setstring`, `addstring`, `strlen`, `tokenize`, `#t` |

`npcdemo_room.nw`: a Keeper (`playerenters`, `playerleaves`, reads the
`server.` flags back) and a block that warps back.

`weapons/weaponDemo%032Wand.txt`: a weapon with a server half
(`if (actionserverside)`) and a client half that sends
`triggeraction 0,0,serverside,Demo Wand,zap` when fired.

`npcs/npcControl-NPC.txt`: the Control-NPC (`playerlogin`, `playerlogout`,
`actionserver...`).

Every block NPC calls `setshape 1,32,32` in `created`: the server does not read
image sizes, so without it the NPC cannot be touched or hit by `triggeraction`.

The level boards are the board of the stock `onlinestartlocal.nw`.

Lab results and the list of GS1 gaps: `docs/lab/2026-10-31-npcserver-gs1.md`.
