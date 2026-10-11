-- Behaviour checks for tycoon_button.rbxlx.
local tycoon = workspace.Tycoon
local cash = player:WaitForChild("leaderstats"):WaitForChild("Cash")
check("the player gets a Cash stat", cash.Value == 0)
local drops = 0
for _, child in workspace:GetChildren() do
	if child:GetAttribute("Value") == 5 then drops += 1 end
end
check("the dropper makes drops", drops > 0, drops .. " drops")
touch(tycoon.BuyButton, hit)
task.wait(0.2)
check("touching the button claims the tycoon", tycoon:GetAttribute("Owner") == player.Name)
check("the wall stays while the player can't pay", tycoon:FindFirstChild("Wall") ~= nil)
cash.Value = 30
touch(tycoon.BuyButton, hit)
task.wait(0.2)
check("buying removes the wall and takes the price", tycoon:FindFirstChild("Wall") == nil and cash.Value == 5, "Cash " .. cash.Value)
