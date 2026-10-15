"""Screen comparisons for batch B's camp checks: the same saved game and
keys in the original (DOSBox-X) and in eclplay --start-menu; each step's
screen in the original against the port's at the same point of the keys
(checklib.frames_at)."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

cokbox = L.cokbox


def marked(steps):
    """STEPS [(keys, name)] as one key string with a \\sNAME. after each."""
    return "".join(rf"{k}\s{name}." for k, name in steps)


def run_pair(work, saves, steps, verbose=False, settle=0.6, box_kw=None, during=None):
    """STEPS: [(keys, name)]. The original types each step's keys and takes a
    screenshot NAME. Returns ({name: the original's Screen}, {name: the
    port's at the same point}, the port's stdout for all the keys)."""
    work = pathlib.Path(work)
    keys = marked(steps)
    out, _, _ = cokbox.eclplay(checklib.port_keys(keys), work / "eclplay", saves=saves,
                               shots=False)
    port = checklib.frames_at(keys, work / "eclplay", saves=saves)
    shots = {}
    with L.box(work, saves, verbose=verbose, turbo=True, **(box_kw or {})) as box:
        for k, name in steps:
            box.keys(k)
            box.wait_input(settle=settle)
            shots[name] = box.screenshot(work / f"{name}.png")
            if during:
                during(box, name)
    return shots, port, out


def judge(verdict, shots, port, work, names=None, masks=()):
    """Each step's screen (or NAMES') against the port's at the same point."""
    for name in names or shots:
        verdict.screen(f"screen {name}", shots[name], port.get(name), masks=masks, work=work)


def a_camp_steps():
    """SAVE/'s game A loaded from the start menu, to the outpost inn's camp."""
    return [(L.load_keys("A") + L.A_TO_CAMP, "camp")]


def check(work, saves, steps, verbose=False, during=None, verdict=None):
    """Run STEPS (see run_pair) and judge every screen in VERDICT (a new one
    by default). Returns (verdict, the original's shots, the port's frames,
    the port's stdout)."""
    verdict = verdict or checklib.Verdict()
    shots, port, out = run_pair(work, saves, steps, verbose=verbose, during=during)
    judge(verdict, shots, port, work)
    return verdict, shots, port, out
