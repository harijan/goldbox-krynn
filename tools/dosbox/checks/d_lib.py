"""Shared helpers for batch D's combat checks (tools/dosbox/checks/d_*.py).

Each check plays the same keys from the title in the original under DOSBox-X
(cokbox.Box(seed=N)) and in eclplay (--start-menu --seed N), SAVE/'s games
and saved games the port writes, so that the dice agree as long as the port
follows the original. The keys carry a mark \\sNAME. after each step; the
original's screen and memory where it waits after a step are compared with
the port's at the same mark: eclplay run on the keys up to the mark stops at
the key read that follows, and a build of eclplay instrumented here
(STATE_DUMP) writes beside its shot there the port's state (the records,
their combat records and effects, the combatants, the turn order, the
battlefield, the speed, the round, RandSeed, the variables), read as the
original's memory is (DS:623f, 68d1, 6a2e, 71ae, 72ce, 4b38, 714b, 43ae, the
records' bytes).

Screens taken between two key reads (a burst while a spell flies, a watch
while the computer plays) are matched with the port's frames at its key
reads and pauses (Crt.Delay, checklib.shot_at_delays) in order: each at or
after the frame the one before it matched.
"""
import concurrent.futures
import json
import os
import pathlib
import re
import shutil
import struct
import sys
import threading
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox

SAVES = checklib.SAVES
# The title, Play, Load Saved Game A, Begin Adventuring (as load_game_screen).
LOAD_A = r"\kxP\v\v\rA\v\v\v\v\v\r"
# Game A's start: Solace's pages to the outpost's menu.
A_PAGES = r"\r\r\r\r\r\r\r"
# Leave the outpost, decline it, Move, north-east: the caravan's draconians
# (4 baaz on open ground), "PRESS ENTER" then the battle.
TO_CARAVAN = A_PAGES + r"Lnm9\r"
# After the caravan's first turn with Helm: the Gods, Continue No, the
# results, Exit, No treasure, the survivors helped back to the outpost.
CARAVAN_WON = r"\@x\r\rEN\r\rY\r\r"
DS_COMBATANTS, DS_RECORDS, DS_COUNT = 0x623F, 0x68D1, 0x6242
DS_MAP = 0x6A2E
DS_SPEED, DS_ROUND, DS_ENTRY, DS_ORDER = 0x4B38, 0x714B, 0x72CE, 0x71AE
DS_PARTY = 0x609A


def load(letter):
    """The title, Play, Load Saved Game LETTER, Begin Adventuring."""
    return rf"\kxP\v\v\r{letter}\v\v\v\v\v\r"


def marked(steps):
    """STEPS [(name, keys)] as one key string with \\sNAME. after each."""
    return "".join(rf"{k}\s{name}." for name, k in steps)


# A port-written game saved in the outpost inn's camp: loading it from the
# start menu and Begin Adventuring resumes at the outpost's menu.
TO_INN_CAMP = A_PAGES + r"I\r"


def make_save(letter, out, args=(), keys=TO_INN_CAMP, block=None):
    """Game A as the port (eclplay) loads it with ARGS (--effect, --set) and
    BLOCK, saved as game LETTER in the camp KEYS reach (by default the
    outpost inn's); returns the directory of its files."""
    out = pathlib.Path(out)
    stdout, saves, _ = cokbox.eclplay(keys + "S" + letter, out, shots=False,
                                      start_menu=False, block=block,
                                      args=["--load", SAVES / "SAVGAMA.DAT", "--play", *args])
    if not (saves / f"SAVGAM{letter}.DAT").exists():
        raise cokbox.BoxError("eclplay wrote no saved game: " + stdout[-300:])
    return saves


def patch_record(saves, letter, member, offset, data):
    """Write DATA at OFFSET in the .SAV of party member MEMBER (1 the first)
    of saved game LETTER in directory SAVES."""
    path = pathlib.Path(saves) / f"CHRDAT{letter}{member}.SAV"
    raw = bytearray(path.read_bytes())
    raw[offset:offset + len(data)] = bytes(data)
    path.write_bytes(bytes(raw))


def patch_game(saves, letter, square=None, words=()):
    """Saved game LETTER's square (x, y, facing, wall, square byte) and
    variables {address: word}."""
    path = pathlib.Path(saves) / f"SAVGAM{letter}.DAT"
    data = bytearray(path.read_bytes())
    for addr, value in dict(words).items():
        struct.pack_into("<H", data, 1 + (addr - 0x4B00) * 2, value)
    if square is not None:
        data[0x1401:0x1406] = bytes(square)
    path.write_bytes(bytes(data))


def spells_save(work):
    """Game J: game A in the outpost inn's camp, GARIN's last four spells
    Magic Missile, Stinking Cloud, Lightning Bolt and Fear (+0x54: 0f 22 33
    54), SIRRION's third Sleep Stinking Cloud (+0x56)."""
    saves = make_save("J", pathlib.Path(work) / "save")
    patch_record(saves, "J", 3, 0x54, [0x0F, 0x22, 0x33, 0x54])
    patch_record(saves, "J", 6, 0x56, [0x22])
    return saves


# Game J (spells_save) to Throtl's guards with Helm: the caravan won by the
# Gods, back to the outpost, Leave, decline, Move, NE, N, enter Throtl, ATTACK.
GATE_J = load("J") + r"Lnm9\r" + CARAVAN_WON + "Lnm98yA"


def save_97(work):
    """Game J in ECL3 block 97 (vars 0x4be6-8 1 for its wall sets, 0x4ce4
    0x80 so the hostages' dragons stay away while saving), then 0x4ce4 0 and
    the square 10, 5 (square byte 0x8b, type 11: "HERE LIES AN OLD AND
    BATTERED DRAGON", 990f)."""
    saves = make_save("J", pathlib.Path(work) / "save", keys="E", block=97, args=[
        "--file", "3", "--set", "4be7=1", "--set", "4be8=1", "--set", "4be6=1",
        "--set", "4ce4=80"])
    patch_game(saves, "J", square=[10, 5, 0, 0, 0x8B], words={0x4CE4: 0})
    return saves


# --------------------------------------------------------- the port's state

STATE_HOOK = ("        else\n            save(p, path);\n    }\n"
              "    if (!cok_key_script_owed(&p->keys) && p->buffered == 0) type_ahead(p, true);\n")
STATE_AT = "static int next_key(void *context)\n"
STATE_CODE = r"""/* tools/dosbox/checks/d_lib.py: the state at each key read's shot, as
 * JSON beside it (NNN.json), read as the original's memory is. */
static void d_hex(FILE *f, const uint8_t *b, size_t n)
{
    fputc('"', f);
    for (size_t i = 0; i < n; ++i) fprintf(f, "%02x", b[i]);
    fputc('"', f);
}

static int d_index(const cok_adventure *g, const cok_character *c)
{
    for (size_t i = 0; i < g->party.count; ++i)
        if (g->party.members[i] == c) return (int)i;
    return -1;
}

/* COK_D_SPEED "N:K": the game's speed (DS:4b38) made N at the K-th key
 * read with a shot, as a check pokes the original's at that wait, and put
 * back at the read where the keys run out. */
static void d_speed(player *p)
{
    static unsigned reads;
    static int was = -1;
    const char *env = getenv("COK_D_SPEED");
    unsigned speed, at;
    if (env == NULL || sscanf(env, "%u:%u", &speed, &at) != 2) return;
    if (++reads == at) {
        was = p->game->speed;
        p->game->speed = (uint8_t)speed;
    } else if (was >= 0 && cok_key_script_done(&p->keys) && p->buffered == 0 && p->owed < 0) {
        p->game->speed = (uint8_t)was;
        was = -1;
    }
}

static void d_state(const player *p, const char *bmp)
{
    const cok_adventure *g = p->game;
    char path[4200];
    size_t n = strlen(bmp);
    snprintf(path, sizeof path, "%.*s.json", (int)(n > 4 ? n - 4 : n), bmp);
    FILE *f = fopen(path, "w");
    if (f == NULL) return;
    const cok_combat *c = &g->combat;
    /* done: the keys have run out at this read (the mark's own read). */
    fprintf(f, "{\"done\": %d, ", cok_key_script_done(&p->keys) && p->buffered == 0 &&
            p->owed < 0);
    fprintf(f, "\"seed\": %lu, \"speed\": %u, \"round\": %u, \"active\": %d, \"count\": %u, "
            "\"turn_index\": %u, \"lost\": %zu,\n\"vars\": ", (unsigned long)g->vm.seed,
            g->speed, g->effects.rolls.round, c->active, c->count, c->turn_index,
            g->lost_weapon_count);
    uint8_t vars[0x800];
    for (size_t i = 0; i < 0x400; ++i) {
        vars[2 * i] = (uint8_t)g->vm.mem4b00[i];
        vars[2 * i + 1] = (uint8_t)(g->vm.mem4b00[i] >> 8);
    }
    d_hex(f, vars, sizeof vars);
    fputs(",\n\"records\": [", f);
    for (size_t i = 0; i < g->party.count; ++i) {
        const cok_character *ch = g->party.members[i];
        fprintf(f, "%s\n{\"record\": ", i ? "," : "");
        d_hex(f, ch->record, COK_CHARACTER_SIZE);
        fputs(", \"combat\": ", f);
        if (ch->combat != NULL) {
            const cok_combat_record *r = ch->combat;
            uint8_t b[22] = {r->spell, r->may_cast, r->may_use, (uint8_t)r->initiative,
                             r->attack_slot, r->sweeps, r->movement, r->guarding, r->attacked,
                             r->facing, 0, 0, 0, 0, r->dying, r->hits, r->forced, r->turns,
                             r->turning, r->not_party, r->fleeing, r->pattern};
            d_hex(f, b, sizeof b);
        } else {
            fputs("null", f);
        }
        fputs(", \"effects\": [", f);
        for (const cok_effect *e = ch->effects; e != NULL; e = e->next)
            fprintf(f, "%s[%u, %u, %u]", e != ch->effects ? ", " : "", e->id, e->duration,
                    e->value);
        fputs("], \"items\": [", f);
        for (size_t k = 0; k < ch->item_count; ++k) {
            if (k) fputs(", ", f);
            d_hex(f, ch->items[k], COK_ITEM_SIZE);
        }
        fputs("], \"slots\": [", f);
        for (size_t k = 0; k < COK_ITEM_SLOTS; ++k)
            fprintf(f, "%s%zu", k ? ", " : "", ch->slots[k]);
        fputs("]}", f);
    }
    fputs("],\n\"combatants\": [", f);
    for (unsigned k = 1; k < c->count && k <= COK_COMBATANTS; ++k) {
        const cok_combatant *e = &c->combatant[k];
        fprintf(f, "%s[%u, %d, %d, %u, %d]", k > 1 ? ", " : "", k, e->x, e->y, e->size,
                e->character != NULL ? d_index(g, e->character) : -1);
    }
    fputs("],\n\"turn\": [", f);
    for (unsigned k = 1; k <= COK_COMBATANTS && c->turn[k] != NULL; ++k)
        fprintf(f, "%s%d", k > 1 ? ", " : "", d_index(g, c->turn[k]));
    fputs("],\n\"bodies\": [", f);
    for (unsigned k = 0; k < c->bodies && k < COK_COMBAT_BODIES; ++k)
        fprintf(f, "%s[%d, %d, %d, %u]", k ? ", " : "",
                c->body[k].character != NULL ? d_index(g, c->body[k].character) : -1,
                c->body[k].x, c->body[k].y, c->body[k].cell);
    fputs("],\n\"cells\": [", f);
    for (int y = 0; c->active && y < COK_COMBAT_HEIGHT; ++y) {
        if (y) fputs(", ", f);
        d_hex(f, c->cells[y], COK_COMBAT_WIDTH);
    }
    fputs("]}\n", f);
    fclose(f);
}

"""


def state_dump(name, text):
    """src/eclplay.c: the port's state written beside each key read's shot."""
    if STATE_HOOK not in text or STATE_AT not in text:
        raise cokbox.BoxError("src/eclplay.c's next_key changed: update d_lib.state_dump")
    text = text.replace(STATE_HOOK, STATE_HOOK.replace(
        "    }\n", "        if (p->crt == NULL && p->game != NULL) {\n            d_speed(p);\n"
        "            d_state(p, path);\n        }\n    }\n", 1))
    return text.replace(STATE_AT, STATE_CODE + STATE_AT)


SHOW_AT = "    cok_picture_draw(&game->screen, &game->combat.buffer, 0, 1, 1, 0, NULL);\n"
TEXT_AT = ("        cok_text_glyph(dst, font, cok_text_index(text[column - x]), 1, (int)column, y, "
           "fg, bg);\n}\n")
RUN_AT = ("static player p;\n\nstatic int run(int argc, char **argv)\n{\n"
          "    p = (player){.keys = cok_key_script_start(NULL), .owed = -1};\n")


def shot_at_shows(name, text):
    """src/arena.c, src/text.c and src/eclplay.c: in a battle, a --shots
    frame each time the battle's map is shown (cok_arena_show, the copy of
    its buffer to the screen) and each time a line of text is written
    (cok_text_string: the panel's, row 24's "Move/Attack, Move Left = N"),
    so that what the original draws between two pauses has a frame too (the
    caster in its attacking pose before the missile flies, a monster's moves
    left counted down, ...)."""
    if name == "arena.c":
        if SHOW_AT not in text:
            raise cokbox.BoxError("src/arena.c's cok_arena_show changed: update d_lib")
        text = text.replace(SHOW_AT, SHOW_AT + "    if (cok_d_shown != NULL) cok_d_shown();\n")
        return text.replace("bool cok_arena_show(cok_adventure *game)\n",
                            "extern void (*cok_d_shown)(void);\n\n"
                            "bool cok_arena_show(cok_adventure *game)\n", 1)
    if name == "text.c":
        if TEXT_AT not in text:
            raise cokbox.BoxError("src/text.c's cok_text_string changed: update d_lib")
        text = text.replace(TEXT_AT, TEXT_AT[:-2] +
                            "    if (cok_d_shown != NULL) cok_d_shown();\n}\n")
        return text.replace("void cok_text_string(",
                            "extern void (*cok_d_shown)(void);\n\nvoid cok_text_string(", 1)
    if RUN_AT not in text:
        raise cokbox.BoxError("src/eclplay.c's run changed: update d_lib.shot_at_shows")
    return text.replace(RUN_AT, (
        RUN_AT.split("\n\n")[0] + "\n\n"
        "void (*cok_d_shown)(void);\n\n"
        "static void d_shown(void)\n{\n"
        "    if (p.shots == NULL || p.game == NULL || !p.game->combat.active) return;\n"
        "    char path[4096];\n"
        "    snprintf(path, sizeof path, \"%s/%03u.bmp\", p.shots, p.shot++);\n"
        "    save(&p, path);\n}\n\n" + RUN_AT.split("\n\n")[1] +
        "    cok_d_shown = d_shown;\n"))


def player(pauses=False):
    """eclplay writing the state at each key read (and with PAUSES a shot at
    each of the game's delays too, checklib.shot_at_delays, and in a battle
    at each show of its map and each line of text, shot_at_shows)."""
    transforms = [("eclplay.c", state_dump)]
    if pauses:
        transforms += [("eclplay.c", checklib.shot_at_delays), ("eclplay.c", shot_at_shows),
                       ("arena.c", shot_at_shows), ("text.c", shot_at_shows)]
    return checklib.build("eclplay", transforms=transforms)


def read_state(bmp):
    """The state written beside shot BMP, or None (a shot not at a key read)."""
    path = pathlib.Path(bmp).with_suffix(".json")
    if not path.exists():
        return None
    s = json.loads(path.read_text())
    s["vars"] = bytes.fromhex(s["vars"])
    for r in s["records"]:
        r["raw"] = bytes.fromhex(r.pop("record"))
        r["combat"] = bytes.fromhex(r["combat"]) if r["combat"] else None
        r["items"] = [bytes.fromhex(i) for i in r["items"]]
        r["effects"] = [tuple(e) for e in r["effects"]]
        r["name"] = pstr(r["raw"][:16])
        r["hp"], r["status"] = r["raw"][0x197], r["raw"][0x188]
    s["cells"] = [bytes.fromhex(row) for row in s["cells"]]
    return s


def reads(shots):
    """The shots of SHOTS taken at key reads (those with a state)."""
    return [p for p in shots if p.with_suffix(".json").exists()]


def mark_read(shots):
    """The index in SHOTS of the key read where the keys ran out: the mark's
    (eclplay goes on after it: in a battle the player's turns pass), or None."""
    for i, p in enumerate(shots):
        path = p.with_suffix(".json")
        if path.exists() and json.loads(path.read_text())["done"]:
            return i
    return None


class Mark:
    """The port at one mark: its screen and state at the key read that
    follows the mark's keys, and the shots up to it."""

    def __init__(self, shots):
        i = mark_read(shots)
        self.shots = shots[:i + 1] if i is not None else []
        self.screen = load_bmp(shots[i]) if i is not None else None
        self.state = read_state(shots[i]) if i is not None else None
        # The last shot before the read that is not a key read's: the
        # battle's when it was set up (eclplay's battlefield hook), if any.
        others = [p for p in self.shots[:-1] if not p.with_suffix(".json").exists()]
        self.setup = others[-1] if others else None


class Port:
    """The port's runs of KEYS (with \\sNAME. marks): its log (LINES, the
    whole keys) and each mark's screen and state."""

    def __init__(self, work, keys, seed, saves=(), args=(), machine="tandy", log_args=()):
        self.work = pathlib.Path(work)
        self.keys = keys
        self.kw = dict(machine=machine, saves=[SAVES, *saves],
                       args=["--seed", str(seed), *map(str, args)])
        out, _, _ = cokbox.eclplay(checklib.port_keys(keys), self.work / "log", shots=False,
                                   binary=player(), **dict(self.kw, args=[
                                       *self.kw["args"], *map(str, log_args)]))
        (self.work / "log" / "stdout.txt").write_text(out)
        self.lines = out.splitlines()
        self.names = [m.group(1) for m in checklib.MARK.finditer(keys)]
        self.at = self._marks()

    def _prefix(self, name):
        m = next(m for m in checklib.MARK.finditer(self.keys) if m.group(1) == name)
        return checklib.port_keys(self.keys[:m.start()])

    def _marks(self):
        out = self.work / "port"

        def one(name):
            _, _, shots = cokbox.eclplay(self._prefix(name), out / name, shots=True,
                                         binary=player(), **self.kw)
            return name, Mark(shots)

        with concurrent.futures.ThreadPoolExecutor(4) as pool:
            return dict(pool.map(one, self.names))

    def frame(self, name):
        return self.at[name].screen

    def state(self, name):
        return self.at[name].state

    def sequence(self, start, end, speed=None):
        """The port's frames from its key read at mark START to the one at mark
        END, with a frame at each of the game's delays and map shows between:
        [(shot name, Screen)]. SPEED: the game's speed from START's read to
        END's (as a check pokes the original's)."""
        first = len(reads(self.at[start].shots))
        if not first:
            return []
        with _ENV:
            os.environ["COK_D_SPEED"] = f"{speed}:{first}" if speed is not None else ""
            try:
                _, _, shots = cokbox.eclplay(
                    self._prefix(end), self.work / f"pauses-{start}-{end}", shots=True,
                    binary=player(pauses=True), **self.kw)
            finally:
                del os.environ["COK_D_SPEED"]
        at, last = reads(shots), mark_read(shots)
        frames = [] if len(at) < first or last is None else \
            [(p.stem, load_bmp(p)) for p in shots[shots.index(at[first - 1]):last + 1]]
        if shots:
            shutil.rmtree(shots[0].parent)  # a long battle's frames take room
        return frames


_ENV = threading.Lock()


# --------------------------------------------------- judging what is compared

def screen(v, what, shown, port, name, work, masks=()):
    """The original's screen SHOWN at mark NAME against the port's there
    (PORT a Port)."""
    return v.screen(what, shown, port.frame(name), masks=masks, work=work)


def value(v, what, original, port, name, fn):
    """The original's value against FN(the port's state at mark NAME)."""
    mine = port.state(name)
    return v.value(what, original, fn(mine) if mine else None)


# The text areas: the side panel (columns 23-38, rows 1-21) and rows 23-24.
# The game writes a line of text there by clearing it to black first (the
# panel's lines, 6346:0af6; the rows' 127f text routine), so a screenshot
# taken between the clear and the glyphs shows black where neither frame
# has it: allowed there, and only there, for a screen caught drawing.
TEXT_AREAS = ((184, 8, 128, 168), (0, 184, 320, 16))


def _text_black():
    out = bytearray(64000)
    for x0, y0, w, h in TEXT_AREAS:
        for y in range(y0, y0 + h):
            out[y * 320 + x0:y * 320 + x0 + w] = b"\1" * w
    return int.from_bytes(out, "little")


_TEXT = _text_black()
_ONES = int.from_bytes(b"\1" * 64000, "little")


def _nonzero(x):
    """Bit 0 of each byte of X set where that byte (a colour, below 16) is
    not 0."""
    x |= x >> 2
    x |= x >> 1
    return x & _ONES


def candidates(s, frames, ints=None):
    """Where screen S may lie among FRAMES, as (position, name): 2j for
    frame j equal to it, 2j + 1 for frames j and j + 1 in a row that it
    lies between, each of its pixels one of theirs or black in a text area
    (the original caught between a line's clear and its glyphs), neither of
    them equal to it. INTS: the frames' pixels as integers (for speed)."""
    exact = {j for j, (_, f) in enumerate(frames) if f.pixels == s.pixels}
    out = [(2 * j, frames[j][0]) for j in sorted(exact)]
    if s.width != 320 or len(frames) < 2:
        return out
    if ints is None:
        ints = [int.from_bytes(f.pixels, "little") for _, f in frames]
    si = int.from_bytes(s.pixels, "little")
    black_ok = (_nonzero(si) ^ _ONES) & _TEXT
    off = [_nonzero(si ^ f) & ~black_ok for f in ints]
    out += [(2 * j + 1, f"{frames[j][0]}~{frames[j + 1][0]}") for j in range(len(frames) - 1)
            if j not in exact and j + 1 not in exact and not off[j] & off[j + 1]]
    return sorted(out)


def in_order(screens, frames):
    """SCREENS (taken in order between two key reads) matched with FRAMES
    ([(name, Screen)], the port's at its key reads, pauses, map shows and
    text lines) in order: of each screen's candidates (equal to a frame, or
    between two in a row: the original caught drawing the second), the
    choice that matches the most screens held across two screenshots or
    more, then the most others, with frames that never go back (a heaviest
    non-decreasing chain). Returns a list of the frames' names (N~M for
    between) or None for a screen left out."""
    ints = [int.from_bytes(f.pixels, "little") for _, f in frames]
    size = 2 * len(frames) + 1
    tree = [(0, None)] * (size + 1)  # Fenwick tree: the best chain ending at or before

    def best(pos):
        out, i = (0, None), pos + 1
        while i > 0:
            out = max(out, tree[i], key=lambda t: t[0])
            i -= i & -i
        return out

    def put(pos, item):
        i = pos + 1
        while i <= size:
            if item[0] > tree[i][0]:
                tree[i] = item
            i += i & -i

    back = {}
    for i, s in enumerate(screens):
        weight = 1000 if getattr(s, "count", 2) > 1 else 1
        found = []
        for pos, name in candidates(s, frames, ints):
            total, prev = best(pos)
            node = (i, pos, name)
            back[node] = prev
            found.append((pos, (total + weight, node)))
        for pos, item in found:
            put(pos, item)
    out = [None] * len(screens)
    node = best(size - 1)[1]
    while node is not None:
        out[node[0]] = node[2]
        node = back[node]
    return out


def judge_sequence(v, what, screens, frames, work, prefix):
    """Every screen of a burst matched in order (in_order) with FRAMES. A
    screen matched by none is a difference (saved as PREFIX-unmatched-N.png),
    except one that a single screenshot caught (.count 1), which is taken
    for the original caught drawing: it
    draws the map, the panel and the menus straight to the screen, in
    stages the port, which shows each whole, has no frame of (a view
    recentred row by row, a list cleared before the map comes back); such
    screens are listed, not counted. A screen held across two screenshots
    or more is not caught drawing and must match."""
    matched = in_order(screens, frames)
    print(f"  {what}: {len(screens)} screens; the port's frames they match in order: {matched}")
    missing = [i for i, m in enumerate(matched) if m is None]
    for i in missing:
        screens[i].save_png(pathlib.Path(work) / f"{prefix}-unmatched-{i:02d}.png")
    held = [i for i in missing if getattr(screens[i], "count", 2) > 1]
    detail = f"{len(screens) - len(missing)} of {len(screens)} screens match the port's " \
        f"frames in order" + (f"; caught drawing (one screenshot each, not compared): "
                              f"{[i for i in missing if i not in held]}" if missing else "")
    if not held:
        v.ok(what, detail)
        return matched
    detail += "; held and matching none (screen: screenshots): " + \
        ", ".join(f"{i}: {getattr(screens[i], 'count', '?')}" for i in held)
    v.differ(what, detail)
    return matched


def whose(idx_order):
    """The name of the order's entry whose turn it is (turn_entry, port_turn)."""
    idx, order = idx_order
    return order[idx - 1] if 0 < idx <= len(order) else None


def fighters(cs):
    """What compare_state compares of combatants CS: n, name, cell, size,
    hit points, status, effects."""
    return [(c["n"], c["name"], c["x"], c["y"], c["size"], c["hp"], c["status"], c["effects"])
            for c in cs]


def members(rs):
    """What compare_state compares of records RS: name, hit points, status,
    items (+0x142), effects."""
    return [(r["name"], r["hp"], r["status"], r["raw"][0x142], r["effects"]) for r in rs]


def original_view(b):
    """The original's memory as compare_state compares it."""
    o = dict(speed=b.ds_byte(DS_SPEED), records=members(party(b)),
             battle=b.far(b.ds * 16 + DS_MAP) is not None)
    if o["battle"]:
        o.update(turn=whose(turn_entry(b)), round=b.ds_byte(DS_ROUND),
                 fighters=fighters(combatants(b)))
    return o


def port_view(s):
    """The port's state S as compare_state compares it."""
    o = dict(speed=s["speed"], records=members(s["records"]), battle=bool(s["active"]))
    if o["battle"]:
        o.update(turn=whose(port_turn(s)), round=s["round"],
                 fighters=fighters(port_combatants(s)))
    return o


def compare_state(v, b, port, name):
    """The original's memory where it waits after mark NAME against the
    port's state there: the speed, the records (name, hit points, status,
    items, effects), whether a battle is set up, and in one whose turn it
    is, the round and the combatants (cell, size, hit points, status,
    effects). Returns the original's (original_view)."""
    o = original_view(b)
    for key, val in o.items():
        value(v, f"{key} at {name}", val, port, name,
              lambda s, k=key: port_view(s).get(k))
    return o


# ------------------------------------------------- the original's memory

def pstr(raw):
    return raw[1:1 + min(raw[0], len(raw) - 1)].decode("latin-1").rstrip("\0")


def word(raw, off):
    return struct.unpack_from("<H", raw, off)[0]


def combat_record(b, rec):
    """The 22-byte combat record of the record at linear REC (+0x183)."""
    p = b.far(rec + 0x183)
    return b.read(p, 22) if p is not None else None


def effects(b, rec):
    """The record's spell effects: (id, minutes, value) along +0xe3's list."""
    out = []
    eff = b.far(rec + 0xE3)
    while eff is not None and len(out) < 30:
        e = b.read(eff, 9)
        out.append((e[0], struct.unpack_from("<H", e, 1)[0], e[3]))
        eff = b.far(eff + 5)
    return out


def _record(b, rec):
    raw = b.read(rec, 0x1A0)
    return dict(rec=rec, raw=raw, name=pstr(raw[:16]), hp=raw[0x197], status=raw[0x188],
                combat=combat_record(b, rec), effects=effects(b, rec))


def combatants(b):
    """The original's combatants (DS:623f, 68d1): dicts n, name, x, y, size,
    rec (linear), raw (0x1a0 bytes), hp (+0x197), status (+0x188), combat
    (the combat record), effects."""
    count = b.ds_byte(DS_COUNT)
    out = []
    for n in range(1, min(count, 73)):
        x, y, _, size = b.ds_read(DS_COMBATANTS + 4 * n, 4)
        rec = b.far(b.ds * 16 + DS_RECORDS + 4 * n)
        if rec is None:
            continue
        out.append(dict(_record(b, rec), n=n, x=x, y=y, size=size))
    return out


def port_combatants(s):
    """The port's combatants from its state S, as combatants() gives them."""
    out = []
    for n, x, y, size, i in s["combatants"]:
        if i < 0:
            continue
        out.append(dict(s["records"][i], n=n, x=x & 0xFF, y=y & 0xFF, size=size))
    return out


def party(b):
    """The original's records (DS:609a's list, next at +0x17f), as dicts."""
    out = []
    rec = b.far(b.ds * 16 + DS_PARTY)
    while rec is not None and len(out) < 80:
        out.append(_record(b, rec))
        rec = b.far(rec + 0x17F)
    return out


def turn_entry(b):
    """The turn order's entry whose turn it is (DS:72ce) and the order's
    records' names (DS:71ae + 4(i - 1))."""
    idx = b.ds_byte(DS_ENTRY)
    names = []
    for i in range(1, 73):
        rec = b.far(b.ds * 16 + DS_ORDER + 4 * (i - 1))
        if rec is None:
            break
        names.append(pstr(b.read(rec, 16)))
    return idx, names


def port_turn(s):
    return s["turn_index"], [s["records"][i]["name"] if i >= 0 else "?" for i in s["turn"]]


def initiatives(b):
    """The order's (name, combat record +0x03)."""
    out = []
    for i in range(72):
        rec = b.far(b.ds * 16 + DS_ORDER + 4 * i)
        if rec is None:
            break
        out.append((pstr(b.read(rec, 16)), combat_record(b, rec)[3]))
    return out


def port_initiatives(s):
    return [(s["records"][i]["name"], s["records"][i]["combat"][3]) for i in s["turn"]]


def battle_map(b):
    """The battle's terrain, 25 rows of 50 bytes (DS:6a2e's map + 7)."""
    m = b.far(b.ds * 16 + DS_MAP)
    data = b.read(m + 7, 50 * 25)
    return [data[r * 50:(r + 1) * 50] for r in range(25)]


def bodies(b):
    """The fallen (DS:69ee + 7k, k from 1, count DS:6a2d): (name, x, y, the
    terrain under the body)."""
    out = []
    for k in range(1, min(b.ds_byte(0x6A2D), 8) + 1):
        rec = b.far(b.ds * 16 + 0x69EE + 7 * k)
        out.append((pstr(b.read(rec, 16)) if rec else None,
                    *b.ds_read(0x69EE + 7 * k + 4, 3)))
    return out


def port_bodies(s):
    return [(s["records"][i]["name"] if i >= 0 else None, x, y, cell)
            for i, x, y, cell in s["bodies"]]


def by_name(cs, name):
    return [c for c in cs if c["name"] == name]


def cells_of(cs):
    """{n: (name, x, y, size)} of combatants CS."""
    return {c["n"]: (c["name"], c["x"], c["y"], c["size"]) for c in cs}


# ---------------------------------------------------------------- reading text

def dax_record(path, record_id):
    """A DAX file's record, decoded (src/dax.c): the directory of 9-byte
    entries (id, offset, decoded size, packed size) and the run-length code."""
    data = pathlib.Path(path).read_bytes()
    count = struct.unpack_from("<H", data, 0)[0] // 9
    for i in range(count):
        rid, offset, size, packed = struct.unpack_from("<BIHH", data, 2 + 9 * i)
        if rid != record_id:
            continue
        src = data[2 + 9 * count + offset:2 + 9 * count + offset + packed]
        out = bytearray()
        j = 0
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
        return bytes(out[:size])
    raise KeyError(record_id)


_FONT = None


def font():
    """The game's 8x8 font (8X8D1.DAX record 201), 64 glyphs of 8 bytes: a
    character's glyph is its code % 64, lower case folded to upper."""
    global _FONT
    if _FONT is None:
        _FONT = dax_record(cokbox.ROOT / "COKAssets" / "8X8D1.DAX", 201)[:512]
    return _FONT


def read_cell(shown, col, row):
    """The character drawn in text cell COL, ROW of a 320x200 Screen, and its
    colours (fg, bg): ('?', None) when no glyph fits."""
    pix = [shown.at(col * 8 + i, row * 8 + j) for j in range(8) for i in range(8)]
    f = font()
    for g in range(64):
        on, off = set(), set()
        for j in range(8):
            bits = f[g * 8 + j]
            for i in range(8):
                (on if bits >> (7 - i) & 1 else off).add(pix[j * 8 + i])
        if on and off and len(on) == 1 and len(off) == 1 and on != off:
            return chr(g + 64 if g < 32 else g), (on.pop(), off.pop())
    if len(set(pix)) == 1:
        return " ", (None, pix[0])
    return "?", None


def read_text(shown, row, col0=0, col1=40):
    """Row ROW's characters from column COL0 to COL1 (exclusive), and the
    foreground colours of the non-blank ones."""
    chars, colours = [], set()
    for c in range(col0, col1):
        ch, cols = read_cell(shown, c, row)
        chars.append(ch)
        if ch not in " ?" and cols:
            colours.add(cols[0])
    return "".join(chars), colours


def screen_text(shown, rows=range(25), col0=0, col1=40):
    """{row: text} of the rows that read as more than blanks."""
    out = {}
    for r in rows:
        t, _ = read_text(shown, r, col0, col1)
        if t.strip(" ?"):
            out[r] = t.rstrip()
    return out


def panel(screens, rows=range(10, 21)):
    """The side panel's lines (row, text) in the order they first show."""
    out = []
    for s in screens:
        for r, t in sorted(screen_text(s, rows, 23, 39).items()):
            t = t.strip(" ?")
            if (r, t) not in out:
                out.append((r, t))
    return out


# ------------------------------------------------- screens between key reads

def _png_screen(data, where):
    """A PNG's bytes as a Screen (cokbox reads PNGs from files)."""
    path = pathlib.Path(where) / "decode.png"
    path.write_bytes(data)
    try:
        return cokbox.Screen.load(path)
    finally:
        path.unlink()


def burst(b, seconds, until=None, work=None, prefix="b"):
    """DOSBox-X's screenshots (F12+P) as fast as it writes them for SECONDS
    (or until UNTIL() is true): each PNG moved out of the capture directory
    as soon as written (DOSBox-X stops numbering at start_999), decoded
    afterwards; the distinct screens in order (saved as PREFIX-NN.png in
    WORK), each with .count, the screenshots running that showed it."""
    raw = pathlib.Path(work or b.workdir) / f"{prefix}-raw"
    raw.mkdir(parents=True, exist_ok=True)
    pngs = []
    start = time.monotonic()
    end = start + seconds
    while time.monotonic() < end and not (until and until()):
        before = set(b.captures.glob("*.png"))
        b._hotkey("p")
        new = b._until(lambda: (set(b.captures.glob("*.png")) - before) or None, 10,
                       "the screenshot")
        png = sorted(new)[-1]
        b._until(lambda: cokbox._complete_png(png), 30, "the screenshot to be written")
        pngs.append((time.monotonic() - start,
                     shutil.move(str(png), raw / f"{len(pngs):04d}.png")))
    seen, times, last = [], [], None
    for at, png in pngs:
        data = pathlib.Path(png).read_bytes()
        pathlib.Path(png).unlink()
        if data == last:  # the same screen again (DOSBox-X writes it alike)
            seen[-1].count += 1
            continue
        last = data
        shot = _png_screen(data, raw)
        if not seen or shot.pixels != seen[-1].pixels:
            if work is not None:
                shot.save_png(pathlib.Path(work) / f"{prefix}-{len(seen):02d}.png")
            shot.count = 1  # the screenshots running that showed it
            seen.append(shot)
            times.append(round(at, 2))
        else:
            seen[-1].count += 1
    raw.rmdir()
    print(f"  {len(pngs)} screenshots in the burst, {len(seen)} different, first taken at "
          f"{times} s")
    return seen


def with_speed(b, speed, fn):
    """FN() with the game's speed (DS:4b38) made SPEED, so that its pauses
    (speed x 100 ms) last for screenshots; then the speed it had."""
    was = b.ds_byte(DS_SPEED)
    b.ds_write(DS_SPEED, bytes([speed]))
    try:
        return fn()
    finally:
        b.ds_write(DS_SPEED, bytes([was]))


def burst_step(v, b, port, start, end, key, work, prefix, seconds, speed=30):
    """At the original's wait at mark START: the speed made SPEED, KEY (a BIOS
    word) put in and a burst of screenshots taken until the game waits again
    (at most SECONDS); then the speed as it was, and the screen at that wait
    (mark END). The burst's screens are judged in order against the port's
    frames from START to END at the same speed (Port.sequence: some of the
    game's animations run once more for each step of the speed, 6346:228c),
    the screen at END against the port's there. Returns (the burst's screens, the frames'
    names they matched, the screen at END)."""
    def shoot():
        b._last_polls, b._streak = b.hook("polls"), 0
        b.push_key(key)
        shots = burst(b, seconds, until=b.waiting, work=work, prefix=prefix)
        b.wait_input(settle=1.0)
        return shots

    shots = with_speed(b, speed, shoot)
    shown = b.screenshot(pathlib.Path(work) / f"{end}.png")
    frames = port.sequence(start, end, speed=speed)
    port.frames = frames  # for the script's own reading of the port's frames
    matched = judge_sequence(v, f"the screens from {start} to {end}", shots, frames, work,
                             prefix)
    screen(v, f"screen {end}", shown, port, end, work)
    return shots, matched, shown


# ---------------------------------------------------------------- traps

def find_code(b, seg, off, size=24):
    """The linear addresses where START_FULL.EXE's code at Ghidra SEG:OFF lies
    in the guest's memory now (an overlay's wherever its buffer holds it; its
    offsets are kept, so the address less OFF is a paragraph's)."""
    probe = b._exe_bytes(seg, off, size)
    data = b.read(0, 0xA0000)
    return [m.start() for m in re.finditer(re.escape(probe), data) if (m.start() - off) % 16 == 0]


def trap(b, seg, off, until, seconds=30):
    """Make the routine at Ghidra SEG:OFF spin where it starts (EB FE, a jump
    to itself): every copy of its code found in memory is patched, again and
    again as the overlay manager loads it afresh, until UNTIL() is true (or
    SECONDS pass). The game then stops there, its screen as it was, until
    untrap. Returns the patched (address, the two bytes replaced).

    The spin is written while the game runs, not with write_paused as
    untrap's bytes are: a pause takes a few tenths of a second, in which the
    game could pass the routine's first call (the turn the trap is for).
    The write tears only if the CPU stands on the routine's first two bytes
    at that instant, which it passes once a turn; untrap's spin keeps it
    there all the time."""
    patched = []
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        # Only in the overlay buffer (Turbo Pascal's OvrHeapOrg and OvrHeapEnd,
        # DS:437a and 437e): stale copies of the code elsewhere may lie in
        # memory the game has since allocated and reads uninitialized.
        lo, hi = b.ds_word(0x437A) * 16, b.ds_word(0x437E) * 16
        for addr in find_code(b, seg, off):
            if not lo <= addr < hi:
                continue
            patched.append((addr, b.read(addr, 2)))
            b.write(addr, b"\xeb\xfe")
        if patched and until():
            break
        time.sleep(0.002)
    if not (patched and until()):
        raise cokbox.BoxError(f"the trap at {seg:04x}:{off:04x} did not hold")
    return patched


def untrap(b, handle):
    """The code TRAP patched given back, with the emulation paused: the game
    spins in it, and restored while it runs, its EB could be fetched with
    the routine's second byte (3995:040b's 89: a jump 117 bytes back), which
    now and then ended d_patrol's and d_gate's battles at once, the monsters'
    records cleared."""
    b.write_paused([(addr, orig) for addr, orig in handle if b.read(addr, 2) == b"\xeb\xfe"])


def part_of_damage(text):
    """Whether TEXT (the panel's row 11) is part of a "TAKES N POINT(S)" line
    and not all of it (the plural as N has it): a screenshot that caught the
    panel being redrawn, which d_bozak and d_bolt leave out (once it gave
    SIRRION's 5, GARIN's 5, twice)."""
    m = re.fullmatch(r"TAKES (\d+) POINT(S?)", text)
    return text.startswith("TAKES") and not (m and (m.group(1) == "1") == (m.group(2) == ""))


def stable(b, seconds=20, interval=0.3):
    """Screenshots until two running are the same; that screen."""
    last = b.screenshot()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        time.sleep(interval)
        shot = b.screenshot()
        if shot.pixels == last.pixels:
            return shot
        last = shot
    raise cokbox.BoxError("the screen did not settle")


# ---------------------------------------------------------------- the box

def box(work, seed, machine="tandy", verbose=False, saves=(), **kw):
    """cokbox.Box with SAVE/'s games (and SAVES) and SEED, its files in WORK/box.
    Its time limit allows for a long battle's burst judged while it waits."""
    kw.setdefault("timeout", 3600)
    return cokbox.Box(workdir=pathlib.Path(work) / "box", machine=machine,
                      saves=[SAVES, *saves], seed=seed, verbose=verbose, **kw)


def step(b, keys, name, work, settle=1.0):
    """Type KEYS in the original, wait for its next wait and take the screen
    there (WORK/NAME.png)."""
    b.keys(keys)
    b.wait_input(settle=settle)
    return b.screenshot(pathlib.Path(work) / f"{name}.png")


_RGB = {bytes((b, g, r)): i for i, (r, g, b) in enumerate(cokbox.PALETTE)}


def load_bmp(path):
    """eclplay's 320x200 24-bit BMP as a Screen, faster than Screen.load
    (exact palette colours; others go through cokbox.nearest)."""
    data = pathlib.Path(path).read_bytes()
    offset, = struct.unpack_from("<I", data, 10)
    width, height = struct.unpack_from("<ii", data, 18)
    if (width, abs(height)) != (320, 200):
        return cokbox.Screen.load(path)
    stride = (width * 3 + 3) & ~3
    out = bytearray(64000)
    k = 0
    for y in range(200):
        src = offset + ((199 - y) if height > 0 else y) * stride
        row = data[src:src + 960]
        for x in range(0, 960, 3):
            px = row[x:x + 3]
            v = _RGB.get(px)
            if v is None:
                v = cokbox.nearest((px[2], px[1], px[0]))
            out[k] = v
            k += 1
    return cokbox.Screen(320, 200, out)
