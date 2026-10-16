"""Shared steps of batch C's checks (places): a saved game the port writes
at a place, then the same keys in the original (DOSBox-X) and in eclplay
from the title, each step's screen and state compared with the port's at
the same point of the keys (Compare). Not a check itself.

The port at a step's key read (port_at): eclplay built with STATE_HOOK (a
print of RandSeed, the pool's coins, vars 0x4b00-0x4eff and each record of
the list from DS:609a with its items, at each key read, and whether the
keys have ended; nothing else changed), run on the keys up to the step:
its screen and State at the first read after the keys end (eclplay
--start-menu goes on to the start menu after that, so it is not the last).
The original's State is read in memory (box_state). A probe is a function
of a State giving {what: value}: both games' values are compared.

Screens between key reads (judge_watch): the original's, as fast as
DOSBox-X takes them, each in order a screen of the port's, as drawn
(drawn_eclplay: a shot at each key read, delay, `Loading...Please Wait`,
end of a picture's load, sprite and text clear) or as cok presents them,
then the frames cok presents at the wait after (an animating picture),
or between two in a row (caught drawing). A wait whose picture animates
("animates"): the original's screen there may be a frame of that
animation as cok presents it.

Resuming a saved game: the original makes no roll of Random between the
title and the first key read after Begin Adventuring (2fd3:3c28 runs only
the load vector); Compare checks the port's rolls there against the
original's.
"""
import concurrent.futures
import os
import pathlib
import re
import shutil
import struct
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import checklib  # noqa: E402

cokbox = checklib.cokbox
SAVE_A = cokbox.ROOT / "SAVE" / "SAVGAMA.DAT"
LOAD = re.compile(r"\\kxP\\v\\v\\r([A-Z])\\v\\v\\v\\v\\v\\r")


def load_keys(letter):
    """Title, Play, Load Saved Game LETTER, Begin Adventuring (the list keeps
    the third row selected after Load)."""
    return r"\kxP\v\v\r" + letter + r"\v\v\v\v\v\r"


def port_save(out, keys, args, block=None, expect=None):
    """Write a saved game with eclplay (not from the title: ARGS say where,
    e.g. --load SAVE/SAVGAMA.DAT --play; BLOCK the ECL block), KEYS ending in
    camp's Save and a letter. Returns (the saves directory, stdout)."""
    text, saves, _ = cokbox.eclplay(keys, out, saves=(), shots=False, start_menu=False,
                                    args=list(map(str, args)), block=block)
    if not any(saves.glob("SAVGAM*.DAT")):
        raise cokbox.BoxError("the port wrote no saved game:\n" + text[-2000:])
    if expect and expect not in text:
        raise cokbox.BoxError(f"the port's run did not say {expect!r}:\n" + text[-2000:])
    return saves, text


def copy_game(saves, old, new, dest=None):
    """Saved game OLD in SAVES copied as NEW (into DEST, default SAVES)."""
    saves = pathlib.Path(saves)
    dest = pathlib.Path(dest or saves)
    dest.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(saves / f"SAVGAM{old}.DAT", dest / f"SAVGAM{new}.DAT")
    for f in saves.glob(f"CHRDAT{old}*"):
        shutil.copyfile(f, dest / f.name.replace(f"CHRDAT{old}", f"CHRDAT{new}"))
    return dest


def edit_record(path, fn):
    raw = bytearray(pathlib.Path(path).read_bytes())
    fn(raw)
    pathlib.Path(path).write_bytes(bytes(raw))


# ------------------------------------------------------------ state

# The list of records from DS:609a, +0x17f the next (src/party.h); the
# records are 409 bytes (.SAV), items 63 linked at +0x2a from +0x143.
PARTY, NEXT, RECORD, ITEMS, ITEM, ITEM_NEXT = 0x609A, 0x17F, 409, 0x143, 63, 0x2A
POOL = 0x6B0C            # the treasure pool's coins, seven longs
MAX_RECORDS = 64


def name_of(raw):
    return bytes(raw[1:1 + min(raw[0], 15)]).decode("latin-1")


def money(raw):
    """A record's coins: seven words from +0xeb (DEVELOPERS.md, ROB): silver, copper,
    bronze, platinum, steel, gems (+0xf5) and jewelry (+0xf7)."""
    return struct.unpack_from("<7H", raw, 0xEB)


def item_name(raw):
    return bytes(raw[1:1 + min(raw[0], 41)]).decode("latin-1").strip()


class Member:
    def __init__(self, record, items):
        self.record, self.items = bytes(record), [bytes(i) for i in items]

    @property
    def name(self):
        return name_of(self.record)


class State:
    """RandSeed, the pool's coins, vars 0x4b00-0x4eff (words) and the
    records of the list (party first)."""

    def __init__(self, seed, pool, vars_, members):
        self.seed, self.pool, self.vars, self.members = seed, list(pool), list(vars_), members

    def var(self, addr):
        return self.vars[addr - 0x4B00]

    def names(self):
        return [m.name for m in self.members]


def party_records(box, size=RECORD):
    """(linear address, raw bytes) of each record of the list."""
    out = []
    rec = box.far(box.ds * 16 + PARTY)
    while rec is not None and len(out) < MAX_RECORDS:
        out.append((rec, box.read(rec, size)))
        rec = box.far(rec + NEXT)
    return out


def box_state(box):
    """The original's State, read in memory."""
    members = []
    for addr, raw in party_records(box):
        items = []
        item = box.far(addr + ITEMS)
        while item is not None and len(items) < 64:
            items.append(box.read(item, ITEM))
            item = box.far(item + ITEM_NEXT)
        members.append(Member(raw, items))
    block = box.read(box.far(box.ds * 16 + 0x4B28), 0x800)
    return State(box.ds_dword(cokbox.RANDSEED), struct.unpack("<7I", box.ds_read(POOL, 28)),
                 struct.unpack("<1024H", block), members)


STATE_HOOK = ("    release(p);\n"
              "    if (!cok_key_script_owed(&p->keys) && p->shots != NULL) {\n")
STATE_PRINT = r"""    if (p->game != NULL) { /* checks: the state at this key read */
        const cok_adventure *g = p->game;
        printf("cstate: read %u ended %d seed %08lx pool", p->shot,
               cok_key_script_done(&p->keys) && p->buffered == 0, (unsigned long)g->vm.seed);
        for (size_t i = 0; i < COK_COINS; ++i) printf(" %lu", (unsigned long)g->pool.coins[i]);
        printf("\ncstate: vars ");
        for (size_t i = 0; i < 0x400; ++i) printf("%04x", g->vm.mem4b00[i]);
        putchar('\n');
        for (size_t m = 0; m < g->party.count; ++m) {
            const cok_character *c = g->party.members[m];
            printf("cstate: member ");
            for (size_t i = 0; i < COK_CHARACTER_SIZE; ++i) printf("%02x", c->record[i]);
            for (size_t k = 0; k < c->item_count; ++k) {
                putchar(' ');
                for (size_t i = 0; i < COK_ITEM_SIZE; ++i) printf("%02x", c->items[k][i]);
            }
            putchar('\n');
        }
        printf("cstate: end\n");
    }
"""


def state_at_reads(name, text):
    """src/eclplay.c: print the State (cstate: lines) at each key read, with
    the number of the shot taken there and whether the keys have ended."""
    if text.count(STATE_HOOK) != 1:
        raise cokbox.BoxError("src/eclplay.c's next_key changed: update c_places.STATE_HOOK")
    return text.replace(STATE_HOOK, STATE_PRINT + STATE_HOOK)


def state_eclplay():
    """eclplay printing the State at each key read."""
    return checklib.build("eclplay", transforms=[("eclplay.c", state_at_reads)])


def end_state(text):
    """(the State at the first key read after the keys ended, the number of
    its shot) from state_eclplay's stdout, or (None, None).

    Not the last read: at the adventure's command menu, keys that end abort
    the adventure and eclplay --start-menu goes on to the start menu, whose
    read comes last (checklib.frames_at's last shot is then that menu)."""
    lines = [l[8:] for l in text.splitlines() if l.startswith("cstate: ")]
    for i, l in enumerate(lines):
        head = l.split()
        if head[0] == "read" and head[3] == "1":
            break
    else:
        return None, None
    words = bytes.fromhex(lines[i + 1].split()[1])
    vars_ = [int.from_bytes(words[k:k + 2], "big") for k in range(0, len(words), 2)]
    members = []
    for l in lines[i + 2:]:
        if l == "end":
            break
        parts = l.split()[1:]
        members.append(Member(bytes.fromhex(parts[0]), [bytes.fromhex(p) for p in parts[1:]]))
    return State(int(head[5], 16), [int(v) for v in head[7:]], vars_, members), int(head[1])


def port_at(keys, out, names=None, **kw):
    """The port's screen and State at each \\sNAME. of KEYS (or only NAMES),
    as checklib.frames_at, with state_eclplay: {name: (Screen or None,
    State or None)}. KW go to cokbox.eclplay."""
    out = pathlib.Path(out)
    exe = state_eclplay()
    marks = [(m.group(1), keys[:m.start()]) for m in checklib.MARK.finditer(keys)
             if names is None or m.group(1) in names]

    def one(item):
        name, prefix = item
        text, _, shots = cokbox.eclplay(checklib.port_keys(prefix), out / f"at-{name}",
                                        binary=exe, shots=True, **kw)
        state, n = end_state(text)
        shot = next((p for p in shots if p.stem == f"{n:03d}"), None) if n is not None else None
        return name, (cokbox.Screen.load(shot) if shot else None, state)

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        return dict(pool.map(one, marks))


# The port's screens between key reads as drawn, not as cok presents them
# (at most every few ms, and only at its waits): an eclplay that saves a
# shot at each key read, each delay (checklib.shot_at_delays), as
# `Loading...Please Wait` is drawn (cok_adventure_loading), as a picture's
# load ends before row 24 is cleared (picture_loaded), before a sprite is
# drawn over the view (cok_adventure_draw_sprite) and after each text
# clear (cok_text_clear: a notice gone before the menu is drawn), and stops
# where the keys run out (checklib.stopping_eclplay's).
LOADING = ('    cok_text_string(&game->screen, &game->font, "Loading...Please Wait", 0, 24, '
           '10, 0);\n}\n')
LOADED = "    cok_adventure_flush(game); /* 6961:0514 */\n"
SPRITE = ("    if (frame < 1 || frame > game->frame_count) return;\n"
          "    const cok_picture *p = &game->frames[frame - 1];\n")
SAVE_FN = "static void save(player *p, const char *path)\n"
PLAYER_SET = "    p.game = &game;\n"
CLEAR = ("    cok_text_glyph(dst, font, cok_text_index(' '), count, x, y, color, color);\n}\n")


def drawn_text(name, text):
    if text.count(CLEAR) != 1:
        raise cokbox.BoxError("src/text.c's cok_text_clear changed: update c_places.CLEAR")
    return text.replace(CLEAR, CLEAR[:-2] + "    checks_cleared(dst);\n}\n").replace(
        "void cok_text_clear(", "void checks_cleared(const cok_picture *dst);\n\n"
        "void cok_text_clear(", 1)


def drawn_adventure(name, text):
    if text.count(LOADING) != 1 or text.count(LOADED) != 1:
        raise cokbox.BoxError("src/adventure.c's loading notice changed: update c_places.LOADING")
    text = text.replace(LOADING, LOADING[:-2] + "    checks_drawn(game);\n}\n")
    text = text.replace(LOADED, "    checks_drawn(game);\n" + LOADED)
    if text.count(SPRITE) != 1:
        raise cokbox.BoxError("src/adventure.c's sprite drawing changed: update c_places.SPRITE")
    text = text.replace(SPRITE, SPRITE + "    checks_drawn(game);\n")
    return text.replace("void cok_adventure_loading(cok_adventure *game)\n{",
                        "void checks_drawn(cok_adventure *game);\n\n"
                        "void cok_adventure_loading(cok_adventure *game)\n{", 1)


def drawn_eclplay_c(name, text):
    if text.count(SAVE_FN) != 1 or text.count(PLAYER_SET) != 1:
        raise cokbox.BoxError("src/eclplay.c changed: update c_places.drawn_eclplay_c")
    text = checklib.shot_at_delays(name, checklib.print_read_shot(name, text))
    text = text.replace(PLAYER_SET, PLAYER_SET + "    checks_player = &p;\n")
    end = text.index("\n}\n", text.index(SAVE_FN)) + 3
    return text[:end] + (
        "\nstatic player *checks_player;\n\n"
        "void checks_drawn(cok_adventure *game);\n"
        "void checks_drawn(cok_adventure *game)\n{\n"
        "    player *p = checks_player;\n"
        "    if (p == NULL || p->shots == NULL || p->game != game) return;\n"
        "    char path[4096];\n"
        "    snprintf(path, sizeof path, \"%s/%03u.bmp\", p->shots, p->shot++);\n"
        "    save(p, path);\n}\n\n"
        "void checks_cleared(const cok_picture *dst);\n"
        "void checks_cleared(const cok_picture *dst)\n{\n"
        "    player *p = checks_player;\n"
        "    if (p != NULL && p->game != NULL && dst == &p->game->screen) checks_drawn(p->game);\n"
        "}\n") + text[end:]


def drawn_eclplay():
    return checklib.build("eclplay", transforms=[
        ("start.c", checklib.stop_at_input_end), ("eclplay.c", drawn_eclplay_c),
        ("adventure.c", drawn_adventure), ("text.c", drawn_text)])


# ------------------------------------------------------------ Random

MUL, MASK = 0x8088405, 0xFFFFFFFF


def roll(seed, n=1):
    """RandSeed after N rolls (Turbo Pascal's Random, src/ecl.c cok_tp_random)."""
    for _ in range(n):
        seed = (seed * MUL + 1) & MASK
    return seed


def rolls(start, end, limit=100000):
    """The rolls from RandSeed START to END, or None (more than LIMIT)."""
    for n in range(limit + 1):
        if start == end:
            return n
        start = roll(start)
    return None


# ------------------------------------------------------------ the two games

class Compare:
    """The original and eclplay, from the title, through the same keys,
    judged in VERDICT (a checklib.Verdict).

    steps: (name, keys[, probe[, watch]]): after each step's keys, both wait
    for a key; the original's screen there is compared with the port's at
    the same point (port_at); probe(State) -> {what: value}, the original's
    values against the port's at the same point. WATCH (seconds): the
    original's screens while the step's keys are taken are kept too
    (self.watched[name], for judge_watch); "animates": a wait whose picture
    animates (see _animation).
    saves: the saved games (a directory). seed: RandSeed at the title
    (where cokbox sets it), for both. When the first step loads a game, the
    rolls each makes from there to the first key read after loading are
    compared (none in the original, 2fd3:3c28). helm: START x Helm /
    eclplay --helm; machine: tandy, ega, cga; cycles: DOSBox-X's.
    """

    def __init__(self, verdict, work, saves, steps, seed=1, helm=False, machine="tandy",
                 timeout=900, verbose=False, settle=1.0, retries=8, cycles=None):
        self.v = verdict
        self.work = pathlib.Path(work)
        self.saves, self.steps, self.seed, self.helm = saves, steps, seed, helm
        self.machine, self.timeout = machine, timeout
        self.verbose, self.settle, self.retries = verbose, settle, retries
        self.cycles = cycles
        self.keys = "".join(rf"{s[1]}\s{s[0]}." for s in steps)
        m = LOAD.match(steps[0][1])
        self.load = m.group(0) if m else None
        self.shown, self.probes, self.watched = {}, {}, {}

    # ---- the port

    def _kw(self):
        args = ["--seed", str(self.seed)] + (["--helm"] if self.helm else [])
        return dict(machine=self.machine, saves=[self.saves] if self.saves else (), args=args)

    def rolls_after_load(self):
        """The rolls the port makes from the title to the first key read
        after loading."""
        text, _, _ = cokbox.eclplay(self.load, self.work / "after-load", binary=state_eclplay(),
                                    shots=False, **self._kw())
        state, _ = end_state(text)
        if state is None:
            raise cokbox.BoxError("the port printed no state after loading:\n" + text[-1500:])
        return rolls(self.seed, state.seed)

    def port_text_of(self):
        """eclplay's stdout for all the keys (the port's log)."""
        text, _, _ = cokbox.eclplay(checklib.port_keys(self.keys), self.work / "all",
                                    shots=False, **self._kw())
        return text

    # ---- both

    def run(self):
        """Run both and judge each step. Returns self (shown: the original's
        screens; probes: the original's values; port_text: the port's log)."""
        self.port_text = self.port_text_of()
        port = port_at(self.keys, self.work / "port", **self._kw())
        frame = lambda name: (port.get(name) or (None, None))[0]  # noqa: E731
        with cokbox.Box(workdir=self.work / "box", machine=self.machine,
                        saves=[self.saves] if self.saves else (), seed=self.seed,
                        args="x Helm" if self.helm else "", timeout=self.timeout,
                        verbose=self.verbose,
                        **({"cycles": self.cycles} if self.cycles else {})) as box:
            self.box = box
            for i, step in enumerate(self.steps):
                name, keys = step[0], step[1]
                if i == 0 and self.load:
                    box.keys(self.load)
                    box.wait_input(settle=0.2)
                    original_rolls = rolls(self.seed, box.ds_dword(cokbox.RANDSEED))
                    keys = keys[len(self.load):]
                box.keys(keys)
                if len(step) > 3 and isinstance(step[3], (int, float)):
                    (self.work / name).mkdir(parents=True, exist_ok=True)
                    self.watched[name] = checklib.watch(box, step[3], self.work / name, "watch")
                box.wait_input(settle=self.settle)
                self.shown[name] = self._shot(box, name, frame(name))
                if len(step) > 2 and step[2]:
                    self.probes[name] = step[2](box_state(box))
        where = self.work.name
        if self.load:
            self.v.value(f"{where}: Random's rolls from the title to the first key read after "
                         "loading", original_rolls, self.rolls_after_load())
        for step in self.steps:
            name = step[0]
            if len(step) > 3 and step[3] == "animates" and self._animation(name, frame(name)):
                continue
            self.v.screen(f"{where} {name}", self.shown[name], frame(name), work=self.work)
        for name, values in self.probes.items():
            probe = next(s[2] for s in self.steps if s[0] == name)
            state = port[name][1]
            if state is None:
                self.v.differ(f"{where} {name}", "the port printed no state there")
                continue
            ported = probe(state)
            for what, value in values.items():
                self.v.value(f"{where} {name}: {what}", value, ported.get(what))
        return self

    def _animation(self, name, port, pause=1000):
        """For a step whose wait shows an animating picture ("animates"):
        when the original's screen is not the port's frame at that read,
        whether it is one of the frames cok presents during PAUSE
        hundredths of a second at that wait (from the first equal to the
        port's frame at the read on: the picture's animation there), judged
        in the verdict. False (not judged: compared with the read's frame)
        when it is not."""
        shown, what = self.shown[name], f"{self.work.name} {name}"
        if port is None or shown.pixels == port.pixels:
            return False
        frames, _ = self.cok_frames(name, pause=pause)
        start = next((j for j, f in enumerate(frames) if f.pixels == port.pixels), None)
        if start is None:
            return False
        hit = next((j for j in range(start, len(frames)) if frames[j].pixels == shown.pixels),
                   None)
        if hit is None:
            return False
        self.v.ok(what, f"a frame of the picture's animation at this wait (cok's {hit - start} "
                  "from the one at the read), not the read's own")
        return True

    def _shot(self, box, name, port):
        """The original's screen at this wait: taken again a few times while
        it is not the port's frame (a picture that animates while a menu
        waits shows the port's frame in turn)."""
        def score(s):
            return 0 if port is None or s.pixels == port.pixels else s.diff(port)[0]
        shown = box.screenshot()
        best = score(shown)
        for _ in range(self.retries if best else 0):
            again = box.screenshot()
            if score(again) < best:
                shown, best = again, score(again)
            if not best:
                break
        shown.save_png(self.work / f"{name}.png")
        return shown

    # ---- screens between key reads

    def cok_frames(self, step, pause=300):
        """Every frame cok presents for the keys up to STEP and PAUSE
        hundredths of a second more at the wait after it (a picture
        animating there)."""
        prefix = self.keys[:self.keys.index(rf"\s{step}.")]
        return cok_frames(self.work / f"cok-{step}", checklib.port_keys(prefix) + rf"\p{pause}.",
                          saves=self.saves, seed=self.seed, helm=self.helm, machine=self.machine)

    def drawn_frames(self, step, pause=300):
        """The port's screens from the title to the key read after STEP as
        drawn (drawn_eclplay), then the frames cok presents for PAUSE
        hundredths of a second at that read (a picture animating there),
        consecutive repeats dropped."""
        prefix = self.keys[:self.keys.index(rf"\s{step}.")]
        text, _, shots = cokbox.eclplay(checklib.port_keys(prefix), self.work / f"drawn-{step}",
                                        binary=drawn_eclplay(), shots=True, **self._kw())
        reads = re.findall(r"^read-shot: (\d+)$", text, re.M)
        if not reads:
            raise cokbox.BoxError("the port's drawn screens: no key read")
        n = int(reads[-1])
        frames = []
        for p in shots:
            if int(p.stem) <= n:
                screen = cokbox.Screen.load(p)
                if not frames or screen.pixels != frames[-1].pixels:
                    frames.append(screen)
        animation, _ = self.cok_frames(step, pause=pause)
        start = next((j for j, f in enumerate(animation) if f.pixels == frames[-1].pixels), None)
        for f in animation[start + 1:] if start is not None else ():
            if f.pixels != frames[-1].pixels:
                frames.append(f)
        return frames

    def judge_watch(self, step, pause=300):
        """The original's screens watched during STEP and its screen at the
        wait after it against the port's: each, in order, a screen of the
        port's as drawn (drawn_frames) or as cok presents them (cok_frames:
        it presents some states the drawn shots miss, e.g. row 24 cleared
        before a menu is drawn); match_in_order: from where the previous one
        matched on."""
        shots = self.watched[step] + [self.shown[step]]
        what = f"{self.work.name} {step}: the screens while its keys are taken, in order"
        port = {"drawn": self.drawn_frames(step, pause=pause),
                "presented by cok": self.cok_frames(step, pause=pause)[0]}

        def first_match():
            for how, frames in port.items():
                found = match_in_order(shots, frames)
                if all(r[0] is not None for r in found):
                    return how, found
            return None, None

        how, found = first_match()
        if how:
            drawn = [i for i, r in enumerate(found) if r[1]]
            self.v.ok(what, f"{len(shots)} screens, each the port's ({how})" +
                      (f"; caught drawing the next: {drawn}" if drawn else ""))
            return
        # A screen caught in one screenshot only, between others, is one the
        # original was drawing through (a list redrawn row by row while a
        # line is printed) in stages the port has no frame of: such screens
        # are left out (and saved), and every screen held across screenshots
        # must match.
        once = [i for i, s in enumerate(shots)
                if getattr(s, "seen", 2) == 1 and 0 < i < len(shots) - 1]
        if once:
            for i in once:
                shots[i].save_png(self.work / f"once-{step}-{i:02d}.png")
            shots = [s for i, s in enumerate(shots) if i not in once]
            what += f" (screens {once} seen once, left out)"
            how, found = first_match()
            if how:
                self.v.ok(what, f"{len(shots)} screens, each the port's ({how})")
                return
        found = match_in_order(shots, port["drawn"])
        misses = [(i, r) for i, r in enumerate(found) if r[0] is None]
        for i, _ in misses:
            shots[i].save_png(self.work / f"unmatched-{step}-{i:02d}.png")
        self.v.differ(what, "; ".join(f"screen {i}: {r[2]} px from the port's drawn screen "
                                      f"{r[3]}" for i, r in misses[:6]))


def match_in_order(shots, frames, masks=()):
    """Each of SHOTS (in order) against FRAMES from where the previous one
    matched on, outside MASKS (checklib's (x, y, w, h) areas): (index of
    the frame it equals, False), or (index of the second of two frames in a
    row, True) when it is neither but has each pixel of one or the other
    (caught while the game drew the next; the next shot may then match only
    from the second on: the drawing goes forward), or (None, False, the
    fewest pixels it differs by from a frame from there on, that frame's
    index)."""
    if not shots:
        return []
    keep = _keep(shots[0], masks)
    out, pos = [], 0
    for s in shots:
        hit = None
        for j in range(pos, len(frames)):
            here = _differ(s, frames[j], keep)
            if here == 0:
                hit = (j, False)
                break
            if j + 1 < len(frames):
                there = _differ(s, frames[j + 1], keep)
                if there and here & there == 0:
                    hit = (j + 1, True)
                    break
        if hit:
            pos = hit[0]
            out.append((hit[0], hit[1], 0, hit[0]))
        else:
            counts = [_differ(s, f, keep).bit_count() for f in frames[pos:]] or \
                [s.width * s.height]
            k = min(range(len(counts)), key=counts.__getitem__)
            out.append((None, False, counts[k], pos + k))
    return out


# Screens as big integers, a byte a pixel (colours 0-15), for comparisons
# at C speed: _differ's bit 8 * i is set where pixel i differs.
_INTS = {}


def _int(screen):
    key = id(screen)
    if key not in _INTS or _INTS[key][0] is not screen:
        _INTS[key] = (screen, int.from_bytes(bytes(screen.pixels), "big"))
    return _INTS[key][1]


def _keep(screen, masks):
    """Bit 0 of each pixel's byte, for the pixels outside MASKS."""
    w, h = screen.width, screen.height
    if not masks:
        return int.from_bytes(b"\1" * (w * h), "big")
    return int.from_bytes(bytes(0 if any(checklib._inside(i % w, i // w, m) for m in masks)
                                else 1 for i in range(w * h)), "big")


def _differ(s, a, keep):
    x = _int(s) ^ _int(a)
    return (x | x >> 1 | x >> 2 | x >> 3) & keep


# ------------------------------------------------------------ saved games

# A saved game's layout (src/party.c cok_saved_game_read): the ECL file,
# vars 0x4b00-0x4eff, 0x7c00-0x7fff and 0x7a00-0x7bff as words, then the
# party's square, facing, the wall ahead and the square's byte.
SAVED_POS = 1 + 0x800 + 0x800 + 0x400


def edit_save(saves, letter, at=None, var=None, geo=None):
    """Change saved game LETTER in SAVES: AT (x, y, dir 0-6) moves the party
    (the wall ahead and the square's byte from GEO, the map's tables), VAR
    {address: value} sets vars of 0x4b00-0x4eff and 0x7c00-0x7fff."""
    path = pathlib.Path(saves) / f"SAVGAM{letter}.DAT"
    data = bytearray(path.read_bytes())
    for addr, value in (var or {}).items():
        if 0x4B00 <= addr <= 0x4EFF:
            struct.pack_into("<H", data, 1 + 2 * (addr - 0x4B00), value)
        else:
            assert 0x7C00 <= addr <= 0x7FFF
            struct.pack_into("<H", data, 1 + 0x800 + 2 * (addr - 0x7C00), value)
    if at:
        x, y, d = at
        data[SAVED_POS:SAVED_POS + 3] = bytes([x, y, d])
        if geo is not None:
            import c_geo
            data[SAVED_POS + 3] = c_geo.wall(geo, d, x, y)
            data[SAVED_POS + 4] = c_geo.square(geo, x, y)
    path.write_bytes(bytes(data))


# Game A's party on the overland map as if from the first outpost with the
# caravan met (c_throtl.py).
OVERLAND = ["--load", SAVE_A, "--play", "--set", "4bf2=11", "--set", "4bc3=1",
            "--set", "4bc4=3", "--set", "4c2d=1"]


def throtl_save(out, letter, var=None, at=None, extra=()):
    """A saved game LETTER inside Throtl (ECL1 block 32, map GEO1 32) at
    7,15 N, the guards beaten (0x4c4d bit 2), no wandering monsters (0x4c09
    0xff) unless VAR says otherwise; AT moves the party in the file."""
    import c_geo
    sets = {0x4BF2: 0x11, 0x4BC3: 1, 0x4BC4: 3, 0x4C2D: 1, 0x4C4D: 2, 0x4C09: 0xFF}
    sets.update(var or {})
    args = ["--load", SAVE_A, "--play", *extra]
    for a, v in sets.items():
        args += ["--set", f"{a:x}={v:x}"]
    # decline the outpost, Move NE then N onto Throtl, enter, Exit Move, Encamp, Save
    saves, text = port_save(out, "nm98yeES" + letter, args, block=16)
    # The vars again in the file: a run can change them (0x4c09 came back 0).
    inside = {a: v for a, v in sets.items() if a not in (0x4BF2, 0x4BC3, 0x4BC4, 0x4C2D)}
    edit_save(saves, letter, at=at, var=inside, geo=c_geo.load(1, 32))
    return saves, text


def cok_frames(out, keys, saves=None, seed=None, helm=False, machine="tandy", timeout=300):
    """Every frame cok (cokbox.COK) presents for KEYS from the title (SDL's dummy driver saving each,
    320x200 at --scale 1 --aspect 1:1, real delays), as Screens,
    consecutive repeats dropped. For screens the original shows between key
    reads (delays, running text), which eclplay's shots miss."""
    out = pathlib.Path(out)
    if out.exists():
        shutil.rmtree(out)
    (out / "frames").mkdir(parents=True)
    (out / "saves").mkdir()
    if saves:
        for f in pathlib.Path(saves).iterdir():
            shutil.copyfile(f, out / "saves" / f.name)
    cfg = out / "krynn.cfg"
    cfg.write_bytes(cokbox.krynn_cfg(cokbox.MACHINES[machine][1]).encode())
    args = [str(cokbox.COK), "--assets", str(cokbox.ROOT / "Assets"),
            "--saves", str(out / "saves"), "--config", str(cfg), "--aspect", "1:1",
            "--scale", "1", "--input", "none", "--no-sound", "--keys", keys]
    if seed is not None:
        args += ["--seed", str(seed)]
    if helm:
        args += ["--helm"]
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_RENDER_DRIVER="software",
               SDL_AUDIODRIVER="dummy", SDL_VIDEO_DUMMY_SAVE_FRAMES="1")
    run = subprocess.run(args, cwd=out / "frames", env=env, capture_output=True, text=True,
                         timeout=timeout)
    frames = []
    for p in sorted((out / "frames").glob("SDL_window*.bmp"),
                    key=lambda p: int("".join(c for c in p.stem if c.isdigit()) or 0)):
        s = cokbox.Screen.load(p)
        if not frames or s.pixels != frames[-1].pixels:
            frames.append(s)
        p.unlink()
    return frames, run.stdout + run.stderr

