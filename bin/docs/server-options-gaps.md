# Server option gaps (issues #96 / #97)

Own inventory of stock-style `serveroptions.txt` keys that playerworld configs
reference but gserver-v2 previously lacked or ignored. Each key is tagged
**implement**, **stub**, or **accept**.

| Key / group | Disposition | Notes |
|---|---|---|
| `aptime5` | **implement** | Sixth AP recharge band (#96). Index helper in `utilities/APSystem.h`. Bands: 0–19, 20–39, 40–59, 60–79, 80–89, 90–100. Default `1200` matches `aptime4` so omitting it keeps the previous top-band pace. |
| `npcrights` | pending | #97 — enable/disable NC account-rights checks. |
| `ghostmodeenabled`, `ghostmodefornotstaff` | pending | #97 — ghost/trial mode. |
| `startap` | pending | #97 — AP for newly created accounts. |
| `speedhacktolerance` | pending | #97 — packet-rate tolerance. |
| `scriptstyle` | pending | #97 — script-engine style alias. |
| `translatedlanguages`, `defaultlanguage` | pending | #97 — translation load / default language. |

Silent no-ops are avoided: tracked keys either drive behaviour or are documented
stubs that operators can set without the value disappearing into the void.

See also `server-unimplemented.md` for era-level gaps (ghost mode gameplay).
