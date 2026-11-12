# Secure login (SEI00001)

Seihai's own login, written from the Seihai project's
`documentation/secure-login.md` (revision 1). Real Graal clients never use it.

Every classic Graal login sends the account and the password in the clear (the
login frame is only zlib-compressed). A Seihai client started with
`--secure-login` sends the version token `SEI00001` instead, runs a key
exchange, and sends the account and password sealed:

1. The client sends its hello: the client type and key byte, `SEI00001`, the
   version token of the build it plays as (for example `GNW28015` for 2.31),
   and a fresh X25519 key.
2. The server checks that version against `allowedversions.txt` before any
   password is sent, then answers with its own fresh X25519 key, its Ed25519
   key and a signature over both hellos. A refused version gets the usual
   "not allowed" message and the connection closes.
3. The client checks the signature and the server's key (it remembers the key
   on the first visit, as SSH does), then sends the rest of the classic login
   sealed with ChaCha20-Poly1305.
4. From there the session is the inner build's ordinary session.

The server log shows such a login as `[SEI00001 (inner GNW28015) (2.31)]`.

## Settings (`serveroptions.txt`)

| Setting | Default | Meaning |
|---|---|---|
| `secure_login` | `true` | Take secure logins. Off: they get "This server does not take secure logins." |
| `secure_login_key` | `config/seihai-login.key` | The server's Ed25519 seed, one line of base64, made on first start with mode 0600. |
| `require_secure_login` | `false` | Refuse classic game logins. This locks out every real Graal client. |

At start the server prints its public key and fingerprint
(`SHA256:` + base64). Publish the fingerprint so players can check it, and
back up the key file: a new key makes every player's client refuse the server
until they remove its old line from `known_servers.txt`.

## Limits

- Only the login is protected. The game session after it is the inner build's
  gen-3/gen-5 stream, which is not secret.
- The server still sees the password, as it always has.
- RC and NC logins are classic only for now; `require_secure_login` does not
  touch them.
- The encrypted gserver to listserver link (section "Gserver to listserver
  link" of the protocol document) is not in this fork yet.
