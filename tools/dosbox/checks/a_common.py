"""Helpers for batch A's checks (startup, title, setup, the start menu): the
same keys in eclplay and in the original under DOSBox-X; each screen taken
at a `\\sNAME.` judged against the port's at the same point of the keys
(checklib.frames_at), the files and the values read in the original's
memory against the port's at the same point (a probe build of eclplay,
below), everything into a checklib.Verdict.
"""
import concurrent.futures
import os
import pathlib
import shutil
import subprocess
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import checklib  # noqa: E402

cokbox = checklib.cokbox
SAVES = [cokbox.ROOT / "SAVE"]


# ------------------------------------------------------- the probe build

# src/eclplay.c's key read, before its shot (next_key).
KEY_READ = "    release(p);\n    if (!cok_key_script_owed(&p->keys) && p->shots != NULL) {\n"
SOUND_CALL = "    cok_sound_call_at(p->sound, call, value);\n"


def _probe_reads(name, text):
    """At each key read, `aprobe:` lines: the number its shot takes (99999
    for none), the
    game's DS:5885 (the game saved), the debug flag and DEBUG.TXT's state (open, BufPos), the menu's
    timeout (DS:6e11), the display (DS:4b76), the sound's DS:577a/577b (with
    --wav), each record of the list (409 bytes) and its items (63 bytes
    each), in hex; in the setup's text mode, the 80x25 cells and the cursor
    (`acrt:`)."""
    if KEY_READ not in text:
        raise cokbox.BoxError("src/eclplay.c's next_key changed: update a_common._probe_reads")
    probe = r'''    if (p->crt != NULL) {
        for (int r = 0; r < COK_CRT_ROWS; ++r) {
            printf("acrt: %02d|", r);
            for (int c = 0; c < COK_CRT_COLUMNS; ++c) printf("%02x", p->crt->chars[r][c]);
            printf("|");
            for (int c = 0; c < COK_CRT_COLUMNS; ++c) printf("%02x", p->crt->attrs[r][c]);
            printf("\n");
        }
        printf("acrt: cursor %d %d\n", p->crt->x, p->crt->y);
    } else if (p->game != NULL) {
        cok_adventure *g = p->game;
        printf("aprobe: read shot %u saved %u debug %u open %u bufpos %zu timeout %u "
               "display %u", !cok_key_script_owed(&p->keys) && p->shots != NULL ? p->shot : 99999u,
               g->effects.rolls.saved, g->debug, g->debug_file.open, g->debug_file.used,
               g->timeout, (unsigned)g->display);
        if (p->sound != NULL) printf(" 577a %u 577b %u", p->sound->device, p->sound->saved);
        printf("\n");
        for (size_t m = 0; m < g->party.count; ++m) {
            const cok_character *ch = g->party.members[m];
            if (ch == NULL) continue;
            printf("aprobe: member %zu ", m);
            for (size_t b = 0; b < sizeof ch->record; ++b) printf("%02x", ch->record[b]);
            for (size_t k = 0; k < ch->item_count; ++k) {
                printf(" ");
                for (size_t b = 0; b < COK_ITEM_SIZE; ++b) printf("%02x", ch->items[k][b]);
            }
            printf("\n");
        }
    }
'''
    return text.replace(KEY_READ, probe + KEY_READ)


def _delay_index(name, text):
    """At each of the game's delays, `adelay: SHOT MS`: the number the
    shot checklib.shot_at_delays then saves takes, and the delay's length."""
    if checklib.DELAY_HOOK not in text:
        raise cokbox.BoxError("src/eclplay.c's delay hook changed: update a_common._delay_index")
    return text.replace(checklib.DELAY_HOOK, checklib.DELAY_HOOK + (
        '    if (p->shots != NULL && p->game != NULL && ms > 0)\n'
        '        printf("adelay: %u %u\\n", p->shot, ms);\n'))


LOG_LINE = '    printf("%s: %s\\n", kind, text);\n}\n'
LOADING = ("    cok_text_string(&game->screen, &game->font, \"Loading...Please Wait\", "
           "0, 24, 10, 0);\n")


def _shot_at_logs(name, text):
    """src/eclplay.c: a shot at each line the game logs (but item:), each
    announced by `alog: SHOT`: the screens the port draws between two delays
    with no delay between them (an NPC joining, a text printed, a picture
    shown) are presented there too."""
    if text.count(LOG_LINE) != 1:
        raise cokbox.BoxError("src/eclplay.c's log_line changed: update a_common._shot_at_logs")
    return text.replace(LOG_LINE, LOG_LINE[:-2] + r'''    {
        player *p = context;
        if (p->shots != NULL && p->game != NULL && strcmp(kind, "item") != 0 &&
            cok_picture_frame(&p->game->screen, 0).width == 320) {
            char path[4096];
            printf("alog: %u\n", p->shot);
            snprintf(path, sizeof path, "%s/%03u.bmp", p->shots, p->shot++);
            save(p, path);
        }
    }
}
''')


def _log_loading(name, text):
    """src/adventure.c: `Loading...Please Wait` logged once shown (so that
    _shot_at_logs presents it: the original shows it while DOS reads the
    record, the port has no delay there)."""
    if text.count(LOADING) != 1:
        raise cokbox.BoxError("src/adventure.c's cok_adventure_loading changed: update "
                              "a_common._log_loading")
    return text.replace(LOADING, LOADING + '    log_text(game, "aloading", "shown");\n')


def probe_eclplay(delays=False):
    """eclplay with batch A's probes (_probe_reads): its behaviour is the
    port's, only its output is more. With
    DELAYS, a shot at each of the game's delays as well
    (checklib.shot_at_delays), each announced by an `adelay:` line, and at
    each line it logs (_shot_at_logs, _log_loading). Without DELAYS it ends
    where the keys run out in the adventure, as checklib.frames_at's does
    (with them it plays on: the demonstration runs after the keys' end)."""
    transforms = [("eclplay.c", _probe_reads)]
    if not delays:
        transforms.append(("start.c", checklib.stop_at_input_end))
    else:
        # shot_at_delays first: _delay_index's line then goes before its shot.
        transforms = [("eclplay.c", checklib.shot_at_delays), ("eclplay.c", _delay_index),
                      ("eclplay.c", _shot_at_logs), ("adventure.c", _log_loading)] + transforms
    return checklib.build("eclplay", transforms=transforms)


class Probe:
    """The last `aprobe:` block of an eclplay run's output: the state at the
    key read where it stopped."""

    def __init__(self, stdout):
        self.fields, self.members = {}, []
        for line in stdout.splitlines():
            if line.startswith("aprobe: read "):
                words = line.split()[2:]
                self.fields = {words[i]: int(words[i + 1]) for i in range(0, len(words), 2)}
                self.members = []
            elif line.startswith("aprobe: member "):
                parts = line.split()[3:]
                self.members.append((bytes.fromhex(parts[0]), [bytes.fromhex(p) for p in parts[1:]]))
        self.stdout = stdout

    def get(self, key):
        return self.fields.get(key)


def at_marks(keys, out, names=None, **kw):
    """The port at each \\sNAME. of KEYS (or only NAMES), as checklib.frames_at
    finds it: the probe build (probe_eclplay(), which ends where the
    keys run out, as checklib.stopping_eclplay) run on the keys before the
    mark stops at the key read that follows, where the box's \\sNAME. takes
    its screen and reads the original's memory. The screen is that read's
    own shot, by the number its `aprobe:` line gives, not the run's last: a
    battle's setup takes one more (eclplay's battlefield hook) after the
    keys' end. KW go to cokbox.eclplay. Returns {name: (Screen or None,
    Probe)}."""
    out = pathlib.Path(out)
    binary = probe_eclplay()
    marks = [(m.group(1), keys[:m.start()]) for m in checklib.MARK.finditer(keys)
             if names is None or m.group(1) in names]

    def one(item):
        name, prefix = item
        text, _, shots = cokbox.eclplay(checklib.port_keys(prefix), out / f"at-{name}",
                                        binary=binary, shots=True, **kw)
        probe = Probe(text)
        shot = probe.get("shot")
        path = next((p for p in shots if int(p.stem) == shot), None)
        return name, (cokbox.Screen.load(path) if path else None, probe)

    with concurrent.futures.ThreadPoolExecutor(4) as pool:
        return dict(pool.map(one, marks))


def probes_at(keys, out, names=None, **kw):
    """The probe build's state at each \\sNAME. of KEYS: {name: Probe}."""
    return {name: probe for name, (_, probe) in at_marks(keys, out, names=names, **kw).items()}


# ------------------------------------------------------ records in memory

def party_records(box, size=409):
    """The original's list from DS:609a: each record's SIZE bytes and its
    items (63 bytes each, linked from +0x143, next at +0x2a)."""
    out = []
    rec = box.far(box.ds * 16 + 0x609A)
    while rec is not None and len(out) < 12:
        raw = box.read(rec, size)
        items, item = [], box.far(rec + 0x143)
        while item is not None and len(items) < 40:
            items.append(box.read(item, 63))
            item = box.far(item + 0x2A)
        out.append((raw, items))
        rec = box.far(rec + 0x17F)
    return out


def name_of(record):
    return record[1:1 + min(record[0], 15)].decode("latin-1")


# --------------------------------------------------------- a pair of runs

class Pair:
    """The same keys in eclplay and in the original (a Box): the port's
    stdout and saves for all the keys, its screen and state at each \\sNAME.
    of them (at_marks), and the box's screens and files."""

    def __init__(self, keys, work, seed=None, machine="tandy", saves=(), box_args="",
                 verbose=False, eclplay_args=(), **box_kw):
        self.work = pathlib.Path(work)
        self.keys = keys
        args = list(eclplay_args) + (["--seed", str(seed)] if seed is not None else [])
        self.kw = dict(machine=machine, saves=saves, args=args)
        self.out, self.port_saves, self.port_shots = cokbox.eclplay(
            checklib.port_keys(keys), self.work / "eclplay", **self.kw)
        self.at = at_marks(keys, self.work / "port", **self.kw)
        self.port = {name: screen for name, (screen, _) in self.at.items()}
        self.box = cokbox.Box(workdir=self.work / "box", machine=machine, saves=saves,
                              args=box_args, seed=seed, verbose=verbose, **box_kw)
        shutil.rmtree(self.work / "shots", ignore_errors=True)
        for old in self.work.glob("diff-*.png"):
            old.unlink()

    def __enter__(self):
        self.box.start()
        return self

    def __exit__(self, *exc):
        self.box.close()

    def probes(self):
        """The port's state at the marks: {name: Probe}."""
        return {name: probe for name, (_, probe) in self.at.items()}

    def run(self, keys=None):
        """Type KEYS (default: all of them) in the box; screens at \\sNAME."""
        self.box.keys(self.keys if keys is None else keys, shots=self.work / "shots")

    def steps(self, during=None):
        """Type the keys mark by mark: at each \\sNAME. the screen, then
        DURING(name) (reading the original's memory there); the keys after
        the last mark are typed too."""
        rest = self.keys
        while r"\s" in rest:
            head, _, tail = rest.partition(r"\s")
            name, _, rest = tail.partition(".")
            self.run(head)
            self.shot(name)
            if during:
                during(name)
        if rest:
            self.run(rest)

    def shot(self, name):
        self.box.wait_input(settle=0.5)
        return self.box.screenshot(self.work / "shots" / f"{name}.png")

    def original(self, name):
        return cokbox.Screen.load(self.work / "shots" / f"{name}.png")

    def judge(self, v, name, masks=(), what=None):
        """The original's screen NAME against the port's at the same mark.
        Returns the pixels differing."""
        screen = self.original(name)
        count = v.screen(what or f"screen {name}", screen, self.port.get(name), masks=masks,
                         work=self.work)
        if count:
            self.nearest(name, screen)
        return count

    def judge_all(self, v, masks=()):
        for name in self.marks():
            self.judge(v, name, masks)

    def marks(self):
        return [m.group(1) for m in checklib.MARK.finditer(self.keys)]

    def nearest(self, name, screen):
        """A diagnostic only: the frame of the whole run nearest to SCREEN."""
        best = None
        for path in self.port_shots:
            frame = cokbox.Screen.load(path)
            n = checklib.masked_diff(screen, frame)[0]
            if best is None or n < best[0]:
                best = (n, path.name)
        if best:
            print(f"    (diagnostic: the nearest of all {len(self.port_shots)} port frames is "
                  f"{best[1]}, {best[0]} pixels)")

    def box_files(self):
        return files_in(self.box.game / "SAVE")

    def port_files(self):
        return files_in(self.port_saves)


def files_in(directory):
    return {p.name: p.read_bytes() for p in pathlib.Path(directory).iterdir() if p.is_file()}


def judge_files(v, orig, port, mask=None, what="file"):
    """Each file of ORIG (the original's) against PORT's, MASK(name, data)
    zeroing what was written from memory (pointers, the stack). Returns the
    number of files differing."""
    differ = 0
    for name in sorted(set(orig) | set(port)):
        a, b = orig.get(name), port.get(name)
        if a is None or b is None:
            v.differ(f"{what} {name}", f"only in {'the original' if a is not None else 'the port'}'s")
            differ += 1
            continue
        if mask:
            a, b = mask(name, a), mask(name, b)
        if a == b:
            v.ok(f"{what} {name}", "identical")
            continue
        differ += 1
        offs = [i for i in range(max(len(a), len(b)))
                if i >= len(a) or i >= len(b) or a[i] != b[i]]
        detail = (f"{len(offs)} bytes differ (sizes {len(a)}, {len(b)}) at "
                  + " ".join(f"{i:#x}:{a[i] if i < len(a) else '-'}/{b[i] if i < len(b) else '-'}"
                             for i in offs[:16]) + (" ..." if len(offs) > 16 else ""))
        v.differ(f"{what} {name}", detail)
    return differ


# The 409-byte record's bytes written from memory (pointers): see save_again.py.
WHO_POINTERS = [range(0xE3, 0xE7), range(0x143, 0x147), range(0x147, 0x17B),
                range(0x17F, 0x183), range(0x183, 0x187)]


def mask_record(record):
    data = bytearray(record)
    for r in WHO_POINTERS:
        for i in r:
            if i < len(data):
                data[i] = 0
    return bytes(data)


def mask_roster(name, data):
    if name.endswith((".WHO", ".SAV")):
        return mask_record(data)
    data = bytearray(data)
    if name.endswith(".STF"):
        spans = [range(i + 0x2A, i + 0x2E) for i in range(0, len(data), 63)]
    elif name.endswith(".SFX"):
        spans = [range(i + 5, i + 9) for i in range(0, len(data), 9)]
    else:
        spans = []
    for r in spans:
        for i in r:
            if i < len(data):
                data[i] = 0
    return bytes(data)


NAMES = 0x1415  # SAVGAM?.DAT: the party's size, then eight string[40] names


def mask_saved(name, data):
    """As mask_roster, and in SAVGAM?.DAT the stack's bytes after each name
    and the slots past the party (save_again.py)."""
    if not name.endswith(".DAT"):
        return mask_roster(name, data)
    data = bytearray(data)
    size = data[NAMES - 1]
    for k in range(8):
        slot = NAMES + 41 * k
        keep = 1 + min(data[slot], 40) if k < size else 0
        for i in range(slot + keep, slot + 41):
            data[i] = 0
    return bytes(data)


def run_probe(binary, keys, out, config_text=None, wav=False, shots=False, extra=()):
    """BINARY (a probe build) from the title with --start-menu, a KRYNN.CFG
    of CONFIG_TEXT (none when None: the setup runs), its own saves. Returns
    (stdout, the shots' paths)."""
    out = pathlib.Path(out)
    shutil.rmtree(out, ignore_errors=True)
    (out / "saves").mkdir(parents=True)
    cfg = out / "krynn.cfg"
    if config_text is not None:
        cfg.write_bytes(config_text)
    args = [str(binary), "--start-menu", "--config", str(cfg), "--saves", str(out / "saves")]
    if wav:
        args += ["--wav", os.devnull]
    if shots:
        (out / "shots").mkdir()
        args += ["--shots", str(out / "shots")]
    run = subprocess.run([*args, *extra, "--keys", keys, str(cokbox.ROOT / "Assets")],
                         capture_output=True, text=True, timeout=900)
    (out / "log.txt").write_text(run.stdout + run.stderr)
    # By number: past 999 the names have four digits.
    return run.stdout, (sorted((out / "shots").glob("*.bmp"), key=lambda p: int(p.stem))
                        if shots else [])
