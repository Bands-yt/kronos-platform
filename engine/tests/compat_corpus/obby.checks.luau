-- Behaviour checks for obby.rbxlx (run by kronos_compat / Bridge Doctor).
local humanoid = character and character:FindFirstChildOfClass("Humanoid")
check("the player has a character with a Humanoid", humanoid ~= nil)
local bar = workspace.SpinnerBar
local before = bar.CFrame.LookVector
task.wait(0.5)
check("the spinner bar turns", (bar.CFrame.LookVector - before).Magnitude > 0.01)
touch(workspace.KillBrick, hit)
task.wait(0.2)
check("touching the kill brick kills the player", humanoid ~= nil and humanoid.Health == 0, humanoid and ("Health " .. humanoid.Health))
