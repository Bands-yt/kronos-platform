# Audio research notes

Notes only.

## Audio workspace with a multitrack timeline (0351)
The pieces that exist as tested code: regions (`splitRegion`), fades (`fadeIn/fadeOut`), envelopes
(`envelopeAt`), tempo grid (`snapToBeat`), waveform peaks (`waveformPeaks`). The window itself is the work:
track lanes, zoom, drag editing, undo. About two weeks.

## Effect plugin host (0370)
Hosting VST3 plugins needs the VST3 SDK (dual licence, GPL3 or Steinberg's agreement) so it needs a licence
decision first. A safer start: Kronos's own effects (gate, ducking, EQ) behind the existing plugin API.

## Voice chat (0384)
Options: peer to peer (simple, leaks IP addresses) or through the game server (costs bandwidth, easier to
moderate). Opus is the codec to use (BSD licence). Moderation and under-13 rules must be decided before any
build. Not planned for v5.
