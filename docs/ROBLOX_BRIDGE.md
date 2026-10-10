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

- **Studio:** File → Import Roblox file. After Import, the window shows the score
  and the most-used missing APIs above the detailed report.
- **Command line:** `kronos_compat` prints the score for each place, the
  total, the most-used missing APIs and the classes Kronos can't build yet.

```
cd engine
./build/src/kronos_compat                      # the test places in tests/compat_corpus
./build/src/kronos_compat my_place.rbxl        # any .rbxl/.rbxlx file or folder
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
| 2026-10-09 | 82% | 52/56 (93%) | 86/95 (91%) | 8/13 (62%) | Step 5, players and characters. The obby scores 100% and is playable in Studio; the tycoon's leaderstats and buy button work. The rest stop at `DataStoreService` (step 7), `Debris`/`TweenService` (step 8), `OnServerEvent` (step 6) or `MouseButton1Click` (step 9) |
| 2026-10-09 | 85% | 53/56 (95%) | 87/95 (92%) | 9/13 (69%) | Step 6, client and server. The GUI shop's server script (`OnServerEvent`) runs, and its LocalScript starts on the client. The rest stop at `DataStoreService` (step 7), `Debris`/`TweenService` (step 8) or `MouseButton1Click` (step 9) |
| 2026-10-10 | 89% | 53/56 (95%) | 90/95 (95%) | 10/13 (77%) | Step 7, DataStores. The leaderboard place saves and loads Cash and scores 100%. The rest stop at `Debris`/`TweenService` (step 8) or `MouseButton1Click` (step 9) |
| 2026-10-10 | 96% | 53/56 (95%) | 95/95 (100%) | 12/13 (92%) | Step 8, common services. The door (TweenService) and tycoon (Debris) places score 100%. Only the GUI shop is left: its LocalScript stops at `MouseButton1Click`, and Frame/TextLabel/TextButton aren't built yet (step 9) |
| 2026-10-10 | 100% | 56/56 (100%) | 95/95 (100%) | 13/13 (100%) | Step 9, GUI. Every test place scores 100%; the GUI shop's button script runs. The corpus is small, so step 11 (a real Roblox-made place) is the next real measure |
| 2026-10-10 | 100% | 56/56 (100%) | 95/95 (100%) | 13/13 (100%) | Step 10, binary files. Same score: the corpus is `.rbxlx`, and `kronos_compat` now reads `.rbxl` too |

Most-used missing APIs at the baseline: `:Connect`, `:GetService`, `game`,
`script`, `Instance`, `:WaitForChild`, `workspace`. Unbuilt classes: the GUI
classes and `RemoteEvent`.

### Not measured yet

- Behaviour checks (the door opens, a coin is counted) inside the score. The
  tests check these by hand for now (`testRobloxPlayers`).

The score runs each place for 3 seconds with one player joined, so errors in
`PlayerAdded` handlers and in later frames count too.

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
- `world.destroy` on a plain Kronos entity doesn't free its physics body.
  Roblox parts are looked after (see "Parts are solid").
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
- While a game runs (Player, server, Studio Play), a part that is made,
  cloned or moved into the workspace gets its body on the next frame. One
  that leaves the workspace or is destroyed loses it. Changing `Size`,
  `Shape`, `Anchored` or `CanCollide` rebuilds it (`core/PartBodies.cpp`).

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

- The player's avatar touches things with one part, its `HumanoidRootPart`
  (Roblox would report a leg or the torso). `hit.Parent` is the character,
  so `hit.Parent:FindFirstChild("Humanoid")` works (step 5).
- `BindableEvent` passes numbers, strings, booleans, Instances, `Vector3`,
  `CFrame`, `Color3`, `BrickColor`, Enum items and tables of these. Other
  datatypes (`UDim2`, `TweenInfo`, ...) arrive as plain tables, and functions
  as `nil`.
- `DescendantRemoving` handlers run after the object has been removed
  (deferred), so `Parent` is already the new parent.
- `:Wait()` on an object that gets destroyed never resumes.
- Connections made from Studio's Debug Console stay until Studio closes.
- `BindToRenderStep` isn't there yet.

## Players and characters (step 5)

Games now know who is playing. When you press Play (Studio) or join a game
(Player), you become a `Player` under `game.Players`, and your avatar becomes
a character: a `Model` in the workspace named after you, holding a
`HumanoidRootPart` and a `Humanoid`. The code is in
`engine/src/core/RobloxPlayers.cpp`; the classes are in the class table
(`core/InstanceTree.cpp`).

```lua
local Players = game:GetService("Players")

Players.PlayerAdded:Connect(function(player)
    local leaderstats = Instance.new("Folder")
    leaderstats.Name = "leaderstats"
    leaderstats.Parent = player
    local coins = Instance.new("IntValue")
    coins.Name = "Coins"
    coins.Parent = leaderstats

    player.CharacterAdded:Connect(function(character)
        character.Humanoid.WalkSpeed = 24
    end)
end)

workspace.Lava.Touched:Connect(function(hit)
    local humanoid = hit.Parent:FindFirstChild("Humanoid")
    if humanoid then humanoid.Health = 0 end
end)
```

### What works

| Area | Supported |
|---|---|
| `Players` | `LocalPlayer`, `GetPlayers`, `GetPlayerFromCharacter`, `GetPlayerByUserId`, `PlayerAdded`, `PlayerRemoving`, `RespawnTime` (5 s), `CharacterAutoLoads`, `MaxPlayers` |
| `Player` | `Name`, `DisplayName`, `UserId` (your Kronos profile id), `Character`, `RespawnLocation`, `LoadCharacter`, `CharacterAdded`, `CharacterRemoving`; a `Backpack` and a `PlayerGui` inside |
| Character | A `Model` named after the player with `PrimaryPart` = `HumanoidRootPart` (your avatar's capsule) and a `Humanoid` |
| `Humanoid` | `Health`, `MaxHealth`, `WalkSpeed`, `JumpPower`, `JumpHeight`, `UseJumpPower`, `AutoRotate`, `Jump`, `MoveDirection`, `RootPart`, `DisplayName`, `HipHeight`, `Sit`, `PlatformStand`, `WalkToPoint`, `RigType`; `TakeDamage`, `MoveTo` (a point, or a point and a part to follow), `Move`, `GetState`, `ChangeState`; `Died`, `HealthChanged`, `MoveToFinished`, `Running`, `Jumping`, `FreeFalling`, `StateChanged` |
| Respawning | When `Health` reaches 0 the character dies (`Died` fires once, input stops). After `Players.RespawnTime` a new character appears at the player's `RespawnLocation`, else at a part named `SpawnLocation`. Falling below `workspace.FallenPartsDestroyHeight` (-500) kills |
| Leaderboard | A player with a `leaderstats` folder shows in the top-right list, one column per value in the order they were added, sorted by the first column. Your own row is blue. In the Player and in Studio Play |
| Moving parts | Setting `CFrame`/`Position` on a part that is simulating now moves its physics body (teleporting a character works) |
| Imported places | Imported scripts start when the game runs (Play), following Roblox's rules; see "Client and server (step 6)" |

Roblox's scale is kept: `WalkSpeed` 16 and `JumpPower` 50 (or `JumpHeight`
7.2) are the normal Kronos walk and jump, and other values scale them
(`WalkSpeed = 32` walks twice as fast). `Running` reports speed in the same
units.

Join order, as in Roblox: scripts load first, then the player joins, so a
script's `PlayerAdded` handler always sees the local player. `PlayerAdded`
handlers run before the character is made, so a `CharacterAdded` connection
made inside `PlayerAdded` catches the first character.

### Differences from Roblox (known limits)

- Only the local player for now. Other players in a network game don't get
  `Player` objects yet (they arrive with remotes over the network, see step 6).
- The character has no `Head`, `Torso` or limb parts, only
  `HumanoidRootPart`; the avatar mesh is drawn on top of it. Scripts that
  look for `character.Head` don't work yet.
- On death the avatar just stands still: no falling apart, no death sound,
  no health regeneration script.
- `Player:Kick`, `GetMouse`, `Teams`/`TeamColor`, `Humanoid:LoadAnimation`
  and tools (`EquipTool`) are planned; using them gives a "planned" error.
- Broken Bones and the bring-up world don't create players (they have their
  own game code).

## Client and server (step 6)

A Roblox game has two sides: the **server** (one per game, runs `Script`s)
and a **client** for each player (runs `LocalScript`s). They talk through
`RemoteEvent`s and `RemoteFunction`s. Kronos now has the same split. The code
is in `engine/src/core/RobloxScripts.cpp` (which scripts run where),
`core/Scripting.cpp` (one Luau VM per side) and `core/ScriptInstanceApi.cpp`
(`require` and the remotes).

```lua
-- ReplicatedStorage has a RemoteEvent "Buy" and a RemoteFunction "GetPrice".

-- Script in ServerScriptService
local RS = game:GetService("ReplicatedStorage")
RS.GetPrice.OnServerInvoke = function(player, item)
    return 25
end
RS.Buy.OnServerEvent:Connect(function(player, item)
    print(player.Name .. " bought " .. item)
    RS.Buy:FireClient(player, "thanks")
end)

-- LocalScript in StarterPlayer.StarterPlayerScripts
local RS = game:GetService("ReplicatedStorage")
RS.Buy.OnClientEvent:Connect(print)
print("price", RS.GetPrice:InvokeServer("Sword"))
RS.Buy:FireServer("Sword")
```

### Which scripts run, and where

| Script | Runs when it is under | Side |
|---|---|---|
| `Script` | `Workspace` (also inside models), `ServerScriptService`, or a player's `Backpack` | server |
| `LocalScript` | the local player's `PlayerScripts`, `PlayerGui`, `Backpack` or character, or `ReplicatedFirst` | client |
| `ModuleScript` | never on its own; it runs the first time a script `require`s it | the requiring side |

The same rules apply to scripts made or moved while the game runs:

- A script starts as soon as it is in one of those places and not `Disabled`.
- It stops when it is destroyed or disabled, and starts again from the top
  when it is enabled again or its source is edited.
- Moving a running script somewhere else doesn't stop it (as in Roblox).
- Scripts in `ReplicatedStorage`, `ServerStorage`, `StarterPlayer`,
  `StarterGui` and `StarterPack` don't run there. They are templates.

The Kronos `--` style scripts (the `world` table, `onUpdate`) still run as
before, each in its own VM.

### Starter folders

| Folder | Copied to | When |
|---|---|---|
| `StarterPlayer.StarterPlayerScripts` | the player's `PlayerScripts` | once, when the player joins |
| `StarterPlayer.StarterCharacterScripts` | the character `Model` | every spawn |
| `StarterPack` | the player's `Backpack` (emptied first) | every spawn |
| `StarterGui` | the player's `PlayerGui` | every spawn; a `ScreenGui` with `ResetOnSpawn = false` is kept instead of copied again |

### One VM per side

All server scripts share one Luau VM, and all client scripts share another,
so `_G`, `shared` and `require` work across scripts like in Roblox. Each
script still has its own `script` and its own top-level variables, and one
script's error doesn't stop the others. `RunService:IsServer()` and
`IsClient()` answer for the side the script runs on, and
`Players.LocalPlayer` is `nil` on the server.

### require

`require(module)` runs a `ModuleScript` once per side and returns the same
value to every later caller. Inside the module, `script` is the module.
A module may wait (`task.wait`); other scripts that require it meanwhile wait
for it to finish. Errors match Roblox:

- "Requested module was required recursively" (A requires B requires A)
- "Module code did not return exactly one value"
- "Requested module experienced an error while loading"
- "Attempted to call require with invalid argument(s)." (not a ModuleScript)

`require("path/to/file.lua")` with a string still loads a Kronos Luau file.

### Remotes

| Object | Supported |
|---|---|
| `RemoteEvent`, `UnreliableRemoteEvent` | `FireServer`, `FireClient`, `FireAllClients`, `OnServerEvent` (gets the sending player first), `OnClientEvent` |
| `RemoteFunction` | `InvokeServer`, `InvokeClient`, `OnServerInvoke`, `OnClientInvoke`. The caller waits for the answer; an error in the callback is raised in the caller |
| `BindableFunction` | `Invoke`, `OnInvoke` (same side) |

Like Roblox:

- Calling from the wrong side errors, e.g. "FireServer can only be called
  from the client", "OnServerEvent can only be used on the server".
- Messages are delivered at the next resume point (deferred), not inside the
  `Fire` call.
- Tables are copied: the receiver gets its own copy.
- A callback can only be set, not read. An invoke waits until a callback
  is set.

### What the client can see

On the client, `ServerStorage` and `ServerScriptService` look empty
(`GetChildren`, `FindFirstChild`, `WaitForChild`, `.Name` lookups). The
server sees everything.

### Where each side runs

| Where | Server side | Client side |
|---|---|---|
| Studio Play | yes | yes, for your player |
| Player, single player or hosting | yes | yes, for your player |
| Player, joined someone else's game | no (the host runs it) | yes |
| Dedicated server (`--server`, also `--headless`) | yes | no |

### Over the network

When players join a hosted or dedicated server, remotes and the player list
travel over the real connection (`core/RobloxRemoteNet.cpp`, carried by
`net/NetworkSession` as one message type):

- `FireServer`/`InvokeServer` on a client reach the server's
  `OnServerEvent`/`OnServerInvoke`, with that client's `Player` first.
- `FireClient`/`InvokeClient` reach that player's computer, and
  `FireAllClients` reaches everyone.
- Every player who joins shows up in `Players` on the server
  (`PlayerAdded`, `GetPlayers`, leaderstats). Leaving fires `PlayerRemoving`.
- Arguments keep their type: nil, booleans, numbers, strings, Vector3,
  CFrame, Color3, BrickColor, Enum items, tables (nested up to 32 deep),
  Players and other Instances. An Instance is sent by its path from `game`,
  so it arrives as the same object on the other side, or `nil` if the other
  side doesn't have it.

Limits, like Roblox's:

| Limit | Value | What happens |
|---|---|---|
| One message | 64 KiB | the call errors with "Remote payload is too large" |
| `UnreliableRemoteEvent` | 900 bytes | dropped with a warning in the log |
| Calls from one player to the server | 240 at once, then 120 per second | extra calls are dropped with a warning |

An `InvokeClient` to a player who leaves fails with "Player has left the
game". When the connection drops, waiting invokes fail with "The connection
was lost".

Both sides must load the same game, since remotes are found by path. An
online join through the catalogue and `engine_runtime --client <address>
<port> --game <slug>` do this. The network protocol version is now 2, so
older Players can't join newer servers.

`engine/tests/remote_network_smoke.sh` checks all of this with a real
windowless server and client on a small test place
(`engine/tests/fixtures/games/remote_check`).

### Replication

The server copies its objects to every client, like Roblox
(`core/RobloxReplication.cpp`):

- **What is copied:** everything in `Workspace`, `ReplicatedStorage`,
  `ReplicatedFirst`, `Lighting`, `Players`, `StarterGui`, `StarterPack`,
  `StarterPlayer`, `Teams` and `SoundService`. That's names, parents,
  properties (those marked replicated) and attributes.
- **What isn't:** `ServerStorage` and `ServerScriptService` stay on the
  server. Each player's `Backpack`, `PlayerGui` and `PlayerScripts` are made
  by that player's own computer from the Starter folders, and characters
  move through the avatar sync. Objects that are only Kronos entities (no
  Roblox class) aren't copied either.
- **When:** a joining player gets a full copy. After that, only what
  changed is sent, 20 times a second. Just before a server script's remote
  message goes out, pending changes go first. So a part made right before
  `FireClient` already exists when the event arrives.
- **Matching:** both sides load the same place, so the first copy matches
  objects by parent, class and name instead of making them twice. Players
  are matched to the client's own `Player` objects. Anything the client
  loaded that the server no longer has is removed.
- **LocalScripts wait** until the first copy has arrived (at most 15
  seconds), so they see the server's world, not just the place file.
- **One way:** a client's own changes stay on that client until the server
  changes the same thing. In Roblox that's also true for everything except
  the player's own character.
- On a client, unanchored parts are moved by the server, so they are
  kinematic there instead of being simulated twice.

`engine/tests/remote_network_smoke.sh` checks it with a real server and
client: the client finds the part the server made when the remote arrives.

### Differences from Roblox (known limits)

- Replication sends whole objects when anything in them changes, and the
  server compares everything 20 times a second. That's fine for small and
  medium places; very large places will need change tracking.
- Moving parts update 20 times a second with no smoothing in between.
- A `LocalScript` or `ModuleScript` the server makes while the game runs
  arrives without its code. Scripts that come with the place work.
- On a client, the player's own avatar uses simple movement without
  physics, so it doesn't bump into parts yet.
- On a dedicated server, other players are plain moving points without a
  physics body, so they don't touch parts (no `Touched` or kill bricks on
  the server for them). On a client, other players have no `Character`.
- Two objects with the same name under the same parent are told apart by
  their order. Give remotes unique names to be safe.
- A client that isn't connected drops `FireServer` without an error.
- Joining a game from the Player's LAN session list doesn't load the host's
  game yet, so its remotes aren't found there.
- If a script is stopped while its `OnServerInvoke` is waiting, the caller
  gets an error ("The callback's script stopped"). If the callback's thread is
  killed mid-wait in another way, the caller may wait forever.
- Error messages from a wrong-side invoke include the position inside
  Kronos's own `Invoke` wrapper (`Invoke:N:`).
- The importer's `autoRunImportedScripts` option no longer does anything;
  the rules above decide which scripts run.

## DataStores (step 7)

Games can now save data between sessions, like coins or levels, with
Roblox's `DataStoreService`. For now the data lives in a local file on the
machine running the server. Saving to the Kronos backend (so data follows a
player between servers) comes later. The code is in
`engine/src/core/RobloxDataStore.cpp` (storage, limits) and the
"DataStoreService" part of `core/ScriptInstanceApi.cpp` (the Luau side).

```lua
local DataStoreService = game:GetService("DataStoreService")
local coinsStore = DataStoreService:GetDataStore("Coins")

game:GetService("Players").PlayerAdded:Connect(function(player)
    local coins = coinsStore:GetAsync("player_" .. player.UserId) or 0
    print(player.Name .. " has " .. coins .. " coins")
    coinsStore:IncrementAsync("player_" .. player.UserId, 10)
end)
```

### What works

| Area | Supported |
|---|---|
| `DataStoreService` | `GetDataStore(name, scope)` (scope defaults to `"global"`; the same name and scope give the same object), `GetGlobalDataStore()` |
| `DataStore` | `GetAsync`, `SetAsync`, `UpdateAsync`, `RemoveAsync` (returns the old value), `IncrementAsync` (whole-number steps; the stored value must be a number or nil) |
| Values | `nil`, booleans, numbers, strings, and tables of them (arrays, or tables with string keys), nested up to 100 levels |
| Errors | Roblox's messages: names and keys over 50 characters, values over 4 MB, invalid UTF-8, NaN/infinity, Instances, datatypes such as `Vector3` (store their numbers instead), tables that mix array items and keys, `SetAsync` with `nil` |
| Client | A LocalScript gets "DataStore can't be accessed from client", as in Roblox. Use a RemoteEvent and let the server save |
| Limits | Roblox's budget: 60 + 10 per player requests per minute for each request type, and a 6-second wait between writes to the same key. Going over only prints Roblox's "added to queue" warning; the request still runs |

`UpdateAsync` calls your function with the current value and saves what it
returns. Returning `nil` cancels the save. As in Roblox, the function can't
wait or yield.

### Where the data is saved

| Where the game runs | Saved in |
|---|---|
| The Player and dedicated servers | `datastores/<game-name>.json` in the folder the program was started from (`KRONOS_DATASTORE_DIR` changes the folder). One file per game, written on every change |
| Studio Play | In memory only. Data lasts until Studio closes, so a test run never touches real player data. (Roblox Studio needs "Enable Studio Access to API Services" for the same reason) |

### Differences from Roblox (known limits)

- Local files only, so each server has its own data. The backend version is
  planned and needs a server-side table and API.
- `GetAsync` returns only the value, not a `DataStoreKeyInfo`. There are no
  versions, metadata, user ids, `ListKeysAsync`, `OrderedDataStore`
  (`GetOrderedDataStore`) or `GetRequestBudgetForRequestType` yet.
- Requests finish straight away instead of waiting like a web request. Over
  the budget they still run (Roblox would queue or fail them).

## Common services (step 8)

The services most Roblox games use for movement, clean-up, sound and mood.
The code is in `engine/src/core/RobloxServices.cpp` (tweens, Debris, sounds,
Lighting), tags in `core/InstanceTree.cpp`, and the Luau side in
`core/ScriptInstanceApi.cpp`. Tweens, Debris timers and sounds advance once
per frame, just before `Heartbeat`.

```lua
local TweenService = game:GetService("TweenService")
local door = workspace.Door
local open = TweenService:Create(door, TweenInfo.new(1, Enum.EasingStyle.Quad),
    {Position = door.Position + Vector3.new(0, 8, 0)})
open.Completed:Connect(function(state) print("door", state.Name) end)
open:Play()

game:GetService("Debris"):AddItem(Instance.new("Part", workspace), 5)

local CollectionService = game:GetService("CollectionService")
for _, coin in CollectionService:GetTagged("Coin") do
    coin.Touched:Connect(function() coin:Destroy() end)
end
```

### What works

| Area | Supported |
|---|---|
| `TweenService` | `Create(instance, TweenInfo, goals)`, `GetValue(alpha, style, direction)`. Tweens `number`, `Vector3`, `CFrame`, `Color3` and `bool` properties (a bool switches at the end) |
| `Tween` | `Play`, `Pause`, `Cancel`, `PlaybackState`, `Instance`, `Completed(playbackState)`. All 11 `EasingStyle`s and 3 `EasingDirection`s, `RepeatCount` (-1 = forever), `Reverses`, `DelayTime`. Playing a tween cancels an older one that moves the same property of the same object |
| `Debris` | `AddItem(instance, lifetime)` (10 s by default) destroys the object later, even if the script that added it has stopped |
| `CollectionService` | `AddTag`, `RemoveTag`, `HasTag`, `GetTags`, `GetTagged`, `GetAllTags`, `GetInstanceAddedSignal`, `GetInstanceRemovedSignal`. Also the newer `Instance:AddTag/RemoveTag/HasTag/GetTags`. Tags are saved in scenes, copied by `Clone`, sent to players by replication, and read from imported `.rbxlx` places |
| `Sound` | `SoundId` (a sound file in your game folder), `Volume` (0.5 by default), `PlaybackSpeed`, `Looped`, `Playing`, `IsPlaying`, `RollOffMinDistance`/`RollOffMaxDistance`; `Play` (from the start), `Stop`, `Pause`, `Resume`; `Played`, `Stopped`, `Paused`, `Resumed`, `Ended`. A Sound inside a part plays from the part's position; anywhere else it plays everywhere. It goes through the Kronos audio mixer (`AUDIO_MIXER.md`) |
| `Lighting` | `ClockTime`, `TimeOfDay`, `GetMinutesAfterMidnight`, `SetMinutesAfterMidnight`, `Brightness` (scales the sun; 2 is normal), `Ambient`, `OutdoorAmbient`, `FogColor`, `FogEnd`. Imported places keep these values. Games still start at 2 pm |

### Differences from Roblox (known limits)

- `SoundId = "rbxassetid://..."` can't play: Kronos can't download Roblox's
  sounds. Put the file in your game folder and use its path. A warning says so
  once per id.
- Two Sounds with the same file share one voice (the 4.4 "per-source sound
  instances" item fixes this). `TimePosition`, `TimeLength`, `SoundGroup`s,
  `PlayOnRemove` and sound effects are not built yet.
- `Ended` only fires where audio is really playing (not on a windowless
  server).
- Lighting shows in the Player only. Studio's viewport keeps its own editing
  light, even during Play. `FogStart`, `GlobalShadows`, `ExposureCompensation`,
  `GeographicLatitude`, `Sky` and post effects are stored but don't change
  the picture yet.
- Each `TweenService:Create` makes a small object that stays in memory until
  the scene reloads (like other objects with no parent, see step 3).
- Tag signals fire when a tag is added to or removed from an object in the
  game, and when a tagged object is destroyed. They don't fire when a tagged
  object is moved into or out of the game. As with other events, a handler
  for a destroyed object runs after it is gone, so it can't read its `Name`.

## GUI (step 9)

Roblox's on-screen interface: buttons, labels and panels in a `ScreenGui`.
Each player sees their own `PlayerGui`, which is filled from `StarterGui`
when their character spawns (step 6). The code is in
`engine/src/core/RobloxGui.cpp` (layout, clicks, drawing). It runs in the
Player and during Studio Play.

```lua
local gui = Instance.new("ScreenGui")
gui.Parent = game.Players.LocalPlayer.PlayerGui

local panel = Instance.new("Frame")
panel.Size = UDim2.new(0, 260, 0, 140)
panel.Position = UDim2.fromScale(0.5, 0.5)
panel.AnchorPoint = Vector2.new(0.5, 0.5)
panel.Parent = gui
Instance.new("UICorner").Parent = panel
Instance.new("UIListLayout").Parent = panel

local buy = Instance.new("TextButton")
buy.Size = UDim2.new(1, 0, 0, 50)
buy.Text = "Buy Sword (10)"
buy.Parent = panel
buy.MouseButton1Click:Connect(function() buy.Text = "Bought!" end)
```

### What works

| Area | Supported |
|---|---|
| Value types | `UDim`, `UDim2` and `Vector2` properties (read, write, tween, save in scenes, replicate, send through remotes) |
| `ScreenGui` | `Enabled`, `DisplayOrder`, `ResetOnSpawn` |
| Every GUI object | `Size`, `Position`, `AnchorPoint`, `BackgroundColor3`, `BackgroundTransparency`, `BorderColor3`, `BorderSizePixel`, `Visible`, `ZIndex`, `LayoutOrder`, `ClipsDescendants`, read-only `AbsolutePosition`/`AbsoluteSize`; `MouseEnter`, `MouseLeave` |
| `Frame` | Yes |
| `TextLabel`, `TextButton` | `Text`, `TextColor3`, `TextSize`, `TextScaled`, `TextWrapped`, `TextTransparency`, `TextXAlignment`, `TextYAlignment` |
| Buttons | `MouseButton1Click`, `MouseButton1Down(x, y)`, `MouseButton1Up(x, y)`, `Activated`; `AutoButtonColor` darkens on hover and press. A click counts only if the mouse goes down and up on the same button |
| `UIListLayout` | `FillDirection`, `Padding`, `SortOrder` (`LayoutOrder` or `Name`), `HorizontalAlignment`, `VerticalAlignment` |
| `UICorner`, `UIPadding` | `CornerRadius`; `PaddingLeft/Right/Top/Bottom` |
| Imported places | GUI properties are read from `.rbxlx` files (sizes, colours, text, alignment, corners) |

Drawing order is Roblox's default (`ZIndexBehavior.Sibling`): a parent first,
then its children, siblings by `ZIndex`. `ScreenGui`s draw in `DisplayOrder`
order. In the Player the GUI sits above the 3D view and under Kronos's own
panels (chat, menus).

### Differences from Roblox (known limits)

- `ImageLabel`/`ImageButton` draw their background but not the image yet,
  and `rbxassetid://` images can't be downloaded.
- One font for every `Font` value. No rich text, `TextBox`, `ScrollingFrame`,
  `UIGridLayout`, `UIScale`, `UIStroke`, `UIGradient`, `BillboardGui`,
  `SurfaceGui` or `Rotation` (stored, not drawn).
- No top-bar inset: `IgnoreGuiInset` changes nothing (Kronos has no top bar).
- Studio shows the GUI only during Play, not `StarterGui` while editing.
- Only the left mouse button and the pointer. Touch, gamepad selection and
  `GuiService` are not built.

## Binary files (step 10)

Roblox Studio saves places as `.rbxl` and models as `.rbxm` by default. These
are a binary format, not XML. Kronos now reads them too
(`migration/RbxBinaryReader.cpp`). The reader turns a binary file into the
same tree the `.rbxlx` reader makes, so the rest of the import (hydration,
safety scan, score, scripts) is shared.

### What works

- Studio: File → Import Roblox file takes `.rbxl`, `.rbxm`, `.rbxlx` and
  `.rbxmx`. The file type is found from its first bytes, not its name.
- `kronos_compat` scores `.rbxl` places as well as `.rbxlx`.
- Chunks: `META`, `SSTR`, `INST`, `PROP`, `PRNT`, `END`; LZ4 compression
  (Kronos has its own small decoder) and uncompressed chunks.
- Property types: string, bool, int, float, double, `UDim`, `UDim2`,
  `BrickColor`, `Color3`, `Vector2`, `Vector3`, `CFrame` (including the
  compact rotation ids), `Enum`, references, `Color3uint8`, int64, and
  `ProtectedString` (script `Source`). `Tags` are read as raw names.
- Damaged or cut-off files are refused with a message; they don't crash
  Studio.

### How it's tested

`tests/fixtures/rbx-test-files/` holds 13 small files from rbx-dom's test
set (MIT licence, see the README there). Each has a binary and an XML copy of
the same thing. `testRobloxBinaryFormat` reads both and checks that every
shared property value matches, plus LZ4, header detection, tag import and
broken files. Checked in Studio: importing `three-unique-parts/binary.rbxm`
builds three named, coloured, tilted parts, and Undo removes them.

### Differences from Roblox (known limits)

- Zstandard-compressed chunks (some newer files) are refused with a clear
  message. Saving the file again from Roblox Studio, or as `.rbxlx`, works
  around it.
- Not read yet: `Font` values, `SharedString` data (e.g. some `MeshPart`
  data), `NumberSequence`/`ColorSequence`/`NumberRange`, `Rect`,
  `PhysicalProperties`, `Faces`/`Axes`, `Optional CFrame`, `UniqueId` and
  `AttributesSerialize`. Those properties keep their Kronos defaults.
- Kronos can't save `.rbxl`/`.rbxm` files; it only imports them.

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
- Imported `Script`s start when you press Play (they used to stay switched
  off); see "Players and characters".

- Roblox Studio saves script sources inside `<![CDATA[ ... ]]>`. The importer
  used to stop at the first one and drop the rest of the place; it now reads
  them. Numeric XML codes such as `&#9;` are decoded too.
