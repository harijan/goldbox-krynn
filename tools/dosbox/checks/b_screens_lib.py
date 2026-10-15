"""Screen comparisons for batch B's camp checks: the same saved game and
keys in the original (DOSBox-X) and in eclplay --start-menu; each step's
screen in the original against the port's at the same point of the keys
(frames_at), and against the port with the known fixes for a
difference."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402
import checklib  # noqa: E402

cokbox = L.cokbox


# eclplay --start-menu goes back to the start menu when the keys run out in
# the adventure (its loop goes on after an adventure whose input ended), and
# takes a last shot there: checklib.frames_at's last shot is then the start
# menu, not the screen at the mark. This build stops where the keys ran out.
STOP_LINE = "        if (status != COK_ECL_OK || vm->abort || game->quit) return status;\n"


def stop_at_input_end(name, text):
    """src/start.c: cok_start_game ends when the adventure's keys ran out."""
    if STOP_LINE not in text:
        raise cokbox.BoxError("src/start.c's adventure loop changed: update stop_at_input_end")
    return text.replace(STOP_LINE, STOP_LINE.replace("game->quit)", "game->quit ||\n"
                                                     "            game->input_ended)"))


def stopping_eclplay(fixed=False):
    """The port's eclplay (with the known fixes when FIXED) that stops when
    the keys run out in the adventure."""
    return checklib.build("eclplay", fixed=fixed, transforms=[("start.c", stop_at_input_end)])


def frames_at(keys, out, fixed=False, **kw):
    """checklib.frames_at through stopping_eclplay: the port's screen at
    each \\sNAME. of KEYS."""
    return checklib.frames_at(keys, out, binary=stopping_eclplay(fixed), **kw)


def marked(steps):
    """STEPS [(keys, name)] as one key string with a \\sNAME. after each."""
    return "".join(rf"{k}\s{name}." for k, name in steps)


def run_pair(work, saves, steps, verbose=False, settle=0.6, box_kw=None, during=None):
    """STEPS: [(keys, name)]. The original types each step's keys and takes a
    screenshot NAME. Returns ({name: the original's Screen}, {name: the
    port's at the same point}, a function giving the fixed port's, the
    port's stdout for all the keys)."""
    work = pathlib.Path(work)
    keys = marked(steps)
    out, _, _ = cokbox.eclplay(checklib.port_keys(keys), work / "eclplay", saves=saves,
                               shots=False)
    port = frames_at(keys, work / "eclplay", saves=saves)
    fixed = {}

    def fixed_frames():
        if not fixed:
            fixed.update(frames_at(keys, work / "eclplay-fixed", fixed=True, saves=saves))
        return fixed

    shots = {}
    with L.box(work, saves, verbose=verbose, turbo=True, **(box_kw or {})) as box:
        for k, name in steps:
            box.keys(k)
            box.wait_input(settle=settle)
            shots[name] = box.screenshot(work / f"{name}.png")
            if during:
                during(box, name)
    return shots, port, fixed_frames, out


def judge(verdict, shots, port, fixed_frames, work, names=None, masks=()):
    """Each step's screen (or NAMES') against the port's at the same point."""
    for name in names or shots:
        verdict.screen(f"screen {name}", shots[name], port.get(name),
                       fixed=lambda n=name: fixed_frames().get(n), masks=masks, work=work)


def a_camp_steps():
    """SAVE/'s game A loaded from the start menu, to the outpost inn's camp."""
    return [(L.load_keys("A") + L.A_TO_CAMP, "camp")]


def check(work, saves, steps, verbose=False, during=None, verdict=None):
    """Run STEPS (see run_pair) and judge every screen in VERDICT (a new one
    by default). Returns (verdict, the original's shots, the port's frames,
    the port's stdout)."""
    verdict = verdict or checklib.Verdict()
    shots, port, fixed, out = run_pair(work, saves, steps, verbose=verbose, during=during)
    judge(verdict, shots, port, fixed, work)
    return verdict, shots, port, out
