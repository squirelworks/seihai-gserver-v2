# Seihai lab world

This fork ships a ready-to-run lab world for testing clients on one machine. It
has two server folders, both set up for loopback only:

| Folder | Generation | Clients | Address |
|---|---|---|---|
| `servers/default` | newmain | 2.00 to 2.31, 3.04 to 3.153 | `127.0.0.1:14802` |
| `servers/classic` | classic | 1.33 to 1.41r1 | `127.0.0.1:14803` |

## Start it

From the folder that holds `gs2emu`, start each server in its own terminal:

```
./gs2emu --server default      # Windows: gs2emu.exe --server default
./gs2emu --server classic      # Windows: gs2emu.exe --server classic
```

Stop a server with Ctrl+C. A one-step setup that starts both will be part of
seihai-servertools.

macOS may block the unsigned `gs2emu` the first time. Open it once with
right-click and Open, or clear the download flag with
`xattr -dr com.apple.quarantine .` in this folder.

## What the lab settings are

Both folders' `config/serveroptions.txt` differ from upstream's defaults in
these settings:

- `serverinterface = 127.0.0.1`: the server listens on loopback only, so other
  machines cannot connect.
- `serverip = 127.0.0.1` and `localip = 127.0.0.1`: the address handed to
  clients.
- `upnp = false`: no router port is opened.
- `localhost = true`: logins are accepted without listserver verification, so
  the world runs on its own. This is only safe while the server listens on
  loopback.
- `listip = 127.0.0.1`, port 14900: a listserver on the same machine, for
  example the Seihai lab listserver. With one running, 2.22 and 2.31 can pick
  the server from their server list. Without one, the server keeps trying to
  reach it and otherwise runs normally.
- `url` is empty.

The classic folder also has `generation = classic`, port 14803,
`flaghack_movement = false` and no `-gr_movement` weapon, and its
`allowedversions.txt` lets in 1.33 to 1.41r1. Its accounts use `head0.gif`,
`sword1.gif` and `shield1.gif`, because 1.33 to 1.39 only have `.gif` versions
of them.

## Connecting

- 1.33 to 1.39 have no server list: use "One Player Online" with `127.0.0.1`
  and port 14803.
- Other clients: connect to `127.0.0.1:14802`, directly or through a lab
  listserver.
- The Seihai client: `seihai-mae --online 127.0.0.1:14802 --account test --password-env GRAAL_PW --version 2.05`
  (any password in `GRAAL_PW`).

Any account name and password log in. A new account starts from
`accounts/(defaultaccount).txt`.

## Opening the server to other machines

Do this only for a server you mean to run for other people:

1. Set `serverinterface`, `serverip` and `localip` back to `AUTO` (or to your
   addresses).
2. Set `localhost = false`, so logins are checked by the listserver.
3. Set `listip` and `listport` to the listserver you use.
4. Turn `upnp` on only if you want the server to open a router port itself.
