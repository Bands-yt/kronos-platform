# Known limitations

Everything here is something Kronos does not do, or does badly, today. Severity is from the point of view of someone
trying to use Kronos: **High** blocks a common job, **Medium** needs a workaround, **Low** is minor. Updated 17 Oct 2026.
The same list in short form is in the README. Labels for each area are in [MATURITY.md](MATURITY.md).

| Area | Limit | Severity | Workaround |
|---|---|---|---|
| Roblox import | Big Roblox games use features Kronos lacks (see ROBLOX_BRIDGE.md) | High | Try the Bridge Doctor first; simple places work |
| Roblox import | Zstd chunks and Font properties are not read | Medium | Re-save the place without them |
| Roblox import | Unions that are negated are not cut out; CDN-hosted unions stay boxes | Low | Rebuild in Studio |
| Roblox API | `PathfindingService`, many others are not mapped | Medium | The import report names each call |
| Playing | Others' real avatar looks are not sent; everyone has the default look | Medium | None yet |
| Playing | A leaving player's avatar stays frozen on other clients | Medium | Rejoin |
| Multiplayer | Full world diff each tick, no interest management | Medium | Keep player counts small (tested with 2 to 4) |
| Multiplayer | No matchmaking, no cloud hosting | High | Host yourself ([SERVER.md](SERVER.md)) |
| Studio | Viewport ignores Roblox Lighting | Low | Check in Play |
| Studio | No Python autocomplete or error markers | Medium | Run and read the log |
| Studio | Gizmo drags and very large scenes are not tested | Medium | Save often |
| Studio | Team Create (live co-editing) does not exist | Medium | Share a package or project folder |
| Windows | Fresh-PC test of the installer not done; software rendering crashed in one test | Medium | Use a real GPU driver (`--gpu-info`) |
| macOS | Not supported | High | Use Windows or Linux |
| Plugins | Sandboxed plugins need Linux (Landlock 5.13+) or Windows; none on macOS | Medium | Trusted plugins work everywhere |
| Servers | `kronos_server` is Linux-only | Medium | Use the container |
| Audio | No HRTF, no VST | Low | |
| Graphics | Mid-range renderer; no ray-traced lighting yet | Low | |
| Accessibility | See [ACCESSIBILITY.md](ACCESSIBILITY.md) | Medium | |
| Data | No "download my data", no automatic deletion jobs | Medium | Ask the owner |

If you find a limit that is not listed, that is a documentation bug. Please report it.
