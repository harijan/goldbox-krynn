"""A model of 5b04:3a6f, a bolt's way (the port's bolt() in src/cast.c),
on a 50 x 25 combat map of walls (cost 0xff) and floor, with no combatants:
whether a bolt from CASTER aimed at SPELL bounces for ever ("forever", the
state of a line after a bounce met again), passes a cell for ever
("passes"), or ends. Batch E's #11 (e_bolt_bounce.py) uses it to choose a
geometry; trace collects (x, y, the line's length, remaining, bounce) at
each segment's end."""

W, H = 50, 25


def i16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def sign(v):
    return (v > 0) - (v < 0)


class Line:
    def __init__(s, x0, y0, x1, y1):
        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
        s.x, s.y = x0, y0
        s.dx, s.dy = abs(x1 - x0), abs(y1 - y0)
        s.sx, s.sy = sign(x1 - x0), sign(y1 - y0)
        s.err = 0
        s.length = 0

    def step(s):
        if s.dx >= s.dy:
            if s.x != s.x1:
                s.x += s.sx
                s.err += 2 * s.dy
                s.length = (s.length + 2) & 0xFF
                if s.err >= s.dx:
                    s.err -= 2 * s.dx
                    s.y += s.sy
                    s.length = (s.length + 1) & 0xFF
                return True
            return False
        if s.y != s.y1:
            s.y += s.sy
            s.err += 2 * s.dx
            s.length = (s.length + 2) & 0xFF
            if s.err >= s.dy:
                s.err -= 2 * s.dy
                s.x += s.sx
                s.length = (s.length + 1) & 0xFF
            return True
        return False


def bolt(walls, caster, spell, length, surcharge, trace=None):
    """'forever', 'ends', 'passes' (the other for-ever case) or 'limit'."""
    def cost(x, y):
        if not (0 <= x < W and 0 <= y < H):
            return 0  # terrain 0: off the map
        return 0xFF if (x, y) in walls else 1
    cx0, cy0 = caster
    sx, sy = spell
    if (cx0, cy0) == (sx, sy):
        return "ends"
    remaining = (length * 2) & 0xFF
    bounce = False
    sgn = 1
    last = 0
    seen = set()
    lines = 0
    terrain = cost(sx, sy)
    while remaining > 0:
        if bounce:
            st = (sx, sy, sgn, remaining, last, surcharge)
            if st in seen:
                return "forever"
            seen.add(st)
            lines += 1
            if lines > 4096:
                return "limit"
        dx, dy = i16(sx - cx0), i16(sy - cy0)
        line = Line(sx, sy, i16(sx + dx * sgn * remaining), i16(sy + dy * sgn * remaining))
        while True:
            stepped = False
            if not (line.x0 == line.x1 and line.y0 == line.y1):
                while True:
                    stepped = line.step()
                    terrain = cost(line.x, line.y)
                    if terrain == 1:
                        bounce = False
                    if not stepped or terrain == 0 or terrain > 1:
                        break
                    if not line.length < remaining:
                        break
            if terrain == 0:
                remaining = 0
            bounce = terrain == 0xFF and not bounce
            if trace is not None:
                trace.append((line.x, line.y, line.length, remaining, bounce))
            if bounce:
                sx, sy = line.x, line.y
                back = Line(sx, sy, cx0, cy0)
                while back.step():
                    pass
                if surcharge and back.length <= 8:
                    line.length = (line.length + 8) & 0xFF
                sgn = -sgn
                surcharge = False
            progress = (line.length - last) & 0xFF
            remaining = remaining - progress if progress < remaining else 0
            last = line.length
            if not bounce and remaining != 0 and not stepped and progress == 0:
                return "passes"
            if bounce or remaining == 0:
                break
    return "ends"


