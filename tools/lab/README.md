# Lab tools

Loopback-only helpers for this fork's own lab world. Never point them at a
public server or the official listserver.

| Tool | What |
|---|---|
| `mknpcdemo.py` | Writes `bin/servers/npcdemo/world/*.nw`. Edit the NPC scripts here, then rerun. |
| `graalnpcs.py` | Lists the NPC scripts in binary `.graal` levels (rough). |
| `scan.py` | Static scan of a folder of `.graal` levels against the fork's GS1 command and function lists. |
| `labcheck.py` | Expected-vs-actual checks for the real-client lab runs (#25, #26, #27, #34) from a packet-logging server's logs. Runbooks: `docs/lab/runbooks/`. Tests: `python3 -m unittest tools/lab/test_labcheck.py`. |

The 1.x levels used for the sweep are not in this repo; keep them out.

The test client these lab records use (`npcprobe`), its `npcdemo` steps file
and the GS1 micro tests moved to seihai-servertools on 2026-11-11, so this
repo stays a GServer-v2 variant: run
`seihai-suite-stress probe ADDR VERSION ACCOUNT STEPS` and see
`docs/lab/probe/` there. The lab records in `docs/lab/` keep the old
`tools/lab/...` paths as they were run.
