# Project connector key exchange

This is the GPL server half of [graal-client-repro issue #429](https://github.com/squirelworks/graal-client-repro/issues/429), written from
its [project cipher contract](https://github.com/squirelworks/graal-client-repro/blob/926cf3aa/graal-6.1.1.3-GNP1905C/connector/CIPHER.md). It is not a
stock operator connector and does not use stock keys.

## Configuration

The exchange is off by default. To enable it on a loopback lab server whose
allowed versions include `GNP1905C`, set these server settings:

```
connector_login = true
connector_server_key = config/lab-server-key.pem
connector_parse_key = config/lab-parse-key.pub.pem
```

Provide the matching lab server private key and parse public key at runtime,
outside versioned source and published artifacts. Coordinate their public
moduli with the client connector script. The server private key must be PKCS#1 PEM
(`RSA PRIVATE KEY`); the parse public key must be SPKI PEM (`PUBLIC KEY`).
Both keys must be RSA-2048 with public exponent 65537. Relative paths resolve
in the server's working directory, like the existing secure-login key path.
No key is generated or substituted if a configured file is absent or invalid.
No private server or parse key fixture is committed by this change. Public
modulus/SPKI material can be shared deliberately; keep private runtime files
out of source, logs and artifacts. This experiment remains loopback only.

## Routing and cipher boundary

The initial raw eight-byte token `SEI00001` selects this connector transport.
A proper partial prefix waits for more input. The first mismatch selects the
classic parser permanently and passes the original bytes unchanged. Once the
raw token selects the connector, malformed framing or key exchange closes it;
there is no fallback. The existing secure login carries its token *inside* a
classic length/zlib frame and retains its original handler and settings.

The next clear EILLLT frame is outer compression 1, counter 2, id `0xfd`,
containing exactly one zlib stream. It inflates to inner compression 0,
counter 1, id `0xfc`, total length 262 and a 256-byte RSA block. PKCS#1 v1.5
type-2 unwrapping must yield exactly the project's raw 16-byte client key.
Truncated frames wait; invalid lengths, surplus inflated/zlib bytes and failed
RSA unwraps close the connection.

The reply uses the same clear frame shape, wrapping to the parse public key.
Its plaintext is cipher type 1, length byte 48, an independent random 16-byte
server key, and length byte 32 for the empty IV. RC4 begins after the complete
clear frame in each direction, with no discarded keystream bytes. Headers and
bodies share the byte stream. Coalesced encrypted input is decrypted exactly
once; fragmented writes retain their stream position. Partial socket writes
retain the unsent clear reply bytes. Pending input is bounded and this
unadmitted connection expires after 30 seconds.

## Scope and validation

This implements the server key-exchange item, not authenticated game login.
The connection remains an awaiting-login object; it creates no player,
authenticates no account and does not dispatch pending account/game bytes.
Those bytes remain bounded until the follow-up defines their login contract.
Issue #429 must remain open until that path and its loopback checks are done.
Do not interpret a received key frame as admission or gameplay success.

`Catch_tests/ConnectorCipher` contains offline RC4 reference/reset tests and
fragment, coalesced, malformed, oversized and key-transform failure tests.
RSA tests use independently generated OpenSSL ciphertexts with a temporary
RSA pair outside the checkout. A separate OpenSSL decrypt checks this module's
outgoing wrap; CI removes the temporary private key even when a test fails.
No private test key is committed. The existing `SecureLogin` tests remain in
sanitizer CI as a separate protocol.
A VM loopback run with the matching client and independent review is still
required before claiming end-to-end compatibility. No server was run on the
host to prepare this change.

RC4 and PKCS#1 v1.5 are used only for this project wire contract. This is not
an authenticated or integrity-protected transport. RSA errors receive the
same close behavior; padding validation is not claimed to be constant time.
