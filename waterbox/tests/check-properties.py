#!/usr/bin/env python3
"""check-properties.py - the gate's properties leg: the Game State domain, read
the way Chimera reads it - by the property table (GetGameProperties), never by
the struct - holds what the game did.

usage: check-properties.py <dump> <movie|timers> [--swap NAME NAME]

<dump> is what a harness's --game-state wrote: every step's Game State block,
one after the other, and the property table beside it in <dump>.json.

  movie   waterbox/tests/gfz1-run.txt in Greenflower Zone Act 1: the player is
          in the level once the game state says so; Forward moves it and gives
          it speed; Jump lifts it; the Turn axis turns it (the angle changes
          only while it is held); speed is what the game reckons from the
          momentum (P_AproxDistance) whenever the player is in the air
  timers  the same with waterbox/tests/timers.lua loaded: from leveltime 20,
          speed shoes 200 and invincibility 150 counting down a tic at a time,
          air 1000 + leveltime and space 3000 + leveltime, and the conveyor and
          platform momenta 5000 + leveltime, -(5000 + leveltime) and
          7000 + leveltime

--swap NAME NAME reads the two properties each where the table puts the
other: the leg's teeth (a table that misplaces two fields of the same type
inside the block does not pass - Player.Angle and Player.Speed for the movie,
Timers.Air and Timers.Space for the timers).

Prints "ok ..." and exits 0, or the first thing wrong and exits 1.
"""
import json
import struct
import sys

FORMATS = {"bool": "<?", "u8": "<B", "s8": "<b", "u16": "<H", "s16": "<h", "u32": "<I", "s32": "<i"}
WANT = ["Game.Tic", "Game.Level Time", "Game.State", "Game.Map", "Player.In Level",
        "Player.X", "Player.Y", "Player.Z", "Player.Momentum X", "Player.Momentum Y", "Player.Momentum Z",
        "Player.Angle", "Player.Speed",
        "Timers.Speed Shoes", "Timers.Invincibility", "Timers.Space", "Timers.Air",
        "Player.Conveyor Momentum X", "Player.Conveyor Momentum Y", "Player.Platform Momentum Z"]
SET_BY_TIMERS = WANT[13:]
GS_LEVEL = 1


def fail(msg):
    print("FAIL " + msg)
    sys.exit(1)


def aprox_distance(dx, dy):
    # SRB2's P_AproxDistance: dx + dy - min(dx, dy) / 2, on 16.16 values
    dx, dy = abs(dx), abs(dy)
    return dx + dy - (min(dx, dy) >> 1)


def main():
    args = sys.argv[1:]
    swap = None
    if "--swap" in args:
        i = args.index("--swap")
        swap = args[i + 1:i + 3]
        del args[i:i + 3]
    if len(args) != 2 or args[1] not in ("movie", "timers"):
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    dump, mode = args

    table = json.load(open(dump + ".json"))
    props = table["properties"]
    if swap:
        a, b = (next(p for p in props if p["name"] == n) for n in swap)
        a["offset"], b["offset"] = b["offset"], a["offset"]
    names = [p["name"] for p in props]
    if names != WANT:
        fail(f"the table's names are {names}, not {WANT}")
    size = max(p["offset"] + struct.calcsize(FORMATS[p["type"]]) for p in props)
    for p in props:
        if p.get("domain") != "Game State" or p.get("writable", True) is not False:
            fail(f"{p['name']}: not a read-only Game State property")

    data = open(dump, "rb").read()
    # the block's size: the dump is whole steps of it, at least as large as
    # the table reaches (72 bytes now; the table may not run past it)
    block = 72
    if len(data) % block or size > block:
        fail(f"the dump ({len(data)} bytes) is not whole {block}-byte blocks, or the table reaches {size}")
    steps = []
    for k in range(len(data) // block):
        b = data[k * block:(k + 1) * block]
        v = {}
        for p in props:
            off = p["offset"]
            fmt = FORMATS[p["type"]]
            if off < 0 or off + struct.calcsize(fmt) > block:
                fail(f"{p['name']} read at {off}, outside the block")
            v[p["name"]] = struct.unpack_from(fmt, b, off)[0]
        steps.append(v)

    level = [(i + 1, v) for i, v in enumerate(steps) if v["Game.State"] == GS_LEVEL and v["Player.In Level"]]
    if not level:
        fail("the player is never in the level")
    for n, v in enumerate(steps, 1):
        if v["Player.In Level"] and v["Game.State"] != GS_LEVEL:
            fail(f"step {n}: in a level, the game state says {v['Game.State']}")
        if v["Game.Map"] != 1 and v["Game.State"] == GS_LEVEL:
            fail(f"step {n}: map {v['Game.Map']}, not Greenflower Zone Act 1")

    if mode == "movie":
        # steps of the movie (waterbox/tests/gfz1-run.txt): Forward 80-260,
        # Jump 120-130, Turn 180-220
        at = dict(level)
        before, after = at[79], at[170]
        moved = abs(after["Player.X"] - before["Player.X"]) + abs(after["Player.Y"] - before["Player.Y"])
        if moved < 200 * 65536:
            fail(f"Forward from step 80 to 170 moved the player {moved / 65536:.2f} units")
        if max(at[s]["Player.Speed"] for s in range(100, 170)) < 20 * 65536:
            fail("no speed while running")
        if max(at[s]["Player.Z"] for s in range(121, 160)) <= before["Player.Z"] + 32 * 65536:
            fail("the jump did not lift the player")
        if max(at[s]["Player.Momentum Z"] for s in range(121, 135)) <= 0:
            fail("no upward momentum after Jump")
        turned = {at[s]["Player.Angle"] for s in range(182, 221)}
        if len(turned) < 10:
            fail(f"the Turn axis held: {len(turned)} angles")
        if len({at[s]["Player.Angle"] for s in range(140, 180)}) != 1 or len({at[s]["Player.Angle"] for s in range(225, len(steps) + 1)}) != 1:
            fail("the angle changed with no Turn held")
        airborne = 0
        for s in range(122, 160):
            v = at[s]
            # in the air speed is the momentum's (no floor to be relative to)
            if v["Player.Momentum Z"] != 0:
                airborne += 1
                want = aprox_distance(v["Player.Momentum X"], v["Player.Momentum Y"])
                if abs(v["Player.Speed"] - want) > 2:
                    fail(f"step {s}: speed {v['Player.Speed']}, the momentum's {want}")
        if airborne < 10:
            fail(f"only {airborne} airborne steps to compare speed with momentum")
        # nothing collected, no conveyor and no moving floor on the movie's path
        for name in SET_BY_TIMERS:
            if any(v[name] for v in steps):
                fail(f"{name} is not 0 on the movie's path")
        print(f"ok movie: {len(level)} steps in the level; moved {moved / 65536:.0f} units, jumped, "
              f"{len(turned)} angles while turning, speed the momentum's on {airborne} airborne steps")
    else:
        seen = 0
        for s, v in level:
            lt = v["Game.Level Time"]
            if lt < 20:
                continue
            seen += 1
            want = {"Timers.Speed Shoes": max(0, 200 - (lt - 20)), "Timers.Invincibility": max(0, 150 - (lt - 20)),
                    "Timers.Air": 1000 + lt, "Timers.Space": 3000 + lt,
                    "Player.Conveyor Momentum X": 5000 + lt, "Player.Conveyor Momentum Y": -(5000 + lt),
                    "Player.Platform Momentum Z": 7000 + lt}
            for name, w in want.items():
                if v[name] != w:
                    fail(f"step {s} (leveltime {lt}): {name} {v[name]}, timers.lua set {w}")
        if seen < 150:
            fail(f"only {seen} steps from leveltime 20")
        print(f"ok timers: speed shoes, invincibility, air, space and the conveyor and platform momenta as timers.lua set them, {seen} steps")


main()
