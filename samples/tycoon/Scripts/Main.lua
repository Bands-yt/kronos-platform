-- Tycoon: a Dropper makes coins, the Collector turns them into money.
local money = 0
local dropper = workspace:FindFirstChild("Dropper")
local collector = workspace:FindFirstChild("Collector")

local function drop()
    local coin = Instance.new("Part")
    coin.Name = "Coin"
    coin.Size = Vector3.new(1, 0.2, 1)
    coin.Position = dropper.Position + Vector3.new(0, 3, 0)
    coin.Parent = workspace
    game:GetService("Debris"):AddItem(coin, 10)
end

if collector then
    collector.Touched:Connect(function(hit)
        if hit.Name == "Coin" then
            hit:Destroy()
            money = money + 5
            print("Money:", money)
        end
    end)
end

while dropper do
    drop()
    task.wait(2)
end
