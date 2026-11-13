# Real-RC recheck of the PR #63 fixes, 2026-11-13

Loopback only: a Debian lab VM, with every program inside a private network
namespace that has only `lo`. The real RCs ran under Wine on a headless Xvfb
display. The setup, accounts, test files and the decoding relay are the ones in
seihai-rc `docs/lab/2026-11-13-archived-rc-captures.md` (branch
`claude/project-thread-rvkilj`). That note has the "before" captures, made on
`a3b6ac23` and `e3da7ace`.

| Part | Commit / build |
|---|---|
| This fork, before | `seihai` at `a3b6ac23` and `e3da7ace`, Linux, GCC 14 + vcpkg, `-DPACKET_LOGGING=ON -DTESTS=OFF -DUPNP=OFF` |
| This fork, after | `claude/project-thread-rvkilj` at `a1859ba` (PR #63), the same build |
| seihai-listserver | `main` at `ae213fc` |
| Real RCs | 2004 RC (`GSERV024`, `NCL11012`) and RemoteControl3 (`GSERV025`, `NCL21075`), both as shipped |

Servers: `default` (14802) and `npcdemo` (14804, built-in NPC server). The config
files were restored from a backup before the run.

## Results

| # | Check | Before | After (`a1859ba`) | Result |
|---|---|---|---|---|
| 1 | Folder config set, 2004 RC | 77 left lines with spaces unquoted; the RC split `head    heads/*` into two lines, and its 54 rewrote `foldersconfig.txt` one word per line | 77 quotes them: `…,"# Folder Configuration","head    heads/*","body    bodies/*.png",…` (824 bytes, was 752). The RC shows each line whole. After opening, adding `file    labdir/*.txt` and sending, the file is the 37 original lines unchanged plus the RC's added blank line and the new line (39). | **pass** |
| 2 | Server options set, 2004 RC | `serveroptions.txt` 572 → 2476 lines | The dialog shows whole lines. After adding `labopt = 1` and sending (52, 25084 bytes), the file is the 572 original lines unchanged plus the blank line and the new line (574). | **pass** |
| 3 | Small put, RC3 (50 → 93) | 42-byte file stored as 43 bytes | 67 `Uploaded file labsmall.txt`; stored 42 bytes, `cmp` equal to the source | **pass** |
| 4 | Large put, RC3 (155, three 50 → 93, 156) | 81200-byte file stored as 81203 bytes (a `\n` after each piece) | 67 `Uploaded large file labup70k.txt`; stored 81200 bytes, `cmp` equal to the source | **pass** |
| 5 | Folder delete, non-empty, RC3 (160 `world/labgone/`) | 16 `filesystem error: cannot remove: Directory not empty …` and the link closed | 67 `Error removing world/labgone/.  Folder may not exist or may not be empty.`; the folder stays; the RC stays connected | **pass** |
| 6 | Folder delete, empty, RC3 (160 `world/labdir/`) | Folder removed, then 16 `filesystem error: directory iterator cannot open directory …` and the link closed | 67 `Folder world/labdir/ has been removed.`, then 65 and 66 (`/`, no files); the folder is gone; the RC stays connected | **pass** |
| 7 | NPC delete, 2004 RC (104) | 159 sent twice to the NC | One 159 for id 1003 to each of the two NCs (`#8` and `#12`), after the 74 chat line | **pass** |
| 8 | Type-6 login | In 12 real-RC type-6 logins, one got a plain-zlib id 42 frame (no compression byte, no gen-5) before the gen-5 stream | 10 real-RC type-6 logins (2004 RC and RC3, both servers): no plain-zlib frame; the relay replay decoded every frame without error | **pass** (small sample: the old fault was a timing race seen once) |
| 9a | 157 NPC attributes, 2004 RC | Lines split at spaces | Lines quoted (`"Variables dump from npc labnpc3",,"labnpc3.type: OBJECT",…`); the RC shows `labnpc3.pos: 30.50, 30.00, 0` and `labnpc3.flags[other.flag]: a b` whole | **pass** |
| 9b | 160 NPC script | `// lab npc script` shown as three lines | `"// lab npc script","if (created) {","  this.lab = 1;",}`; the RC shows the four lines as typed | **pass** |
| 9c | 161 NPC flags | Unquoted | `"other.flag=a b",labnpcflag=1` | **pass** |
| 9d | 162 class script | Unquoted | `"// rc3 class","function onCreated() {","  this.x = ""a, b"";",…`; the RC shows the quotes and the comma intact | **pass** |
| 10 | Weapon added from one NC (117) | 167 only to the NC that added it; the other NC's list stayed stale | 167 (the full list) to both NCs; the new weapon `labbcast` appeared in the other 2004 RC's open Weapons window | **pass** |
| 11 | Account reset, 2004 RC (75) | Wrote `COMMUNITYNAME (defaultaccount)` | 74 `labrc2 has reset the attributes of account: labprops`; no `(defaultaccount)` in the file | **pass**, with a note below |

Note on 11: before the reset, `labprops.txt` had `COMMUNITYNAME labprops`; after
it, the file has no `COMMUNITYNAME` line at all (`NAME labprops` is still there).
If the loader falls back to the account name when the line is missing, that is
fine. If not, a reset account now has an empty community name.

## Not fixed on purpose (not rechecked)

- rc133's (`GSERV014`) id 53 account add. It needs version-gated ids, and its
  props layout is unknown.
- `BANREASON` stored tokenized: by design.

## Logs

The relay's decoded log for this run (`run5`) is in the lab VM and on the lab
host; it is not committed. Times in it are UTC (VM clock).
