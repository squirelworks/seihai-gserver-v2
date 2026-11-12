# Secure login (SEI00001), loopback run, 2026-11-12

No real Graal client: they never send this token. Cloud container, loopback.

- Server: this branch's `gs2emu`, a copy of `bin/servers/default` on port
  14812, `localhost = true`, allowed versions 2.0 to 2.31 and 3.041 to 3.153.
- Client: `seihai-core` v0.0.6 (`login_gserver_secure`, the code behind
  seihai-mae's `--secure-login`), a pinned server key, password `labpw`.
- A Python byte relay on 14813 logged what the client sent.

| Run | Result |
|---|---|
| First start | `Secure login (SEI00001): created key config/seihai-login.key`, mode 0600, public key and fingerprint printed. Second start: `loaded key`, same key. |
| Secure 2.31 (gen 5) | Logged in; 37 messages in the first 3 s, same burst as a classic 2.31 login; log `[SEI00001 (inner GNW28015) (2.31)]` |
| Secure 2.22 (gen 5) | Logged in; `(inner GNW03014)` |
| Secure 2.05r2 (gen 3, PLTYPE_CLIENT with a key byte) | Logged in; `[Client] [SEI00001 (inner GNW03081) (2.05r2)]` |
| Secure 3.04 (gen 5) | Logged in; `(inner G3D03014)` |
| Classic 2.31 | Logged in as before |
| Secure with another pinned key | The client stopped: `server key SHA256:V/QV… is not the given key …; not sending the password`; no login on the server |
| Secure 6.03r7 (not allowed on this world) | Refused after the hello, before the password: "Your client version is not allowed on this server." |
| `require_secure_login = true`, classic 2.31 | `Classic login refused (require_secure_login)`; the client got the disconnect message |
| Same, secure 2.31 | Logged in |
| Relay, classic 2.31 | First frame unzipped: `%9GNW28015(relaycls%labpw` |
| Relay, secure 2.31 | First frame: the hello; second: `SEI00001` + base64. `labpw` appears nowhere in the bytes sent |

The Catch test `SecureLogin` also checks a fixed vector made separately with
Python's pyca/cryptography from the protocol document (fixed X25519 and
Ed25519 keys), so the C++ side matches a second implementation byte for byte.
