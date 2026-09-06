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

// A second, dedicated connection for Redis's own pub/sub protocol: once
// a connection issues SUBSCRIBE it can no longer run ordinary commands,
// so the realtime gateway (src/realtime/gateway.js) needs a connection
// of its own rather than sharing `redis` above, which every request-path
// route (heartbeats, rate limits, player counts) still needs to keep
// using normally.
export const redisSubscriber = new Redis(config.redisUrl, { maxRetriesPerRequest: 2, lazyConnect: false });
redisSubscriber.on('error', (err) => {
  console.error('[redis:sub] %s', err.message);
});

export const keys = {
  serverHeartbeat: (serverKey) => `srv:hb:${serverKey}`,
  serverPlayers: (serverKey) => `srv:players:${serverKey}`,
  gamePlayers: (gameId) => `game:players:${gameId}`,
  rateLimit: (bucket, id) => `rl:${bucket}:${id}`,
  // Matchmaking: one FIFO ticket queue per game+region, plus the ticket's
  // own state (queued/matched/cancelled) keyed by ticket id so GET
  // /tickets/:id can report status without re-running the whole queue.
  matchmakingQueue: (gameId, region) => `mm:queue:${gameId}:${region}`,
  matchmakingTicket: (ticketId) => `mm:ticket:${ticketId}`,
};

// Realtime pub/sub channel names, shared between the routes that publish
// (inventory grants/consumes, leaderboard score submissions, matchmaking
// ticket resolution) and src/realtime/gateway.js, which is the only
// thing that ever subscribes.
export const channels = {
  inventory: (userId) => `inventory:${userId}`,
  leaderboard: (gameId, boardKey) => `leaderboard:${gameId}:${boardKey}`,
  matchmakingTicket: (ticketId) => `matchmaking:${ticketId}`,
};
