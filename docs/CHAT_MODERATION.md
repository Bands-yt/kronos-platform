# In-game chat and moderation

## Opening chat

In the Player, press `/` (like Roblox) or click the speech-bubble button in the top-left corner. Press Enter to send and Escape to close. Your character stops moving while the chat box is open. The Esc menu also has a "Chat (/)" button.

## What happens to a message

Every message goes through the same steps, both online and when you play alone:

1. **Chat allowed?** World chat must be on, and the sender must not be muted (for this session or on their account).
2. **Rate limit.** Three messages per second. Extra messages are refused with "You're sending messages too fast."
3. **Profanity filter.** Swear words are replaced with `****` in what other players see.
4. **Local classifier** (`safety::TrustSafetyService`). It flags risky messages and blocks hard-block categories. A blocked message is not shown, and the sender sees why.
5. **Gemini** (only when `GEMINI_API_KEY` is set). The message waits up to 2.5 s for Gemini's verdict:
   - safe: the message is shown
   - flagged: the message shows as `[Message removed by moderation]`
   - no answer in time: the message is shown anyway, because the local filters already passed it

When you play alone, the Player runs these steps itself. Refusals show up in the chat box as `[System]` lines.

## Turning on Gemini

```
GEMINI_API_KEY=your-key ./engine_runtime
```

The Engine Log then says `Gemini chat moderation active (model gemini-flash-lite-latest)`. Without a key it says chat runs on local filters only.

## Saving messages to review or train a model

Set `KRONOS_CHAT_REVIEW_LOG` to a file path. Every message that reaches the filters is then added to that file as one JSON line:

```
KRONOS_CHAT_REVIEW_LOG=chat_review.jsonl GEMINI_API_KEY=your-key ./engine_runtime
```

```json
{"time":1791581582472,"text":"this is shit","shown":"this is ****","profanity":true,
 "local":{"flagged":false,"blocked":false,"confidence":0.0,"categories":[]},
 "gemini":{"safe":true,"reason":"SAFE","fallback":false,"detail":""},"outcome":"censored"}
```

| Field | Meaning |
|---|---|
| `text` | what the player typed |
| `shown` | what other players saw (empty when blocked) |
| `local` | the local classifier's result |
| `gemini` | Gemini's verdict, or `null` when Gemini wasn't asked; `fallback: true` means Gemini didn't decide (no key, rate limited, or unreachable) |
| `outcome` | `delivered`, `censored`, `blocked_local`, `blocked_gemini` or `gemini_timeout` |

It is off by default. Only use it for your own test messages. Saving other players' chat needs their permission and a privacy policy first.

Messages refused for chat being off, a mute or the rate limit are not written, since no filter looked at them.

## Known limits

- Online, a player whose message is blocked gets no notice yet; only playing alone shows the `[System]` line.
- A host that is also playing (not a dedicated server) has no local chat path yet.
- The older `moderation_training_data.log` (flagged messages only, written when a server turns on `persistModerationLogs`) still exists alongside the new log.
