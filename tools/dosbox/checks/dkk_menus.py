#!/usr/bin/env python3
"""Death Knights of Krynn's last menus by index in the original against
eclplay --game death-knights --start-menu (DEVELOPERS.md, "Death Knights'
spell lists and the ECL's menus"): Magic's spell lists (5dc5:016e, its list
6a69:1b82), PRESS KEY and HORIZONTAL MENU (3c1e:1631) and YES NO
(6a69:2048). The same keys (and clicks) typed to both, the original's
screen at each wait against the port's at the key read the same input
leaves it in:

lists: saved game A with NEACAIL's spells memorized Cure Light Wounds
alone (dkk_levels' cast flow's), begun: camp, Magic, NEACAIL, Cast's list
("Choose Spell: Cast Exit"), Space read past, the scan code 0x45 (its
Exit, as Escape), Cast again, Right to Exit, Enter; Memorize: the table and
the grimoire ("Memorize Next Exit"), Next, PgUp, the scan code 0x4e (a page
down), Down, M (memorized; Enter would be Next, selected by N), Escape; the spells marked, "spells OK? Keep
Exit": Keep; Escape; View, Spells (the list only to look: "Exit" alone,
from column 0), Enter. NEACAIL's memorized spells after.

ecl: saved game A begun: block 16's first PRESS KEY taken by Escape (with
0x4cf7 0 a special key picks no one), the second's Space, F1 and the
keypad's 5 read past, Enter; YES NO ("DO YOU WISH TO REST?", no keypad):
6 (No), F1 and Escape read past, 4 (Yes), Right and Enter (No): the
eulogy (its portrait's eyes masked: PRESS KEY animates it).

post: saved game A resuming in block 36 (as dkk_shops.py writes it), at
3,7 facing north with 0x4c1a 3 and 0x4c2d 1: Move, north into square code
14 (0x8e), the soldiers (SETUP MONSTERS 64, PRESS KEY), "WHAT'S YOUR
TASK?": HORIZONTAL MENU CEMETERY PATROL GUARD (9bad): Escape, Down (no
pick), the keypad's 5 and Space read past, Right, C; the texts after.
The result (0x4cec) after.

clicks (the mouse, DKK.CFG's M, as dkk_mouse.py clicks): lists' saved
game: Cast's list, a click off the list and its two items (read on), the
heading's row (picked out), the spell's row (picked out), the spell's row
again (picked: on whom).

taskclicks: post's saved game: the HORIZONTAL MENU's PATROL clicked on
row 24 after a click off the menu (three items: read on); then the PRESS
KEY item clicked.

Seeded (Box seed, eclplay --seed). In run_all.py's Death Knights set (make
dkk-checks). About five minutes, each flow side by side there.

    python3 tools/dosbox/checks/dkk_menus.py [--workdir DIR] [--seed N]
        [--flows lists,ecl,post,clicks,taskclicks]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402
import dkk_levels  # noqa: E402
import dkk_lib as K  # noqa: E402
import dkk_mouse  # noqa: E402

cokbox = checklib.cokbox

# After Begin: block 16's two texts, Yes to rest: the camp; Magic, NEACAIL
# (the fifth: down four).
CAMP = [("camp", r"\r\r\r"), ("magic", "M"), ("neacail", r"\v\v\v\v")]
LISTS = CAMP + [
    ("list", "C"),               # Choose Spell: Cast Exit
    ("list-space", " "),         # read past
    ("list-45", r"\#45"),        # the scan code 0x45: Exit, Magic's menu
    ("list-again", "C"),
    ("list-exit", r"\>"),        # Exit selected
    ("listed-exit", r"\r"),      # Exit: Magic's menu
    ("grimoire", "M"),           # the table and the grimoire: Memorize Next Exit
    ("next", "N"),               # the next page: Memorize Prev Exit
    ("pgup", r"\#49"),           # the first again
    ("plus", r"\#4e"),           # 0x4e: a page down
    ("down", r"\v"),
    ("memorized", "M"),          # the spell marked, the list again (Enter: Next, selected)
    ("grimoire-left", r"\e"),    # the marked: spells OK? Keep Exit
    ("kept", "K"),               # Magic's menu
    ("magic-left", r"\e"),       # the camp
    ("view", "V"),
    ("spells", "S"),             # the list only to look: Exit alone
    ("spells-left", r"\r"),      # View's menu
]
ECL = [
    ("press-escape", r"\e"),     # PRESS KEY by Escape: the second text
    ("press-space", " "),
    ("press-f1", r"\#3b"),
    ("press-5", "5"),            # the keypad's 5, a special key: read on
    ("rest-ask", r"\r"),         # DO YOU WISH TO REST? Yes No
    ("ask-6", "6"),              # No selected
    ("ask-f1", r"\#3b"),
    ("ask-escape", r"\e"),
    ("ask-4", "4"),              # Yes selected
    ("ask-no", r"\>\r"),         # No: the eulogy
]
POST = [
    ("move", "M"),
    ("soldiers", r"\^"),         # SOLDIERS DRAW NEAR. ...: PRESS KEY
    ("task", r"\r"),             # WHAT'S YOUR TASK?: CEMETERY PATROL GUARD
    ("task-escape", r"\e"),
    ("task-down", r"\v"),
    ("task-5", "5"),
    ("task-space", " "),
    ("task-right", r"\>"),       # PATROL selected
    ("task-c", "c"),             # CEMETERY: 'THEN GO TO THE TEMPLE.
    ("temple", r"\r"),           # 'WELL MET, MY FRIENDS.'
    ("met", r"\r"),
]
# Mouse flows: from the title's Play, saved game A by keys and begun (as
# dkk_mouse.py's camp flow).
BEGIN = [("start", r"\r"), ("start-load", r"\v\v"), ("from", r"\r"), ("which", r"\r"),
         ("loaded", r"\r"), ("begun", r"\v\v\v\v\v\r")]
click = dkk_mouse.click
CLICKS = BEGIN + [("text-2", r"\r"), ("rest-ask", r"\r"), ("camp", r"\r"), ("magic", "M"),
                  ("neacail", r"\v\v\v\v"), ("list", "C"),
                  ("off", click(200, 100)),       # off the list and the menu: read on
                  ("heading", click(20, 44)),     # row 5, 1st Level: picked out
                  ("spell", click(20, 52)),       # row 6: picked out
                  ("cast", click(20, 52))]        # picked: on whom
# Row 24's items from column 0: CEMETERY (0-7), PATROL (9-14), GUARD (16-20).
POST_CLICKS = BEGIN + [("move", "M"), ("soldiers", r"\^"), ("task", r"\r"),
                       ("task-off", click(200, 100)),
                       ("task-patrol", click(84, 194)),
                       ("next", click(40, 194))]


def post_saves(work):
    """Saved game A resuming in block 36 at 3,7 facing north, before
    square 3,6's code 14 (with 0x80) whose script (9339) the counts 0x4c1a
    3 and 0x4c2d 1 reach."""
    saves = work / "saves-post"
    if saves.exists():
        shutil.rmtree(saves)
    shutil.copytree(K.ASSETS / "SAVE", saves)
    path = saves / "SAVGAMA.PTY"
    data = bytearray(path.read_bytes())
    data[1 + 0xF2] = 36                       # var 0x4bf2, the block
    data[1 + 0xC5] = 36                       # var 0x4bc5, the map
    data[1 + 0xE6] = 1                        # var 0x4be6, a 3D area
    data[0xA01:0xA04] = bytes([3, 7, 0])
    data[0xA05] = 0x92                        # the square's byte
    data[0xA08:0xA0C] = bytes([1, 0, 0, 0])  # wall set 1 in slot 0
    data[1 + 0x11A] = 3                       # var 0x4c1a
    data[1 + 0x12D] = 1                       # var 0x4c2d
    path.write_bytes(bytes(data))
    return saves


def load(path):
    return cokbox.Screen.load(path) if path is not None else None


# The eulogy's portrait blinks (dkk_levels.REST_MASKS): PRESS KEY animates
# it (3c1e:1631's picture argument), which frame a screenshot catches
# depends on the host's timing.
MASKS = {"ask-no": dkk_levels.REST_MASKS["eulogy"]}


def keyed(v, work, opts, flow, saves, steps):
    seed = opts.seed
    with cokbox.Box(workdir=work / f"box-{flow}", game="death-knights", machine="ega",
                    seed=seed, saves=[saves], verbose=opts.verbose, timeout=900,
                    keep=True) as box:
        v.check(K.title(box), f"the title's four pictures ({flow})")
        K.type_steps(box, K.LOAD_A_STEPS, settle=2.0)
        dkk_levels.run_steps(v, work, box, flow, steps, K.LOAD_A_BEGIN, seed, saves,
                             masks=MASKS)
        keys = K.LOAD_A_BEGIN + "".join(typed for _, typed in steps)
        _, shot, _, state = K.port(work, f"{flow}-end", keys, seed, saves=saves)
        if flow == "lists":
            raw = dkk_levels.json_records(work / f"port-{flow}-end" / "final.json")
            spells = box.read(box.far(K.party(box)[4]["rec"] + 0xC2), 0x4A)
            v.value(f"{flow}: NEACAIL's spells memorized", spells.hex(),
                    raw[4][0xD8:0xD8 + 0x4A].hex() if raw else None)
        if flow == "post":
            low, _ = K.box_vars(box)
            ported = K.port_vars(shot) if shot is not None else None
            v.value(f"{flow}: the task (0x4cec, 0x4ced)", list(low[0x1EC:0x1EE]),
                    list(ported[0][0x1EC:0x1EE]) if ported is not None else None)
        v.value(f"{flow}: RandSeed", box.ds_dword(K.DS_RANDSEED),
                state["seed"] if state is not None else None)


def clicked(v, work, opts, flow, saves, steps):
    keys = ""
    with dkk_mouse.MouseBox(workdir=work / f"box-{flow}", game="death-knights", machine="ega",
                            input="M", extra_conf=dkk_mouse.CONF, seed=opts.seed,
                            saves=[saves], verbose=opts.verbose, timeout=900,
                            keep=True) as box:
        v.check(K.title(box), f"{flow}: the title's four pictures")
        K.type_steps(box, ["x", "x"], settle=2.0)
        for name, step in steps:
            if isinstance(step, str):
                box.keys(step)
            else:
                box.wait_input()
                box.click(step[1], step[2])
            box.wait_input(settle=2.0)
            keys += dkk_mouse.port_input(step)
            shown = box.screenshot(work / f"dosbox-{flow}-{name}.png")
            _, shot, _, _ = K.port(work, f"{flow}-{name}", keys, opts.seed, saves=saves,
                                   args=["--mouse"])
            v.screen(f"{flow}: {name}", shown, load(shot), work=work)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--workdir", default=str(K.ROOT / "build/dosbox-checks/dkk_menus"))
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--flows", default="lists,ecl,post,clicks,taskclicks",
                        help="which flows: lists, ecl, post, clicks, taskclicks "
                        "(comma-separated)")
    parser.add_argument("--verbose", "-v", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    flows = opts.flows.split(",")
    if "lists" in flows:
        keyed(v, work, opts, "lists", dkk_levels.cure_saves(work), LISTS)
    if "ecl" in flows:
        keyed(v, work, opts, "ecl", K.ASSETS / "SAVE", ECL)
    if "post" in flows:
        keyed(v, work, opts, "post", post_saves(work), POST)
    if "clicks" in flows:
        clicked(v, work, opts, "clicks", dkk_levels.cure_saves(work), CLICKS)
    if "taskclicks" in flows:
        clicked(v, work, opts, "taskclicks", post_saves(work), POST_CLICKS)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
