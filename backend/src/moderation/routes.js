// The backend half of moderation that was missing: a centralized place
// for a report to land (every existing report/review-queue/escalation
// log lives only on the single game-server process that generated it --
// see engine/src/moderation/*), and a real HTTP route surface for the
// account termination/appeal logic in bans.js, which existed and was
// tested but, before this file, was only ever reachable from tests.
import express from 'express';

import { query } from '../db.js';
import { asyncRoute, badRequest, forbidden, notFound, unauthorized } from '../errors.js';
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

// REPORT_CATEGORIES plus the two Gemini Vision-specific findings
// (010_moderation_queue.sql's own comment) that a human report has no
// reason to file under.
const QUEUE_CATEGORIES = new Set([...REPORT_CATEGORIES, 'nsfw', 'ip_violation']);
const QUEUE_CONTENT_TYPES = new Set(['chat', 'voice_transcript', 'ugc_asset', 'movie_export']);

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

// --- automated queue: classifier verdicts on content nobody reported ----
//
// Distinct from /moderation/reports above: a report is a human filing a
// complaint, this is a classifier (Gemini text/vision today, a future
// behavioral-signal job later) flagging something on its own. See
// 010_moderation_queue.sql's own comment for why that earns a second
// table instead of overloading content_reports with a null reporter_id.

// Authenticates the caller two different ways depending on what kind of
// content it's reporting on: a live game server proving itself with the
// same server_key secret sessions/routes.js's own heartbeat route
// already trusts (the real, exercised path today -- a server-side chat/
// voice classifier), or a signed-in admin (for a content type, like
// ugc_asset or movie_export, that has no game-server process to hold a
// server_key at all -- e.g. a moderator manually filing a verdict, or a
// future upload-time job with no per-server identity of its own).
// Deliberately NOT trusting an ordinary end user's own token to self-file
// a verdict about their own content -- that is an obvious "always send
// is_safe: true" bypass waiting to happen.
async function resolveQueueCaller(req) {
  const serverKey = req.body?.server_key ? String(req.body.server_key) : null;
  if (serverKey) {
    const { rows } = await query(`SELECT game_id FROM game_servers WHERE server_key = $1 AND enabled = TRUE`, [serverKey]);
    if (rows.length === 0) throw unauthorized('Unknown or disabled server key.');
    // The server's OWN game_id, looked up server-side -- never the
    // client-supplied body.game_id, so a compromised server for game A
    // cannot attribute a flagged item to game B.
    return { gameId: String(rows[0].game_id) };
  }
  await new Promise((resolve, reject) => requireAuth(req, {}, (err) => (err ? reject(err) : resolve())));
  await new Promise((resolve, reject) => requireAdmin(req, {}, (err) => (err ? reject(err) : resolve())));
  const gameId = /^\d+$/.test(String(req.body?.game_id || '')) ? String(req.body.game_id) : null;
  return { gameId };
}

moderationRouter.post(
  '/moderation/queue',
  // Keyed on req.ip only, deliberately not on the caller-supplied
  // server_key: keying on an unvalidated body field would let a caller
  // pick its own bucket by rotating that value, sidestepping the limit
  // entirely (this middleware runs before resolveQueueCaller has had a
  // chance to reject an invalid key).
  rateLimit({ bucket: 'moderationqueue', limit: 1200, windowSeconds: 3600 }),
  asyncRoute(async (req, res) => {
    const { gameId } = await resolveQueueCaller(req);

    const contentType = String(req.body?.content_type || '');
    if (!QUEUE_CONTENT_TYPES.has(contentType)) throw badRequest('Not a real content_type.');

    const flaggedUserId = String(req.body?.flagged_user_id || '');
    if (!/^\d+$/.test(flaggedUserId)) throw badRequest('flagged_user_id is required.');
    const target = await query(`SELECT id FROM users WHERE id = $1`, [flaggedUserId]);
    if (target.rows.length === 0) throw notFound('No such user.');

    const category = String(req.body?.category || '');
    if (!QUEUE_CATEGORIES.has(category)) throw badRequest('Not a real category.');

    if (typeof req.body?.is_safe !== 'boolean') throw badRequest('is_safe (boolean) is required.');

    const { rows } = await query(
      `INSERT INTO moderation_queue
         (content_type, content_ref, flagged_user_id, game_id, server_key, category, is_safe, used_fallback, detail, classifier)
       VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)
       RETURNING id, created_at`,
      [
        contentType,
        String(req.body?.content_ref || '').slice(0, 500),
        flaggedUserId,
        gameId,
        req.body?.server_key ? String(req.body.server_key).slice(0, 128) : null,
        category,
        req.body.is_safe,
        req.body?.used_fallback === true,
        String(req.body?.detail || '').slice(0, 2000),
        String(req.body?.classifier || '').slice(0, 128),
      ],
    );
    res.status(201).json({ id: String(rows[0].id), status: 'open', created_at: rows[0].created_at });
  }),
);

moderationRouter.get(
  '/moderation/queue',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const status = ['open', 'resolved', 'dismissed'].includes(req.query.status) ? req.query.status : 'open';
    const contentType = QUEUE_CONTENT_TYPES.has(req.query.content_type) ? req.query.content_type : null;
    const limit = Math.min(Math.max(Number(req.query.limit) || 50, 1), 200);
    const cursor = Number(req.query.cursor) || 0;

    const params = [status, limit + 1];
    let where = 'q.status = $1';
    if (contentType) {
      params.push(contentType);
      where += ` AND q.content_type = $${params.length}`;
    }
    if (cursor > 0) {
      params.push(cursor);
      where += ` AND q.id < $${params.length}`;
    }

    const { rows } = await query(
      `SELECT q.id, q.content_type, q.content_ref, q.flagged_user_id, q.game_id, q.server_key,
              q.category, q.is_safe, q.used_fallback, q.detail, q.classifier,
              q.status, q.resolution_note, q.created_at, q.reviewed_at,
              flagged.display_name AS flagged_name
         FROM moderation_queue q
         JOIN users flagged ON flagged.id = q.flagged_user_id
        WHERE ${where}
        ORDER BY q.id DESC
        LIMIT $2`,
      params,
    );

    const hasMore = rows.length > limit;
    const page = hasMore ? rows.slice(0, limit) : rows;

    res.json({
      items: page.map((r) => ({
        id: String(r.id),
        content_type: r.content_type,
        content_ref: r.content_ref,
        flagged_user: { id: String(r.flagged_user_id), display_name: r.flagged_name },
        game_id: r.game_id ? String(r.game_id) : null,
        category: r.category,
        is_safe: r.is_safe,
        used_fallback: r.used_fallback,
        detail: r.detail,
        classifier: r.classifier,
        status: r.status,
        resolution_note: r.resolution_note,
        created_at: r.created_at,
        reviewed_at: r.reviewed_at,
      })),
      next_cursor: hasMore ? String(page[page.length - 1].id) : null,
    });
  }),
);

moderationRouter.post(
  '/moderation/queue/:id/resolve',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const queueId = String(req.params.id || '');
    if (!/^\d+$/.test(queueId)) throw badRequest('A numeric queue item id is required.');
    const dismiss = req.body?.dismiss === true;
    const note = String(req.body?.note || '').slice(0, 2000);
    const newStatus = dismiss ? 'dismissed' : 'resolved';

    const { rows } = await query(
      `UPDATE moderation_queue
          SET status = $2, resolution_note = $3, reviewed_by = $4, reviewed_at = NOW()
        WHERE id = $1 AND status = 'open'
        RETURNING flagged_user_id`,
      [queueId, newStatus, note, req.user.id],
    );
    if (rows.length === 0) throw notFound('No open queue item with that id.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason, queue_item_id)
       VALUES ($1, $2, $3, $4, $5)`,
      [req.user.id, rows[0].flagged_user_id, dismiss ? 'dismiss_queue_item' : 'resolve_queue_item', note, queueId],
    );
    res.json({ status: newStatus });
  }),
);

// --- ops: behavioral signals ---------------------------------------------
//
// Three real, computed-on-demand signals -- not stored, so there is
// never a stale cached number for a moderator to distrust. All three are
// plain SQL over content_reports/moderation_actions/users, deliberately
// NOT a new engine-side class: unlike engine::safety::
// BehavioralPatternAnalyzer's DM-content heuristics (which only ever see
// one game server's own traffic), report velocity, repeat-offense, and
// account age are inherently cross-session, account-level facts that
// only the centralized backend can see all of.
moderationRouter.get(
  '/moderation/users/:userId/signals',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');

    const user = await query(`SELECT created_at FROM users WHERE id = $1`, [userId]);
    if (user.rows.length === 0) throw notFound('No such user.');
    const accountAgeDays = Math.max(
      0,
      Math.floor((Date.now() - new Date(user.rows[0].created_at).getTime()) / 86400000),
    );

    const [reports7d, reports30d, confirmed] = await Promise.all([
      query(
        `SELECT COUNT(*)::int AS n FROM content_reports
          WHERE reported_user_id = $1 AND created_at > NOW() - INTERVAL '7 days'`,
        [userId],
      ),
      query(
        `SELECT COUNT(*)::int AS n FROM content_reports
          WHERE reported_user_id = $1 AND created_at > NOW() - INTERVAL '30 days'`,
        [userId],
      ),
      // "Repeat offense" means a PAST report that an admin actually
      // upheld (status = 'resolved'), not just a raw report count --
      // an unresolved or dismissed report is an accusation, not a
      // confirmed prior violation.
      query(`SELECT COUNT(*)::int AS n FROM content_reports WHERE reported_user_id = $1 AND status = 'resolved'`, [userId]),
    ]);

    const reportCount7d = reports7d.rows[0].n;
    const reportCount30d = reports30d.rows[0].n;
    const confirmedViolationCount = confirmed.rows[0].n;

    res.json({
      user_id: userId,
      account_age_days: accountAgeDays,
      report_count_7d: reportCount7d,
      report_count_30d: reportCount30d,
      confirmed_violation_count: confirmedViolationCount,
      // Thresholds are a starting point, not a tuned model -- easy to
      // move once ops has real data to tune against, which is exactly
      // why they live here as plain comparisons instead of buried in a
      // migration or a magic column.
      high_velocity_flag: reportCount7d >= 3,
      repeat_offender_flag: confirmedViolationCount >= 2,
      // A brand-new account already drawing reports is a stronger
      // signal than the same report count on a years-old account.
      new_account_high_risk_flag: accountAgeDays <= 7 && reportCount30d >= 2,
    });
  }),
);

// --- ops: age verification --------------------------------------------
//
// There is no real identity-verification provider integrated yet (that
// is a real, separate build with its own vendor/compliance decisions,
// not something to fabricate here) -- this is the honest, minimal seam:
// an admin sets the flag age-gating actually checks (games.mature's own
// migration comment; sessions/routes.js's /allocate and
// matchmaking/routes.js's ticket route both check it live), auditably,
// after reviewing whatever evidence that future provider (or a support
// ticket, today) supplies. /auth/birthdate's self-report is input a
// reviewer can look at; it is deliberately NOT what flips this flag by
// itself.
moderationRouter.post(
  '/admin/users/:userId/age-verify',
  requireAuth,
  requireAdmin,
  asyncRoute(async (req, res) => {
    const userId = String(req.params.userId || '');
    if (!/^\d+$/.test(userId)) throw badRequest('A numeric user id is required.');
    const verified = req.body?.verified !== false;

    const { rows } = await query(
      `UPDATE users SET age_verified = $2 WHERE id = $1 RETURNING id`,
      [userId, verified],
    );
    if (rows.length === 0) throw notFound('No such user.');

    await query(
      `INSERT INTO moderation_actions (admin_id, target_user_id, action_type, reason)
       VALUES ($1, $2, $3, $4)`,
      [req.user.id, userId, verified ? 'age_verify' : 'age_unverify', String(req.body?.reason || '').slice(0, 2000)],
    );
    res.json({ status: verified ? 'verified' : 'unverified' });
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
