import Redis from 'ioredis';
import { config } from './config.js';

export const redis = new Redis(config.redisUrl, { maxRetriesPerRequest: 2, lazyConnect: false });

redis.on('error', (err) => {
  // Redis here holds only volatile, reconstructible state (heartbeats,
  // player counts, rate-limit counters). Losing it degrades the service
  // -- allocation stops finding servers -- but must never take the
  // process down, so this is logged rather than thrown.
  console.error('[redis] %s', err.message);
});

export const keys = {
  serverHeartbeat: (serverKey) => `srv:hb:${serverKey}`,
  serverPlayers: (serverKey) => `srv:players:${serverKey}`,
  gamePlayers: (gameId) => `game:players:${gameId}`,
  rateLimit: (bucket, id) => `rl:${bucket}:${id}`,
};
