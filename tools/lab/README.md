# Lab tools

Loopback-only helpers for testing the fork. Never point them at a public
server or the official listserver.

| Tool | What |
|---|---|
| `npcprobe/` | Headless test client built on `seihai-core` (Seihai's protocol code). `npcprobe ADDR VERSION ACCOUNT STEPS` logs in (password `labpw`; `localhost = true` accepts any), runs a steps file and prints every server message. `STEP_MS` sets the wait after each step (default 300). |
| `steps/npcdemo.txt` | Steps that touch every NPC in the `npcdemo` world. |
| `mknpcdemo.py` | Writes `bin/servers/npcdemo/world/*.nw`. Edit the NPC scripts here, then rerun. |
| `graalnpcs.py` | Lists the NPC scripts in binary `.graal` levels (rough). |
| `microtests.gs1` | Chat-driven NPC script: put it in a lab level, say `t1`, `t2`, ... beside it. |
| `scan.py` | Static scan of a folder of `.graal` levels against the fork's GS1 command and function lists. |

Steps file commands: `wait SECONDS`, `walk X Y DIR`, `chat TEXT`,
`trigger X Y ACTION PARAMS`, `warp LEVEL X Y`, `enter`. Lines starting with `#`
are comments.

Build the probe: `cd tools/lab/npcprobe && cargo build --release`.
The 1.x levels used for the sweep are not in this repo; keep them out.
