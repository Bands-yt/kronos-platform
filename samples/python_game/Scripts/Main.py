# A crate that spins and counts the bumps it gets. See docs/PYTHON_SCRIPTS.md.
crate = world.find("Crate")
bumps = 0

@events.on_update
def spin(dt):
    x, y, z = world.get_position(crate)
    world.set_position(crate, x, 1 + 0.5 * (world.time() % 2), z)

@events.on_collision
def bump(a, b):
    global bumps
    bumps += 1
    print("bump", bumps, world.name(a), world.name(b))
