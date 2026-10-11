-- Behaviour checks for gui_shop.rbxlx.
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local cash = player:WaitForChild("leaderstats"):WaitForChild("Cash")
check("the shop GUI is copied to the player", player.PlayerGui:FindFirstChild("ShopGui") ~= nil)
ReplicatedStorage.BuyItem:FireServer("Sword")
task.wait(0.2)
check("buying without cash gives nothing", player.Backpack:FindFirstChild("Sword") == nil)
cash.Value = 15
ReplicatedStorage.BuyItem:FireServer("Sword")
task.wait(0.2)
check("buying with cash gives the Sword and takes the price", player.Backpack:FindFirstChild("Sword") ~= nil and cash.Value == 5, "Cash " .. cash.Value)
