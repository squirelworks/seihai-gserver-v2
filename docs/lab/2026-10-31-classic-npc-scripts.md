# Classic level NPC scripts on a serverside world, 2026-10-31

## The problem

With `serverside = true`, stock GServer-v2 runs everything above `//#CLIENTSIDE`
on the NPC-server. A level NPC script with no separator therefore runs entirely on
the server and the client gets none of it. None of the 1,648 NPC scripts in the 690
levels shipped with the 1.x clients has the separator (they were written for
clients that ran every level script themselves), so on a serverside world they
all ran on the server: `play`, `toinventory` and other client-side commands
failed, bare `shootball;` was refused 2,292 times in one sweep, and the client
showed none of their effects. See `2026-10-31-npcserver-gs1.md`, gap 2.

## Decision

Owner had no preference; the coordinator's recommendation was taken: by default a
level NPC script with no `//#CLIENTSIDE` line is client-side, as in 1.x, with an
opt-in per world or per level to run it on the server.

## Change

Two server options (`serveroptions.txt`), read when a level file is loaded:

| Option | Default | Meaning |
|---|---|---|
| `levelnpcscripts` | `clientside` | `clientside`: the whole script goes to the client. `serverside`: the whole script runs on the NPC-server (stock behaviour). |
| `serversidescriptlevels` | empty | Level globs (`*`, `?`, case-insensitive) that run such scripts on the NPC-server even when `levelnpcscripts = clientside`. |

How: `LevelLoader` puts a `//#CLIENTSIDE` line in front of a level NPC script
that has none, unless the options say server-side. Nothing else changes: scripts
with a separator split as before; database NPCs, weapons, classes and `putnpc2`
NPCs are untouched; worlds with `serverside = false` are untouched (they already
send everything to the client).

`bin/servers/npcdemo` sets `serversidescriptlevels = npcdemo_*.nw`, because its
level scripts are meant to run on the server. `default` and `classic` document
the options commented out (`serverside` is off there).

## Lab runs (loopback, Linux release build, probe `tools/lab/npcprobe` as 2.31)

| World | Setting | Result |
|---|---|---|
| `npcdemo` | `serversidescriptlevels = npcdemo_*.nw` | as before this change: sign, counter, weapon, split script, warp, Greeter, trigger all answer from the server |
| `npcdemo` copy | no opt-in (default `clientside`) | NPC props carry the whole script text (comments stripped) for the client; no server-side replies (no sign, no counter, no greeting) |
| `npcdemo` copy | `levelnpcscripts = serverside` | as the first row (stock behaviour) |
| the 690 1.x levels (lab-only copy, not in the repo) | default `clientside` | the probe warped into each of the 449 levels the sweep list reaches and walked them: no connection closed, server still up, 0 lines in `npclog.txt`, 0 lexing or parsing failures (stock: 129 scripts failed to lex in 20 levels, 12 failed to parse in 11), and 1,572 NPC prop messages carried script text for the client |

No real Graal client was run; the next check is real 2.05 to 2.31 clients under
Wine on the classic levels, to see the client-side scripts actually run.

## Next steps

- Real clients on the 1.x levels with this build (Wine), and on npcdemo.
- `play`, `shootball` and the other client-side commands now run where 1.x ran them, so
  gaps 3 and 4 of the GS1 write-up only matter for opted-in levels.
- Consider an RC command to reload a level after changing the options.
