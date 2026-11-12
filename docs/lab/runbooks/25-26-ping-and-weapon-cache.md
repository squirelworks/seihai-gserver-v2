# Runbook: #25 ping and #26 weapon cache (4.20r8, 5.12, 6.03r7)

Both use `servers/modern` (`127.0.0.1:14805`) and the same three clients, so
run them in one sitting.

## Needs

- A packet-logging build with PR #46 (the `PLI_PONG` handler and
  `clientping`). Without it, a 162 from a game client shows up in
  `console.log` as "Unknown Player Packet", which is also worth recording.
- Clients: **4.20r8**, **6.03r7**, and **5.12** as the control. Any 4.04r2 to
  5.12 build adds to #26.
- In `servers/modern/config/serveroptions.txt`: `clientping = 5`.
- For #26, the world needs at least one GS1 weapon the client gets at login
  (the default `-Wbow` / `-Gani` weapons do). Note its name.

## Steps

1. Empty `servers/modern/logs/`, then start the server with
   `| tee servers/modern/console.log`.
2. **4.20r8**, account `lab420`: log in, stand still 30 s (six pings), walk a
   little, log out.
3. **6.03r7**, account `lab603`: the same. If it stalls at login as in #23,
   note how far it got. The ping runs from login, so 30 s at the stall still
   tests #25.
4. **5.12**, account `lab512`: the same, 30 s.
5. **#26, second login.** Log in again with **4.20r8** as `lab420` (same
   client folder, so its `weblevels/weapons/` cache is kept) and stay 10 s.
   Before you do, copy the client's `weapons/*.code` files (if any) next to
   your notes.
6. Stop the server.

## Check

```
python3 tools/lab/labcheck.py ping servers/modern
python3 tools/lab/labcheck.py weaponcache servers/modern
```

**#25, expected with PR #46:**

| Row | Expected |
|---|---|
| `lab420 (4.208) is sent 108` / `lab603 (6.037) is sent 108` | PASS, about one per 5 s |
| `every 108 is sub 0` | PASS |
| `answers with 162` | PASS: the analysis says both answer `{162}{'!'}{s value}` |
| `each 162 is '!' + a value we sent` | PASS. A FAIL here means the analysis's layout is off: copy the hex from the row into the note |
| `lab512 (5.12) is never sent 108` | PASS |
| `round trips` (INFO) | the server's `[Ping]` lines, in ms |
| `no 'Unknown Player Packet'` | PASS |

**#26, expected today (before #26 is built):**

- Each `login N` INFO row lists what went out: `33 xN` is the weapon sent
  whole (`PLO_NPCWEAPONADD`), `193 xN` the hash, `158 asked xN` the scripts
  the client asked back.
- `weapons go out as 193` FAILs for 4.20r8 and 5.12. That is the gap #26
  describes: GS1 weapons go out whole to 4.04r2 to 5.12. Record the rows as
  the baseline.
- Once #26 is built, the same run should PASS both `weapons go out as 193`
  and `the last login asks no script back (cache used)`.

## Write down by hand

- For 6.03r7: how far the login got (the #23 stall).
- Whether any client showed an error box or dropped.
- For #26: the names and sizes of the client's `weapons/*.code` files after
  the first login. #26's first step needs them to know which bytes the client
  takes its CRC over.
