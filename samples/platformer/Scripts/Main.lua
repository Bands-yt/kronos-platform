-- Platformer: JumpPad parts launch you, Coin parts add to your score.
local Players = game:GetService("Players")
local score = 0

for _, part in ipairs(workspace:GetChildren()) do
    if part.Name == "JumpPad" then
        part.Touched:Connect(function(hit)
            local root = hit.Parent and hit.Parent:FindFirstChild("HumanoidRootPart")
            if root then
                root.AssemblyLinearVelocity = Vector3.new(0, 80, 0)
            end
        end)
    elseif part.Name == "Coin" then
        part.Touched:Connect(function(hit)
            if Players:GetPlayerFromCharacter(hit.Parent) then
                score = score + 1
                part:Destroy()
                print("Coins:", score)
            end
        end)
    end
end
