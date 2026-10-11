# Python next to the place's Luau scripts (docs/PYTHON_SCRIPTS.md).
ticks = 0

@events.on_update
def count(dt):
    global ticks
    ticks += 1
    if ticks == 3:
        print("PYTHON ticked", ticks, "times")

print("PYTHON loaded")
