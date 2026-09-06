// Real-time player inventories. Grants and consumes are server-
// authoritative ONLY -- trusted via the exact same "knowledge of the
// server_key returned at registration time IS the credential" model
// sessions/routes.js's own heartbeat route already uses, never callable
// with an end-user access token. The "real-time" half is a publish onto
// the shared realtime bus (see realtime/gateway.js) after every write,
// so a connected client sees its own inventory change without polling.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, conflict, notFound } from '../errors.js';
import { optionalAuth, requireAuth } from '../middleware/auth.js';
import { redis, channels } from '../redis.js';
import { requireOwnedGame } from '../catalog/ownership.js';

export const inventoryRouter = express.Router();

const SKU_RE = /^[A-Za-z0-9][A-Za-z0-9_.-]{0,99}$/;

function validateSku(raw) {
  const sku = String(raw || '').trim();
  if (!SKU_RE.test(sku)) throw badRequest('sku must be 1-100 characters: letters, numbers, "_", "." or "-".');
  return sku;
}

function validateQuantity(raw) {
  const quantity = Number(raw);
  if (!Number.isInteger(quantity) || quantity <= 0 || quantity > 1_000_000_000) {
    throw badRequest('quantity must be a positive integer.');
  }
  return quantity;
}

async function requireRegisteredServer(slug, serverKey) {
  const { rows } = await query(
    `SELECT gs.id, gs.game_id FROM game_servers gs JOIN games g ON g.id = gs.game_id
      WHERE gs.server_key = $1 AND g.slug = $2`,
    [serverKey, slug],
  );
  if (rows.length === 0) throw notFound('Unknown server key for that game.');
  return rows[0];
}

// --- creator-managed item catalog ------------------------------------------

inventoryRouter.post(
  '/games/:slug/catalog',
  requireAuth,
  asyncRoute(async (req, res) => {
    const game = await requireOwnedGame(req.params.slug, req.user.id);
    const sku = validateSku(req.body?.sku);
    const name = String(req.body?.name || '').trim().slice(0, 200);
    if (!name) throw badRequest('name is required.');
    const kind = String(req.body?.kind || 'item').trim().slice(0, 40) || 'item';
    const metadata = req.body?.metadata && typeof req.body.metadata === 'object' ? req.body.metadata : {};

    await query(
      `INSERT INTO item_catalog (game_id, sku, name, kind, metadata) VALUES ($1, $2, $3, $4, $5)
       ON CONFLICT (game_id, sku) DO UPDATE SET name = EXCLUDED.name, kind = EXCLUDED.kind, metadata = EXCLUDED.metadata`,
      [game.id, sku, name, kind, JSON.stringify(metadata)],
    );
    res.json({ status: 'saved', sku });
  }),
);

inventoryRouter.get(
  '/games/:slug/catalog',
  optionalAuth,
  asyncRoute(async (req, res) => {
    const { rows: games } = await query(`SELECT id FROM games WHERE slug = $1`, [req.params.slug]);
    if (games.length === 0) throw notFound('No such game.');
    const { rows } = await query(
      `SELECT sku, name, kind, metadata FROM item_catalog WHERE game_id = $1 ORDER BY sku`, [games[0].id],
    );
    res.json({ items: rows });
  }),
);

// --- server-authoritative grant/consume ------------------------------------

inventoryRouter.post(
  '/games/:slug/servers/:serverKey/grant',
  asyncRoute(async (req, res) => {
    const server = await requireRegisteredServer(req.params.slug, req.params.serverKey);
    const userId = Number(req.body?.user_id);
    if (!Number.isFinite(userId)) throw badRequest('user_id must be a real user id.');
    const sku = validateSku(req.body?.sku);
    const quantity = validateQuantity(req.body?.quantity);

    const { rows: catalog } = await query(`SELECT id FROM item_catalog WHERE game_id = $1 AND sku = $2`, [server.game_id, sku]);
    if (catalog.length === 0) throw badRequest('Unknown sku -- register it in this game\'s item catalog first.');

    const { rows } = await query(
      `INSERT INTO inventory_items (user_id, game_id, sku, quantity) VALUES ($1, $2, $3, $4)
       ON CONFLICT (user_id, game_id, sku) DO UPDATE SET quantity = inventory_items.quantity + EXCLUDED.quantity, updated_at = NOW()
       RETURNING quantity`,
      [userId, server.game_id, sku, quantity],
    );
    const newQuantity = Number(rows[0].quantity);
    await redis.publish(channels.inventory(userId), JSON.stringify({ game_id: server.game_id, sku, quantity: newQuantity }));
    res.json({ status: 'granted', sku, quantity: newQuantity });
  }),
);

inventoryRouter.post(
  '/games/:slug/servers/:serverKey/consume',
  asyncRoute(async (req, res) => {
    const server = await requireRegisteredServer(req.params.slug, req.params.serverKey);
    const userId = Number(req.body?.user_id);
    if (!Number.isFinite(userId)) throw badRequest('user_id must be a real user id.');
    const sku = validateSku(req.body?.sku);
    const quantity = validateQuantity(req.body?.quantity);

    // The WHERE quantity >= $4 clause is what makes this atomic: two
    // concurrent consumes racing against the same low balance can never
    // both succeed and drive quantity negative.
    const { rows } = await query(
      `UPDATE inventory_items SET quantity = quantity - $4, updated_at = NOW()
        WHERE user_id = $1 AND game_id = $2 AND sku = $3 AND quantity >= $4
        RETURNING quantity`,
      [userId, server.game_id, sku, quantity],
    );
    if (rows.length === 0) throw conflict('Insufficient quantity to consume.');

    const newQuantity = Number(rows[0].quantity);
    await redis.publish(channels.inventory(userId), JSON.stringify({ game_id: server.game_id, sku, quantity: newQuantity }));
    res.json({ status: 'consumed', sku, quantity: newQuantity });
  }),
);

// --- player-facing read -----------------------------------------------------

inventoryRouter.get(
  '/me',
  requireAuth,
  asyncRoute(async (req, res) => {
    const gameId = Number(req.query.game_id);
    if (!Number.isFinite(gameId)) throw badRequest('game_id query parameter is required.');
    const { rows } = await query(
      `SELECT sku, quantity, metadata, updated_at FROM inventory_items WHERE user_id = $1 AND game_id = $2 ORDER BY sku`,
      [req.user.id, gameId],
    );
    res.json({ items: rows.map((r) => ({ ...r, quantity: Number(r.quantity) })) });
  }),
);
