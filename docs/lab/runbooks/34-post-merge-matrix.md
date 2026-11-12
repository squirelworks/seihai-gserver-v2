# Runbook: #34 real-client run after the beta4 25396a1 merge

Shows the Seihai fixes survived the upstream merge (#30) and that
`servers/modern` (#31) works with real clients.

## Needs

A packet-logging build of `seihai` at the commit being checked (write down
its hash), and the lab world folders from `bin/servers/`.

## Steps

Run each world on its own, with its logs emptied first and its console saved
(see [README.md](README.md)). Account names: `x` + the version, without dots
(`x139`, `x205`, ...).

**`servers/classic` (14803):** 1.39 and 1.41r1. Log in, walk, warp to
another level, chat, log out, and log in again.

**`servers/default` (14802):** 2.05r2, 2.17.1, 2.22, 2.31 and 3.153. Each one:
walk, warp, cross a gmap edge, chat, log out and log in again.

**`servers/modern` (14805):** 4.20r8, 5.12 and 6.03r7. First put one `.gpake`
and its key in place (`seihai-servertools gpak pack`; see lab-world.md), so
`PLO_FILEDECRYPTKEY` (104) is sent. 6.03r7 is expected to stall as in #23:
note where.

**RC, on `servers/default`:** a real 2.22 RC with a staff account:
1. Open an account, change nothing, save. Check afterwards that its admin
   level and worlds are unchanged in `accounts/<name>.txt`.
2. Give an account the rights `rw levels/*` and save.
3. Ban a player who is online. The server must not hang: one more client
   logging in afterwards proves it.

**`servers/npcdemo` (14804):** one real client (2.22 or 2.31): walk to each
demo NPC and trigger it.

## Check

For each world folder:
```
python3 tools/lab/labcheck.py matrix servers/<name>
```

Expected: one `logged in` PASS per client session, with its levels, warps
and files. Relogins show as a second row. `104 at out #N` appears on the
modern rows (4.20r8 and later) with N small: 104 should come before the
files. The RC's session shows up as a row too. A ban that hangs the server
shows as missing rows after the ban. No `Unknown Player Packet`.

A FAIL row with `DISCONNECTED: ...` gives the server's reason. Compare it
with `serverlog.txt`.

## Write down by hand

Per client: walk, warp, gmap edge, chat, relogin (worked / what broke). For
the RC: the three steps. For modern: whether the `.gpake` content (an image
from it) showed. Then file a new issue for each regression, and list the
builds checked in the note so #32's release notes can cite it.
