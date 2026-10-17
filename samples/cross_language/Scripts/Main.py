# Python half of the cross-language sample (see Main.lua).
bumps = 0

@events.on_collision
def bump(a, b):
    global bumps
    bumps += 1
    print("Python counted bump", bumps)
