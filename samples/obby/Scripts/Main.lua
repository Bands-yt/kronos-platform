-- Obby: touch a Checkpoint to save your spot, touch a KillBrick to go back to it.
local Players = game:GetService("Players")
local spawnPoints = {}

local function respawnAt(player)
    local point = spawnPoints[player]
    if point and player.Character then
        player.Character:PivotTo(point.CFrame + Vector3.new(0, 4, 0))
    end
end

for _, part in ipairs(workspace:GetChildren()) do
    if part.Name == "Checkpoint" then
        part.Touched:Connect(function(hit)
            local player = Players:GetPlayerFromCharacter(hit.Parent)
            if player then
                spawnPoints[player] = part
            end
        end)
    elseif part.Name == "KillBrick" then
        part.Touched:Connect(function(hit)
            local player = Players:GetPlayerFromCharacter(hit.Parent)
            if player then
                respawnAt(player)
            end
        end)
    end
end
