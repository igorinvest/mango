//Token buckets: `burst` tokens, refilled at `perSecond`; take() is false when the bucket is empty.

export class Bucket {
  constructor(burst, perSecond) {
    this.burst = burst;
    this.perSecond = perSecond;
    this.tokens = burst;
    this.at = Date.now();
  }

  refill() {
    const now = Date.now();
    this.tokens = Math.min(this.burst, this.tokens + (now - this.at) / 1000 * this.perSecond);
    this.at = now;
    return this.tokens;
  }

  take() {
    if (this.refill() < 1) {
      return false;
    }
    this.tokens -= 1;
    return true;
  }
}

//One bucket per key (an IP address). sweep() drops the full ones, so the map only holds keys in use.
export class Buckets {
  constructor(burst, perSecond) {
    this.burst = burst;
    this.perSecond = perSecond;
    this.map = new Map();
  }

  take(key) {
    let bucket = this.map.get(key);
    if (!bucket) {
      bucket = new Bucket(this.burst, this.perSecond);
      this.map.set(key, bucket);
    }
    return bucket.take();
  }

  sweep() {
    for (const [key, bucket] of this.map) {
      if (bucket.refill() >= this.burst) {
        this.map.delete(key);
      }
    }
  }
}
