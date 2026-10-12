# Writes the compatibility test places (our own, free to use). Run: python3 make_corpus.py
import os
from xml.sax.saxutils import escape
OUT = os.path.dirname(os.path.abspath(__file__))
ref = [0]
def nref():
    ref[0] += 1; return 'RBX%d' % ref[0]
def item(cls, name, props='', children=()):
    return ('<Item class="%s" referent="%s"><Properties><string name="Name">%s</string>%s</Properties>%s</Item>'
            % (cls, nref(), name, props, ''.join(children)))
def part(name, pos, size, rgb=(163,162,165), anchored=True, cls='Part', extra='', children=()):
    x,y,z = pos; sx,sy,sz = size
    packed = 0xFF000000 | (rgb[0]<<16) | (rgb[1]<<8) | rgb[2]
    cf = '<CoordinateFrame name="CFrame"><X>%g</X><Y>%g</Y><Z>%g</Z><R00>1</R00><R01>0</R01><R02>0</R02><R10>0</R10><R11>1</R11><R12>0</R12><R20>0</R20><R21>0</R21><R22>1</R22></CoordinateFrame>' % (x,y,z)
    props = (cf + '<Vector3 name="size"><X>%g</X><Y>%g</Y><Z>%g</Z></Vector3>' % (sx,sy,sz)
             + '<Color3uint8 name="Color3uint8">%d</Color3uint8>' % packed
             + '<bool name="Anchored">%s</bool>' % ('true' if anchored else 'false') + extra)
    return item(cls, name, props, children)
def script(name, src, cls='Script'):
    return item(cls, name, '<ProtectedString name="Source"><![CDATA[%s]]></ProtectedString>' % (src.strip() + '\n'))
def place(fname, *top):
    ref[0] = 0
    doc = '<roblox version="4">' + ''.join(top) + '</roblox>\n'
    open(os.path.join(OUT, fname), 'w').write(doc)

LEADERSTATS = '''
local Players = game:GetService("Players")

Players.PlayerAdded:Connect(function(player)
	local leaderstats = Instance.new("Folder")
	leaderstats.Name = "leaderstats"
	leaderstats.Parent = player

	local cash = Instance.new("IntValue")
	cash.Name = "Cash"
	cash.Value = 0
	cash.Parent = leaderstats
end)
'''

place('obby.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (64,1,64), (91,154,76)),
    part('SpawnLocation', (0,0.5,-24), (6,1,6), (13,105,172), cls='SpawnLocation'),
    part('Stage1', (0,2,-14), (4,1,4), (196,40,28)),
    part('Stage2', (0,4,-6), (4,1,4), (245,205,48)),
    part('KillBrick', (0,1,2), (8,0.5,6), (255,0,0)),
    part('Stage3', (0,6,10), (4,1,4), (75,151,75)),
    part('SpinnerBar', (0,7.5,16), (14,1,1), (99,95,98), children=[script('Spin', '''
local RunService = game:GetService("RunService")
local bar = script.Parent

RunService.Heartbeat:Connect(function(dt)
	bar.CFrame = bar.CFrame * CFrame.Angles(0, math.rad(90) * dt, 0)
end)
''')]),
    part('Finish', (0,8,22), (6,1,6), (255,255,255)),
    script('KillBricks', '''
local killBrick = workspace:WaitForChild("KillBrick")

killBrick.Touched:Connect(function(hit)
	local humanoid = hit.Parent:FindFirstChild("Humanoid")
	if humanoid then
		humanoid.Health = 0
	end
end)
'''),
    script('FinishLine', '''
local Players = game:GetService("Players")
local finish = workspace.Finish
local finished = {}

finish.Touched:Connect(function(hit)
	local player = Players:GetPlayerFromCharacter(hit.Parent)
	if player and not finished[player] then
		finished[player] = true
		print(player.Name .. " finished the obby!")
	end
end)
'''),
    script('Welcome', 'print("Obby loaded")'),
  ]))

place('door.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (40,1,40), (91,154,76)),
    part('SpawnLocation', (0,0.5,-12), (6,1,6), (13,105,172), cls='SpawnLocation'),
    part('WallLeft', (-6,4,0), (8,8,1), (105,64,40)),
    part('WallRight', (6,4,0), (8,8,1), (105,64,40)),
    part('Door', (0,4,0), (4,8,1), (160,95,53), children=[script('DoorScript', '''
local TweenService = game:GetService("TweenService")
local door = script.Parent
local isOpen = false
local info = TweenInfo.new(0.5)

door.Touched:Connect(function(hit)
	if isOpen or not hit.Parent:FindFirstChild("Humanoid") then
		return
	end
	isOpen = true
	TweenService:Create(door, info, {Transparency = 0.8}):Play()
	door.CanCollide = false
	wait(3)
	TweenService:Create(door, info, {Transparency = 0}):Play()
	door.CanCollide = true
	isOpen = false
end)
''')]),
  ]))

place('tycoon_button.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (48,1,48), (91,154,76)),
    part('SpawnLocation', (0,0.5,-16), (6,1,6), (13,105,172), cls='SpawnLocation'),
    item('Model', 'Tycoon', '', [
      part('Dropper', (8,8,0), (3,3,3), (99,95,98)),
      part('Conveyor', (8,1,6), (4,1,14), (27,42,53)),
      part('Collector', (8,1.5,14), (4,1,4), (75,151,75)),
      part('BuyButton', (-4,0.6,0), (4,0.2,4), (0,255,0)),
      part('Wall', (-4,4,8), (10,8,1), (163,162,165)),
      script('DropperScript', '''
local Debris = game:GetService("Debris")
local dropper = script.Parent.Dropper

while true do
	local drop = Instance.new("Part")
	drop.Size = Vector3.new(1, 1, 1)
	drop.Position = dropper.Position - Vector3.new(0, 2, 0)
	drop.BrickColor = BrickColor.new("Bright yellow")
	drop:SetAttribute("Value", 5)
	drop.Parent = workspace
	Debris:AddItem(drop, 10)
	task.wait(1.5)
end
'''),
      script('CollectorScript', '''
local collector = script.Parent.Collector
local owner = nil

collector.Touched:Connect(function(hit)
	local value = hit:GetAttribute("Value")
	if value and owner then
		owner.leaderstats.Cash.Value += value
		hit:Destroy()
	end
end)

script.Parent:GetAttributeChangedSignal("Owner"):Connect(function()
	owner = game:GetService("Players"):FindFirstChild(script.Parent:GetAttribute("Owner"))
end)
'''),
      script('BuyButtonScript', '''
local Players = game:GetService("Players")
local tycoon = script.Parent
local button = tycoon.BuyButton
local wall = tycoon.Wall
local price = 25

button.Touched:Connect(function(hit)
	local player = Players:GetPlayerFromCharacter(hit.Parent)
	if not player then return end
	tycoon:SetAttribute("Owner", player.Name)
	local cash = player.leaderstats.Cash
	if cash.Value >= price and wall.Parent then
		cash.Value -= price
		wall:Destroy()
		button.BrickColor = BrickColor.new("Dark stone grey")
	end
end)
'''),
    ]),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [script('Leaderstats', LEADERSTATS)]))

place('leaderboard.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (40,1,40), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
    part('PointsPad', (10,0.6,0), (4,0.2,4), (245,205,48)),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [script('PointsLeaderboard', '''
local Players = game:GetService("Players")
local DataStoreService = game:GetService("DataStoreService")
local store = DataStoreService:GetDataStore("PointsV1")

Players.PlayerAdded:Connect(function(player)
	local leaderstats = Instance.new("Folder")
	leaderstats.Name = "leaderstats"
	leaderstats.Parent = player

	local points = Instance.new("IntValue")
	points.Name = "Points"
	points.Parent = leaderstats

	local ok, saved = pcall(function()
		return store:GetAsync(tostring(player.UserId))
	end)
	if ok and saved then
		points.Value = saved
	end
end)

Players.PlayerRemoving:Connect(function(player)
	local points = player.leaderstats.Points.Value
	pcall(function()
		store:SetAsync(tostring(player.UserId), points)
	end)
end)

local pad = workspace:WaitForChild("PointsPad")
pad.Touched:Connect(function(hit)
	local player = Players:GetPlayerFromCharacter(hit.Parent)
	if player then
		player.leaderstats.Points.Value += 1
	end
end)
''')]))

place('gui_shop.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (40,1,40), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
  ]),
  item('ReplicatedStorage', 'ReplicatedStorage', '', [
    item('RemoteEvent', 'BuyItem'),
    item('Tool', 'Sword', '', [part('Handle', (0,0,0), (1,4,1), (163,162,165), anchored=False)]),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('Leaderstats', LEADERSTATS),
    script('ShopServer', '''
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local buyItem = ReplicatedStorage:WaitForChild("BuyItem")
local prices = { Sword = 10 }

buyItem.OnServerEvent:Connect(function(player, itemName)
	local price = prices[itemName]
	local cash = player.leaderstats.Cash
	if price and cash.Value >= price then
		cash.Value -= price
		ReplicatedStorage[itemName]:Clone().Parent = player.Backpack
	end
end)
'''),
  ]),
  item('StarterGui', 'StarterGui', '', [
    item('ScreenGui', 'ShopGui', '', [
      item('Frame', 'ShopFrame', '', [
        item('TextLabel', 'Title'),
        item('TextButton', 'BuySword', '', [script('BuyButton', '''
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local buyItem = ReplicatedStorage:WaitForChild("BuyItem")
local button = script.Parent

button.MouseButton1Click:Connect(function()
	buyItem:FireServer("Sword")
end)
''', cls='LocalScript')]),
      ]),
    ]),
  ]))


place('simulator.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (64,1,64), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
    item('Folder', 'Coins', '', [
      part('Coin', (10,1.5,0), (2,2,0.5), (245,205,48), extra='<bool name="CanCollide">false</bool>'),
      part('Coin', (14,1.5,0), (2,2,0.5), (245,205,48), extra='<bool name="CanCollide">false</bool>'),
      part('Coin', (18,1.5,0), (2,2,0.5), (245,205,48), extra='<bool name="CanCollide">false</bool>'),
    ]),
    part('SellPad', (0,0.6,12), (6,0.2,6), (0,255,0)),
    part('Dumbbell', (-10,1,0), (3,1,1), (27,42,53), children=[item('ClickDetector', 'ClickDetector')]),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('Stats', '''
local Players = game:GetService("Players")

Players.PlayerAdded:Connect(function(player)
	local leaderstats = Instance.new("Folder")
	leaderstats.Name = "leaderstats"
	leaderstats.Parent = player

	for _, name in { "Coins", "Cash", "Strength" } do
		local value = Instance.new("IntValue")
		value.Name = name
		value.Parent = leaderstats
	end
end)
'''),
    script('CoinSpawner', '''
local Players = game:GetService("Players")
local RESPAWN = 2

for _, coin in workspace.Coins:GetChildren() do
	local taken = false
	coin.Touched:Connect(function(hit)
		local player = Players:GetPlayerFromCharacter(hit.Parent)
		if not player or taken then return end
		taken = true
		player.leaderstats.Coins.Value += 1
		coin.Transparency = 1
		task.delay(RESPAWN, function()
			coin.Transparency = 0
			taken = false
		end)
	end)
end
'''),
    script('Sell', '''
local Players = game:GetService("Players")
local pad = workspace.SellPad

pad.Touched:Connect(function(hit)
	local player = Players:GetPlayerFromCharacter(hit.Parent)
	if not player then return end
	local stats = player.leaderstats
	stats.Cash.Value += stats.Coins.Value * 10
	stats.Coins.Value = 0
end)
'''),
    script('Training', '''
local detector = workspace.Dumbbell.ClickDetector

detector.MouseClick:Connect(function(player)
	player.leaderstats.Strength.Value += 1
end)
'''),
  ]))

place('combat_arena.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (64,1,64), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
    part('Lava', (16,0.6,0), (6,0.2,6), (255,85,0), extra='<token name="Material">288</token>'),
    part('HealPad', (-16,0.6,0), (6,0.2,6), (0,255,255)),
  ]),
  item('StarterPack', 'StarterPack', '', [
    item('Tool', 'Sword', '', [
      part('Handle', (0,0,0), (1,4,1), (163,162,165), anchored=False, extra='<bool name="CanCollide">false</bool>'),
      script('SwordScript', '''
local tool = script.Parent
local handle = tool.Handle
local DAMAGE = 25
local swinging = false
local hitThisSwing = {}

tool.Activated:Connect(function()
	if swinging then return end
	swinging = true
	hitThisSwing = {}
	task.wait(0.5)
	swinging = false
end)

handle.Touched:Connect(function(hit)
	if not swinging then return end
	local humanoid = hit.Parent:FindFirstChildOfClass("Humanoid")
	local owner = tool.Parent
	if humanoid and hit.Parent ~= owner and not hitThisSwing[humanoid] then
		hitThisSwing[humanoid] = true
		local tag = Instance.new("ObjectValue")
		tag.Name = "creator"
		tag.Value = game:GetService("Players"):GetPlayerFromCharacter(owner)
		tag.Parent = humanoid
		humanoid:TakeDamage(DAMAGE)
	end
end)
'''),
    ]),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('KillTracker', '''
local Players = game:GetService("Players")

Players.PlayerAdded:Connect(function(player)
	local leaderstats = Instance.new("Folder")
	leaderstats.Name = "leaderstats"
	leaderstats.Parent = player
	local kills = Instance.new("IntValue")
	kills.Name = "Kills"
	kills.Parent = leaderstats

	player.CharacterAdded:Connect(function(character)
		local humanoid = character:WaitForChild("Humanoid")
		humanoid.Died:Connect(function()
			local tag = humanoid:FindFirstChild("creator")
			if tag and tag.Value and tag.Value ~= player then
				tag.Value.leaderstats.Kills.Value += 1
			end
		end)
	end)
end)
'''),
    script('Hazards', '''
local lava = workspace.Lava
local heal = workspace.HealPad
local cooldown = {}

lava.Touched:Connect(function(hit)
	local humanoid = hit.Parent:FindFirstChildOfClass("Humanoid")
	if humanoid then
		humanoid.Health = 0
	end
end)

heal.Touched:Connect(function(hit)
	local humanoid = hit.Parent:FindFirstChildOfClass("Humanoid")
	if not humanoid or cooldown[humanoid] then return end
	cooldown[humanoid] = true
	humanoid.Health = math.min(humanoid.MaxHealth, humanoid.Health + 50)
	task.wait(1)
	cooldown[humanoid] = nil
end)
'''),
  ]))

ITEMS_MODULE = '''
local Items = {}

Items.List = {
	Apple = { price = 5, heal = 10 },
	Shield = { price = 40, heal = 0 },
	Potion = { price = 15, heal = 50 },
}

function Items.price(name)
	local item = Items.List[name]
	return item and item.price
end

return Items
'''

place('inventory.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (40,1,40), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
  ]),
  item('ReplicatedStorage', 'ReplicatedStorage', '', [
    script('Items', ITEMS_MODULE, cls='ModuleScript'),
    item('RemoteFunction', 'Buy'),
    item('RemoteEvent', 'InventoryChanged'),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('InventoryServer', '''
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local Items = require(ReplicatedStorage.Items)
local inventories = {}

Players.PlayerAdded:Connect(function(player)
	inventories[player] = {}
	player:SetAttribute("Gold", 50)
end)

Players.PlayerRemoving:Connect(function(player)
	inventories[player] = nil
end)

ReplicatedStorage.Buy.OnServerInvoke = function(player, itemName)
	local price = Items.price(itemName)
	if type(itemName) ~= "string" or not price then
		return false, "no such item"
	end
	local gold = player:GetAttribute("Gold")
	if gold < price then
		return false, "not enough gold"
	end
	player:SetAttribute("Gold", gold - price)
	local inventory = inventories[player]
	inventory[itemName] = (inventory[itemName] or 0) + 1
	ReplicatedStorage.InventoryChanged:FireClient(player, inventory)
	return true, inventory[itemName]
end
'''),
  ]),
  item('StarterPlayer', 'StarterPlayer', '', [
    item('StarterPlayerScripts', 'StarterPlayerScripts', '', [script('InventoryClient', '''
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local player = game:GetService("Players").LocalPlayer

ReplicatedStorage.InventoryChanged.OnClientEvent:Connect(function(inventory)
	local count = 0
	for _, amount in inventory do
		count += amount
	end
	player:SetAttribute("ItemCount", count)
end)
''', cls='LocalScript')]),
  ]))

place('round_system.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Lobby', (0,-0.5,0), (30,1,30), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
    part('Arena', (0,-0.5,100), (40,1,40), (163,162,165)),
    part('ArenaSpawn', (0,0.6,100), (4,0.2,4), (255,0,0)),
  ]),
  item('ReplicatedStorage', 'ReplicatedStorage', '', [
    item('StringValue', 'Status'),
    item('IntValue', 'Round'),
  ]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('RoundManager', '''
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local status = ReplicatedStorage.Status
local round = ReplicatedStorage.Round
local INTERMISSION = 3
local ROUND_LENGTH = 5

local function teleportAll(target)
	for i, player in Players:GetPlayers() do
		local character = player.Character
		if character and character.PrimaryPart then
			character:PivotTo(CFrame.new(target.Position + Vector3.new(i * 3, 3, 0)))
		end
	end
end

while true do
	for t = INTERMISSION, 1, -1 do
		status.Value = "Intermission: " .. t
		task.wait(1)
	end
	round.Value += 1
	status.Value = "Round " .. round.Value
	teleportAll(workspace.ArenaSpawn)
	task.wait(ROUND_LENGTH)
	status.Value = "Round over"
	teleportAll(workspace.SpawnLocation)
	task.wait(1)
end
'''),
  ]))

place('gui_menu.rbxlx',
  item('Workspace', 'Workspace', '', [
    part('Baseplate', (0,-0.5,0), (40,1,40), (91,154,76)),
    part('SpawnLocation', (0,0.5,0), (6,1,6), (13,105,172), cls='SpawnLocation'),
  ]),
  item('ReplicatedStorage', 'ReplicatedStorage', '', [item('RemoteEvent', 'RedeemCode')]),
  item('ServerScriptService', 'ServerScriptService', '', [
    script('Codes', '''
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local CODES = { KRONOS = 100, LAUNCH = 50 }
local used = {}

ReplicatedStorage.RedeemCode.OnServerEvent:Connect(function(player, code)
	if type(code) ~= "string" then return end
	code = string.upper(code)
	local reward = CODES[code]
	used[player] = used[player] or {}
	if reward and not used[player][code] then
		used[player][code] = true
		player:SetAttribute("Gems", (player:GetAttribute("Gems") or 0) + reward)
	end
end)
'''),
  ]),
  item('StarterGui', 'StarterGui', '', [
    item('ScreenGui', 'Menu', '', [
      item('Frame', 'Codes', '', [
        item('UICorner', 'UICorner'),
        item('UIStroke', 'UIStroke'),
        item('TextBox', 'CodeBox', '<string name="PlaceholderText">Enter a code</string><string name="Text"></string>'),
        item('TextButton', 'Redeem', '<string name="Text">Redeem</string>'),
      ]),
      item('ScrollingFrame', 'Shop', '<UDim2 name="Size"><XS>0</XS><XO>330</XO><YS>0</YS><YO>220</YO></UDim2>', [
        item('UIGridLayout', 'UIGridLayout'),
        item('TextButton', 'Item1', '<string name="Text">Hat</string>'),
        item('TextButton', 'Item2', '<string name="Text">Trail</string>'),
        item('TextButton', 'Item3', '<string name="Text">Pet</string>'),
      ]),
      script('MenuClient', '''
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local gui = script.Parent
local box = gui.Codes.CodeBox

local function redeem()
	if box.Text ~= "" then
		ReplicatedStorage.RedeemCode:FireServer(box.Text)
		box.Text = ""
	end
end

gui.Codes.Redeem.MouseButton1Click:Connect(redeem)
box.FocusLost:Connect(function(enterPressed)
	if enterPressed then
		redeem()
	end
end)

local count = 0
for _, child in gui.Shop:GetChildren() do
	if child:IsA("TextButton") then
		count += 1
	end
end
gui.Shop.CanvasSize = UDim2.new(0, 0, 0, count * 110)
''', cls='LocalScript'),
    ]),
  ]))
