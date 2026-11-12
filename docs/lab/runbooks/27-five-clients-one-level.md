# Runbook: #27 five clients in one level running one GS1 NPC

Clients: **1.39, 2.22, 2.31, 5.24, 6.03r7**, at the same time, in one level.

## The catch: one server for all five

No single lab world lets in 1.39 and 6.03r7 today: the folders split by
generation (`classic` 1.33 to 1.41r1, `default` 2.00 to 3.153, `modern` 4.04r2
to 6.037; see [lab-world.md](../../../bin/docs/lab-world.md)). Make a
throwaway folder for the run:

1. Copy `servers/default` to `servers/milestone`.
2. In its `config/allowedversions.txt`, make the `newmain` line take all five:
   `newmain = GNW07080, GNW03014, GNW28015, G3D14097:G3D0511C`
   (1.39, 2.22, 2.31, then 5.12 to 6.037). The version table (gs2lib
   `IUtil.cpp`) has no 5.24 token: the 5.24 client's own token is not in the
   range by name, so it may get "version not allowed". If it does, write down
   its token from `serverlog.txt` (the refused login prints it). That is a
   finding for #27 and gs2lib, not a lab failure; go on with four, or with 5.12
   in its place.
3. In its `config/serveroptions.txt`: `serverport = 14806`, keep
   `localhost = true`.
4. Add the test level `milestone.nw` to `world/` with one NPC whose GS1
   script contains the marker `lab27npc`, for example:
   ```
   NPC - 30 30
   // lab27npc
   if (playerenters) { message lab27npc; }
   if (playertouchsme) { say2 lab27npc touched by #a; }
   NPCEND
   ```
   and set `startlevel = milestone.nw` (or warp each client there).
5. PNG art: the pre-2.1 client needs GIF names. Note which body, head and
   sword images the default account uses, and whether GIF copies exist.

## Steps

1. Empty `servers/milestone/logs/`, then start the server with
   `| tee servers/milestone/console.log`.
2. Log in all five, accounts `m139`, `m222`, `m231`, `m524`, `m603`, one after
   another, all into `milestone.nw`. 6.03r7 goes through its login redirect;
   note whether it gets in (#23).
3. Once all are in, each client walks a few tiles and says its token:
   `c27-m139`, `c27-m222`, `c27-m231`, `c27-m524`, `c27-m603`.
4. Each client touches the NPC once.
5. Keep all five in for at least 70 s, then log out.

## Check

```
python3 tools/lab/labcheck.py milestone servers/milestone --accounts m139,m222,m231,m524,m603
```

| Row | Expected |
|---|---|
| `X logged in`, `X stayed 60 s` | PASS for all five |
| `X sees Y and its movement` | PASS (2 or more prop updates for Y) for every pair |
| `X gets Y's chat 'c27-Y'` | PASS for every pair |
| `X gets the NPC` | PASS "as GS1 text" up to 5.07. For 5.24 and 6.03r7, INFO "as bytecode" or "FINDING": that gap is the expected finding #27 asks to write down |
| `files the server could not send` (INFO) | for 1.39, the PNG names it asked for: the GIF gap |
| `level reached` (INFO, 6.x) | `milestone.nw` |

## Write down by hand

Per client: did it see the others move smoothly (old clients in half-tile
steps), did the NPC's `message` / `say2` show, and did the player images and
heads draw on 1.39. The checker sees the bytes, not the screen.
