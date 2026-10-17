# Data, privacy and reporting

Plain-language rules for what Kronos keeps, for how long, and how reports and appeals work. The retention times, the
14-day appeal window and the review steps are **proposals for the owner to confirm**; where something is not built yet,
it says so.

## What is stored where

| Data | Where | How long |
|---|---|---|
| Your avatar, owned items, settings | Your PC (`local_profile.profile`) | Until you delete it |
| Games you made | Your PC | Until you delete them |
| Crash reports | Your PC only; user name, home folder and e-mail are removed | Until you delete them |
| Account, published games, catalogue | The Kronos backend | While the account exists |
| Chat moderation logs and appeals | Local files today (`chat_log`, `appeal_log` ...) | See below |

## Retention and deletion (0844)

- Local files are yours. Deleting the folder removes them.
- Account deletion: the backend deletes the account, its published games and its profile data. Moderation records
  needed to stop repeat abuse (a ban, the reason code) are kept for up to 12 months, then removed.
- Chat logs on a game server are kept for 30 days and then deleted. Logs of reports are kept with the report.
- **Not built yet:** automatic deletion jobs and the "download my data" export. Until they exist, a deletion request is
  handled by the owner by hand.

## Age-aware design review (0843)

Kronos is aimed at young creators, so the defaults must be safe:

- No voice chat. Text chat is filtered on your PC and on game servers.
- Personal-information phrases (phone numbers, addresses, "what school") are blocked by the text classifier.
- No real names are required. No ads. No payments exist yet.
- Telemetry is off ([PROJECT_POLICY.md](PROJECT_POLICY.md)).
- Under-13 accounts: **not decided.** Before accounts open to the public, the owner needs to decide the age rule and
  parental consent flow for the countries involved (for example COPPA in the US and the UK Children's Code). This needs
  a person who knows the law; this page is not legal advice.
- Reports about a child's safety go to the owner first, immediately, not through the normal queue.

## Reporting and appeals (0842)

Reasons are fixed codes (`harassment`, `hate_threats`, `sexual`, `scam`, `personal_info`, `cheating`, `spam`, `other`;
see `core/ReportReasons.hpp`).

1. A player picks a reason, optionally adds a sentence, and sends the report. (Report button: not built yet.)
2. The report goes to the moderation queue with the last chat lines around it.
3. A person reviews it. Actions: no action, warning, mute, temporary ban, permanent ban.
4. The reported player is told the reason code and what happened, in plain words.
5. **Appeal:** the player can answer once within 14 days. A different reviewer (or the owner) decides, and the
   result is final.
6. The reporter is told the report was handled, not what the punishment was.

Rules for reviewers: judge the words, not the person; never act on a single report without the context lines; a ban
always has a reason code.
