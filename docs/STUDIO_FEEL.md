# Studio feel (4.4)

The owner found Roblox Studio nicer to use than Kronos Studio after building the same obby in both. This page tracks the work in small, tested steps.

## Audit (11 Oct 2026)

| Area | What Kronos Studio had | Gap compared with Roblox Studio |
|---|---|---|
| Layout | Docked panels, View > Reset Layout to Default | Fine for now |
| Explorer | A real parent/child tree with icons and multi-select | The top level used Kronos groups (Prop, Physics, Economy...) instead of Workspace and the services; scripts had no icon |
| Inspector | Sections per component (Transform, Renderable, Physics, Light, Sound) | No Roblox properties at all: Anchored, Transparency, Material, CanCollide, a GUI's Text... couldn't be seen or edited |
| Script Editor | Lint, autocomplete, Problems list | Reported an end-of-file error one line lower than Luau does |

## Done

- **Explorer tree** (default): Workspace first, then each service that holds something (Players, Lighting, ReplicatedStorage, ServerScriptService, ServerStorage, StarterGui, ...), in Roblox Studio's order. Empty services are hidden. Scripts, parts, models, folders, lights, GUI objects and welds have class icons. The old grouped view is still there under **Groups**.
- **Properties** in the Inspector: every property of the selected object's Roblox class, grouped by class (for example Part, then BasePart), with a filter box. Booleans are checkboxes, numbers drag, colours use a picker, enums (Material, Shape, ...) are dropdowns, and object references show the target's name. Every change can be undone. Position, rotation and CFrame stay in the Transform section; derived duplicates (BrickColor, Position) aren't repeated.

- **Insert Object**: right-click Workspace or any object > Insert Object (parts, scripts, lights, sounds, welds, GUI, remotes and values). The new object goes inside the one you clicked and is selected; new parts are anchored and placed above their parent.
- **Explorer search**: type in "Search objects" for a flat list of matching objects with their full paths; click one to select it.
- **Object references** (PrimaryPart, Part0, Part1, ...) are picked from a searchable list. Joints and PrimaryPart only offer parts, and PrimaryPart only offers parts inside the model.
- **Script Editor**: errors at the end of a script now show the same line Luau reports (the editor used to count one extra line).

- **Insert Object undo**: Ctrl+Z removes an inserted object and Ctrl+Y puts it back in the same parent.
- **Property categories**: the Properties section is grouped like Roblox Studio (Appearance, Data, Text, Image, Transform, Parts, Behavior, Character, Light, Sound, Collision, Part); anything else is listed under its class name.
- Union pieces and other hidden helpers don't show up in the Explorer or Scene Search.

## Next

- Pick the next gap from real use (the owner's next build in Studio).
