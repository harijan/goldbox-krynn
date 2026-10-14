"""Screen comparisons for batch B's camp checks: the same saved game and
keys in the original (DOSBox-X) and in eclplay --start-menu --shots, the
original's screenshots matched to the nearest eclplay frame."""
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402

cokbox = L.cokbox


def cells(a, b):
    """The 8x8 text cells (col, row) where screens A and B differ."""
    out = set()
    for i, (x, y) in enumerate(zip(a.pixels, b.pixels)):
        if x != y:
            out.add(((i % a.width) // 8, (i // a.width) // 8))
    return sorted(out, key=lambda c: (c[1], c[0]))


def describe(cs, limit=24):
    rows = {}
    for c, r in cs:
        rows.setdefault(r, []).append(c)
    parts = []
    for r, cols in sorted(rows.items()):
        parts.append(f"row {r} cols {cols[0]}-{cols[-1]} ({len(cols)})")
    return "; ".join(parts[:limit]) + (" ..." if len(parts) > limit else "")


def run_pair(work, saves, steps, port_keys=None, verbose=False, settle=0.6, box_kw=None,
             during=None):
    """STEPS: [(keys, name)]. The original types each step's keys and takes a
    screenshot NAME; eclplay --start-menu --shots types them all (or
    PORT_KEYS). Returns (results {name: (pixels, cells, shot path, screen)},
    eclplay stdout). DURING(box, name) is called after each screenshot."""
    work = pathlib.Path(work)
    keys = "".join(k for k, _ in steps)
    out, _, bmps = L.port_menu(port_keys if port_keys is not None else keys,
                               work / "eclplay", saves=saves)
    results = {}
    shots = {}
    with L.box(work, saves, verbose=verbose, turbo=True, **(box_kw or {})) as box:
        for k, name in steps:
            box.keys(k)
            box.wait_input(settle=settle)
            shots[name] = box.screenshot(work / f"{name}.png")
            if during:
                during(box, name)
    for name, shot in shots.items():
        best = None
        for p in bmps:
            want = cokbox.Screen.load(p)
            n, _ = shot.diff(want)
            if best is None or n < best[0]:
                best = (n, p, want)
        n, p, want = best
        cs = cells(shot, want) if n else []
        if n:
            shot.save_png(work / f"diff-{name}.png", highlight=want)
        results[name] = (n, cs, p, shot)
    return results, out


def report(results):
    for name, (n, cs, p, _) in results.items():
        print(f"  {name}: {n} pixels differ from eclplay's {p.name}" +
              (f" in {describe(cs)}" if n else ""))


def a_camp_steps():
    """SAVE/'s game A loaded from the start menu, to the outpost inn's camp."""
    return [(L.load_keys("A") + L.A_TO_CAMP, "camp")]


def check(work, saves, steps, verbose=False, during=None, must_match=None):
    """Run STEPS (see run_pair), print the comparison; 0 if the screens in
    MUST_MATCH (default all) are eclplay's, else 1."""
    results, out = run_pair(work, saves, steps, verbose=verbose, during=during)
    report(results)
    names = must_match if must_match is not None else list(results)
    return (0 if all(results[n][0] == 0 for n in names) else 1), results, out
