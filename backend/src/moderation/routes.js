// The backend half of moderation that was missing: a centralized place
// for a report to land (every existing report/review-queue/escalation
// log lives only on the single game-server process that generated it --
// see engine/src/moderation/*), and a real HTTP route surface for the
// account termination/appeal logic in bans.js, which existed and was
// tested but, before this file, was only ever reachable from tests.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, forbidden, notFound } from '../errors.js';
import { requireAdmin, requireAuth } from '../middleware/auth.js';
import { rateLimit } from '../middleware/rateLimit.js';
import { terminateAccount, grantAppeal, denyAppeal } from './bans.js';

export const moderationRouter = express.Router();

// Matches engine::safety::TextClassifierStub's own real category set
// (Harassment/SexualContent/PiiSolicitation/OffPlatformRedirect/
// Grooming/Hate/SelfHarm/Threats/Spam) plus a catch-all -- one shared
// vocabulary between the engine's own classifier and a human-filed
// report, not a second taxonomy invented here.
const REPORT_CATEGORIES = new Set([
  'harassment', 'sexual_content', 'pii_solicitation', 'off_platform_redirect',
  'grooming', 'hate', 'self_harm', 'threats', 'spam', 'other',
]);

// --- player-facing: file a report --------------------------------------

// Deliberately NOT guest-barred, unlike /friends/*: the person
// experiencing harassment is exactly who guest mode's own social-graph
// restriction was never meant to silence. Keyed by user id (not IP) so
// a single abusive account behind a shared/rotating IP can't hide
// inside the limit, and legitimate players behind the same NAT/VPN
// aren't wrongly throttled together.
moderationRouter.post(
  '/moderation/reports',
  requireAuth,
  rateLimit({ bucket: 'report', limit: 30, windowSeconds: 3600, keyFn: (req) => req.user.id }),
  asyncRoute(async (req, res) => {
    const reportedUserId = String(req.body?.reported_user_id || '');
    if (!/^\d+$/.test(reportedUserId)) throw badRequest('reported_user_id is required.');
    if (reportedUserId === String(req.user.id)) throw badRequest('You cannot report yourself.');

    const category = String(req.body?.category || '');
    if (!REPORT_CATEGORIES.has(category)) throw badRequest('Not a real report category.');

    const target = await query(`SELECT id FROM users WHERE id = $1`, [reportedUserId]);
    if (target.rows.length === 0) throw notFound('No such user.');

    const gameId = /^\d+$/.test(String(req.body?.game_id || '')) ? String(req.body.game_id) : null;
    const serverKey = req.body?.server_key ? String(req.body.server_key).slice(0, 128) : null;
    const detail = String(req.body?.detail || '').slice(0, 2000);

    const { rows } = await query(
      `INSERT INTO content_reports (reporter_id, reported_user_id, game_id, server_key, category, detail)
       VALUES ($1, $2, $3, $4, $5, $6) RETURNING id, created_at`,
      [req.user.id, reportedUserId, gameId, serverKey, category, detail],
    );
    res.status(201).json({ id: String(rows[0].id), status: 'open', created_at: rows[0].created_at });
  }),
);

// --- ops: review the report queue ---------------------------------------

moderationRouter.get(
  '/moderation/reports',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const status = ['open', 'resolved', 'dismissed'].includes(req.query.status) ? req.query.status : 'open';
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const cursor = Number(req.query.cursor) || 0;

    const params = [status, limit + 1];
    let where = 'r.status = $1';
    if (cursor > 0) {
      params.push(cursor);
      where += ` AND r.id < $${params.length}`;
    }

    const { rows } = await query(
      `SELECT r.id, r.reporter_id, r.reported_user_id, r.game_id, r.server_key, r.category, r.detail,
              r.status, r.resolution_note, r.created_at, r.resolved_at,
              reporter.display_name AS reporter_name, reported.display_name AS reported_name
         FROM content_reports r
         JOIN users reporter ON reporter.id = r.reporter_id
         JOIN users reported ON reported.id = r.reported_user_id
        WHERE ${where}
        ORDER BY r.id DESC
        LIMIT $2`,
      params,
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;

    res.json({
      reports: page.map((r) => ({
        id: String(r.id),
        reporter: { id: String(r.reporter_id), display_name: r.reporter_name },
        reported_user: { id: String(r.reported_user_id), display_name: r.reported_name },
        game_id: r.game_id ? String(r.game_id) : null,
        server_key: r.server_key,
        category: r.category,
        detail: r.detail,
        status: r.status,
        resolution_note: r.resolution_note,
        created_at: r.created_at,
        resolved_at: r.resolved_at,
      })),
      next_cursor: hasMore ? String(page[page.length - 1].id) : null,
    });
  }),
);

moderationRouter.post(
  '/moderation/reports/:id/resolve',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const reportId = String(req.params.id || '');
    if (!/^\d+$/.test(reportId)) throw badRequest('A numeric report id is required.');
    const dismiss = req.body?.dismiss === true;
    const note = String(req.body?.note || '').slice(0, 2000);
    const newStatus = dismiss ? 'dismissed' : 'resolved';

    const { rows } = await query(
      `UPDATE content_reports
          SET status = $2, resolution_note = $3, resolved_by = $4, resolved_at = NOW()
        WHERE id = $1 AND status = 'open'
        RETURNING reported_user_id`,
      [reportId, newStatus, note, req.user.id],
    );
    if (rows.length === 0) throw notFound('No open report with that id.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason, report_id)
       VALUES ($1, $2, $3, $4, $5)`,
      [req.user.id, rows[0].reported_user_id, dismiss ? 'dismiss_report' : 'resolve_report', note, reportId],
    );
    res.json({ status: newStatus });
  }),
);

// --- ops: account termination / appeals ----------------------------------

moderationRouter.post(
  '/admin/users/:userId/terminate',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');

    const target = await query(`SELECT id, role FROM users WHERE id = $1`, [userId]);
    if (target.rows.length === 0) throw notFound('No such user.');
    // An admin account must be demoted before it can be terminated --
    // a stray click must not be able to lock out every admin at once.
    if (target.rows[0].role === 'admin') throw forbidden('Demote this admin before terminating the account.');

    const reason = String(req.body?.reason || '').slice(0, 2000);
    const reportId = /^\d+$/.test(String(req.body?.report_id || '')) ? String(req.body.report_id) : null;

    const result = await terminateAccount(userId, {
      reason,
      hwid: req.body?.hwid || null,
      ip: req.body?.ip || null,
      expiresAt: req.body?.expires_at || null,
    });
    if (!result) throw notFound('No such user.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason, report_id)
       VALUES ($1, $2, 'terminate', $3, $4)`,
      [req.user.id, userId, reason, reportId],
    );
    res.json({ status: 'terminated', locked_username: result.lockedUsername });
  }),
);

moderationRouter.post(
  '/admin/appeals/:appealId/grant',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const appealId = String(req.params.appealId || '');
    if (!/^\d+$/.test(appealId)) throw badRequest('A numeric appeal id is required.');

    const result = await grantAppeal(appealId);
    if (!result) throw notFound('No open appeal with that id.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason)
       VALUES ($1, $2, 'grant_appeal', $3)`,
      [req.user.id, result.userId, String(req.body?.reason || '').slice(0, 2000)],
    );
    res.json({ status: 'granted', account_state: result.state });
  }),
);

moderationRouter.post(
  '/admin/appeals/:appealId/deny',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const appealId = String(req.params.appealId || '');
    if (!/^\d+$/.test(appealId)) throw badRequest('A numeric appeal id is required.');
    const resolution = String(req.body?.reason || '').slice(0, 2000);

    const result = await denyAppeal(appealId, resolution);
    if (!result) throw notFound('No open appeal with that id.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason)
       VALUES ($1, $2, 'deny_appeal', $3)`,
      [req.user.id, result.userId, resolution],
    );
    res.json({ status: 'denied' });
  }),
);
