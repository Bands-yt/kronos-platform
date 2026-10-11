-- Behaviour checks for door.rbxlx.
local door = workspace.Door
check("the door starts closed", door.CanCollide == true)
touch(door, hit)
task.wait(0.2)
check("touching the door opens it", door.CanCollide == false)
task.wait(4)
check("the door closes again after 3 seconds", door.CanCollide == true and door.Transparency < 0.01, "Transparency " .. door.Transparency)
