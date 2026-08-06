#!/usr/bin/env python3
"""Generate a seeded pseudo-random gameplay input script for the harness.
Boot -> Start into game with defaults -> random play inputs."""
import random, sys

seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
frames = int(sys.argv[2]) if len(sys.argv) > 2 else 150000
rng = random.Random(seed)

ev = [(950, 955, "S"), (1250, 1255, "S"), (1550, 1555, "S"), (1850, 1855, "S")]
DIRS = ["U", "D", "L", "R", "UL", "UR", "DL", "DR", ""]
BTNS = ["", "A", "B", "C", "B", "C"]          # B/C weighted (shoot/pass)
f = 2200
while f < frames - 100:
    hold = rng.randint(8, 45)
    mask = rng.choice(DIRS) + rng.choice(BTNS)
    if mask:
        ev.append((f, f + hold, mask))
    f += hold + rng.randint(2, 25)
    if rng.random() < 0.004:                  # rare pause on/off to hit pause menu
        ev.append((f, f + 4, "S"))
        f += 120
        ev.append((f, f + 4, "S"))
        f += 60

for s, e, m in ev:
    print(s, e, m)
