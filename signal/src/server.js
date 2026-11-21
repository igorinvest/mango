//Mango signaling server: GET /health and the WebSocket at /v1 (protocol in README.md). Listens on
//127.0.0.1 by default; behind a Cloudflare tunnel or proxy, so the client's address comes from
//CF-Connecting-IP.
//
//  PORT=5010 HOST=127.0.0.1 node src/server.js
import fs from 'node:fs';
import http from 'node:http';
import { WebSocketServer } from 'ws';
import { Hub } from './hub.js';
import { Buckets } from './limits.js';

const PORT = Number(process.env.PORT ?? 5010);
const HOST = process.env.HOST ?? '127.0.0.1';
//A deploy may write VERSION next to package.json
const VERSION = process.env.SIGNAL_VERSION
  ?? readText(new URL('../VERSION', import.meta.url))
  ?? 'dev';

const MAX_MESSAGE_BYTES = 64 * 1024;
const MAX_SOCKETS = 10_000;
const MAX_SOCKETS_PER_IP = 20;
const HEARTBEAT_MS = 30_000; //Cloudflare closes WebSockets idle for 100 s

function readText(url) {
  try {
    return fs.readFileSync(url, 'utf8').trim() || null;
  } catch {
    return null;
  }
}

const log = (...args) => console.log(new Date().toISOString(), ...args);
const hub = new Hub({ log });
//New connections per IP: 30 at once, one a second after that
const connectRate = new Buckets(30, 1);
const socketsPerIp = new Map();

const server = http.createServer((req, res) => {
  const path = new URL(req.url, 'http://localhost').pathname;
  if (req.method === 'GET' && path === '/health') {
    res.writeHead(200, { 'Content-Type': 'application/json', 'Cache-Control': 'no-store' });
    return res.end(JSON.stringify({ ok: true, version: VERSION, devices: hub.devices.size, sockets: wss.clients.size }));
  }
  res.writeHead(404, { 'Content-Type': 'text/plain' }).end('Not found');
});

const wss = new WebSocketServer({ noServer: true, maxPayload: MAX_MESSAGE_BYTES, perMessageDeflate: false });

function reject(socket, status, text) {
  socket.end(`HTTP/1.1 ${status} ${text}\r\nConnection: close\r\nContent-Length: 0\r\n\r\n`);
}

server.on('upgrade', (req, socket, head) => {
  socket.on('error', () => {});
  if (new URL(req.url, 'http://localhost').pathname !== '/v1') {
    return reject(socket, 404, 'Not Found');
  }
  const ip = String(req.headers['cf-connecting-ip'] ?? req.socket.remoteAddress);
  if (wss.clients.size >= MAX_SOCKETS) {
    return reject(socket, 503, 'Service Unavailable');
  }
  if ((socketsPerIp.get(ip) ?? 0) >= MAX_SOCKETS_PER_IP || !connectRate.take(ip)) {
    return reject(socket, 429, 'Too Many Requests');
  }
  wss.handleUpgrade(req, socket, head, ws => attach(ws, ip));
});

function attach(ws, ip) {
  socketsPerIp.set(ip, (socketsPerIp.get(ip) ?? 0) + 1);
  ws.alive = true;
  ws.on('pong', () => { ws.alive = true; });
  const session = hub.open({
    ip,
    send: message => ws.readyState === ws.OPEN && ws.send(JSON.stringify(message)),
    close: (code, reason) => ws.close(code, reason),
  });
  ws.on('message', (data, isBinary) => session.receive(isBinary ? null : data.toString()));
  ws.on('error', () => {});
  ws.on('close', () => {
    session.closed();
    const left = socketsPerIp.get(ip) - 1;
    left > 0 ? socketsPerIp.set(ip, left) : socketsPerIp.delete(ip);
  });
}

const heartbeat = setInterval(() => {
  for (const ws of wss.clients) {
    if (!ws.alive) {
      ws.terminate();
      continue;
    }
    ws.alive = false;
    ws.ping();
  }
}, HEARTBEAT_MS);

const sweep = setInterval(() => {
  hub.sweep();
  connectRate.sweep();
}, 60_000);

server.listen(PORT, HOST, () => log(`signal ${VERSION} listening on ${HOST}:${PORT}`));

function shutdown(signal) {
  log(`${signal}: closing`);
  clearInterval(heartbeat);
  clearInterval(sweep);
  for (const ws of wss.clients) {
    ws.close(1001, 'restarting');
  }
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 3_000).unref();
}
process.on('SIGTERM', () => shutdown('SIGTERM'));
process.on('SIGINT', () => shutdown('SIGINT'));
