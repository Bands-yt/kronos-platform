# Accessibility checklist

A short list of what Kronos does for players and creators who need it, what we checked, and what is still open.
Nothing here is a certification. Updated 17 Oct 2026.

## What exists

- **Reduced motion** (Player settings): turns off non-essential UI animation.
- **Colour-blind mode**: a screen-wide filter for protanopia, deuteranopia and tritanopia (Accessibility options).
- **Keyboard**: Studio works from the keyboard for the common jobs ([shortcut list](#shortcuts): press F1 in Studio).
- **Chat filter**: runs on your PC, no account needed.

## Checked

| Check | Result |
|---|---|
| Text contrast on the app palette | Test `testPaletteTextContrast`. Bright, muted, blue, green and amber text reach 4.5:1 on every panel colour. Red text reaches 4.5:1 on the app background and main panels. Faint grey text reaches 3:1, so it is only for disabled or decorative text |
| Status colours for colour-blind players | Green, amber and blue are almost the same brightness for red-green colour blindness (1.0 to 1.3:1 when simulated). Status must also be shown with words or an icon |

## Still open

- Faint grey text and red text on raised buttons and sliders are under 4.5:1.
- Not every status in the UI has a word or icon next to its colour (needs an audit).
- Text size setting for Studio and the Player.
- Screen reader support: none. ImGui does not expose controls to screen readers.
- Remappable game controls in the Player.

## Rules for new UI

1. Never use colour as the only signal. Add a word, a shape or an icon.
2. Body text uses the bright or muted colour. Faint colour is for disabled or decorative text only.
3. Every action reachable by mouse is also reachable from the menu or the command palette.
4. Anything that moves can be switched off by the reduced-motion setting.
5. Test the new screen with the colour-blind filter on.

## WCAG-inspired checklist (897)

Based on the ideas in WCAG 2.2, adapted for an app (not a web page). Tick a box only when it was checked on the real screen.

- [ ] Text contrast at least 4.5:1 (3:1 for large text); see the checked table above.
- [ ] Nothing flashes more than 3 times a second.
- [ ] Every control has a visible label or tooltip.
- [ ] Everything works with the keyboard; focus is visible and moves in a sensible order.
- [ ] Colour is never the only signal.
- [ ] Text can be made bigger (not built).
- [ ] Time limits can be turned off or extended.
- [ ] Errors say what went wrong and what to do.

## Error message wording (896)

1. Say what happened in plain words: "Couldn't open the file", not "ENOENT".
2. Say what to do next: "Check the file still exists, then try again."
3. Never blame the player. Never show only a code; put the code after the sentence for bug reports.
4. Keep it under two sentences. No jargon the owner's 14-year-old self wouldn't know.
5. Same problem, same words everywhere.

## Shortcuts

Press **F1** in Studio for the list. **F10** hides the side panels (focus mode); **Shift+F10** also hides the ribbon (presentation mode, Esc leaves it).
