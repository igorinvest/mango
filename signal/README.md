# Mango signaling server

Lets two Mango apps find each other and exchange WebRTC offers, answers and ICE candidates. Media never
passes through it. Mango connects to the URL in `remoteSignalingUrl` (settings.json), for example
`wss://signal.example.com/v1`; put a TLS-terminating proxy in front of this server for `wss`.

```
npm start                                       # local, 127.0.0.1:5010
npm run smoke                                   # starts a local server on :5011 and runs the end-to-end check
npm run smoke -- wss://signal.example.com/v1    # the same check against a running server
```

The server is stateless: everything is in memory, and a restart closes every socket with 1001, so clients
reconnect. `GET /health` → `{ ok, version, devices, sockets }`.

## Protocol v1

One WebSocket per running app, JSON text messages, at most 64 KB each. Any message may carry a `requestId`;
replies and errors to it repeat that `requestId`. (Mango builds before it sent `id`, and still get their
`id` back.)

### Identity and auth

Each Mango install makes an **Ed25519** key pair once and keeps it.

- `publicKey`: the raw 32-byte public key, base64url without padding.
- `deviceId`: lowercase hex SHA-256 of those 32 bytes (64 characters).

```
S→C  { type: "challenge", nonce }
C→S  { type: "auth", publicKey, signature }   signature = Ed25519 over UTF-8 "mango-signal-v1 auth:" + nonce, base64url
S→C  { type: "ready", deviceId }
```

`auth` must come first and within 5 s. A newer socket for the same device closes the older one with 4001.

### Pairing

A shows a QR code or link containing a random **invite secret** (at least 16 characters; use 128 random
bits, base64url). Only its hash goes to the server.

```
A→S  { type: "invite.open", inviteHash, ttl? }      inviteHash = hex SHA-256 of the UTF-8 secret; ttl 30–600 s (default 600)
S→A  { type: "invite.opened", inviteHash, expiresAt }
B→S  { type: "invite.join", inviteSecret }
S→A  { type: "invite.joined", inviteHash, peer: { deviceId, publicKey } }
S→B  { type: "invite.accepted", inviteHash, peer: { deviceId, publicKey } }
A→S  { type: "invite.close", inviteHash }           cancel (optional)
```

An invite works once and only while A is connected; otherwise B gets `offline` and the invite stays
until it expires. A device can have at most 5 open invites.

**The server is not trusted with pairing.** Before both apps remember each other, they confirm over the
first `signal` exchange that each holds the invite secret. For example, each sends HMAC-SHA256(secret,
"mango-pair-v1" + both public keys in a fixed order). This proves that the peer the server introduced is
the one that scanned the code.

### Signals

```
C→S  { type: "signal", to: deviceId, payload }      payload: any JSON
S→C  { type: "signal", from: deviceId, payload }
```

The server forwards `signal` to any connected device and stores nothing; the apps decide whom they talk to.
Two Mango apps that are paired both ways have two connections between them, one per direction. Each is set up
with its own offer and answer, so the server sees nothing different.
**Mango must:**

- Accept a `signal` only from a paired `deviceId` (A enforces revocation by ignoring the revoked device).
- Sign every offer and answer SDP with the device key. The peer must check the signature against the
  public key saved at pairing. The SDP's DTLS fingerprint is what the WebRTC encryption is bound to, so a
  signature the peer checks means a compromised signaling server can't intercept the connection.

### Other messages

```
C→S  { type: "ping" }   →   S→C  { type: "pong" }
S→C  { type: "error", code, requestId?, message? }
```

| Error `code` | Meaning |
|---|---|
| `bad_request` | Malformed message or field |
| `offline` | The target device (or the invite's owner) isn't connected |
| `invite_not_found` | No such invite, or it expired or was used |
| `invite_exists` | Another device already opened an invite with this hash |
| `invite_limit` | Too many open invites |

| Close code | Meaning |
|---|---|
| 1001 | Server restarting: reconnect with backoff |
| 4000 | Auth failed, timed out, or a message came before auth |
| 4001 | The same device connected from another socket |
| 4008 | Rate limited (more than 50 messages at once / 10 a second, or too many wrong invite secrets from this IP) |

New connections are limited per IP (20 open, 30 at once then one a second); over the limit the upgrade is
refused with 429. The server pings every 30 s and drops sockets that don't answer. Clients that send nothing
for long stretches should also send `ping`, since Cloudflare closes WebSockets idle for 100 s.

### Not provided

TURN. v1 uses public STUN (`stun:stun.cloudflare.com:3478`, `stun:stun.l.google.com:19302`), and Mango
reports a clear error when no direct connection works.
