-- Luau half of the cross-language sample: it counts seconds, Main.py reacts to bumps.
local seconds = 0
while true do
    task.wait(1)
    seconds = seconds + 1
    print("Luau says", seconds, "seconds")
end
