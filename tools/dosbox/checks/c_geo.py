"""A GEO map's squares and passages (README, 3D view and Adventure loop:
cok_view_wall, cok_view_passage), and paths between squares as Move keys,
for batch C's checks to walk the party in both games."""
import collections
import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent))
import cokbox  # noqa: E402

STEP = {0: (0, -1), 2: (1, 0), 4: (0, 1), 6: (-1, 0)}


def dax_record(path, wanted):
    """Record WANTED of a DAX file, decoded (src/dax.c: a count, 9-byte
    entries of id, offset, packed and decoded sizes, then run-length data)."""
    data = pathlib.Path(path).read_bytes()
    count = struct.unpack_from("<H", data, 0)[0] // 9
    for i in range(count):
        rid, off, size, packed = struct.unpack_from("<BIHH", data, 2 + i * 9)
        if rid != wanted:
            continue
        src = data[2 + count * 9 + off:2 + count * 9 + off + packed]
        out, j = bytearray(), 0
        while j < len(src):
            c = src[j]
            j += 1
            if c in (127, 128):
                continue
            if c < 127:
                out += src[j:j + c + 1]
                j += c + 1
            else:
                out += bytes([src[j]]) * (256 - c)
                j += 1
        return bytes(out)
    raise KeyError(f"{path}: no record {wanted}")


def load(file, record):
    """GEO<file>.DAX record RECORD's four tables (after its two bytes)."""
    return dax_record(cokbox.ROOT / f"Assets/GEO{file}.DAX", record)[2:]


def wall(m, d, x, y):
    i = (y % 16) * 16 + x % 16
    return {0: m[i] >> 4, 2: m[i] & 15, 4: m[0x100 + i] >> 4, 6: m[0x100 + i] & 15}[d]


def passable(m, d, x, y):
    if wall(m, d, x, y) == 0:
        return True
    bits = m[0x300 + (y % 16) * 16 + x % 16] >> d & 3
    return bits == 1


def square(m, x, y):
    return m[0x200 + (y % 16) * 16 + x % 16]


def path(m, start, goal, avoid=()):
    """Move keys (in Move mode: \\^ ahead, \\< \\> turns) from START (x, y,
    dir) to GOAL (x, y) or (x, y, dir), not through AVOID squares."""
    sx, sy, sd = start
    prev = {(sx, sy): None}
    queue = collections.deque([(sx, sy)])
    while queue:
        x, y = queue.popleft()
        if (x, y) == tuple(goal[:2]):
            break
        for d, (dx, dy) in STEP.items():
            nx, ny = (x + dx) % 16, (y + dy) % 16
            if (nx, ny) in prev or (nx, ny) in avoid or not passable(m, d, x, y):
                continue
            prev[(nx, ny)] = ((x, y), d)
            queue.append((nx, ny))
    else:
        raise SystemExit(f"no path from {start} to {goal}")
    dirs = []
    at = tuple(goal[:2])
    while prev[at] is not None:
        at, d = prev[at]
        dirs.append(d)
    dirs.reverse()
    if len(goal) > 2:
        dirs.append(("face", goal[2]))
    keys, facing = "", sd
    for d in dirs:
        face = d[1] if isinstance(d, tuple) else d
        turn = (face - facing) % 8
        keys += {0: "", 2: r"\>", 4: r"\>\>", 6: r"\<"}[turn]
        facing = face
        if not isinstance(d, tuple):
            keys += r"\^"
    return keys


def approaches(m, target, quiet=(0,)):
    """(x, y, dir) squares beside TARGET whose special (square & 63) is in
    QUIET, facing it, from which a step goes in."""
    tx, ty = target
    out = []
    for d, (dx, dy) in STEP.items():
        x, y = (tx - dx) % 16, (ty - dy) % 16
        if square(m, x, y) & 63 in quiet and passable(m, d, x, y):
            out.append((x, y, d))
    return out
