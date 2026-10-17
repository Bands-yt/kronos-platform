# Kronos v5 plan: the 1,000-item backlog, rated

_Written 17 Oct 2026. Source: the advisor's 1,000-item backlog (16 Oct 2026). Every item is rated for **our** pace (one agent plus the owner), not a human team._

## How to read this

| Dot | Meaning | Our time for one item |
|---|---|---|
| 🔴 | Extremely hard: a whole system, easy to get wrong | 4 days (the very biggest: 10 days) |
| 🟠 | Hard but important: a big feature, needs careful tests | about 1 day (8 hours) |
| 🟡 | Medium: a normal feature, one sitting | about 1.5 hours |
| 🟢 | Easy: small change, a doc, or a quick check | about 15 minutes |

Other marks: ✅ already exists or was finished (checked against the code), ◐ partly done (half the time counted), ⛔ **needs you** (an account, money, server access, a legal choice or permission, so I can't finish it alone). Items with no mark were not checked against the code yet; some of them probably exist already, so the totals are on the high side.

**How I estimated:** a day's real pace so far: the Windows sandbox took about 5 hours including the VM; small and medium Studio features (tool picker, undo for dragging, hover cards, GPU timer) took 20 to 40 minutes each including a screen check; waiting for Windows CI is 40 minutes per run and I can build while it runs. One 'day' below means about 10 hours of work. The advisor's own S/M/L size was the starting point and I changed it where I knew better.

## The big picture

| | Items | Time (at our pace) |
|---|---|---|
| 🔴 Extremely hard | 31 | 38.0 work weeks |
| 🟠 Hard | 236 | 33.6 work weeks |
| 🟡 Medium | 488 | 11.2 work weeks |
| 🟢 Easy | 245 | 4.7 days |
| **All 1,000** | **1000** | **83.8 work weeks** |

- ✅ **100** items already exist or are finished, ◐ **289** are partly done, ⛔ **150** need you (11.9 work weeks of the total).
- Without the ⛔ items, the work I can do alone is **71.9 work weeks**.
- P0 (42 items): 9.8 days
- P1 (278 items): 48.8 work weeks
- P2 (680 items): 33.0 work weeks

**Honest note:** this total is a planning number, not a promise. Real work always finds surprises, like the three Windows build failures this week. The red items are the least certain: treat their times as 'about this, maybe double'. The ⛔ items can also slip because they wait for you.

## What I suggest v5 should contain

Doing all 1,000 items is about **83.8 work weeks** (a work week is 5 days of about 10 hours). That is too much for one release, so here is a cut:

| Slice | Items | Time |
|---|---|---|
| **v5 core:** every P0 item, plus every 🟢 and 🟡 P1 item (nothing that needs you) | 130 | 3.5 work weeks |
| **v5 extras if time allows:** the 🟠 P1 items (terrain, materials, animation, netcode, Player settings) | 127 | 20.3 work weeks |
| **After v5:** all P2 items, all 🔴 items, and everything marked ⛔ | rest | rest |

The v5 core is 3.5 work weeks. At the pace of a busy week that is several weeks of building, with the Windows VM test and the owner-only steps done in between.

## By area

| Area | 🔴 | 🟠 | 🟡 | 🟢 | Time |
|---|---|---|---|---|---|
| First-run UX and UI foundation | 0 | 3 | 10 | 12 | 3.6 days |
| Workspace layout and visual design | 0 | 6 | 10 | 9 | 5.6 days |
| Scene graph and inspector | 0 | 5 | 15 | 5 | 3.3 days |
| Viewport and editor navigation | 0 | 5 | 15 | 5 | 5.4 days |
| Native modeling and mesh editing | 2 | 6 | 12 | 5 | 2.8 work weeks |
| Terrain and procedural worldbuilding | 1 | 10 | 9 | 5 | 2.6 work weeks |
| Asset import pipeline | 1 | 6 | 14 | 4 | 2.0 work weeks |
| Asset browser and content management | 0 | 5 | 15 | 5 | 5.8 days |
| Materials and shader authoring | 1 | 5 | 15 | 4 | 3.1 work weeks |
| Core renderer architecture | 2 | 6 | 12 | 5 | 2.9 work weeks |
| Lighting and environment | 1 | 9 | 10 | 5 | 2.5 work weeks |
| Visual effects and particles | 1 | 5 | 14 | 5 | 3.1 work weeks |
| Animation and rigging | 0 | 8 | 12 | 5 | 7.6 days |
| Movie Maker and cinematic sequencer | 2 | 5 | 13 | 5 | 3.8 work weeks |
| Audio Maker and sound design tools | 2 | 8 | 11 | 4 | 4.3 work weeks |
| Runtime audio engine | 3 | 6 | 11 | 5 | 4.8 work weeks |
| Luau scripting experience | 0 | 8 | 13 | 4 | 5.7 days |
| Python scripting environment | 0 | 6 | 15 | 4 | 4.1 days |
| Rust SDK and polyglot architecture | 0 | 6 | 14 | 5 | 3.8 days |
| Debugging and developer tools | 0 | 8 | 12 | 5 | 6.3 days |
| Roblox migration and compatibility bridge | 1 | 9 | 10 | 5 | 2.1 work weeks |
| Kronos Player and end-user experience | 0 | 7 | 13 | 5 | 7 days |
| Build packaging and release engineering | 2 | 6 | 12 | 5 | 2.6 work weeks |
| Multiplayer runtime and netcode | 0 | 8 | 13 | 4 | 7.5 days |
| Cloud servers and matchmaking | 1 | 8 | 10 | 6 | 2.3 work weeks |
| Backend and developer portal | 0 | 6 | 14 | 5 | 7 days |
| Persistence, economy and commerce | 0 | 7 | 13 | 5 | 7.7 days |
| Realtime collaboration and Team Create | 5 | 3 | 12 | 5 | 8.5 work weeks |
| Plugin architecture and developer API | 0 | 6 | 14 | 5 | 6.7 days |
| Plugin marketplace and ecosystem | 0 | 6 | 9 | 10 | 6.4 days |
| Templates, samples and documentation | 0 | 5 | 8 | 12 | 5.5 days |
| AI-assisted creator workflows | 2 | 5 | 13 | 5 | 2.8 work weeks |
| Quality assurance and automation | 0 | 5 | 15 | 5 | 5 days |
| Security, permissions and safety | 0 | 6 | 8 | 11 | 3.9 days |
| Performance profiling and optimization | 0 | 5 | 13 | 7 | 3.3 days |
| Input, accessibility and controls | 0 | 4 | 12 | 9 | 4.5 days |
| Mobile and low-end platform readiness | 2 | 5 | 14 | 4 | 5.1 work weeks |
| Web and console portability | 1 | 4 | 12 | 8 | 3.0 work weeks |
| Creator discovery and community tooling | 0 | 5 | 15 | 5 | 6.4 days |
| Product identity and strategic differentiation | 1 | 0 | 6 | 18 | 4.7 days |

## Suggested order for v5

1. **Finish the important basics (P0).** 42 items, about half already done. First 15 minutes for a new creator.
2. **All the 🟢 and 🟡 items in the P1 list.** Many quick wins, big feel improvement.
3. **The 🟠 items that make Kronos useful for real games:** terrain, materials, animation, netcode, Player settings.
4. **🔴 items last, one at a time,** each as a small measured prototype first (the advisor's own rule).
5. **⛔ items whenever you have time with me:** backend migrations, the Gemini key, signing certificates, payments, the flagship game.

## Every item

Columns: dot, ID, priority, item, ours (time), marks.

### 1. First-run UX and UI foundation (3.6 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟡 | 0001 | P0 | Welcome wizard with engine mode selection | done | ✅ |
| 🟡 | 0002 | P0 | One-click sample project creation | 45 min | ◐ |
| 🟢 | 0003 | P0 | First-launch hardware and driver check | 8 min | ◐ |
| 🟠 | 0004 | P1 | Missing dependency installer with repair mode | 8 h |  |
| 🟢 | 0005 | P1 | Clear pre-beta warning and crash-safe reset | done | ✅ |
| 🟡 | 0006 | P1 | UI scale and text size preferences | 45 min | ◐ |
| 🟡 | 0007 | P1 | Light dark and high-contrast themes | 1.5 h |  |
| 🟡 | 0008 | P1 | Consistent spacing and typography tokens | 1.5 h |  |
| 🟡 | 0009 | P2 | Unified icon library with clear metaphors | 1.5 h |  |
| 🟢 | 0010 | P2 | Searchable command palette | done | ✅ |
| 🟡 | 0011 | P2 | Context-sensitive help panel | 1.5 h |  |
| 🟢 | 0012 | P2 | Undo and redo status visibility | 15 min |  |
| 🟢 | 0013 | P2 | Global notification center | done | ✅ |
| 🟢 | 0014 | P2 | In-app release notes and migration hints | 8 min | ◐ |
| 🟢 | 0015 | P2 | Empty states with actionable next steps | 15 min |  |
| 🟢 | 0016 | P2 | Keyboard shortcut discovery overlay | done | ✅ |
| 🟡 | 0017 | P2 | Recent projects with thumbnails | 45 min | ◐ |
| 🟡 | 0018 | P2 | Project health dashboard on startup | 45 min | ◐ |
| 🟡 | 0019 | P2 | Recover unsaved work after crash | 45 min | ◐ |
| 🟠 | 0020 | P2 | First-five-minutes guided tutorial | 8 h |  |
| 🟢 | 0021 | P2 | UI performance budget and frame-time overlay | done | ✅ |
| 🟠 | 0022 | P2 | Localization-ready UI string catalog | 8 h |  |
| 🟢 | 0023 | P2 | Customizable accent colors | done | ✅ |
| 🟢 | 0024 | P2 | Reduced-motion UI option | 15 min |  |
| 🟢 | 0025 | P2 | Accessibility audit checklist | done | ✅ |

### 2. Workspace layout and visual design (5.6 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0026 | P0 | Resizable dock panels with saved layouts | done | ✅ |
| 🟢 | 0027 | P0 | Reset workspace to known-good default | done | ✅ |
| 🟠 | 0028 | P0 | Per-task workspace presets for scripting modeling and cinematic work | 8 h |  |
| 🟠 | 0029 | P1 | Multi-monitor window placement restore | 8 h |  |
| 🟢 | 0030 | P1 | Draggable tabs with clear drop targets | 15 min |  |
| 🟡 | 0031 | P1 | Dock preview before releasing panel | 1.5 h |  |
| 🟡 | 0032 | P1 | Panel search and panel visibility manager | 1.5 h |  |
| 🟡 | 0033 | P1 | Persistent workspace per project | 45 min | ◐ |
| 🟡 | 0034 | P2 | Quick switch between Studio Player and Movie Maker | 1.5 h |  |
| 🟢 | 0035 | P2 | Compact and spacious density modes | 8 min | ◐ |
| 🟢 | 0036 | P2 | Responsive layout for 720p displays | 15 min |  |
| 🟡 | 0037 | P2 | No clipped text at common DPI scales | 1.5 h |  |
| 🟡 | 0038 | P2 | DPI-aware icons and hit targets | 1.5 h |  |
| 🟢 | 0039 | P2 | Consistent property row alignment | 15 min |  |
| 🟡 | 0040 | P2 | Color tokens for status selection and warnings | 1.5 h |  |
| 🟡 | 0041 | P2 | Shared style guide for all windows | 1.5 h |  |
| 🟢 | 0042 | P2 | Empty panel onboarding | 15 min |  |
| 🟢 | 0043 | P2 | Panel-level keyboard shortcuts | 15 min |  |
| 🟠 | 0044 | P2 | Layout migration between versions | 8 h |  |
| 🟡 | 0045 | P2 | Pop-out script editor window | 1.5 h |  |
| 🟠 | 0046 | P2 | Multi-window viewport support | 8 h |  |
| 🟢 | 0047 | P2 | Focus mode hides nonessential chrome | done | ✅ |
| 🟢 | 0048 | P2 | Presentation mode for screen recording | 8 min | ◐ |
| 🟠 | 0049 | P2 | UI screenshot regression suite | 8 h |  |
| 🟡 | 0050 | P2 | UI consistency lint rules | 1.5 h |  |

### 3. Scene graph and inspector (3.3 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0051 | P0 | Virtualized hierarchy for large scenes | done | ✅ |
| 🟠 | 0052 | P0 | Multi-select entities with clear selection summary | done | ✅ |
| 🟠 | 0053 | P0 | Drag-and-drop parenting with cycle prevention | done | ✅ |
| 🟠 | 0054 | P1 | Search hierarchy by name type and tag | 8 h |  |
| 🟡 | 0055 | P1 | Filter hierarchy by component | 45 min | ◐ |
| 🟡 | 0056 | P1 | Favorites and pinned entities | 45 min | ◐ |
| 🟠 | 0057 | P1 | Rename in place with validation | 8 h |  |
| 🟡 | 0058 | P1 | Bulk rename with patterns | 45 min | ◐ |
| 🟡 | 0059 | P2 | Bulk component add remove and reset | 1.5 h |  |
| 🟡 | 0060 | P2 | Inspector sections grouped by component | 1.5 h |  |
| 🟢 | 0061 | P2 | Property search and quick focus | 15 min |  |
| 🟡 | 0062 | P2 | Mixed-value display for multi-selection | 1.5 h |  |
| 🟡 | 0063 | P2 | Numeric property scrub controls | 45 min | ◐ |
| 🟢 | 0064 | P2 | Vector and transform gizmos in inspector | 15 min |  |
| 🟡 | 0065 | P2 | Copy and paste component values | 45 min | ◐ |
| 🟡 | 0066 | P2 | Copy entity path or stable ID | 45 min | ◐ |
| 🟢 | 0067 | P2 | Reparent and duplicate with undo | done | ✅ |
| 🟡 | 0068 | P2 | Lock hide and solo entities | 45 min | ◐ |
| 🟡 | 0069 | P2 | Scene tree diff after import | 45 min | ◐ |
| 🟡 | 0070 | P2 | Entity reference picker | 1.5 h |  |
| 🟢 | 0071 | P2 | Component dependency warnings | 8 min | ◐ |
| 🟡 | 0072 | P2 | Missing-reference repair panel | 45 min | ◐ |
| 🟡 | 0073 | P2 | Inspector schema generated from reflection metadata | 1.5 h |  |
| 🟢 | 0074 | P2 | Saveable custom inspector layouts | 15 min |  |
| 🟡 | 0075 | P2 | Hierarchy performance stress test with 100k entities | 1.5 h |  |

### 4. Viewport and editor navigation (5.4 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0076 | P1 | Orbit pan and zoom controls with presets | 8 h |  |
| 🟠 | 0077 | P1 | Frame selected object shortcut | 8 h |  |
| 🟠 | 0078 | P1 | Focus selected bounding box smoothly | 8 h |  |
| 🟠 | 0079 | P1 | Fly camera with adjustable speed | 8 h |  |
| 🟡 | 0080 | P1 | Walkthrough camera with collision | 1.5 h |  |
| 🟡 | 0081 | P1 | Orthographic top front side and isometric views | 45 min | ◐ |
| 🟠 | 0082 | P1 | Perspective lens and field-of-view controls | 8 h |  |
| 🟡 | 0083 | P1 | Viewport bookmarks and named cameras | 45 min | ◐ |
| 🟡 | 0084 | P2 | Toggle grid axes and safe guides | 45 min | ◐ |
| 🟡 | 0085 | P2 | Snap to grid and angular increments | 45 min | ◐ |
| 🟢 | 0086 | P2 | Vertex edge face and object selection modes | 15 min |  |
| 🟡 | 0087 | P2 | Box lasso and paint selection | 45 min | ◐ |
| 🟡 | 0088 | P2 | Occlusion-aware selection | 45 min | ◐ |
| 🟢 | 0089 | P2 | X-ray and wireframe selection | 15 min |  |
| 🟡 | 0090 | P2 | Transform gizmo coordinate-space toggle | 45 min | ◐ |
| 🟡 | 0091 | P2 | Pivot editing and reset | 45 min | ◐ |
| 🟢 | 0092 | P2 | Surface alignment placement tool | 8 min | ◐ |
| 🟡 | 0093 | P2 | Measurement ruler and distance annotations | 45 min | ◐ |
| 🟡 | 0094 | P2 | Screenshot capture without editor chrome | 1.5 h |  |
| 🟡 | 0095 | P2 | Viewport statistics overlay | 45 min | ◐ |
| 🟢 | 0096 | P2 | Camera clip-plane controls | 8 min | ◐ |
| 🟡 | 0097 | P2 | Near-camera clipping diagnostics | 45 min | ◐ |
| 🟡 | 0098 | P2 | Gizmo hit-target accessibility | 45 min | ◐ |
| 🟢 | 0099 | P2 | Multi-viewport split layouts | 15 min |  |
| 🟡 | 0100 | P2 | Viewport interaction recording tests | 45 min | ◐ |

### 5. Native modeling and mesh editing (2.8 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0101 | P1 | Primitive mesh creation with editable parameters | 8 h |  |
| 🟠 | 0102 | P1 | Extrude faces with inset and offset | 8 h |  |
| 🟠 | 0103 | P1 | Inset and bevel mesh operations | 8 h |  |
| 🟠 | 0104 | P1 | Loop cut and edge slide | 8 h |  |
| 🟡 | 0105 | P1 | Vertex merge and weld cleanup | 45 min | ◐ |
| 🟠 | 0106 | P1 | Knife and plane cut tools | 8 h |  |
| 🔴 | 0107 | P1 | Boolean union subtract and intersect | 4 days |  |
| 🟠 | 0108 | P1 | Non-destructive modifier stack | 8 h |  |
| 🟡 | 0109 | P2 | Mirror modifier with clipping | 45 min | ◐ |
| 🟡 | 0110 | P2 | Array and radial duplication modifiers | 45 min | ◐ |
| 🟢 | 0111 | P2 | Solidify and shell modifier | 8 min | ◐ |
| 🟡 | 0112 | P2 | Subdivision surface modifier | 45 min | ◐ |
| 🔴 | 0113 | P2 | Decimate and retopology assist | 4 days |  |
| 🟢 | 0114 | P2 | Normals display and recalculation | 8 min | ◐ |
| 🟡 | 0115 | P2 | UV seam marking | 45 min | ◐ |
| 🟡 | 0116 | P2 | Mesh validation for non-manifold geometry | 45 min | ◐ |
| 🟢 | 0117 | P2 | Pivot and origin editing | 8 min | ◐ |
| 🟡 | 0118 | P2 | Mesh island selection and separation | 45 min | ◐ |
| 🟡 | 0119 | P2 | Mesh join with material preservation | 45 min | ◐ |
| 🟡 | 0120 | P2 | LOD generation preview | 45 min | ◐ |
| 🟢 | 0121 | P2 | Procedural mesh from Luau parameters | 15 min |  |
| 🟡 | 0122 | P2 | Mesh operation history and replay | 1.5 h |  |
| 🟡 | 0123 | P2 | Gizmo-based deform brushes | 1.5 h |  |
| 🟢 | 0124 | P2 | Mesh compare and diff view | 8 min | ◐ |
| 🟡 | 0125 | P2 | Export selected mesh with clean transforms | 45 min | ◐ |

### 6. Terrain and procedural worldbuilding (2.6 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0126 | P1 | Heightmap terrain import and export | 8 h |  |
| 🟠 | 0127 | P1 | Editable heightmap terrain brush | 8 h |  |
| 🟠 | 0128 | P1 | Smooth raise lower flatten and erosion brushes | 8 h |  |
| 🟠 | 0129 | P1 | Terrain layer painting with blend masks | 8 h |  |
| 🟠 | 0130 | P1 | Runtime terrain chunk streaming | 8 h |  |
| 🟠 | 0131 | P1 | Voxel terrain block editing | 8 h |  |
| 🔴 | 0132 | P1 | Smooth voxel terrain via marching cubes prototype | 4 days |  |
| 🟠 | 0133 | P1 | Terrain chunk LOD and crack prevention | 8 h |  |
| 🟡 | 0134 | P2 | Spline-based road and river tools | 45 min | ◐ |
| 🟡 | 0135 | P2 | Landscape stamps and reusable brushes | 45 min | ◐ |
| 🟢 | 0136 | P2 | Procedural biome rules | 8 min | ◐ |
| 🟡 | 0137 | P2 | Scatter vegetation along slope and masks | 45 min | ◐ |
| 🟡 | 0138 | P2 | Foliage instancing with density controls | 45 min | ◐ |
| 🟢 | 0139 | P2 | Runtime terrain carving API | 8 min | ◐ |
| 🟡 | 0140 | P2 | Terrain collision mesh generation | 45 min | ◐ |
| 🟡 | 0141 | P2 | Terrain sculpt undo history | 1.5 h |  |
| 🟢 | 0142 | P2 | Terrain material layers with distance blending | 8 min | ◐ |
| 🟠 | 0143 | P2 | Cave and overhang workflow | 8 h |  |
| 🟡 | 0144 | P2 | Terrain streaming diagnostics | 45 min | ◐ |
| 🟠 | 0145 | P2 | World origin rebasing for large maps | 8 h |  |
| 🟢 | 0146 | P2 | Splatmap import support | 8 min | ◐ |
| 🟡 | 0147 | P2 | Terrain export to heightmap and meshes | 45 min | ◐ |
| 🟠 | 0148 | P2 | Erosion simulation with bake step | 8 h |  |
| 🟢 | 0149 | P2 | Terrain boundary stitching | 8 min | ◐ |
| 🟡 | 0150 | P2 | Sample open-world terrain project | 1.5 h |  |

### 7. Asset import pipeline (2.0 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0151 | P0 | Import queue with progress and cancellation | 4 h | ◐ |
| 🟠 | 0152 | P0 | Drag-and-drop multi-file import | 8 h |  |
| 🟠 | 0153 | P0 | Import profile per asset type | 8 h |  |
| 🟠 | 0154 | P1 | Import presets per target platform | 8 h |  |
| 🟡 | 0155 | P1 | Import report with warnings and skipped fields | 1.5 h |  |
| 🟡 | 0156 | P1 | Reimport when source files change | 45 min | ◐ |
| 🟠 | 0157 | P1 | Asset dependency tracking | 8 h |  |
| 🟡 | 0158 | P1 | Stable GUID assignment for imported assets | 45 min | ◐ |
| 🟡 | 0159 | P2 | Duplicate asset detection by hash | 45 min | ◐ |
| 🟡 | 0160 | P2 | Import conflict resolution UI | 45 min | ◐ |
| 🟢 | 0161 | P2 | glTF 2.0 import quality suite | 15 min |  |
| 🟡 | 0162 | P2 | OBJ and MTL import reliability | 1.5 h |  |
| 🔴 | 0163 | P2 | FBX skeletal animation import | 4 days |  |
| 🟠 | 0164 | P2 | USD and USDZ evaluation prototype | 8 h |  |
| 🟡 | 0165 | P2 | Blender export workflow documentation | 1.5 h |  |
| 🟡 | 0166 | P2 | SVG and raster image import | 1.5 h |  |
| 🟢 | 0167 | P2 | HDR EXR and environment map import | 15 min |  |
| 🟡 | 0168 | P2 | Font import with shaping validation | 1.5 h |  |
| 🟡 | 0169 | P2 | Video clip import metadata extraction | 1.5 h |  |
| 🟡 | 0170 | P2 | Audio waveform and metadata import | 1.5 h |  |
| 🟢 | 0171 | P2 | Sprite sheet slicing on import | 8 min | ◐ |
| 🟡 | 0172 | P2 | Texture channel packing presets | 45 min | ◐ |
| 🟡 | 0173 | P2 | Unit scale and axis conversion preview | 45 min | ◐ |
| 🟢 | 0174 | P2 | Import sandbox for untrusted parsers | 15 min |  |
| 🟡 | 0175 | P2 | Batch import regression corpus | 1.5 h |  |

### 8. Asset browser and content management (5.8 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0176 | P1 | Thumbnail generation cache | 8 h |  |
| 🟠 | 0177 | P1 | Search by name tag type and usage | 8 h |  |
| 🟠 | 0178 | P1 | Smart collections based on rules | 8 h |  |
| 🟠 | 0179 | P1 | User-created asset folders and favorites | 8 h |  |
| 🟡 | 0180 | P1 | Duplicate and orphan asset scanner | 45 min | ◐ |
| 🟡 | 0181 | P1 | Dependency graph for assets | 45 min | ◐ |
| 🟠 | 0182 | P1 | Find references before delete | 8 h |  |
| 🟡 | 0183 | P1 | Replace asset across project | 45 min | ◐ |
| 🟡 | 0184 | P2 | Asset usage counts by scene | 45 min | ◐ |
| 🟡 | 0185 | P2 | Version history for source assets | 1.5 h |  |
| 🟢 | 0186 | P2 | Metadata tags and custom fields | 8 min | ◐ |
| 🟡 | 0187 | P2 | License and attribution metadata | 1.5 h |  |
| 🟡 | 0188 | P2 | Content hash verification | 45 min | ◐ |
| 🟢 | 0189 | P2 | Thumbnail contact sheets | 8 min | ◐ |
| 🟡 | 0190 | P2 | Preview 3D models with lighting presets | 1.5 h |  |
| 🟡 | 0191 | P2 | Preview audio and video in browser | 1.5 h |  |
| 🟢 | 0192 | P2 | Preview animation clips on rig | 15 min |  |
| 🟡 | 0193 | P2 | Asset comparison view | 1.5 h |  |
| 🟡 | 0194 | P2 | Drag assets directly into scene | 1.5 h |  |
| 🟡 | 0195 | P2 | Download and cache remote assets | 1.5 h |  |
| 🟢 | 0196 | P2 | Asset pack creation wizard | 8 min | ◐ |
| 🟡 | 0197 | P2 | Reusable project-level asset aliases | 45 min | ◐ |
| 🟡 | 0198 | P2 | Missing asset relink wizard | 45 min | ◐ |
| 🟢 | 0199 | P2 | Asset database consistency repair | 15 min |  |
| 🟡 | 0200 | P2 | Asset browser keyboard navigation | 1.5 h |  |

### 9. Materials and shader authoring (3.1 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🔴 | 0201 | P1 | Node-based material editor | 2.0 work weeks |  |
| 🟠 | 0202 | P1 | Material graph validation and error paths | 8 h |  |
| 🟠 | 0203 | P1 | PBR base color metallic roughness pipeline | 8 h |  |
| 🟠 | 0204 | P1 | Normal height and occlusion map support | 8 h |  |
| 🟡 | 0205 | P1 | Material instance parameters | 45 min | ◐ |
| 🟡 | 0206 | P1 | Material preview sphere and custom mesh | 1.5 h |  |
| 🟠 | 0207 | P1 | Texture sampler and tiling controls | 8 h |  |
| 🟡 | 0208 | P1 | UV channel preview | 1.5 h |  |
| 🟡 | 0209 | P2 | Triplanar mapping node | 45 min | ◐ |
| 🟡 | 0210 | P2 | Vertex-color material blending | 45 min | ◐ |
| 🟢 | 0211 | P2 | Decal authoring and projection bounds | 15 min |  |
| 🟡 | 0212 | P2 | Shader include and reusable function library | 45 min | ◐ |
| 🟡 | 0213 | P2 | Shader compile cache with diagnostics | 1.5 h |  |
| 🟢 | 0214 | P2 | Hot reload material graphs | 15 min |  |
| 🟡 | 0215 | P2 | Per-platform shader variant selection | 45 min | ◐ |
| 🟡 | 0216 | P2 | Visualize normals roughness and overdraw | 1.5 h |  |
| 🟢 | 0217 | P2 | Material batching compatibility hints | 8 min | ◐ |
| 🟡 | 0218 | P2 | Procedural noise and gradient nodes | 45 min | ◐ |
| 🟡 | 0219 | P2 | Water material preset with depth fade | 45 min | ◐ |
| 🟡 | 0220 | P2 | Toon and stylized shading templates | 45 min | ◐ |
| 🟠 | 0221 | P2 | Subsurface-scattering research prototype | 8 h |  |
| 🟡 | 0222 | P2 | Shader graph version migration | 1.5 h |  |
| 🟡 | 0223 | P2 | Exportable material presets | 45 min | ◐ |
| 🟢 | 0224 | P2 | Shader documentation generated from nodes | 8 min | ◐ |
| 🟡 | 0225 | P2 | Material graph unit tests | 1.5 h |  |

### 10. Core renderer architecture (2.9 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🔴 | 0226 | P1 | Frame graph with explicit resource lifetimes | 4 days |  |
| 🟠 | 0227 | P1 | GPU resource leak tracking | 8 h |  |
| 🟠 | 0228 | P1 | Bindless texture capability detection | 8 h |  |
| 🔴 | 0229 | P1 | Multi-draw indirect path for compatible GPUs | 4 days |  |
| 🟡 | 0230 | P1 | GPU frustum culling pass | 45 min | ◐ |
| 🟠 | 0231 | P1 | GPU occlusion culling prototype | 8 h |  |
| 🟠 | 0232 | P1 | Draw-call and pipeline-state reduction report | 8 h |  |
| 🟡 | 0233 | P1 | Persistent mapped upload buffer manager | 1.5 h |  |
| 🟡 | 0234 | P2 | Async texture upload | 1.5 h |  |
| 🟡 | 0235 | P2 | Async mesh upload | 1.5 h |  |
| 🟢 | 0236 | P2 | GPU memory budget manager | 8 min | ◐ |
| 🟡 | 0237 | P2 | Descriptor allocation diagnostics | 1.5 h |  |
| 🟡 | 0238 | P2 | Render-thread command queue profiling | 1.5 h |  |
| 🟢 | 0239 | P2 | Pipeline cache persistence | 15 min |  |
| 🟡 | 0240 | P2 | Shader permutation budget enforcement | 45 min | ◐ |
| 🟡 | 0241 | P2 | RenderDoc-friendly debug markers | 1.5 h |  |
| 🟢 | 0242 | P2 | Render backend capability report | done | ✅ |
| 🟡 | 0243 | P2 | Swapchain recreation reliability | 1.5 h |  |
| 🟡 | 0244 | P2 | Dynamic resolution scaling option | 45 min | ◐ |
| 🟡 | 0245 | P2 | Render scale presets | 45 min | ◐ |
| 🟢 | 0246 | P2 | GPU timing queries with graceful fallback | 15 min |  |
| 🟠 | 0247 | P2 | Mesh shader capability-gated experiment | 8 h |  |
| 🟠 | 0248 | P2 | Clustered geometry culling prototype | 8 h |  |
| 🟢 | 0249 | P2 | Renderer feature flags per driver | 15 min |  |
| 🟡 | 0250 | P2 | Render correctness test scenes | 1.5 h |  |

### 11. Lighting and environment (2.5 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0251 | P1 | Directional point spot and area light improvements | 8 h |  |
| 🟠 | 0252 | P1 | Shadow quality presets | 8 h |  |
| 🟠 | 0253 | P1 | Cascaded shadow-map stability | 8 h |  |
| 🟠 | 0254 | P1 | Contact-shadow option | 8 h |  |
| 🔴 | 0255 | P1 | Baked lightmap pipeline | 4 days |  |
| 🟠 | 0256 | P1 | Light probe volume workflow | 8 h |  |
| 🟠 | 0257 | P1 | Reflection probe capture and blending | 8 h |  |
| 🟠 | 0258 | P1 | Screen-space ambient occlusion | 8 h |  |
| 🟠 | 0259 | P2 | Screen-space reflections quality path | 8 h |  |
| 🟠 | 0260 | P2 | Volumetric fog prototype | 8 h |  |
| 🟢 | 0261 | P2 | Height fog and distance fog controls | 8 min | ◐ |
| 🟡 | 0262 | P2 | Sky atmosphere presets | 45 min | ◐ |
| 🟡 | 0263 | P2 | Procedural sky and cloud layers | 1.5 h |  |
| 🟢 | 0264 | P2 | Day-night timeline component | 8 min | ◐ |
| 🟡 | 0265 | P2 | Automatic exposure controls | 45 min | ◐ |
| 🟡 | 0266 | P2 | Bloom and tone mapping presets | 45 min | ◐ |
| 🟢 | 0267 | P2 | Color grading LUT support | 8 min | ◐ |
| 🟡 | 0268 | P2 | Light linking and exclusion groups | 45 min | ◐ |
| 🟡 | 0269 | P2 | Light bake preview and invalidation | 1.5 h |  |
| 🟡 | 0270 | P2 | Emissive material indirect-light contribution research | 1.5 h |  |
| 🟢 | 0271 | P2 | Shadow caster diagnostics | 15 min |  |
| 🟡 | 0272 | P2 | Per-light performance budget | 45 min | ◐ |
| 🟡 | 0273 | P2 | Indoor lighting sample scene | 1.5 h |  |
| 🟢 | 0274 | P2 | Environment profile assets | 15 min |  |
| 🟡 | 0275 | P2 | Lighting regression comparison captures | 45 min | ◐ |

### 12. Visual effects and particles (3.1 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0276 | P1 | GPU particle emitter with CPU fallback | 8 h |  |
| 🟠 | 0277 | P1 | Burst and continuous emission modes | 8 h |  |
| 🟠 | 0278 | P1 | Particle collision with world and terrain | 8 h |  |
| 🟠 | 0279 | P1 | Particle trails and ribbons | 8 h |  |
| 🟡 | 0280 | P1 | Mesh particles | 1.5 h |  |
| 🟡 | 0281 | P1 | Flipbook sprite particles | 45 min | ◐ |
| 🟠 | 0282 | P1 | Curve editors for size speed and color | 8 h |  |
| 🟡 | 0283 | P1 | Deterministic seeded particle playback | 45 min | ◐ |
| 🟡 | 0284 | P2 | Particle LOD and distance culling | 45 min | ◐ |
| 🔴 | 0285 | P2 | VFX graph with reusable blocks | 2.0 work weeks |  |
| 🟢 | 0286 | P2 | Sub-emitter support | 8 min | ◐ |
| 🟡 | 0287 | P2 | Beam and laser effects | 45 min | ◐ |
| 🟡 | 0288 | P2 | Decal burst emitters | 1.5 h |  |
| 🟢 | 0289 | P2 | Wind and turbulence forces | 8 min | ◐ |
| 🟡 | 0290 | P2 | Volumetric fog interaction prototype | 1.5 h |  |
| 🟡 | 0291 | P2 | Particle event hooks for scripts | 45 min | ◐ |
| 🟢 | 0292 | P2 | VFX preview window | 15 min |  |
| 🟡 | 0293 | P2 | VFX timeline authoring | 1.5 h |  |
| 🟡 | 0294 | P2 | Effect pooling for runtime | 45 min | ◐ |
| 🟡 | 0295 | P2 | VFX budget overlay | 45 min | ◐ |
| 🟢 | 0296 | P2 | Asset presets for fire smoke and magic | 8 min | ◐ |
| 🟡 | 0297 | P2 | GPU overflow diagnostics | 45 min | ◐ |
| 🟡 | 0298 | P2 | Lightweight mobile particle profile | 45 min | ◐ |
| 🟢 | 0299 | P2 | VFX capture and thumbnail | 15 min |  |
| 🟡 | 0300 | P2 | VFX sample library | 1.5 h |  |

### 13. Animation and rigging (7.6 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0301 | P1 | Animation clip browser and preview | 8 h |  |
| 🟠 | 0302 | P1 | Skeleton hierarchy visualization | 8 h |  |
| 🟠 | 0303 | P1 | Import humanoid rigs with retargeting | 8 h |  |
| 🟠 | 0304 | P1 | Avatar retarget profile | 8 h |  |
| 🟠 | 0305 | P1 | Animation state machine graph | 8 h |  |
| 🟠 | 0306 | P1 | Blend trees for locomotion | 8 h |  |
| 🟠 | 0307 | P1 | Animation layers and masks | 8 h |  |
| 🟡 | 0308 | P1 | Root-motion extraction and preview | 45 min | ◐ |
| 🟠 | 0309 | P2 | Inverse-kinematics target controls | 8 h |  |
| 🟡 | 0310 | P2 | Foot planting and slope correction | 45 min | ◐ |
| 🟢 | 0311 | P2 | Constraint stack for props and limbs | 15 min |  |
| 🟡 | 0312 | P2 | Timeline markers and events | 45 min | ◐ |
| 🟡 | 0313 | P2 | Animation curve editor | 45 min | ◐ |
| 🟢 | 0314 | P2 | Quaternion-safe rotation editing | 8 min | ◐ |
| 🟡 | 0315 | P2 | Motion capture cleanup tools | 45 min | ◐ |
| 🟡 | 0316 | P2 | Procedural animation hooks for Luau | 1.5 h |  |
| 🟢 | 0317 | P2 | Python animation utilities | 15 min |  |
| 🟡 | 0318 | P2 | Pose library and pose mirroring | 45 min | ◐ |
| 🟡 | 0319 | P2 | Facial blendshape timeline | 1.5 h |  |
| 🟡 | 0320 | P2 | Ragdoll-to-animation blending | 45 min | ◐ |
| 🟢 | 0321 | P2 | Animation compression settings | 8 min | ◐ |
| 🟡 | 0322 | P2 | Runtime animation LOD | 45 min | ◐ |
| 🟡 | 0323 | P2 | Retargeting validation report | 45 min | ◐ |
| 🟢 | 0324 | P2 | Animation preview against live physics | 15 min |  |
| 🟡 | 0325 | P2 | Animation regression corpus | 1.5 h |  |

### 14. Movie Maker and cinematic sequencer (3.8 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0326 | P1 | Dedicated Movie Maker workspace | 8 h |  |
| 🟠 | 0327 | P1 | Track-based timeline with zoom and snapping | 8 h |  |
| 🟠 | 0328 | P1 | Camera cuts and multi-camera tracks | 8 h |  |
| 🟠 | 0329 | P1 | Transform keyframes with curve handles | 8 h |  |
| 🟡 | 0330 | P1 | Lens focal length and aperture animation | 45 min | ◐ |
| 🟡 | 0331 | P1 | Depth-of-field preview | 45 min | ◐ |
| 🟠 | 0332 | P1 | Motion blur quality controls | 8 h |  |
| 🟡 | 0333 | P1 | Lighting and time-of-day tracks | 45 min | ◐ |
| 🟡 | 0334 | P2 | Material and post-effect keyframes | 45 min | ◐ |
| 🟡 | 0335 | P2 | Audio waveform and sync tracks | 1.5 h |  |
| 🟢 | 0336 | P2 | Animation and facial tracks | 15 min |  |
| 🟡 | 0337 | P2 | Scene shot list and storyboard board | 45 min | ◐ |
| 🟡 | 0338 | P2 | Camera rig presets for dolly orbit and crane | 45 min | ◐ |
| 🟢 | 0339 | P2 | Camera shake library with envelopes | 8 min | ◐ |
| 🟡 | 0340 | P2 | Focus target and rack-focus tool | 45 min | ◐ |
| 🟡 | 0341 | P2 | Marker and annotation tracks | 45 min | ◐ |
| 🟢 | 0342 | P2 | Timeline nested sequences | 8 min | ◐ |
| 🟡 | 0343 | P2 | Proxy preview for heavy scenes | 1.5 h |  |
| 🟡 | 0344 | P2 | Render queue with resumable jobs | 45 min | ◐ |
| 🔴 | 0345 | P2 | Offline high-quality renderer evaluation | 4 days |  |
| 🟢 | 0346 | P2 | Hardware ray-tracing capability probe | done | ✅ |
| 🔴 | 0347 | P2 | Path-tracing research branch with fallback | 2.0 work weeks |  |
| 🟡 | 0348 | P2 | EXR PNG and image-sequence export | 45 min | ◐ |
| 🟢 | 0349 | P2 | MP4 export through bundled or documented encoder | 15 min |  |
| 🟡 | 0350 | P2 | Frame-accurate render tests | 45 min | ◐ |

### 15. Audio Maker and sound design tools (4.3 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🔴 | 0351 | P1 | Dedicated audio workspace with multitrack timeline | 2.0 work weeks |  |
| 🟠 | 0352 | P1 | Non-destructive waveform editing | 8 h |  |
| 🟠 | 0353 | P1 | Region split trim fade and slip editing | 8 h |  |
| 🟠 | 0354 | P1 | Envelope and automation lanes | 8 h |  |
| 🟡 | 0355 | P1 | Clip gain and track gain controls | 45 min | ◐ |
| 🟡 | 0356 | P1 | Loop and region playback | 45 min | ◐ |
| 🟠 | 0357 | P1 | Tempo and time-signature grid | 8 h |  |
| 🟡 | 0358 | P1 | Marker and cue regions | 45 min | ◐ |
| 🟡 | 0359 | P2 | Sample slicing and transient detection | 45 min | ◐ |
| 🟠 | 0360 | P2 | Pitch and time-stretch evaluation | 8 h |  |
| 🟢 | 0361 | P2 | Normalize and loudness analysis | 8 min | ◐ |
| 🟠 | 0362 | P2 | Noise gate and denoise offline tools | 8 h |  |
| 🟡 | 0363 | P2 | EQ and compressor effect rack | 45 min | ◐ |
| 🟢 | 0364 | P2 | Reverb delay chorus and saturation presets | 8 min | ◐ |
| 🟠 | 0365 | P2 | Sidechain routing prototype | 8 h |  |
| 🟡 | 0366 | P2 | Bus and send routing | 45 min | ◐ |
| 🟢 | 0367 | P2 | Stereo and surround channel routing | 8 min | ◐ |
| 🟡 | 0368 | P2 | Audio clip crossfade editing | 45 min | ◐ |
| 🟡 | 0369 | P2 | Sample-rate conversion quality report | 45 min | ◐ |
| 🔴 | 0370 | P2 | Plugin host for supported audio effects | 4 days |  |
| 🟠 | 0371 | P2 | MIDI note editor prototype | 8 h |  |
| 🟡 | 0372 | P2 | Sound library browser | 1.5 h |  |
| 🟡 | 0373 | P2 | Batch audio conversion | 1.5 h |  |
| 🟢 | 0374 | P2 | Waveform cache and peaks generation | 8 min | ◐ |
| 🟡 | 0375 | P2 | Audio export stems and mixdown | 45 min | ◐ |

### 16. Runtime audio engine (4.8 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0376 | P1 | Audio mixer bus graph | 8 h |  |
| 🟠 | 0377 | P1 | Per-category volume sliders | 8 h |  |
| 🟠 | 0378 | P1 | 3D positional sound attenuation | 8 h |  |
| 🟠 | 0379 | P1 | Directional cone emitters | 8 h |  |
| 🟠 | 0380 | P1 | Occlusion and obstruction filtering | 8 h |  |
| 🟡 | 0381 | P1 | Audio zones and reverb regions | 45 min | ◐ |
| 🟠 | 0382 | P1 | Doppler shift option | 8 h |  |
| 🟡 | 0383 | P1 | Streaming long music tracks | 1.5 h |  |
| 🔴 | 0384 | P2 | Voice chat architecture evaluation | 4 days |  |
| 🟡 | 0385 | P2 | Audio ducking and sidechain control | 45 min | ◐ |
| 🟢 | 0386 | P2 | Low-latency voice buffer tuning | 8 min | ◐ |
| 🔴 | 0387 | P2 | Spatial audio headphone mode | 2.0 work weeks |  |
| 🔴 | 0388 | P2 | HRTF capability evaluation | 4 days |  |
| 🟢 | 0389 | P2 | Sample-accurate scheduled playback | 8 min | ◐ |
| 🟡 | 0390 | P2 | Audio pooling and voice stealing | 45 min | ◐ |
| 🟡 | 0391 | P2 | Priority-based voice limits | 45 min | ◐ |
| 🟢 | 0392 | P2 | Audio asset compression presets | 8 min | ◐ |
| 🟡 | 0393 | P2 | Runtime audio capture permission model | 1.5 h |  |
| 🟡 | 0394 | P2 | Master limiter to prevent clipping | 45 min | ◐ |
| 🟡 | 0395 | P2 | Loudness meter and peak warning | 45 min | ◐ |
| 🟢 | 0396 | P2 | Music state and snapshot transitions | 8 min | ◐ |
| 🟡 | 0397 | P2 | Audio events exposed to Luau | 1.5 h |  |
| 🟡 | 0398 | P2 | Python audio event API | 1.5 h |  |
| 🟢 | 0399 | P2 | Audio profiler with active voice graph | 8 min | ◐ |
| 🟡 | 0400 | P2 | Audio loopback test harness | 1.5 h |  |

### 17. Luau scripting experience (5.7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0401 | P0 | Full syntax-highlighted Luau editor | done | ✅ |
| 🟠 | 0402 | P0 | Luau autocomplete using reflected APIs | done | ✅ |
| 🟠 | 0403 | P0 | API documentation hover cards | done | ✅ |
| 🟠 | 0404 | P1 | Signature help for function calls | 8 h |  |
| 🟡 | 0405 | P1 | Go to definition and references | 45 min | ◐ |
| 🟡 | 0406 | P1 | Find all script usages | 45 min | ◐ |
| 🟠 | 0407 | P1 | Rename symbols across scripts | 8 h |  |
| 🟡 | 0408 | P1 | Multi-file tabs and split panes | 1.5 h |  |
| 🟡 | 0409 | P2 | Script project tree and filter | 45 min | ◐ |
| 🟡 | 0410 | P2 | Script formatting on save | 45 min | ◐ |
| 🟢 | 0411 | P2 | Configurable lint rules | 8 min | ◐ |
| 🟡 | 0412 | P2 | Static type checking integration | 1.5 h |  |
| 🟡 | 0413 | P2 | Type-aware autocomplete for Instances | 1.5 h |  |
| 🟢 | 0414 | P2 | Module dependency graph | 8 min | ◐ |
| 🟠 | 0415 | P2 | Breakpoints and pause execution | 8 h |  |
| 🟠 | 0416 | P2 | Variable watch window | 8 h |  |
| 🟠 | 0417 | P2 | Call stack and coroutine inspector | 8 h |  |
| 🟡 | 0418 | P2 | Evaluate expression in debug console | 1.5 h |  |
| 🟡 | 0419 | P2 | Per-script CPU and memory budgets | 1.5 h |  |
| 🟡 | 0420 | P2 | Hot-reload diff before applying | 1.5 h |  |
| 🟢 | 0421 | P2 | Script profiler flame graph | 8 min | ◐ |
| 🟡 | 0422 | P2 | Unit-test runner for Luau | 1.5 h |  |
| 🟡 | 0423 | P2 | Script template gallery | 1.5 h |  |
| 🟢 | 0424 | P2 | API compatibility annotations for Roblox imports | 15 min |  |
| 🟡 | 0425 | P2 | Safe script refactoring tools | 45 min | ◐ |

### 18. Python scripting environment (4.1 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0426 | P0 | Enable Python execution in Studio Play consistently | done | ✅ |
| 🟠 | 0427 | P0 | Python script editor with syntax and diagnostics | 4 h | ◐ |
| 🟠 | 0428 | P0 | PocketPy version and capability reporting | done | ✅ |
| 🟠 | 0429 | P1 | Typed world API stubs for editor autocomplete | 8 h |  |
| 🟡 | 0430 | P1 | Python-to-Luau API parity matrix | done | ✅ |
| 🟡 | 0431 | P1 | Cross-language entity handle type | done | ✅ |
| 🟠 | 0432 | P1 | Python event subscriptions with cancellation | 8 h |  |
| 🟡 | 0433 | P1 | Python coroutine and async design review | done | ✅ |
| 🟡 | 0434 | P2 | Deterministic Python execution constraints | done | ✅ |
| 🟡 | 0435 | P2 | Per-script memory and time budget UI | 1.5 h |  |
| 🟢 | 0436 | P2 | Python stack traces linked to source lines | 15 min |  |
| 🟠 | 0437 | P2 | Debugger hooks for Python handlers | 8 h |  |
| 🟡 | 0438 | P2 | Python package allowlist policy | done | ✅ |
| 🟢 | 0439 | P2 | Project-local pure-Python module packaging | 8 min | ◐ |
| 🟡 | 0440 | P2 | Dependency lock manifest for allowed packages | done | ✅ |
| 🟡 | 0441 | P2 | Python formatter integration where feasible | 1.5 h |  |
| 🟡 | 0442 | P2 | Python unit-test harness | 1.5 h |  |
| 🟡 | 0443 | P2 | Python type hint documentation generation | 1.5 h |  |
| 🟡 | 0444 | P2 | Luau-to-Python data conversion rules | 45 min | ◐ |
| 🟡 | 0445 | P2 | Shared event bus between script languages | 45 min | ◐ |
| 🟢 | 0446 | P2 | Cross-language call tracing | 8 min | ◐ |
| 🟡 | 0447 | P2 | Python script hot reload | 1.5 h |  |
| 🟡 | 0448 | P2 | Python sandbox escape regression tests | done | ✅ |
| 🟢 | 0449 | P2 | Python API compatibility versioning | done | ✅ |
| 🟡 | 0450 | P2 | Dual-language starter project | 1.5 h |  |

### 19. Rust SDK and polyglot architecture (3.8 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0451 | P0 | Document which languages are game scripts versus plugins | done | ✅ |
| 🟠 | 0452 | P0 | Formal language runtime capability matrix | done | ✅ |
| 🟠 | 0453 | P0 | Stable C ABI plugin contract versioning | done | ✅ |
| 🟠 | 0454 | P1 | Rust SDK examples for editor panels | 8 h |  |
| 🟡 | 0455 | P1 | Rust SDK safe wrapper audit | 45 min | ◐ |
| 🟡 | 0456 | P1 | Rust plugin build templates for Windows Linux and macOS | 45 min | ◐ |
| 🟠 | 0457 | P1 | Cargo feature selection guide | 8 h |  |
| 🟡 | 0458 | P1 | Plugin ABI conformance test binary | 1.5 h |  |
| 🟡 | 0459 | P2 | Ownership-safe scene handle wrapper | 1.5 h |  |
| 🟡 | 0460 | P2 | Typed entity IDs with stale-handle checks | 1.5 h |  |
| 🟢 | 0461 | P2 | Rust panic containment at ABI boundary | done | ✅ |
| 🟡 | 0462 | P2 | Versioned API structs with size fields | done | ✅ |
| 🟠 | 0463 | P2 | Rust plugin hot reload support design | 8 h |  |
| 🟢 | 0464 | P2 | Rust plugin sandbox capability manifest | done | ✅ |
| 🟡 | 0465 | P2 | Rust plugin diagnostics and logging macros | 1.5 h |  |
| 🟡 | 0466 | P2 | Shared JSON schema for cross-language messages | done | ✅ |
| 🟢 | 0467 | P2 | Language-agnostic event bus | 8 min | ◐ |
| 🟡 | 0468 | P2 | Language-neutral asset and scene schema | 1.5 h |  |
| 🟡 | 0469 | P2 | Generated Rust bindings from API schema | 45 min | ◐ |
| 🟡 | 0470 | P2 | Generated Python API stubs from API schema | 45 min | ◐ |
| 🟢 | 0471 | P2 | Clear distinction between native and sandboxed plugins | done | ✅ |
| 🟡 | 0472 | P2 | Polyglot debugging correlation IDs | 45 min | ◐ |
| 🟡 | 0473 | P2 | C++ Rust Python Luau API conformance suite | 45 min | ◐ |
| 🟢 | 0474 | P2 | Plugin API deprecation policy | done | ✅ |
| 🟡 | 0475 | P2 | Example plugin proving end-to-end workflow | done | ✅ |

### 20. Debugging and developer tools (6.3 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0476 | P0 | Unified debug console with filtering | done | ✅ |
| 🟠 | 0477 | P0 | Structured logs with categories and severity | done | ✅ |
| 🟠 | 0478 | P0 | Crash report with consent and redaction | 4 h | ◐ |
| 🟠 | 0479 | P1 | Crash dump symbol upload workflow | 8 h | ⛔ |
| 🟠 | 0480 | P1 | Live entity/component inspector | 8 h |  |
| 🟡 | 0481 | P1 | Runtime variable watch window | 1.5 h |  |
| 🟠 | 0482 | P1 | Breakpoint support across script types | 8 h |  |
| 🟡 | 0483 | P1 | Frame-by-frame simulation stepping | 1.5 h |  |
| 🟡 | 0484 | P2 | Physics contact visualization | 1.5 h |  |
| 🟡 | 0485 | P2 | Network replication visualization | 1.5 h |  |
| 🟢 | 0486 | P2 | Request and response inspector for backend calls | 15 min | ⛔ |
| 🟡 | 0487 | P2 | Asset loading timeline | 1.5 h |  |
| 🟡 | 0488 | P2 | Shader compilation error grouping | 1.5 h |  |
| 🟢 | 0489 | P2 | Import warning aggregation | 8 min | ◐ |
| 🟡 | 0490 | P2 | Copyable diagnostics bundle | 1.5 h |  |
| 🟡 | 0491 | P2 | Searchable project-wide logs | 1.5 h |  |
| 🟢 | 0492 | P2 | Error panel with source links | 15 min |  |
| 🟡 | 0493 | P2 | Recoverable error notification queue | 1.5 h |  |
| 🟡 | 0494 | P2 | Built-in command console with permission checks | 1.5 h |  |
| 🟠 | 0495 | P2 | Remote server log tail | 8 h | ⛔ |
| 🟢 | 0496 | P2 | Server/client correlated logs | 15 min |  |
| 🟠 | 0497 | P2 | Reproducible bug report recorder | 8 h |  |
| 🟡 | 0498 | P2 | Debug-only simulation overlays | 1.5 h |  |
| 🟢 | 0499 | P2 | Diagnostic mode toggles | 8 min | ◐ |
| 🟡 | 0500 | P2 | One-click environment report export | 1.5 h |  |

### 21. Roblox migration and compatibility bridge (2.1 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0501 | P0 | Compatibility score by class and API | done | ✅ |
| 🟠 | 0502 | P0 | Test corpus for common game genres | done | ✅ |
| 🟠 | 0503 | P0 | Binary RBXL and RBXM parsing hardening | 4 h | ◐ |
| 🟠 | 0504 | P1 | Round-trip preservation of unknown properties | 8 h |  |
| 🟡 | 0505 | P1 | API shims for common services | 1.5 h |  |
| 🟡 | 0506 | P1 | GUI constraints and layout compatibility | 1.5 h |  |
| 🟠 | 0507 | P1 | UDim and anchor-point edge-case tests | 8 h |  |
| 🟡 | 0508 | P1 | MeshPart and texture preservation | 1.5 h |  |
| 🟠 | 0509 | P2 | Terrain and voxel import support | 8 h |  |
| 🔴 | 0510 | P2 | Animation and humanoid behavior parity | 4 days |  |
| 🟢 | 0511 | P2 | Sound and music mapping report | 15 min |  |
| 🟡 | 0512 | P2 | Lighting and atmosphere mapping | 1.5 h |  |
| 🟠 | 0513 | P2 | Constraints and attachments translation | 8 h |  |
| 🟢 | 0514 | P2 | CollectionService tag preservation | 15 min |  |
| 🟡 | 0515 | P2 | RemoteEvent network semantic parity tests | 1.5 h |  |
| 🟡 | 0516 | P2 | DataStore emulator with clear migration notes | 1.5 h |  |
| 🟢 | 0517 | P2 | Unsupported API call source report | 15 min |  |
| 🟠 | 0518 | P2 | Script rewrite suggestions for unsupported APIs | 8 h |  |
| 🟠 | 0519 | P2 | Import preview with before-after screenshots | 8 h |  |
| 🟡 | 0520 | P2 | Batch place conversion CLI | 1.5 h |  |
| 🟢 | 0521 | P2 | Asset-license provenance report | 8 min | ◐ |
| 🟡 | 0522 | P2 | Roblox-version metadata handling | 1.5 h |  |
| 🟡 | 0523 | P2 | Safe handling of malformed place files | done | ✅ |
| 🟢 | 0524 | P2 | Import regression bundle from licensed test places | 15 min |  |
| 🟡 | 0525 | P2 | Compatibility report export for marketing claims | 1.5 h |  |

### 22. Kronos Player and end-user experience (7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0526 | P0 | Instant project/game launch page | 8 h |  |
| 🟠 | 0527 | P0 | Recent and favorite games | 8 h |  |
| 🟠 | 0528 | P0 | Per-game update notes | 8 h | ⛔ |
| 🟠 | 0529 | P1 | Download resume and integrity verification | 8 h |  |
| 🟠 | 0530 | P1 | Controller-friendly navigation | 8 h |  |
| 🟠 | 0531 | P1 | Gamepad-first UI navigation | 8 h |  |
| 🟠 | 0532 | P1 | In-game graphics settings | 8 h |  |
| 🟡 | 0533 | P1 | Keybind remapping menu | 45 min | ◐ |
| 🟡 | 0534 | P2 | Per-game audio settings | 45 min | ◐ |
| 🟡 | 0535 | P2 | Accessible subtitles and captions | 45 min | ◐ |
| 🟢 | 0536 | P2 | Avatar customization saved across games | 15 min |  |
| 🟡 | 0537 | P2 | Avatar preview performance mode | 1.5 h |  |
| 🟡 | 0538 | P2 | Game discovery categories and tags | 45 min | ◐ |
| 🟢 | 0539 | P2 | Content reporting with reason codes | 8 min | ◐ |
| 🟡 | 0540 | P2 | Local parental and privacy settings | 45 min | ◐ |
| 🟡 | 0541 | P2 | Clear offline mode behavior | 1.5 h |  |
| 🟢 | 0542 | P2 | Rejoin last server action | 8 min | ◐ |
| 🟡 | 0543 | P2 | Server region and ping display | 45 min | ◐ |
| 🟡 | 0544 | P2 | Join friend flow with permission controls | 45 min | ◐ |
| 🟡 | 0545 | P2 | Crash-safe game return to home | 1.5 h |  |
| 🟢 | 0546 | P2 | Game-specific save-data status feedback | 8 min | ◐ |
| 🟡 | 0547 | P2 | Downloaded content cache manager | 45 min | ◐ |
| 🟡 | 0548 | P2 | Player update rollback safety | 1.5 h |  |
| 🟢 | 0549 | P2 | First-launch performance telemetry opt-in | 8 min | ◐ |
| 🟡 | 0550 | P2 | User-facing privacy and safety center | 1.5 h |  |

### 23. Build packaging and release engineering (2.6 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0551 | P0 | Single source of truth for version numbers | done | ✅ |
| 🟠 | 0552 | P0 | Reproducible Windows build pipeline | 4 h | ◐ |
| 🟠 | 0553 | P0 | Reproducible Linux build pipeline | 4 h | ◐ |
| 🔴 | 0554 | P1 | macOS build feasibility spike | 4 days |  |
| 🟡 | 0555 | P1 | Clean-machine installer smoke tests | 1.5 h |  |
| 🟡 | 0556 | P1 | Portable archive validation | 1.5 h |  |
| 🟠 | 0557 | P1 | Dependency manifest per release | 8 h |  |
| 🟠 | 0558 | P1 | Signed release artifact plan | 8 h | ⛔ |
| 🟡 | 0559 | P2 | Update channel for stable preview and nightly | 1.5 h |  |
| 🔴 | 0560 | P2 | Delta updates evaluation | 4 days |  |
| 🟢 | 0561 | P2 | Download integrity hashes and signatures | 15 min | ⛔ |
| 🟠 | 0562 | P2 | Rollback after failed update | 8 h |  |
| 🟡 | 0563 | P2 | Player Studio version compatibility checks | 45 min | ◐ |
| 🟢 | 0564 | P2 | Project packaging manifest | done | ✅ |
| 🟡 | 0565 | P2 | Exclude source files and editor-only assets on publish | 45 min | ◐ |
| 🟡 | 0566 | P2 | Asset compression profile per target | 1.5 h |  |
| 🟢 | 0567 | P2 | Build cache reuse | 15 min |  |
| 🟡 | 0568 | P2 | Build progress and cancel controls | 1.5 h |  |
| 🟡 | 0569 | P2 | Build error summary with source paths | 1.5 h |  |
| 🟡 | 0570 | P2 | Headless command-line build mode | 1.5 h |  |
| 🟢 | 0571 | P2 | CI-generated compatibility report | 8 min | ◐ |
| 🟡 | 0572 | P2 | Release checklist automation | done | ✅ |
| 🟡 | 0573 | P2 | Changelog generation from labels | 45 min | ◐ |
| 🟢 | 0574 | P2 | Safe prerelease migration backups | 8 min | ◐ |
| 🟡 | 0575 | P2 | Installer uninstall cleanup and preserve-user-data choice | 1.5 h |  |

### 24. Multiplayer runtime and netcode (7.5 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0576 | P1 | Authoritative server simulation mode | 8 h |  |
| 🟠 | 0577 | P1 | State replication priority classes | 8 h |  |
| 🟠 | 0578 | P1 | Delta-compressed component replication | 8 h |  |
| 🟠 | 0579 | P1 | Interest management by spatial cell | 8 h |  |
| 🟡 | 0580 | P1 | Entity ownership and authority rules | done | ✅ |
| 🟠 | 0581 | P1 | Client prediction for character movement | 8 h |  |
| 🟠 | 0582 | P1 | Server reconciliation diagnostics | 8 h |  |
| 🟠 | 0583 | P1 | Rollback netcode integration tests | 8 h |  |
| 🟡 | 0584 | P2 | Deterministic tick scheduler validation | 45 min | ◐ |
| 🟡 | 0585 | P2 | Network interpolation buffers | 45 min | ◐ |
| 🟠 | 0586 | P2 | Lag compensation for hitscan prototype | 8 h |  |
| 🟡 | 0587 | P2 | Reliable ordered and unreliable message channels | 45 min | ◐ |
| 🟡 | 0588 | P2 | Bandwidth budget per connection | 45 min | ◐ |
| 🟢 | 0589 | P2 | Packet loss and latency simulator | done | ✅ |
| 🟡 | 0590 | P2 | Network congestion and backoff policy | 45 min | ◐ |
| 🟡 | 0591 | P2 | Clock synchronization and drift measurement | 45 min | ◐ |
| 🟢 | 0592 | P2 | Late join world snapshot recovery | 15 min |  |
| 🟡 | 0593 | P2 | Networked physics ownership transfer | 45 min | ◐ |
| 🟡 | 0594 | P2 | Disconnect and reconnect state recovery | 45 min | ◐ |
| 🟡 | 0595 | P2 | Replication schema compatibility checks | 45 min | ◐ |
| 🟢 | 0596 | P2 | Secure remote-call rate limits | 15 min |  |
| 🟡 | 0597 | P2 | Network fuzzing harness | 1.5 h |  |
| 🟡 | 0598 | P2 | Packet capture redaction and replay | 45 min | ◐ |
| 🟢 | 0599 | P2 | 2 to 16-player reference sample | 15 min |  |
| 🟡 | 0600 | P2 | Server-client desync detector | 45 min | ◐ |

### 25. Cloud servers and matchmaking (2.3 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0601 | P1 | Local dedicated-server launcher wizard | 8 h |  |
| 🟠 | 0602 | P1 | Server process supervisor with graceful restart | 8 h |  |
| 🟠 | 0603 | P1 | Container image for dedicated server | 8 h |  |
| 🟠 | 0604 | P1 | Docker compose dev deployment | 8 h |  |
| 🟡 | 0605 | P1 | Server health and readiness endpoints | 45 min | ◐ |
| 🔴 | 0606 | P1 | Autoscaling policy prototype with limits | 4 days | ⛔ |
| 🟠 | 0607 | P1 | Region-aware server selection | 8 h | ⛔ |
| 🟠 | 0608 | P1 | Party-aware matchmaking | 8 h | ⛔ |
| 🟡 | 0609 | P2 | Skill or latency matchmaking plugin API | 1.5 h | ⛔ |
| 🟡 | 0610 | P2 | Capacity-aware queue with timeout | 1.5 h | ⛔ |
| 🟢 | 0611 | P2 | Join tokens with expiry and single use | 8 min | ◐ |
| 🟠 | 0612 | P2 | Server drain and rolling updates | 8 h | ⛔ |
| 🟡 | 0613 | P2 | Crash restart budget and alerting | 1.5 h | ⛔ |
| 🟢 | 0614 | P2 | Per-instance CPU memory and player metrics | 15 min | ⛔ |
| 🟢 | 0615 | P2 | Cost estimator before cloud deployment | 15 min | ⛔ |
| 🟢 | 0616 | P2 | Bring-your-own-host deployment docs | done | ✅ |
| 🟢 | 0617 | P2 | Kubernetes deployment evaluation only after container MVP | done | ✅ |
| 🟡 | 0618 | P2 | Secrets injection without committing credentials | done | ✅ |
| 🟡 | 0619 | P2 | Region failover behavior | 1.5 h | ⛔ |
| 🟡 | 0620 | P2 | Server version compatibility gate | 45 min | ◐ |
| 🟢 | 0621 | P2 | Matchmaking load-test simulator | 15 min | ⛔ |
| 🟡 | 0622 | P2 | Queue visibility and estimated wait | 1.5 h | ⛔ |
| 🟡 | 0623 | P2 | Reserved private servers | 1.5 h | ⛔ |
| 🟠 | 0624 | P2 | Server browser for community-hosted games | 8 h | ⛔ |
| 🟡 | 0625 | P2 | Local-first deploy preview without cloud charges | done | ✅ |

### 26. Backend and developer portal (7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0626 | P1 | Versioned public backend API | 8 h | ⛔ |
| 🟠 | 0627 | P1 | OpenAPI schema and generated clients | 8 h | ⛔ |
| 🟠 | 0628 | P1 | Developer portal login hardening | 8 h | ⛔ |
| 🟠 | 0629 | P1 | Per-game environment settings | 8 h | ⛔ |
| 🟡 | 0630 | P1 | Staging and production environment separation | 1.5 h | ⛔ |
| 🟠 | 0631 | P1 | Publish API with resumable uploads | 8 h | ⛔ |
| 🟠 | 0632 | P1 | Release channel management | 8 h | ⛔ |
| 🟡 | 0633 | P1 | API rate-limit dashboard | 1.5 h | ⛔ |
| 🟡 | 0634 | P2 | Request tracing with correlation IDs | 1.5 h | ⛔ |
| 🟡 | 0635 | P2 | Backend migration rollback plan | 1.5 h | ⛔ |
| 🟢 | 0636 | P2 | Database schema migration tests | 15 min | ⛔ |
| 🟡 | 0637 | P2 | Service health dashboard | 1.5 h | ⛔ |
| 🟡 | 0638 | P2 | Object storage lifecycle policy | 1.5 h | ⛔ |
| 🟢 | 0639 | P2 | Asset upload virus and type checks | 15 min | ⛔ |
| 🟡 | 0640 | P2 | Per-project API credentials | 1.5 h | ⛔ |
| 🟡 | 0641 | P2 | Credential rotation flow | 1.5 h | ⛔ |
| 🟢 | 0642 | P2 | Webhook signature verification | 15 min | ⛔ |
| 🟡 | 0643 | P2 | Idempotency keys for publish requests | 1.5 h | ⛔ |
| 🟡 | 0644 | P2 | Audit log for creator actions | 1.5 h | ⛔ |
| 🟡 | 0645 | P2 | Backend outage status page | 1.5 h | ⛔ |
| 🟢 | 0646 | P2 | Local backend mock for tests | 15 min | ⛔ |
| 🟡 | 0647 | P2 | Developer portal accessibility audit | 1.5 h | ⛔ |
| 🟡 | 0648 | P2 | Project ownership transfer workflow | 1.5 h | ⛔ |
| 🟢 | 0649 | P2 | Export user data package | 15 min | ⛔ |
| 🟡 | 0650 | P2 | Delete project and account flow with confirmation | 1.5 h | ⛔ |

### 27. Persistence, economy and commerce (7.7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0651 | P1 | Versioned DataStore emulator | 8 h | ⛔ |
| 🟠 | 0652 | P1 | Atomic update operations and conflict tests | 8 h | ⛔ |
| 🟠 | 0653 | P1 | Per-key rate limits with backoff | 8 h | ⛔ |
| 🟠 | 0654 | P1 | Persistent data schema migrations | 8 h | ⛔ |
| 🟡 | 0655 | P1 | Backup and restore per game | 1.5 h | ⛔ |
| 🟠 | 0656 | P1 | Cross-server session locking | 8 h | ⛔ |
| 🟠 | 0657 | P1 | Server-side receipt validation interface | 8 h | ⛔ |
| 🟡 | 0658 | P1 | Developer-defined currency service template | 1.5 h | ⛔ |
| 🟡 | 0659 | P2 | Inventory API with unique item IDs | 1.5 h | ⛔ |
| 🟡 | 0660 | P2 | Transaction idempotency protection | 1.5 h | ⛔ |
| 🟢 | 0661 | P2 | Audit ledger for item grants and purchases | 15 min | ⛔ |
| 🟡 | 0662 | P2 | Refund and reversal event handling | 1.5 h | ⛔ |
| 🟡 | 0663 | P2 | Fraud and duplication detection hooks | 1.5 h | ⛔ |
| 🟢 | 0664 | P2 | Entitlement checks that never trust the client | 15 min | ⛔ |
| 🟡 | 0665 | P2 | Transparent platform fee policy controls | 1.5 h | ⛔ |
| 🟠 | 0666 | P2 | BYO payment-provider adapter interface | 8 h | ⛔ |
| 🟢 | 0667 | P2 | No payment details stored in the game client | 15 min | ⛔ |
| 🟡 | 0668 | P2 | Test-mode purchase simulator | 1.5 h | ⛔ |
| 🟡 | 0669 | P2 | Regional tax and compliance checklist | 1.5 h | ⛔ |
| 🟡 | 0670 | P2 | Economy balance simulation spreadsheet export | 1.5 h | ⛔ |
| 🟢 | 0671 | P2 | Developer analytics for purchases | 15 min | ⛔ |
| 🟡 | 0672 | P2 | User purchase history controls | 1.5 h | ⛔ |
| 🟡 | 0673 | P2 | Gift and trade permission system | 1.5 h | ⛔ |
| 🟢 | 0674 | P2 | Spending limits and parental controls evaluation | 15 min | ⛔ |
| 🟡 | 0675 | P2 | Marketplace item ownership validation | 1.5 h | ⛔ |

### 28. Realtime collaboration and Team Create (8.5 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🔴 | 0676 | P1 | Shared edit session with explicit invite | 2.0 work weeks |  |
| 🔴 | 0677 | P1 | Authoritative collaboration session service | 2.0 work weeks |  |
| 🟠 | 0678 | P1 | Presence avatars and cursor positions | 8 h |  |
| 🟠 | 0679 | P1 | Selection outlines for collaborators | 8 h |  |
| 🔴 | 0680 | P1 | Live scene transform synchronization | 4 days |  |
| 🔴 | 0681 | P1 | Conflict handling for simultaneous property edits | 4 days |  |
| 🔴 | 0682 | P1 | CRDT feasibility prototype for selected data types | 2.0 work weeks |  |
| 🟡 | 0683 | P1 | Script editing collaboration design | 1.5 h |  |
| 🟡 | 0684 | P2 | Conflict-aware script merge UI | 1.5 h |  |
| 🟡 | 0685 | P2 | Lock and lease policy for high-risk edits | 1.5 h |  |
| 🟢 | 0686 | P2 | Shared undo history semantics | 8 min | ◐ |
| 🟠 | 0687 | P2 | Session reconnect and snapshot recovery | 8 h |  |
| 🟡 | 0688 | P2 | Permission roles for owner editor viewer | 1.5 h |  |
| 🟢 | 0689 | P2 | Invite revocation and access expiry | 8 min | ◐ |
| 🟡 | 0690 | P2 | Collaborative asset import status | 1.5 h |  |
| 🟡 | 0691 | P2 | Voice or text session chat design | 1.5 h |  |
| 🟢 | 0692 | P2 | Follow collaborator camera mode | 8 min | ◐ |
| 🟡 | 0693 | P2 | Change attribution timeline | 1.5 h |  |
| 🟡 | 0694 | P2 | Session audit log | 1.5 h |  |
| 🟡 | 0695 | P2 | Offline edits conflict review | 1.5 h |  |
| 🟢 | 0696 | P2 | Bandwidth and latency collaboration simulator | 8 min | ◐ |
| 🟡 | 0697 | P2 | Large-scene sync benchmarks | 1.5 h |  |
| 🟡 | 0698 | P2 | Multi-user test harness | 1.5 h |  |
| 🟢 | 0699 | P2 | Collaboration disabled fallback | done | ✅ |
| 🟡 | 0700 | P2 | Local-hosted collaboration option | 1.5 h |  |

### 29. Plugin architecture and developer API (6.7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0701 | P1 | Formal plugin lifecycle documentation | 8 h |  |
| 🟠 | 0702 | P1 | Manifest schema with permissions | 8 h |  |
| 🟠 | 0703 | P1 | Plugin API capability negotiation | 8 h |  |
| 🟠 | 0704 | P1 | Signed plugin package metadata | 8 h | ⛔ |
| 🟠 | 0705 | P1 | Sandboxed plugin process health watchdog | 8 h |  |
| 🟡 | 0706 | P1 | Per-plugin CPU and memory telemetry | 1.5 h |  |
| 🟠 | 0707 | P1 | Crash isolation and automatic disable | 8 h |  |
| 🟡 | 0708 | P1 | Plugin compatibility testing across versions | 1.5 h |  |
| 🟡 | 0709 | P2 | Plugin dependency resolution | 45 min | ◐ |
| 🟡 | 0710 | P2 | Plugin configuration persistence | 45 min | ◐ |
| 🟢 | 0711 | P2 | Plugin command registration API | 15 min |  |
| 🟡 | 0712 | P2 | Menu and toolbar extension points | 1.5 h |  |
| 🟡 | 0713 | P2 | Inspector custom property editors | 1.5 h |  |
| 🟢 | 0714 | P2 | Scene gizmo extension API | 15 min |  |
| 🟡 | 0715 | P2 | Asset importer extension API | 1.5 h |  |
| 🟡 | 0716 | P2 | Exporter extension API | 1.5 h |  |
| 🟢 | 0717 | P2 | Custom viewport render-pass API | 15 min |  |
| 🟡 | 0718 | P2 | Script language extension API | 1.5 h |  |
| 🟡 | 0719 | P2 | Project validator plugin API | 1.5 h |  |
| 🟡 | 0720 | P2 | Plugin logging and diagnostic bundle | 1.5 h |  |
| 🟢 | 0721 | P2 | Plugin API sample gallery | done | ✅ |
| 🟡 | 0722 | P2 | Safe plugin update and rollback | 45 min | ◐ |
| 🟡 | 0723 | P2 | Plugin permission review UI | 45 min | ◐ |
| 🟢 | 0724 | P2 | API deprecation and migration guide | done | ✅ |
| 🟡 | 0725 | P2 | Headless plugin test harness | 1.5 h |  |

### 30. Plugin marketplace and ecosystem (6.4 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0726 | P1 | Curated featured plugin directory | 8 h | ⛔ |
| 🟠 | 0727 | P1 | Signed plugin catalog metadata | 8 h | ⛔ |
| 🟠 | 0728 | P1 | Search plugins by task and compatibility | 8 h | ⛔ |
| 🟠 | 0729 | P1 | Plugin reviews with abuse controls | 8 h | ⛔ |
| 🟡 | 0730 | P1 | Verified author badges criteria | 1.5 h | ⛔ |
| 🟡 | 0731 | P1 | Plugin version pinning per project | 1.5 h | ⛔ |
| 🟠 | 0732 | P1 | Dependency compatibility warnings | 8 h | ⛔ |
| 🟠 | 0733 | P1 | Private team plugin registry | 8 h | ⛔ |
| 🟡 | 0734 | P2 | One-click install update and remove | 1.5 h | ⛔ |
| 🟡 | 0735 | P2 | Plugin uninstall cleanup preview | 1.5 h | ⛔ |
| 🟢 | 0736 | P2 | Report malicious plugin workflow | 15 min | ⛔ |
| 🟢 | 0737 | P2 | Vulnerability disclosure channel | 15 min | ⛔ |
| 🟢 | 0738 | P2 | Free and paid plugin policy decision | 15 min | ⛔ |
| 🟢 | 0739 | P2 | Revenue split transparency if paid plugins launch | 15 min | ⛔ |
| 🟡 | 0740 | P2 | Plugin sandbox threat model review | 1.5 h | ⛔ |
| 🟡 | 0741 | P2 | Automated package scanning | 1.5 h | ⛔ |
| 🟢 | 0742 | P2 | Plugin demo projects | 15 min | ⛔ |
| 🟢 | 0743 | P2 | Plugin changelog and screenshots | 15 min | ⛔ |
| 🟡 | 0744 | P2 | Plugin download integrity verification | 1.5 h | ⛔ |
| 🟢 | 0745 | P2 | Deprecation notices for abandoned plugins | 15 min | ⛔ |
| 🟢 | 0746 | P2 | Plugin analytics opt-in | 15 min | ⛔ |
| 🟡 | 0747 | P2 | Offline plugin installation package | 1.5 h | ⛔ |
| 🟢 | 0748 | P2 | Marketplace moderation guidelines | 15 min | ⛔ |
| 🟢 | 0749 | P2 | Plugin licensing metadata display | 15 min | ⛔ |
| 🟡 | 0750 | P2 | Export and import plugin sets | 1.5 h | ⛔ |

### 31. Templates, samples and documentation (5.5 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0751 | P1 | Five-minute first game tutorial | 8 h |  |
| 🟠 | 0752 | P1 | Guided obby template | 8 h |  |
| 🟠 | 0753 | P1 | Tycoon mechanics sample | 8 h |  |
| 🟠 | 0754 | P1 | Multiplayer arena sample | 8 h |  |
| 🟡 | 0755 | P1 | Platformer starter project | 1.5 h |  |
| 🟡 | 0756 | P1 | Narrative game template | 1.5 h |  |
| 🟠 | 0757 | P1 | Racing game template | 8 h |  |
| 🟡 | 0758 | P1 | Open-world streaming sample | 1.5 h |  |
| 🟢 | 0759 | P2 | Audio-driven rhythm sample | 15 min |  |
| 🟢 | 0760 | P2 | Cinematic storyboard sample | 15 min |  |
| 🟢 | 0761 | P2 | Terrain sculpting sample | 15 min |  |
| 🟢 | 0762 | P2 | Python game scripting sample | 15 min |  |
| 🟢 | 0763 | P2 | Rust Studio plugin sample | 15 min |  |
| 🟡 | 0764 | P2 | C ABI C plugin sample | 1.5 h |  |
| 🟢 | 0765 | P2 | Cross-language Luau Python sample | 15 min |  |
| 🟡 | 0766 | P2 | API reference generated from reflection | 1.5 h |  |
| 🟢 | 0767 | P2 | Searchable offline docs | 8 min | ◐ |
| 🟡 | 0768 | P2 | Versioned tutorial matching release | 1.5 h |  |
| 🟢 | 0769 | P2 | Troubleshooting decision tree | done | ✅ |
| 🟢 | 0770 | P2 | Known limitations page with severity | done | ✅ |
| 🟢 | 0771 | P2 | Feature maturity labels stable preview experimental | done | ✅ |
| 🟡 | 0772 | P2 | Sample project automated smoke tests | 1.5 h |  |
| 🟡 | 0773 | P2 | Learning path for non-programmers | 1.5 h |  |
| 🟢 | 0774 | P2 | Learning path for programmers | done | ✅ |
| 🟢 | 0775 | P2 | Sample asset licensing manifest | done | ✅ |

### 32. AI-assisted creator workflows (2.8 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0776 | P1 | Opt-in explain selection assistant | 8 h | ⛔ |
| 🟠 | 0777 | P1 | Generate script from described behavior with preview | 8 h | ⛔ |
| 🟠 | 0778 | P1 | Explain existing script selection | 8 h | ⛔ |
| 🟠 | 0779 | P1 | Script error explanation linked to line | 8 h | ⛔ |
| 🟡 | 0780 | P1 | AI-generated code tests before insertion | 1.5 h | ⛔ |
| 🔴 | 0781 | P1 | Prompt-to-prototype scene generation | 4 days | ⛔ |
| 🟠 | 0782 | P1 | Procedural level seed suggestions | 8 h | ⛔ |
| 🔴 | 0783 | P1 | Material graph generation from brief | 4 days | ⛔ |
| 🟡 | 0784 | P2 | AI-assisted asset tagging local-first option | 1.5 h | ⛔ |
| 🟡 | 0785 | P2 | Natural-language scene search | 1.5 h | ⛔ |
| 🟢 | 0786 | P2 | Documentation question answering grounded in current API | 15 min | ⛔ |
| 🟡 | 0787 | P2 | AI diff viewer before accepting changes | 1.5 h | ⛔ |
| 🟡 | 0788 | P2 | Never silently overwrite user code | 1.5 h | ⛔ |
| 🟢 | 0789 | P2 | Provider adapter architecture | 15 min | ⛔ |
| 🟡 | 0790 | P2 | Local model endpoint option | 1.5 h | ⛔ |
| 🟡 | 0791 | P2 | Cost and token usage meter | 1.5 h | ⛔ |
| 🟢 | 0792 | P2 | Redact secrets from prompts | 15 min | ⛔ |
| 🟡 | 0793 | P2 | AI feature opt-out and disable switch | 1.5 h | ⛔ |
| 🟡 | 0794 | P2 | Generated content provenance metadata | 1.5 h | ⛔ |
| 🟡 | 0795 | P2 | Asset rights and safety warnings | 1.5 h | ⛔ |
| 🟢 | 0796 | P2 | Deterministic fallback when service unavailable | 15 min | ⛔ |
| 🟡 | 0797 | P2 | Context window budgeting | 1.5 h | ⛔ |
| 🟡 | 0798 | P2 | Prompt injection threat model for imported content | 1.5 h | ⛔ |
| 🟢 | 0799 | P2 | Benchmark task success on fixtures | 15 min | ⛔ |
| 🟡 | 0800 | P2 | AI feature telemetry opt-in only | 1.5 h | ⛔ |

### 33. Quality assurance and automation (5 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0801 | P0 | Unit test coverage map by subsystem | 8 h |  |
| 🟠 | 0802 | P0 | Core engine smoke test suite | done | ✅ |
| 🟠 | 0803 | P0 | Fresh project creation smoke test | 4 h | ◐ |
| 🟠 | 0804 | P1 | Player launch and clean exit test | 8 h |  |
| 🟡 | 0805 | P1 | Studio launch and clean exit test | 1.5 h |  |
| 🟡 | 0806 | P1 | Import corpus regression runner | 1.5 h |  |
| 🟠 | 0807 | P1 | Screenshot comparison for editor UI | 8 h |  |
| 🟡 | 0808 | P1 | GPU renderer golden-image tests | 1.5 h |  |
| 🟡 | 0809 | P2 | Physics behavior scenario suite | 1.5 h |  |
| 🟡 | 0810 | P2 | Network simulation integration suite | 1.5 h |  |
| 🟢 | 0811 | P2 | Script language conformance tests | 15 min |  |
| 🟡 | 0812 | P2 | Plugin crash isolation tests | 1.5 h |  |
| 🟡 | 0813 | P2 | Save reopen identity tests | 1.5 h |  |
| 🟢 | 0814 | P2 | Data migration backward-compat tests | 15 min |  |
| 🟡 | 0815 | P2 | Fuzz test asset parsers | 45 min | ◐ |
| 🟡 | 0816 | P2 | Fuzz test project manifest loader | 1.5 h |  |
| 🟢 | 0817 | P2 | Memory leak checks on exit | 15 min |  |
| 🟡 | 0818 | P2 | Sanitizer CI builds | 1.5 h |  |
| 🟡 | 0819 | P2 | Windows clean-machine smoke tests | 1.5 h |  |
| 🟡 | 0820 | P2 | Linux clean-machine smoke tests | 1.5 h |  |
| 🟢 | 0821 | P2 | Long-running soak test | 15 min |  |
| 🟡 | 0822 | P2 | Automated packaged game launch test | 1.5 h |  |
| 🟡 | 0823 | P2 | Crash reproduction capture format | 45 min | ◐ |
| 🟢 | 0824 | P2 | Release-blocking severity policy | done | ✅ |
| 🟡 | 0825 | P2 | Test flake tracking dashboard | 45 min | ◐ |

### 34. Security, permissions and safety (3.9 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0826 | P0 | Threat model for Player and Studio | done | ✅ |
| 🟠 | 0827 | P0 | Script sandbox escape regression suite | done | ✅ |
| 🟠 | 0828 | P0 | Least-privilege plugin capability profiles | 4 h | ◐ |
| 🟠 | 0829 | P1 | Signed release artifacts plan | 8 h | ⛔ |
| 🟠 | 0830 | P1 | Secure update manifest verification | 8 h | ⛔ |
| 🟡 | 0831 | P1 | TLS and certificate failure handling | 1.5 h |  |
| 🟠 | 0832 | P1 | Secret scanning in source and logs | 8 h |  |
| 🟡 | 0833 | P1 | Redaction of tokens from diagnostics | 1.5 h |  |
| 🟡 | 0834 | P2 | Untrusted project safe-open mode | 45 min | ◐ |
| 🟡 | 0835 | P2 | Zip path traversal protection | done | ✅ |
| 🟢 | 0836 | P2 | Asset parser resource limits | 15 min |  |
| 🟡 | 0837 | P2 | Backend authorization checks per game | 1.5 h |  |
| 🟡 | 0838 | P2 | Server-side validation for remote calls | done | ✅ |
| 🟢 | 0839 | P2 | Rate limits for chat and RPC | 8 min | ◐ |
| 🟢 | 0840 | P2 | Anti-cheat authority boundary documentation | done | ✅ |
| 🟡 | 0841 | P2 | Moderation actions audit trail | 1.5 h |  |
| 🟢 | 0842 | P2 | User reporting and appeal workflow | 8 min | ◐ |
| 🟢 | 0843 | P2 | Age-aware privacy design review | 8 min | ◐ |
| 🟢 | 0844 | P2 | Data retention and deletion policy | 8 min | ◐ |
| 🟢 | 0845 | P2 | Secure dependency update process | done | ✅ |
| 🟢 | 0846 | P2 | Third-party license inventory | done | ✅ |
| 🟢 | 0847 | P2 | Security contact and disclosure process | 8 min | ◐ |
| 🟡 | 0848 | P2 | Backup encryption options | 1.5 h |  |
| 🟢 | 0849 | P2 | Permission prompt for external integrations | 8 min | ◐ |
| 🟢 | 0850 | P2 | Security checklist for each release | done | ✅ |

### 35. Performance profiling and optimization (3.3 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0851 | P0 | CPU frame-time breakdown | done | ✅ |
| 🟠 | 0852 | P0 | GPU frame-time breakdown | 4 h | ◐ |
| 🟠 | 0853 | P0 | Per-system ECS timing | done | ✅ |
| 🟠 | 0854 | P1 | Allocation hot-spot view | 8 h |  |
| 🟡 | 0855 | P1 | Physics broadphase and contact metrics | 45 min | ◐ |
| 🟢 | 0856 | P1 | Script execution flame graph | 8 min | ◐ |
| 🟠 | 0857 | P1 | Asset load and decode timing | 8 h |  |
| 🟡 | 0858 | P1 | Network bandwidth profiler | 45 min | ◐ |
| 🟡 | 0859 | P2 | Audio voice and CPU meter | 1.5 h |  |
| 🟡 | 0860 | P2 | Render pass cost waterfall | 45 min | ◐ |
| 🟢 | 0861 | P2 | Entity count and component count stats | 8 min | ◐ |
| 🟡 | 0862 | P2 | Large-scene benchmark suite | 45 min | ◐ |
| 🟡 | 0863 | P2 | Low-end reference hardware profile | 45 min | ◐ |
| 🟢 | 0864 | P2 | Cold-start time benchmark | 8 min | ◐ |
| 🟢 | 0865 | P2 | Hot-start time benchmark | 8 min | ◐ |
| 🟡 | 0866 | P2 | Memory high-water-mark view | 1.5 h |  |
| 🟢 | 0867 | P2 | GPU memory high-water-mark view | done | ✅ |
| 🟡 | 0868 | P2 | Asset streaming hitch detector | 45 min | ◐ |
| 🟡 | 0869 | P2 | Shader compile hitch detector | 45 min | ◐ |
| 🟡 | 0870 | P2 | UI layout performance trace | 1.5 h |  |
| 🟢 | 0871 | P2 | Scalable quality settings by GPU tier | 8 min | ◐ |
| 🟡 | 0872 | P2 | Batch and instancing efficiency report | 45 min | ◐ |
| 🟡 | 0873 | P2 | Frame pacing and missed-deadline report | 45 min | ◐ |
| 🟢 | 0874 | P2 | Optimization before-after benchmark snapshots | 8 min | ◐ |
| 🟡 | 0875 | P2 | Performance regression budgets in CI | 45 min | ◐ |

### 36. Input, accessibility and controls (4.5 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0876 | P1 | Fully remappable keyboard controls | 8 h |  |
| 🟠 | 0877 | P1 | Gamepad control remapping | 8 h |  |
| 🟠 | 0878 | P1 | Mouse sensitivity presets | 8 h |  |
| 🟠 | 0879 | P1 | Invert axis controls | 8 h |  |
| 🟡 | 0880 | P1 | Hold versus toggle accessibility options | 45 min | ◐ |
| 🟢 | 0881 | P1 | Color-blind friendly status palette | 8 min | ◐ |
| 🟢 | 0882 | P1 | Text contrast validation | 8 min | ◐ |
| 🟡 | 0883 | P1 | Keyboard-only Studio navigation | 1.5 h |  |
| 🟡 | 0884 | P2 | Screen-reader labels for controls | 1.5 h |  |
| 🟡 | 0885 | P2 | Captions and subtitle style controls | 45 min | ◐ |
| 🟢 | 0886 | P2 | Scalable UI fonts | 8 min | ◐ |
| 🟡 | 0887 | P2 | Large cursor and focus indicators | 45 min | ◐ |
| 🟢 | 0888 | P2 | Reduced flashing effect mode | 8 min | ◐ |
| 🟢 | 0889 | P2 | Motion-sickness camera options | 8 min | ◐ |
| 🟡 | 0890 | P2 | Controller glyph system by device | 45 min | ◐ |
| 🟡 | 0891 | P2 | Input action map per context | 45 min | ◐ |
| 🟢 | 0892 | P2 | Multi-device simultaneous input handling | 8 min | ◐ |
| 🟡 | 0893 | P2 | Virtual joystick template | 45 min | ◐ |
| 🟡 | 0894 | P2 | Touch gesture customization | 45 min | ◐ |
| 🟡 | 0895 | P2 | Accessibility options saved per user | 45 min | ◐ |
| 🟢 | 0896 | P2 | Accessible error message wording | done | ✅ |
| 🟢 | 0897 | P2 | WCAG-inspired UI checklist | done | ✅ |
| 🟡 | 0898 | P2 | Input latency measurement tool | 45 min | ◐ |
| 🟢 | 0899 | P2 | Focus order regression tests | 15 min |  |
| 🟡 | 0900 | P2 | Accessible template project | 1.5 h |  |

### 37. Mobile and low-end platform readiness (5.1 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0901 | P1 | Device capability tier detection | 8 h |  |
| 🟠 | 0902 | P1 | Dynamic resolution on sustained GPU load | 8 h |  |
| 🟠 | 0903 | P1 | Texture memory budget profiles | 8 h |  |
| 🟠 | 0904 | P1 | Reduced shadow and post-process modes | 8 h |  |
| 🟡 | 0905 | P1 | Low-overhead UI render path | 1.5 h |  |
| 🟡 | 0906 | P1 | Touch-first runtime controls | 1.5 h |  |
| 🟠 | 0907 | P1 | Virtual thumbstick component | 8 h |  |
| 🟡 | 0908 | P1 | Gesture input API | 45 min | ◐ |
| 🟡 | 0909 | P2 | Safe-area layout support | 45 min | ◐ |
| 🟡 | 0910 | P2 | Battery and thermal-aware quality hints | 45 min | ◐ |
| 🟢 | 0911 | P2 | Mobile asset compression profiles | 8 min | ◐ |
| 🟡 | 0912 | P2 | Background pause and audio policy | 45 min | ◐ |
| 🟡 | 0913 | P2 | App suspend and resume recovery | 1.5 h |  |
| 🟢 | 0914 | P2 | Low-memory asset eviction | 8 min | ◐ |
| 🟡 | 0915 | P2 | Small download game packaging profile | 1.5 h |  |
| 🔴 | 0916 | P2 | Android build feasibility prototype | 2.0 work weeks |  |
| 🔴 | 0917 | P2 | iOS engine portability assessment | 2.0 work weeks |  |
| 🟡 | 0918 | P2 | Mobile network handover resilience | 1.5 h |  |
| 🟡 | 0919 | P2 | Mobile profiler counters | 1.5 h |  |
| 🟡 | 0920 | P2 | Device compatibility report | 1.5 h |  |
| 🟢 | 0921 | P2 | Scaled physics budget for low-end CPUs | 8 min | ◐ |
| 🟡 | 0922 | P2 | Shader fallback for older GPUs | 1.5 h |  |
| 🟡 | 0923 | P2 | Offline cache quota controls | 45 min | ◐ |
| 🟢 | 0924 | P2 | Mobile player onboarding | 8 min | ◐ |
| 🟡 | 0925 | P2 | Battery-safe frame-rate caps | 45 min | ◐ |

### 38. Web and console portability (3.0 work weeks)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🔴 | 0926 | P1 | WebAssembly engine feasibility prototype | 2.0 work weeks |  |
| 🟠 | 0927 | P1 | Browser WebGPU capability probe | 8 h |  |
| 🟠 | 0928 | P1 | Browser fallback renderer decision record | 8 h |  |
| 🟠 | 0929 | P1 | Asset bundle size budget for web | 8 h |  |
| 🟡 | 0930 | P1 | Web runtime sandbox boundaries | 1.5 h |  |
| 🟡 | 0931 | P1 | Browser save-data adapter design | 1.5 h |  |
| 🟠 | 0932 | P1 | Web networking transport evaluation | 8 h |  |
| 🟡 | 0933 | P1 | Browser audio autoplay compliance handling | 1.5 h |  |
| 🟡 | 0934 | P2 | Pointer-lock and fullscreen UX | 1.5 h |  |
| 🟡 | 0935 | P2 | Web crash and context-loss recovery | 1.5 h |  |
| 🟢 | 0936 | P2 | Web build CI experiment | 8 min | ◐ |
| 🟢 | 0937 | P2 | Console architecture assessment document | done | ✅ |
| 🟡 | 0938 | P2 | Platform abstraction audit | 1.5 h |  |
| 🟢 | 0939 | P2 | Gamepad certification checklist draft | done | ✅ |
| 🟡 | 0940 | P2 | Platform-specific input adapter tests | 1.5 h |  |
| 🟡 | 0941 | P2 | Cross-platform save migration format | 1.5 h |  |
| 🟢 | 0942 | P2 | Cross-platform shader variant tests | 15 min |  |
| 🟢 | 0943 | P2 | Platform capability matrix | done | ✅ |
| 🟡 | 0944 | P2 | Shared project format conformance suite | 1.5 h |  |
| 🟡 | 0945 | P2 | Per-platform packaging profiles | 1.5 h |  |
| 🟢 | 0946 | P2 | Graceful unsupported-feature fallback | 8 min | ◐ |
| 🟡 | 0947 | P2 | Platform feature flags | 1.5 h |  |
| 🟡 | 0948 | P2 | Web performance benchmark scenes | 1.5 h |  |
| 🟢 | 0949 | P2 | Browser cache versioning strategy | done | ✅ |
| 🟢 | 0950 | P2 | Portability risk register | done | ✅ |

### 39. Creator discovery and community tooling (6.4 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟠 | 0951 | P1 | Public creator profile page | 8 h | ⛔ |
| 🟠 | 0952 | P1 | Game page with screenshots and tags | 8 h | ⛔ |
| 🟠 | 0953 | P1 | Search and filtering for published games | 8 h | ⛔ |
| 🟠 | 0954 | P1 | Featured collections with transparent rules | 8 h | ⛔ |
| 🟡 | 0955 | P1 | Recently updated games feed | 1.5 h | ⛔ |
| 🟡 | 0956 | P1 | Creator portfolio linking projects | 1.5 h | ⛔ |
| 🟠 | 0957 | P1 | Public demo links and safe launch flow | 8 h | ⛔ |
| 🟡 | 0958 | P1 | Game changelog visible to players | 1.5 h | ⛔ |
| 🟡 | 0959 | P2 | Player feedback survey widget | 1.5 h | ⛔ |
| 🟡 | 0960 | P2 | Bug report link from a game page | 1.5 h | ⛔ |
| 🟢 | 0961 | P2 | Creator follower and update notifications | 15 min | ⛔ |
| 🟡 | 0962 | P2 | Community showcase submission workflow | 1.5 h | ⛔ |
| 🟡 | 0963 | P2 | Showcase video metadata and thumbnail | 1.5 h | ⛔ |
| 🟢 | 0964 | P2 | Verified links to official project downloads | 15 min | ⛔ |
| 🟡 | 0965 | P2 | Community guidelines for asset use | 1.5 h | ⛔ |
| 🟡 | 0966 | P2 | Spam and impersonation reporting | 1.5 h | ⛔ |
| 🟢 | 0967 | P2 | Developer forum category taxonomy | 15 min | ⛔ |
| 🟡 | 0968 | P2 | Monthly challenge starter kits | 1.5 h | ⛔ |
| 🟡 | 0969 | P2 | Community-contributed tutorials review queue | 1.5 h | ⛔ |
| 🟡 | 0970 | P2 | Contributor onboarding issue list | 1.5 h | ⛔ |
| 🟢 | 0971 | P2 | Release announcement template generator | 15 min | ⛔ |
| 🟡 | 0972 | P2 | Public compatibility badges backed by tests | 1.5 h | ⛔ |
| 🟡 | 0973 | P2 | Creator analytics privacy controls | 1.5 h | ⛔ |
| 🟢 | 0974 | P2 | Project attribution and credits page | 15 min | ⛔ |
| 🟡 | 0975 | P2 | Independent project health metrics dashboard | 1.5 h | ⛔ |

### 40. Product identity and strategic differentiation (4.7 days)

| | ID | Pri | Item | Time | |
|---|---|---|---|---|---|
| 🟢 | 0976 | P1 | Define one-sentence product positioning | done | ✅ |
| 🟢 | 0977 | P1 | Publish accurate feature maturity matrix | done | ✅ |
| 🟡 | 0978 | P1 | Create comparison page with qualified claims | done | ✅ |
| 🟡 | 0979 | P1 | State licensing terms consistently across repository and website | 1.5 h |  |
| 🟡 | 0980 | P1 | Clarify what is local-only versus cloud-hosted | done | ✅ |
| 🟡 | 0981 | P1 | Publish data flow and privacy diagram | 1.5 h |  |
| 🟡 | 0982 | P1 | Document how Kronos differs from Unity | done | ✅ |
| 🟡 | 0983 | P1 | Document how Kronos differs from Roblox Studio | done | ✅ |
| 🟢 | 0984 | P2 | Document target audience and non-goals | done | ✅ |
| 🟢 | 0985 | P2 | Define compatibility guarantee levels | done | ✅ |
| 🟢 | 0986 | P2 | Version compatibility policy for old projects | done | ✅ |
| 🟢 | 0987 | P2 | Publish engine architecture overview | done | ✅ |
| 🟢 | 0988 | P2 | Keep roadmap priorities measurable | 15 min |  |
| 🟢 | 0989 | P2 | Separate shipped features from prototypes | done | ✅ |
| 🟢 | 0990 | P2 | Add reproducible showcase benchmark methodology | done | ✅ |
| 🟢 | 0991 | P2 | Maintain public known limitations page | done | ✅ |
| 🟢 | 0992 | P2 | Set minimum supported hardware clearly | 8 min | ◐ |
| 🟢 | 0993 | P2 | Establish bug severity and response policy | done | ✅ |
| 🟢 | 0994 | P2 | Create contributor decision-making process | done | ✅ |
| 🟢 | 0995 | P2 | Establish security disclosure expectations | 8 min | ◐ |
| 🔴 | 0996 | P2 | Build a flagship game with public permission to showcase | 4 days | ⛔ |
| 🟢 | 0997 | P2 | Choose stable-versus-preview release criteria | done | ✅⛔ |
| 🟢 | 0998 | P2 | Publish transparent cloud-cost policy | done | ✅⛔ |
| 🟢 | 0999 | P2 | Decide open-source and plugin licensing boundaries | 15 min | ⛔ |
| 🟢 | 1000 | P2 | Define success metrics around active creators not hype | done | ✅ |
