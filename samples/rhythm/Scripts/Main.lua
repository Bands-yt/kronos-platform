-- Rhythm: a note appears every beat; press Space near the beat to score.
local UserInputService = game:GetService("UserInputService")
local bpm = 120
local beat = 60 / bpm
local score, nextBeat = 0, os.clock() + beat

UserInputService.InputBegan:Connect(function(input)
    if input.KeyCode == Enum.KeyCode.Space then
        local off = math.abs(os.clock() - nextBeat)
        if off < 0.15 then
            score = score + 1
            print("Hit! Score:", score)
        else
            print("Miss by", math.floor(off * 1000), "ms")
        end
    end
end)

while true do
    task.wait(beat)
    nextBeat = os.clock() + beat
end
