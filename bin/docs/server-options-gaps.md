# Server option gaps (issues #96 / #97)

Own inventory of stock-style `serveroptions.txt` keys that playerworld configs
reference but gserver-v2 previously lacked or ignored. Each key is tagged
**implement**, **stub**, or **accept**.

| Key / group | Disposition | Notes |
|---|---|---|
| `aptime5` | **implement** | Sixth AP recharge band (#96). Index helper in `utilities/APSystem.h`. Bands: 0–19, 20–39, 40–59, 60–79, 80–89, 90–100. Default `1200` matches `aptime4` so omitting it keeps the previous top-band pace. |
| `npcrights` | **implement** | #97 — when true (default), NC login still needs `PLPERM_NPCCONTROL`; when false, any RC receives the NC address. |
| `ghostmodeenabled`, `ghostmodefornotstaff` | **stub** | #97 — accepted and tracked; ghost/trial gameplay remains unimplemented (`server-unimplemented.md`). |
| `startap` | **implement** | #97 — when set, overrides `(defaultaccount)` AP for newly created accounts (same path as `startlevel` / `startx` / `starty`). |
| `speedhacktolerance` | **stub** | #97 — accepted and tracked (default 90); no packet-rate / speedhack detector consumes it yet. |
| `scriptstyle` | **implement** (alias) | #97 — stock-style alias for `default_scriptengine` when that key is unset. Tracked; NPC-server init prefers `default_scriptengine`, then `scriptstyle`, then `gs1`. |
| `translatedlanguages`, `defaultlanguage` | pending | #97 — translation load / default language. |

Silent no-ops are avoided: tracked keys either drive behaviour or are documented
stubs that operators can set without the value disappearing into the void.

See also `server-unimplemented.md` for era-level gaps (ghost mode gameplay).
