"""Helpers for batch B's magic and item checks (#41, 42, 45, 48, 49, 51):
the same camp keys in the port and the original from a saved game (game A's
files, patched), then the camp's Save in both, compared."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import b_lib as L  # noqa: E402


def lines(out, prefixes=("menu", "choice", "print", "list", "item", "heading", "error")):
    return [ln for ln in out.splitlines() if ln.startswith(prefixes)]


def run_both(work, game, keys, save_letter="F", verbose=False, steps=None, shots=False,
             probe=None, rests=()):
    """Game A's files in GAME (patched): load A, the outpost to the inn's
    camp, KEYS, Save as SAVE_LETTER, in the port (eclplay --load) and in the
    original (the start menu's Load). STEPS, if given, is a list of key
    strings typed one after another in the original (joined they are KEYS),
    PROBE(box, i) called after each (i -1 in camp, before them; after a
    rest, the steps' indexes in RESTS, once its clock stopped). Returns
    (port stdout, port saves dir, original saves dir, port shots)."""
    work = pathlib.Path(work)
    out, psaves, pshots = L.port(L.A_TO_CAMP + keys + L.save_keys(save_letter), work / "port",
                                 ["--load", pathlib.Path(game) / "SAVGAMA.DAT", "--play"],
                                 saves=[game], shots=shots)
    with L.box(work, [game], verbose=verbose, turbo=True) as box:
        box.keys(L.load_keys("A") + L.A_TO_CAMP)
        box.wait_input(settle=0.3)
        if probe:
            probe(box, -1)
        for i, k in enumerate(steps or [keys]):
            box.keys(k)
            if i in rests:
                L.wait_rest(box)
            box.wait_input(settle=0.3)
            if probe:
                probe(box, i)
        box.keys(L.save_keys(save_letter))
        box.wait_input(settle=0.3)
        odir = L.keep_saves(box, work / "saved")
    return out, psaves, odir, pshots
