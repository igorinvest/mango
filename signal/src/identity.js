//Device identity: an Ed25519 key pair made once per Mango install. The device ID is the lowercase hex
//SHA-256 of the raw 32-byte public key. A device proves it holds the key by signing AUTH_CONTEXT + the
//server's nonce.
import crypto from 'node:crypto';

export const AUTH_CONTEXT = 'mango-signal-v1 auth:';

const BASE64URL = /^[A-Za-z0-9_-]+$/;
export const DEVICE_ID = /^[0-9a-f]{64}$/;

export const sha256Hex = data => crypto.createHash('sha256').update(data).digest('hex');

export const newNonce = () => crypto.randomBytes(32).toString('base64url');

//The device ID when `signature` is the key's signature of the nonce, otherwise null
export function verifyAuth(publicKey, signature, nonce) {
  if (typeof publicKey !== 'string' || typeof signature !== 'string' || !BASE64URL.test(publicKey) || !BASE64URL.test(signature)) {
    return null;
  }
  const raw = Buffer.from(publicKey, 'base64url');
  const sig = Buffer.from(signature, 'base64url');
  if (raw.length !== 32 || sig.length !== 64) {
    return null;
  }
  let key;
  try {
    key = crypto.createPublicKey({ key: { kty: 'OKP', crv: 'Ed25519', x: raw.toString('base64url') }, format: 'jwk' });
  } catch {
    return null;
  }
  return crypto.verify(null, Buffer.from(AUTH_CONTEXT + nonce), key, sig) ? sha256Hex(raw) : null;
}
