# Roblox bridge

The 4.3 goal: a Roblox creator brings a place over and it runs in Kronos.
This page tracks how far that has got. The plan is in `ROADMAP.md`, section 4.3.

## Compatibility score

Kronos measures how much of a Roblox place it can run, so the bridge work
goes to the APIs that matter most rather than guesses.

Three numbers, averaged into one overall score:

| Number | Meaning |
|---|---|
| Instances | Objects in the place that Kronos can build (parts, lights, scripts, models, folders, services) |
| API uses | Roblox API uses in scripts that Kronos supports (`print`, `task`, and every API the bridge has added) |
| Scripts | `Script`s and `LocalScript`s whose top level runs without an error; `ModuleScript`s only need to compile |

Local variables are not counted as API uses: `local Players = ...` declares a
variable, it doesn't use the `Players` service.

### Where you see it

- **Studio:** File → Import .rbxlx. After Import, the window shows the score
  and the most-used missing APIs above the detailed report.
- **Command line:** `kronos_compat` prints the score for each place, the
  total, the most-used missing APIs and the classes Kronos can't build yet.

```
cd engine
./build/src/kronos_compat                      # the test places in tests/compat_corpus
./build/src/kronos_compat my_place.rbxlx       # any .rbxlx file or folder
./build/src/kronos_compat --min 90             # exit code 1 if the average is below 90%
```

### Test places

`engine/tests/compat_corpus/` holds five small places written for Kronos (free
to use): an obby, a door, a tycoon button with a dropper, a saved leaderboard
and a GUI shop. Their scripts use common Roblox code. `make_corpus.py` in the
same folder writes them again after a change.

Only test with places you made or that are clearly licensed for it.

### Scores

| Date | Average | Instances | API uses | Scripts | Notes |
|---|---|---|---|---|---|
| 2026-10-07 | 34% | 51/56 (91%) | 3/94 (3%) | 1/13 (8%) | Baseline before the bridge. Every Roblox script stops at `game`, `workspace` or `script` on its first line |
| 2026-10-07 | 36% | 51/56 (91%) | 9/94 (10%) | 1/13 (8%) | Step 2, datatypes. Scripts still stop at `game` (step 3) |
| 2026-10-09 | 57% | 52/56 (93%) | 66/94 (70%) | 1/13 (8%) | Step 3, Instance tree. Scripts now get past `game`/`workspace` and stop at events (`Touched`, `PlayerAdded`, `:Connect`, step 4) or services (TweenService, DataStoreService) |
| 2026-10-09 | 75% | 52/56 (93%) | 83/95 (87%) | 6/13 (46%) | Step 4, events. All four obby scripts run. The rest stop at `PlayerAdded` (step 5), `OnServerEvent` (step 6), `DataStoreService` (step 7), `TweenService`/`Debris` (step 8) or `MouseButton1Click` (step 9) |

Most-used missing APIs at the baseline: `:Connect`, `:GetService`, `game`,
`script`, `Instance`, `:WaitForChild`, `workspace`. Unbuilt classes: the GUI
classes and `RemoteEvent`.

### Not measured yet

- Behaviour checks (the door opens, a coin is counted). Scripts now get past
  `game` and `workspace`, but every test place waits on events (step 4).
- Errors after the first frame. Scripts run their top level only.

## Datatypes (step 2)

Every Kronos script (Luau) now has Roblox's value types, built in
`engine/src/core/RobloxDatatypes.cpp`:

| Type | What works |
|---|---|
| `Vector3`, `Vector2` | `new`, `zero`, `one`, axis constants, `X`/`Y`/`Z`, `Magnitude`, `Unit`, `+ - * /`, unary `-`, `==`, `Dot`, `Cross`, `Lerp`, `FuzzyEq`, `Min`, `Max`, `Abs`, `Floor`, `Ceil`, `Sign`, `Angle` |
| `CFrame` | `new` (all forms: position, look-at, quaternion, 12 numbers), `identity`, `lookAt`, `Angles`/`fromEulerAnglesXYZ`, `fromEulerAnglesYXZ`/`fromOrientation`, `fromAxisAngle`, `fromMatrix`; `Position`, `Rotation`, `LookVector`, `RightVector`, `UpVector`, `X/Y/Z`; `*` with CFrame or Vector3, `+`/`-` Vector3; `Inverse`, `Lerp`, `ToWorldSpace`, `ToObjectSpace`, the `Point`/`Vector` space conversions, `GetComponents`, `ToEulerAnglesXYZ`, `ToOrientation`, `ToAxisAngle`, `FuzzyEq` |
| `Color3` | `new`, `fromRGB`, `fromHSV`, `fromHex`, `toHSV`; `R/G/B`, `Lerp`, `ToHSV`, `ToHex` |
| `BrickColor` | `new` by name, number, Color3 or RGB (nearest colour), `random`, `Red()` and the other named colours; `Name`, `Number`, `Color`. About 40 common palette colours, not all 200+ |
| `UDim`, `UDim2` | `new`, `fromScale`, `fromOffset`, `X/Y`, `Width/Height`, `+ -`, `Lerp` |
| `Enum` | 27 common enums (`Material`, `PartType`, `KeyCode`, `EasingStyle`, `HumanoidStateType`, `Font`, ...) with Roblox's values; `Name`, `Value`, `EnumType`, `GetEnumItems`, `FromName`, `FromValue` |
| `TweenInfo`, `NumberRange`, `NumberSequence`, `ColorSequence` (+ keypoints), `Ray`, `RaycastParams`, `Random` | Constructors and properties as in Roblox |

Like Roblox, the values are read-only (`v.X = 5` is an error), unknown members
are errors, and `typeof(v)` returns `"Vector3"`, `"CFrame"` and so on.

Differences from Roblox:
- `Random` gives the same numbers for the same seed every time, but not the
  same numbers Roblox's generator gives.
- Values are Luau tables, so `type(v)` is `"table"` (Roblox says `"userdata"`).
  Use `typeof`, as Roblox recommends.
- Studio's script autocomplete doesn't know these types yet.

## Instance tree (step 3)

Scripts now see the scene as Roblox's tree of objects. The code is in
`engine/src/core/InstanceTree.cpp` (the class table and the tree rules) and
`engine/src/core/ScriptInstanceApi.cpp` (what Luau sees).

```lua
local part = Instance.new("Part")
part.Name = "Bridge"
part.Size = Vector3.new(4, 1, 2)
part.Position = Vector3.new(3, 3, 0)
part.Color = Color3.fromRGB(50, 150, 255)
part.Parent = workspace            -- now it appears in the world

print(workspace:FindFirstChild("Bridge"):GetFullName())  -- Workspace.Bridge
part:Destroy()
```

### What works

| Area | Supported |
|---|---|
| Globals | `game` (and `Game`), `workspace` (and `Workspace`), `script`, `Instance.new` |
| Every Instance | `Name`, `ClassName`, `Parent`, `Archivable`; `GetChildren`, `GetDescendants`, `FindFirstChild` (with `recursive`), `FindFirstChildOfClass`, `FindFirstChildWhichIsA`, `FindFirstAncestor`, `FindFirstAncestorOfClass`, `FindFirstAncestorWhichIsA`, `WaitForChild` (with timeout), `IsA`, `IsDescendantOf`, `IsAncestorOf`, `Clone`, `Destroy`, `ClearAllChildren`, `GetFullName`, `GetAttribute`, `SetAttribute`, `GetAttributes`; `parent.ChildName` lookups |
| `game` | `GetService`, `FindService`, `game.Players`-style lookups |
| Parts (`Part`, `SpawnLocation`, `WedgePart`, `MeshPart`, `Seat`, ...) | `Position`, `CFrame`, `Orientation`, `Rotation`, `Size`, `Color`, `BrickColor`, `Transparency`, `CastShadow`, `Anchored`, `Material`, `Shape` (`Block`, `Ball`, `Cylinder`), `Reflectance`, `CanCollide`, `CanTouch`, `CanQuery`, `Locked` |
| Containers | `Model` (`PrimaryPart`), `Folder`, `Configuration` |
| Lights | `PointLight`, `SpotLight`, `SurfaceLight`: `Brightness`, `Color`, `Enabled`, `Shadows`, `Range`, `Angle` |
| Values | `IntValue`, `NumberValue`, `StringValue`, `BoolValue`, `ObjectValue`, `Vector3Value`, `Color3Value`, `CFrameValue`, `BrickColorValue` |
| Scripts | `Script`, `LocalScript`, `ModuleScript` (`Disabled`) |
| Services | `Players`, `Lighting`, `ReplicatedStorage`, `ReplicatedFirst`, `ServerScriptService`, `ServerStorage`, `StarterGui`, `StarterPack`, `StarterPlayer`, `SoundService`, `Teams`, `Chat` exist as containers. What they *do* comes in later steps |
| Placeholders | `RemoteEvent`, `RemoteFunction`, `BindableFunction`, `Sound`, `Tool`, `Accessory` exist so places import, but have no behaviour yet (`BindableEvent` works, see Events) |

Roblox's rules are kept:
- `Instance.new` makes an object with `Parent = nil`. It isn't drawn, saved
  or listed in Studio until it gets a parent.
- Setting `Parent` keeps the object where it is in the world.
- An object can't be its own ancestor, `game` and `workspace` can't be moved
  or destroyed, and a destroyed object's `Parent` is locked. The error
  messages match Roblox's.
- `Clone` copies the object and everything under it, skips objects with
  `Archivable = false`, and returns a copy with `Parent = nil`.
- Parts in `ReplicatedStorage` or `ServerStorage` aren't drawn.
- The same object always gives back the same Luau value (`a == b` works).
- Reading `workspace.Name` always looks at the current tree. (Luau normally
  works out `workspace.X` once when a script loads; Kronos turns that off for
  `game`, `workspace` and `script`, as Roblox does.)

The classes and their properties come from one class table. Each property is
marked as saved or derived (for example `Position` comes from `CFrame`, so only
`CFrame` is saved), replicated or not, and shown in Studio or not. The
importer, `Instance.new`, scene files and (later) the Studio inspector and
networking all read the same table.

Scenes now save each object's Roblox class, stored properties and attributes
(text scenes: an `INSTANCE` line; binary scenes: version 8).

### Differences from Roblox (known limits)

- **Children follow their parent.** In Kronos, moving a Model or Part moves
  everything under it (Kronos keeps a scene graph). In Roblox, parts only move
  together when they are welded.
- Setting `Position`/`CFrame` on a part that is already simulating doesn't
  move its physics body yet; `Destroy` doesn't free the physics body yet
  (the same as `world.destroy`). Parts made while the game is running get no
  physics body until the next Play. This comes with the physics work in 4.4.
- `Material` is stored and saved, but only `Neon` changes how a part looks
  (it glows).
- Properties that point at another object (`PrimaryPart`, `ObjectValue.Value`)
  work while the game runs but aren't saved in scene files yet.
- On scene load, children find their parents by name. Two objects with the
  same name under different parents can get mixed up until permanent object
  ids arrive (4.5).
- Objects made with `Instance.new` or `Clone` and never given a parent stay in
  memory until the scene is reloaded (Roblox frees them when no script holds
  them). They are not drawn, listed or saved.
- 1 Kronos unit = 1 stud. The import window has a stud scale option.
- GUI classes (`ScreenGui`, `Frame`, `TextButton`, ...) aren't in the class
  table yet: step 9.

### Parts are solid

Like in Roblox, every part has a physics body that matches what you see:
- `Instance.new("Part")` and imported parts collide. Parts start unanchored
  (they fall when the game runs); `Anchored = true` keeps them still.
- Changing `Size` or `Shape` changes the collider too (`Ball` is a sphere).
- `CanCollide = false`: players and parts pass through, but `Touched` still
  fires.
- Parts kept in `ReplicatedStorage` or `ServerStorage`, or with
  `Parent = nil`, have no body.
- Parts inside a `Model` or `Folder` collide where they are drawn.

## Events (step 4)

Scripts can now react to things happening, with Roblox's `RBXScriptSignal`
objects. The code is in `engine/src/core/InstanceSignals.cpp` (the queue and
the connections) and `engine/src/core/ScriptInstanceApi.cpp` (what Luau
sees).

```lua
local pad = workspace.Pad
pad.Touched:Connect(function(hit)
    print(hit.Name .. " stepped on the pad")
end)

local coins = Instance.new("IntValue")
coins.Changed:Connect(function(value) print("coins:", value) end)
coins.Value = 10

game:GetService("RunService").Heartbeat:Connect(function(dt)
    pad.Transparency = (math.sin(time() * 3) + 1) / 2
end)
```

### What works

| Area | Supported |
|---|---|
| Signals | `:Connect(fn)`, `:Once(fn)`, `:Wait()`, `:ConnectParallel(fn)` (runs like `Connect`, there are no Actors yet); `typeof` gives `RBXScriptSignal` |
| Connections | `connection.Connected`, `connection:Disconnect()`; `typeof` gives `RBXScriptConnection` |
| Every Instance | `Changed` (the property name), `GetPropertyChangedSignal(name)`, `AttributeChanged`, `GetAttributeChangedSignal(name)`, `ChildAdded`, `ChildRemoved`, `DescendantAdded`, `DescendantRemoving`, `AncestryChanged`, `Destroying` |
| Value objects (`IntValue`, ...) | `Changed` passes the new value, and fires only for `Value` |
| Parts | `Touched(otherPart)`, `TouchEnded(otherPart)`, from real physics contacts. Both parts need `CanTouch = true` |
| `BindableEvent` | `:Fire(...)` and `.Event`. Works between scripts. Tables are copied |
| `RunService` | `Stepped(time, dt)`, `PreSimulation(dt)`, `PostSimulation(dt)`, `Heartbeat(dt)`, `RenderStepped(dt)`, `PreRender(dt)`; `IsServer`, `IsClient`, `IsStudio`, `IsRunning`, `IsRunMode`, `IsEdit` |
| `task` | `task.spawn`, `task.defer`, `task.delay`, `task.wait`, `task.cancel` |
| Older globals | `wait`, `spawn`, `delay`, `tick`, `time`, `elapsedTime` |

Roblox's rules are kept:
- **Handlers run deferred**, like Roblox's default `SignalBehavior.Deferred`.
  Firing an event only queues the handlers; they run when the current script
  stops or yields. So `part.Name = "A"` followed by `print("after")` prints
  `after` first.
- `Changed` fires only when the value really changes. Setting `CFrame` also
  fires `Position` (and the other way round), and `Color` and `BrickColor` go
  together.
- A handler that keeps firing its own event stops after 10 levels, with
  Roblox's error `Maximum event re-entrancy depth exceeded`.
- An error in one handler is printed (`runtime error in Touched handler:
  ...`) and doesn't stop the other handlers or the script.
- `Destroying` runs before the object goes away, then all its connections are
  disconnected.
- When a script stops (Stop in Studio, a script reloaded), its connections go
  with it.

### Frame order

Every frame runs in this order, in the Player and in Studio's Play mode:

1. `PreRender`, then `RenderStepped` (before the frame is drawn).
2. `Stepped`, then `PreSimulation`.
3. Scripts: waiting threads (`task.wait`, `wait`) resume, then the queued
   handlers run.
4. Physics steps.
5. `Touched`/`TouchEnded` from that step, then `PostSimulation`, then
   `Heartbeat`.

In the Player, physics runs at a fixed 120 steps a second, so `Stepped` and
`Heartbeat` can run more than once per drawn frame (or not at all when the
frame is very short). `RenderStepped` runs once per drawn frame. On a
server there's no drawing, so no `RenderStepped`.

### Differences from Roblox (known limits)

- The player's avatar touches things with one body part, named `Player`.
  `hit.Parent:FindFirstChild("Humanoid")` doesn't find anything yet; that
  comes with characters in step 5.
- The avatar's body floats a little above the floor (like Roblox's
  `HumanoidRootPart`), so the floor rarely gets `Touched`. Walls, pads you
  walk into and parts that fall on you work.
- `BindableEvent` passes numbers, strings, booleans, Instances, `Vector3`,
  `CFrame`, `Color3`, `BrickColor`, Enum items and tables of these. Other
  datatypes (`UDim2`, `TweenInfo`, ...) arrive as plain tables, and functions
  as `nil`.
- `DescendantRemoving` handlers run after the object has been removed
  (deferred), so `Parent` is already the new parent.
- `:Wait()` on an object that gets destroyed never resumes.
- Connections made from Studio's Debug Console stay until Studio closes.
- `BindToRenderStep` isn't there yet.

## From entity ids to Instances

Older Kronos scripts use the `world` table with numeric entity ids. That still
works, and both styles see the same objects:

| Old style | Roblox style |
|---|---|
| `local id = world.findByName("Door")` | `local door = workspace.Door` or `workspace:FindFirstChild("Door")` |
| `world.setPosition(id, 0, 5, 0)` | `door.Position = Vector3.new(0, 5, 0)` |
| `world.setColor(id, 1, 0, 0)` | `door.Color = Color3.new(1, 0, 0)` |
| `world.destroy(id)` | `door:Destroy()` |
| `script.entity` (a number) | `script` is now the script's Instance: `script.Parent`, `script.Name`. `script.entity` still gives the number |

To use an id with the old API from an Instance, read `instance.entity`.

## Import fixes

- Imported parts were half their real size; they now use the full `Size`.
- Lights, Folders and other objects without a position now sit at their
  parent instead of the world origin.
- `Ball` parts are round (they used to be long pills).
- Imported parts are solid (see "Parts are solid").
- Every Roblox class in the class table imports. Classes without a shape
  (`Folder`, `IntValue`, `RemoteEvent`, ...) become groups that keep their
  class, and `Anchored`, `CanCollide`, `Material`, `Shape` and `Value` are kept.
- Importing a second place adds to the existing `Workspace` and services
  instead of making second copies.
- Undoing an import removes everything it added, including children.

- Roblox Studio saves script sources inside `<![CDATA[ ... ]]>`. The importer
  used to stop at the first one and drop the rest of the place; it now reads
  them. Numeric XML codes such as `&#9;` are decoded too.
