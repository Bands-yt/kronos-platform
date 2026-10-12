-- Behaviour checks for round_system.rbxlx.
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local status = ReplicatedStorage.Status
local function waitFor(condition, limit)
	local waited = 0
	while not condition() and waited < limit do
		waited += task.wait(0.1)
	end
	return condition()
end
check("the round system shows an intermission", string.find(status.Value, "Intermission") ~= nil, status.Value)
check("a round starts after the intermission", waitFor(function() return status.Value == "Round 1" end, 5), status.Value)
task.wait(0.5)
check("players are moved to the arena", (hit.Position - workspace.ArenaSpawn.Position).Magnitude < 15, tostring(hit.Position))
check("the round ends", waitFor(function() return status.Value == "Round over" end, 8), status.Value)
task.wait(0.5)
check("everyone comes back to the lobby", (hit.Position - workspace.SpawnLocation.Position).Magnitude < 15, tostring(hit.Position))
check("rounds repeat", waitFor(function() return ReplicatedStorage.Round.Value >= 2 end, 6), tostring(ReplicatedStorage.Round.Value))
