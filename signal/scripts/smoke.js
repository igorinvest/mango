//End-to-end check of the signaling server, playing two Mango devices (A and B): auth, pairing through an
//invite, forwarding, and the refusals. Black box over WebSocket and /health.
//
//  npm run smoke                                  starts a local server on port 5011 and checks it
//  npm run smoke -- wss://signal.example.com/v1   checks a running server
import crypto from 'node:crypto';
import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const AUTH_CONTEXT = 'mango-signal-v1 auth:';
const TIMEOUT_MS = 5_000;

let url = process.argv[2];
let child;
if (!url) {
  const port = 5011;
  url = `ws://127.0.0.1:${port}/v1`;
  child = spawn(process.execPath, [fileURLToPath(new URL('../src/server.js', import.meta.url))], {
    env: { ...process.env, PORT: String(port), HOST: '127.0.0.1', SIGNAL_VERSION: 'smoke' },
    stdio: ['ignore', 'ignore', 'inherit'],
  });
}
const healthUrl = url.replace(/^ws/, 'http').replace(/\/v1$/, '/health');

function newKey() {
  const { publicKey, privateKey } = crypto.generateKeyPairSync('ed25519');
  const raw = Buffer.from(publicKey.export({ format: 'jwk' }).x, 'base64url');
  return { privateKey, publicKey: raw.toString('base64url'), deviceId: crypto.createHash('sha256').update(raw).digest('hex') };
}

//A WebSocket client that queues incoming messages, so a test can wait for the next one of a type
class Client {
  static async connect(key) {
    const client = new Client();
    await client.opened;
    const { nonce } = await client.next('challenge');
    if (key) {
      client.send({ type: 'auth', publicKey: key.publicKey, signature: crypto.sign(null, Buffer.from(AUTH_CONTEXT + nonce), key.privateKey).toString('base64url') });
      await client.next('ready');
    }
    return client;
  }

  constructor() {
    this.queue = [];
    this.waiters = [];
    this.ws = new WebSocket(url);
    this.opened = new Promise((resolve, reject) => {
      this.ws.onopen = resolve;
      this.ws.onerror = () => reject(new Error(`can't connect to ${url}`));
    });
    this.closedWith = new Promise(resolve => { this.ws.onclose = event => resolve(event.code); });
    this.ws.onmessage = event => {
      this.queue.push(JSON.parse(event.data));
      this.flush();
    };
  }

  flush() {
    for (const waiter of [...this.waiters]) {
      const index = this.queue.findIndex(msg => msg.type === waiter.type);
      if (index >= 0) {
        this.waiters.splice(this.waiters.indexOf(waiter), 1);
        clearTimeout(waiter.timer);
        waiter.resolve(this.queue.splice(index, 1)[0]);
      }
    }
  }

  next(type) {
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`no ${type} message within ${TIMEOUT_MS} ms`)), TIMEOUT_MS);
      this.waiters.push({ type, resolve, timer });
      this.flush();
    });
  }

  send(msg) {
    this.ws.send(JSON.stringify(msg));
  }

  closed() {
    return Promise.race([this.closedWith, new Promise((_, reject) => setTimeout(() => reject(new Error('socket not closed')), TIMEOUT_MS))]);
  }

  close() {
    this.ws.close();
  }
}

function expect(condition, what) {
  if (!condition) {
    throw new Error(what);
  }
}

async function waitForHealth() {
  for (let i = 0; i < 50; i++) {
    try {
      const res = await fetch(healthUrl);
      if (res.ok) {
        return res.json();
      }
    } catch {
      //not up yet
    }
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error(`${healthUrl} doesn't answer`);
}

const keyA = newKey();
const keyB = newKey();
const secret = crypto.randomBytes(16).toString('base64url');
const inviteHash = crypto.createHash('sha256').update(secret).digest('hex');
let a, b;

const steps = [
  ['health answers', async () => {
    const health = await waitForHealth();
    expect(health.ok === true && typeof health.version === 'string', `unexpected health ${JSON.stringify(health)}`);
  }],
  ['A and B authenticate', async () => {
    a = await Client.connect(keyA);
    b = await Client.connect(keyB);
  }],
  ['B pairs with A through an invite', async () => {
    a.send({ type: 'invite.open', requestId: 1, inviteHash, ttl: 60 });
    expect((await a.next('invite.opened')).inviteHash === inviteHash, 'invite.opened for another hash');
    b.send({ type: 'invite.join', requestId: 2, inviteSecret: secret });
    const accepted = await b.next('invite.accepted');
    const joined = await a.next('invite.joined');
    expect(accepted.peer.deviceId === keyA.deviceId && accepted.peer.publicKey === keyA.publicKey, 'B got the wrong peer');
    expect(joined.peer.deviceId === keyB.deviceId && joined.peer.publicKey === keyB.publicKey, 'A got the wrong peer');
  }],
  ['an invite works once', async () => {
    b.send({ type: 'invite.join', requestId: 3, inviteSecret: secret });
    expect((await b.next('error')).code === 'invite_not_found', 'second join was not refused');
  }],
  ['signals are forwarded both ways', async () => {
    b.send({ type: 'signal', to: keyA.deviceId, payload: { kind: 'offer', sdp: 'v=0 offer' } });
    const offer = await a.next('signal');
    expect(offer.from === keyB.deviceId && offer.payload.sdp === 'v=0 offer', 'offer changed in transit');
    a.send({ type: 'signal', to: keyB.deviceId, payload: { kind: 'answer', sdp: 'v=0 answer' } });
    expect((await b.next('signal')).payload.kind === 'answer', 'answer changed in transit');
  }],
  ['a signal to an offline device is refused', async () => {
    b.send({ type: 'signal', requestId: 4, to: newKey().deviceId, payload: {} });
    const error = await b.next('error');
    expect(error.code === 'offline' && error.requestId === 4, `expected offline, got ${JSON.stringify(error)}`);
  }],
  ['ping answers', async () => {
    a.send({ type: 'ping', requestId: 5 });
    expect((await a.next('pong')).requestId === 5, 'pong without the requestId');
  }],
  ['a client before requestId gets its "id" back', async () => {
    a.send({ type: 'ping', id: 6 });
    const pong = await a.next('pong');
    expect(pong.id === 6 && pong.requestId === undefined, `expected the id back, got ${JSON.stringify(pong)}`);
  }],
  ['a wrong signature is closed', async () => {
    const intruder = await Client.connect();
    intruder.send({ type: 'auth', publicKey: keyA.publicKey, signature: Buffer.alloc(64).toString('base64url') });
    expect(await intruder.closed() === 4000, 'expected close 4000');
  }],
  ['a message before auth is closed', async () => {
    const early = await Client.connect();
    early.send({ type: 'signal', to: keyA.deviceId, payload: {} });
    expect(await early.closed() === 4000, 'expected close 4000');
  }],
  ['the same device connecting again replaces the old socket', async () => {
    const again = await Client.connect(keyA);
    expect(await a.closed() === 4001, 'old socket not closed with 4001');
    b.send({ type: 'signal', to: keyA.deviceId, payload: 'hi' });
    expect((await again.next('signal')).payload === 'hi', 'signal not delivered to the new socket');
    a = again;
  }],
];

let failed = 0;
try {
  for (const [name, run] of steps) {
    try {
      await run();
      console.log(`  ✔ ${name}`);
    } catch (error) {
      failed++;
      console.log(`  ✘ ${name}: ${error.message}`);
      break;
    }
  }
} finally {
  a?.close();
  b?.close();
  child?.kill('SIGTERM');
}
console.log(failed ? `Failed against ${url}` : `All ${steps.length} passed against ${url}`);
process.exit(failed ? 1 : 0);
