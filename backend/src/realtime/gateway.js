// The realtime gateway backing every "real-time" ask in v0.4.0's unified
// backend services (inventory changes, leaderboard updates, matchmaking
// ticket resolution) -- one authenticated WebSocket endpoint and one
// subscribe/publish bus, instead of three bespoke push mechanisms.
//
// Auth happens at the HTTP upgrade itself (a WS handshake has no custom-
// header story browsers can rely on, so the access token travels as a
// query parameter instead), verified with the exact same
// verifyAccessToken() requireAuth already uses. After that, a socket may
// subscribe to named channels; channelAllowed() below is what stops a
// connected client from listening to someone else's inventory.
//
// Publishing is NOT this file's job -- inventory/routes.js and
// leaderboards/routes.js just call redis.publish(channel, ...) directly
// after a real write; this file only relays.
import { WebSocketServer } from 'ws';

import { redis, redisSubscriber, keys } from '../redis.js';
import { verifyAccessToken } from '../auth/tokens.js';

const MAX_SUBSCRIPTIONS_PER_SOCKET = 20;

async function channelAllowed(userId, channel) {
  if (channel.startsWith('inventory:')) return channel === `inventory:${userId}`;
  if (channel.startsWith('leaderboard:')) return true; // public read, same as the REST GET.
  if (channel.startsWith('matchmaking:')) {
    const ticketId = channel.slice('matchmaking:'.length);
    const raw = await redis.get(keys.matchmakingTicket(ticketId));
    if (!raw) return false;
    try {
      return String(JSON.parse(raw).user_id) === String(userId);
    } catch {
      return false;
    }
  }
  return false;
}

export function attachWebSocketGateway(httpServer) {
  const wss = new WebSocketServer({ noServer: true });
  const socketsByChannel = new Map(); // channel -> Set<ws>

  function addSocketToChannel(channel, ws) {
    let sockets = socketsByChannel.get(channel);
    if (!sockets) {
      sockets = new Set();
      socketsByChannel.set(channel, sockets);
      // Only ever subscribe Redis to a channel the FIRST time any socket
      // anywhere wants it -- one real Redis subscription serves every
      // connected client listening to the same channel.
      redisSubscriber.subscribe(channel).catch((err) => console.error('[realtime] subscribe %s failed: %s', channel, err.message));
    }
    sockets.add(ws);
  }

  function removeSocketFromChannel(channel, ws) {
    const sockets = socketsByChannel.get(channel);
    if (!sockets) return;
    sockets.delete(ws);
    if (sockets.size === 0) {
      socketsByChannel.delete(channel);
      redisSubscriber.unsubscribe(channel).catch(() => {});
    }
  }

  redisSubscriber.on('message', (channel, message) => {
    const sockets = socketsByChannel.get(channel);
    if (!sockets) return;
    for (const ws of sockets) {
      if (ws.readyState === ws.OPEN) ws.send(JSON.stringify({ channel, data: JSON.parse(message) }));
    }
  });

  wss.on('connection', (ws) => {
    ws.subscribedChannels = new Set();

    ws.on('message', async (raw) => {
      let msg;
      try {
        msg = JSON.parse(raw.toString());
      } catch {
        return;
      }
      if (typeof msg.subscribe === 'string') {
        const channel = msg.subscribe;
        if (ws.subscribedChannels.has(channel)) return;
        if (ws.subscribedChannels.size >= MAX_SUBSCRIPTIONS_PER_SOCKET) {
          ws.send(JSON.stringify({ error: 'too many subscriptions on this connection' }));
          return;
        }
        if (!(await channelAllowed(ws.userId, channel))) {
          ws.send(JSON.stringify({ error: `not permitted to subscribe to ${channel}` }));
          return;
        }
        ws.subscribedChannels.add(channel);
        addSocketToChannel(channel, ws);
        ws.send(JSON.stringify({ subscribed: channel }));
      } else if (typeof msg.unsubscribe === 'string') {
        ws.subscribedChannels.delete(msg.unsubscribe);
        removeSocketFromChannel(msg.unsubscribe, ws);
      }
    });

    ws.on('close', () => {
      for (const channel of ws.subscribedChannels) removeSocketFromChannel(channel, ws);
    });
  });

  httpServer.on('upgrade', async (req, socket, head) => {
    const url = new URL(req.url, 'http://localhost');
    // Not this gateway's path -- leave the socket alone rather than
    // destroying it, in case something else on this server ever wants
    // to claim its own upgrade path.
    if (url.pathname !== '/v1/ws') return;

    let userId;
    try {
      const payload = await verifyAccessToken(url.searchParams.get('token') || '');
      userId = payload.sub;
    } catch {
      socket.write('HTTP/1.1 401 Unauthorized\r\n\r\n');
      socket.destroy();
      return;
    }

    wss.handleUpgrade(req, socket, head, (ws) => {
      ws.userId = userId;
      wss.emit('connection', ws, req);
    });
  });

  return wss;
}
