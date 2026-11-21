//The signaling protocol (README.md): authenticated devices, pairing invites and message forwarding.
//Everything is in memory; a restart only makes devices reconnect. The hub doesn't know about WebSockets:
//server.js hands it a connection { ip, send(message), close(code, reason) }.
import { Bucket, Buckets } from './limits.js';
import { DEVICE_ID, newNonce, sha256Hex, verifyAuth } from './identity.js';

export const CLOSE = {
  protocol: 4000, //bad auth, or a message before auth
  replaced: 4001, //the same device connected again
  rateLimited: 4008,
};

const AUTH_TIMEOUT_MS = 5_000;
const INVITE_TTL_S = { default: 600, min: 30, max: 600 };
const INVITES_PER_DEVICE = 5;
const INVITES_TOTAL = 10_000;
const INVITE_HASH = /^[0-9a-f]{64}$/;

export class Hub {
  constructor({ log = console.log } = {}) {
    this.log = log;
    this.devices = new Map(); //deviceId -> Session
    this.invites = new Map(); //inviteHash -> { deviceId, expiresAt }
    //Wrong invite secrets per IP: 10, then one a minute
    this.joinFailures = new Buckets(10, 1 / 60);
  }

  open(conn) {
    return new Session(this, conn);
  }

  //Drops expired invites and idle rate-limit entries; called every minute
  sweep() {
    const now = Date.now();
    for (const [hash, invite] of this.invites) {
      if (invite.expiresAt <= now) {
        this.invites.delete(hash);
      }
    }
    this.joinFailures.sweep();
  }

  liveInvite(hash) {
    const invite = this.invites.get(hash);
    if (invite && invite.expiresAt <= Date.now()) {
      this.invites.delete(hash);
      return null;
    }
    return invite ?? null;
  }
}

//The request's ID, to repeat in the reply, under the key it came with: "requestId", or "id" from Mango
//builds before it.
function requestRef(msg) {
  if (msg.requestId !== undefined) {
    return { requestId: msg.requestId };
  }
  return msg.id !== undefined ? { id: msg.id } : {};
}

class Session {
  constructor(hub, conn) {
    this.hub = hub;
    this.conn = conn;
    this.deviceId = null;
    this.publicKey = null;
    this.nonce = newNonce();
    //Messages from this socket: 50 at once, 10 a second after that
    this.rate = new Bucket(50, 10);
    this.authTimer = setTimeout(() => conn.close(CLOSE.protocol, 'auth timeout'), AUTH_TIMEOUT_MS);
    conn.send({ type: 'challenge', nonce: this.nonce });
  }

  receive(text) {
    if (!this.rate.take()) {
      this.hub.log(`rate limited: ${this.conn.ip}${this.deviceId ? ' ' + this.deviceId.slice(0, 12) : ''}`);
      return this.conn.close(CLOSE.rateLimited, 'rate limited');
    }
    let msg;
    try {
      msg = typeof text === 'string' ? JSON.parse(text) : null;
    } catch {
      msg = null;
    }
    if (!msg || typeof msg !== 'object' || Array.isArray(msg) || typeof msg.type !== 'string') {
      return this.error('bad_request', {}, 'expected a JSON object with a type');
    }
    if (!this.deviceId) {
      return msg.type === 'auth' ? this.auth(msg) : this.conn.close(CLOSE.protocol, 'auth first');
    }
    const ref = requestRef(msg);
    switch (msg.type) {
      case 'invite.open': return this.inviteOpen(msg, ref);
      case 'invite.close': return this.inviteClose(msg, ref);
      case 'invite.join': return this.inviteJoin(msg, ref);
      case 'signal': return this.signal(msg, ref);
      case 'ping': return this.conn.send({ type: 'pong', ...ref });
      default: return this.error('bad_request', ref, `unknown type ${msg.type}`);
    }
  }

  closed() {
    clearTimeout(this.authTimer);
    if (this.deviceId && this.hub.devices.get(this.deviceId) === this) {
      this.hub.devices.delete(this.deviceId);
    }
  }

  //ref: the request's ID (see requestRef)
  error(code, ref, message) {
    this.conn.send({ type: 'error', code, ...ref, message });
  }

  auth(msg) {
    const deviceId = verifyAuth(msg.publicKey, msg.signature, this.nonce);
    if (!deviceId) {
      return this.conn.close(CLOSE.protocol, 'auth failed');
    }
    clearTimeout(this.authTimer);
    this.deviceId = deviceId;
    this.publicKey = msg.publicKey;
    const previous = this.hub.devices.get(deviceId);
    this.hub.devices.set(deviceId, this);
    previous?.conn.close(CLOSE.replaced, 'replaced');
    this.conn.send({ type: 'ready', deviceId });
  }

  inviteOpen(msg, ref) {
    const { inviteHash } = msg;
    if (typeof inviteHash !== 'string' || !INVITE_HASH.test(inviteHash)) {
      return this.error('bad_request', ref, 'inviteHash: hex SHA-256 of the invite secret');
    }
    const ttl = Number.isInteger(msg.ttl) ? Math.min(INVITE_TTL_S.max, Math.max(INVITE_TTL_S.min, msg.ttl)) : INVITE_TTL_S.default;
    const existing = this.hub.liveInvite(inviteHash);
    if (existing && existing.deviceId !== this.deviceId) {
      return this.error('invite_exists', ref);
    }
    if (!existing) {
      const mine = [...this.hub.invites.values()].filter(invite => invite.deviceId === this.deviceId && invite.expiresAt > Date.now()).length;
      if (mine >= INVITES_PER_DEVICE || this.hub.invites.size >= INVITES_TOTAL) {
        return this.error('invite_limit', ref);
      }
    }
    const expiresAt = Date.now() + ttl * 1000;
    this.hub.invites.set(inviteHash, { deviceId: this.deviceId, expiresAt });
    this.conn.send({ type: 'invite.opened', ...ref, inviteHash, expiresAt });
  }

  inviteClose(msg, ref) {
    const invite = this.hub.invites.get(msg.inviteHash);
    if (invite?.deviceId === this.deviceId) {
      this.hub.invites.delete(msg.inviteHash);
    }
    this.conn.send({ type: 'invite.closed', ...ref, inviteHash: msg.inviteHash });
  }

  inviteJoin(msg, ref) {
    const { inviteSecret } = msg;
    if (typeof inviteSecret !== 'string' || inviteSecret.length < 16 || inviteSecret.length > 256) {
      return this.error('bad_request', ref, 'inviteSecret: 16 to 256 characters');
    }
    const inviteHash = sha256Hex(inviteSecret);
    const invite = this.hub.liveInvite(inviteHash);
    if (!invite) {
      if (!this.hub.joinFailures.take(this.conn.ip)) {
        this.hub.log(`invite guessing: ${this.conn.ip}`);
        return this.conn.close(CLOSE.rateLimited, 'too many wrong invites');
      }
      return this.error('invite_not_found', ref);
    }
    if (invite.deviceId === this.deviceId) {
      return this.error('bad_request', ref, 'own invite');
    }
    const owner = this.hub.devices.get(invite.deviceId);
    if (!owner) {
      return this.error('offline', ref);
    }
    this.hub.invites.delete(inviteHash);
    owner.conn.send({ type: 'invite.joined', inviteHash, peer: { deviceId: this.deviceId, publicKey: this.publicKey } });
    this.conn.send({ type: 'invite.accepted', ...ref, inviteHash, peer: { deviceId: owner.deviceId, publicKey: owner.publicKey } });
  }

  signal(msg, ref) {
    const { to, payload } = msg;
    if (typeof to !== 'string' || !DEVICE_ID.test(to) || to === this.deviceId || payload === undefined) {
      return this.error('bad_request', ref, 'to: another device ID; payload: any JSON');
    }
    const peer = this.hub.devices.get(to);
    if (!peer) {
      return this.error('offline', ref);
    }
    peer.conn.send({ type: 'signal', from: this.deviceId, payload });
  }
}
