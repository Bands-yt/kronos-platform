-- Behaviour checks for leaderboard.rbxlx.
local points = player:WaitForChild("leaderstats"):WaitForChild("Points")
check("the player gets a Points stat", points.Value == 0, "Points " .. points.Value)
touch(workspace.PointsPad, hit)
task.wait(0.2)
check("touching the pad adds a point", points.Value == 1, "Points " .. points.Value)
