// Backend persistence for a player's own avatar appearance. The real
// data model (skin tone, head shape, body sliders, clothing fit, and
// per-category equipped catalogue item ids) already exists client-side
// (engine/src/core/AvatarLoadout.hpp, LocalProfile.hpp) -- this is just
// where it lives beyond one machine's local disk. See
// 005_avatar.sql's own header comment for the full design reasoning.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, notFound } from '../errors.js';
import { requireAuth } from '../middleware/auth.js';

export const avatarRouter = express.Router();

// Matches core::AvatarItemCategory / avatarItemCategoryName() exactly
// (engine/src/core/AvatarItem.hpp) -- equipped_items keys are validated
// against this fixed set, not accepted as arbitrary strings.
const ALLOWED_CATEGORIES = new Set([
  'Head', 'Hair', 'Face', 'Torso', 'Legs', 'Accessory',
  'LayeredClothing', 'Emote', 'Shoes', 'Back', 'Bundle',
]);

// The real, honest default every fresh avatar already renders with
// client-side (core::AvatarHair's own "bacon-hair"-inspired default
// hair + RiggedAvatar's baked-in shirt/trousers) -- returned here
// whenever a user has no saved row yet, so "no config saved" and "the
// real starter look" are the same answer, not a null the client has to
// special-case.
const DEFAULT_CONFIG = {
  skin_tone_index: -1,
  head_shape_index: 0,
  body_height: 1.0,
  body_width: 1.0,
  body_limb_scale: 1.0,
  body_torso_length: 1.0,
  body_shoulder_width: 1.0,
  clothing_fit_index: 0,
  equipped_items: {},
};

function clamp(value, min, max, fallback) {
  const n = Number(value);
  if (!Number.isFinite(n)) return fallback;
  return Math.min(Math.max(n, min), max);
}

function clampIndex(value, min, max, fallback) {
  const n = Math.round(Number(value));
  if (!Number.isFinite(n)) return fallback;
  return Math.min(Math.max(n, min), max);
}

// Real, honest validation of equipped_items: a plain object, keys drawn
// from the fixed category set, values non-empty item-id strings within
// a sane length. Item ids are NOT checked against a catalogue -- there
// is no backend-authoritative catalogue yet (each client still keeps
// its own local catalogue.json), which is real, deliberate future
// marketplace-backend scope, not an oversight here.
function validateEquippedItems(input) {
  if (input === undefined || input === null) return {};
  if (typeof input !== 'object' || Array.isArray(input)) {
    throw badRequest('equipped_items must be an object.');
  }
  const out = {};
  for (const [category, itemId] of Object.entries(input)) {
    if (!ALLOWED_CATEGORIES.has(category)) {
      throw badRequest(`"${category}" is not a real avatar item category.`);
    }
    if (typeof itemId !== 'string' || itemId.length === 0 || itemId.length > 128) {
      throw badRequest(`equipped_items.${category} must be a non-empty item id.`);
    }
    out[category] = itemId;
  }
  return out;
}

function rowToConfig(row) {
  if (!row) return { ...DEFAULT_CONFIG };
  return {
    skin_tone_index: row.skin_tone_index,
    head_shape_index: row.head_shape_index,
    body_height: row.body_height,
    body_width: row.body_width,
    body_limb_scale: row.body_limb_scale,
    body_torso_length: row.body_torso_length,
    body_shoulder_width: row.body_shoulder_width,
    clothing_fit_index: row.clothing_fit_index,
    equipped_items: row.equipped_items,
  };
}

avatarRouter.get(
  '/me',
  requireAuth,
  asyncRoute(async (req, res) => {
    const { rows } = await query(`SELECT * FROM avatar_configs WHERE user_id = $1`, [req.user.id]);
    res.json(rowToConfig(rows[0]));
  }),
);

avatarRouter.get(
  '/:userId',
  requireAuth,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');
    const target = await query(`SELECT id, account_state FROM users WHERE id = $1`, [userId]);
    if (target.rows.length === 0 || target.rows[0].account_state === 'terminated') {
      throw notFound('No such user.');
    }
    const { rows } = await query(`SELECT * FROM avatar_configs WHERE user_id = $1`, [userId]);
    res.json(rowToConfig(rows[0]));
  }),
);

avatarRouter.put(
  '/me',
  requireAuth,
  asyncRoute(async (req, res) => {
    const body = req.body || {};
    const config = {
      skin_tone_index: clampIndex(body.skin_tone_index, -1, 63, DEFAULT_CONFIG.skin_tone_index),
      head_shape_index: clampIndex(body.head_shape_index, 0, 1, DEFAULT_CONFIG.head_shape_index),
      // Real range core::RiggedAvatar.hpp already clamps to -- re-clamped
      // here too; the backend never trusts the client's own clamp.
      body_height: clamp(body.body_height, 0.85, 1.15, DEFAULT_CONFIG.body_height),
      body_width: clamp(body.body_width, 0.85, 1.15, DEFAULT_CONFIG.body_width),
      body_limb_scale: clamp(body.body_limb_scale, 0.85, 1.15, DEFAULT_CONFIG.body_limb_scale),
      body_torso_length: clamp(body.body_torso_length, 0.85, 1.15, DEFAULT_CONFIG.body_torso_length),
      body_shoulder_width: clamp(body.body_shoulder_width, 0.85, 1.15, DEFAULT_CONFIG.body_shoulder_width),
      clothing_fit_index: clampIndex(body.clothing_fit_index, 0, 1, DEFAULT_CONFIG.clothing_fit_index),
      equipped_items: validateEquippedItems(body.equipped_items),
    };

    await query(
      `INSERT INTO avatar_configs (
         user_id, skin_tone_index, head_shape_index,
         body_height, body_width, body_limb_scale, body_torso_length, body_shoulder_width,
         clothing_fit_index, equipped_items, updated_at
       ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10::jsonb, NOW())
       ON CONFLICT (user_id) DO UPDATE SET
         skin_tone_index = EXCLUDED.skin_tone_index,
         head_shape_index = EXCLUDED.head_shape_index,
         body_height = EXCLUDED.body_height,
         body_width = EXCLUDED.body_width,
         body_limb_scale = EXCLUDED.body_limb_scale,
         body_torso_length = EXCLUDED.body_torso_length,
         body_shoulder_width = EXCLUDED.body_shoulder_width,
         clothing_fit_index = EXCLUDED.clothing_fit_index,
         equipped_items = EXCLUDED.equipped_items,
         updated_at = NOW()`,
      [
        req.user.id, config.skin_tone_index, config.head_shape_index,
        config.body_height, config.body_width, config.body_limb_scale,
        config.body_torso_length, config.body_shoulder_width,
        config.clothing_fit_index, JSON.stringify(config.equipped_items),
      ],
    );

    res.json(config);
  }),
);
