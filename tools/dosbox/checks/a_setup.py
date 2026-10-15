#!/usr/bin/env python3
"""Check #1: startup's text screens (README, Checks against the original:
"Startup's text screens (see Startup and the setup): run `START.EXE` without
`krynn.cfg` in DOSBox (`machine=tandy`, then `svga_s3`) and compare each
screen, the file written and the note to Tandy users ...; the text colour
..., Backspace at the start of a wrapped row, and that Ctrl-Break does
nothing. Interrupt the setup (close DOSBox at the path) and check that an
empty `krynn.cfg` is left, which the next start reads as CGA with no sound;
and that a hidden `krynn.cfg` brings the setup back. With a joystick ...").

screens: the setup with Ctrl-C and Ctrl-Break (pressed through X) among the
digits, a path one character longer than its first row, three Backspaces
back over the wrap, `B` and Enter. At each key read the original's 80x25
text screen (characters and attributes, read from DOSBox-X's video memory)
and the BIOS cursor (0040:0050) against the port's (a_common's probe build
prints its text screen at each key read); then the note to Tandy users,
and KRYNN.CFG byte for byte. In machine tandy and svga_s3.
interrupt: the setup closed at the path: the original leaves KRYNN.CFG
with 0 bytes (asserted as recorded: the port is not compared there, it
removes the file it made when its keys end in the setup, README "Startup
and the setup"); then a start with that empty file: the display (DS:4b76)
and the device (DS:577a) against the port's with the same file, and the
title's first screen against the port's first shot.
hidden: KRYNN.CFG given the hidden attribute (ATTRIB +H) before START.EXE,
on DOSBox-X's local drive and on a FAT image (IMGMAKE, IMGMOUNT); FF.COM
(INT 21h 4Eh with CX 0) shows what FindFirst returns. Not compared with the
port: DOSBox-X's FindFirst returns the hidden file (MS-DOS would not), so
the original goes to the title; that recorded observation is asserted, so
that a DOSBox-X that hides it is noticed (the check then needs doing).
The joystick's calibration is not run (see the batch's results).
"""
import argparse
import pathlib
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

cokbox = A.cokbox
PATH = "C:\\" + "A" * 33          # 36 characters: the prompt's 45 columns and one more row
KEYS = r"\cC3\cC43" + PATH.replace("\\", "\\\\") + r"\b\b\bB\r"
# FindFirst("KRYNN.CFG"), CX 0 (or 2 with an argument): "found ATTR NAME" or
# "error N" (nasm -f bin; the source is in dosbox-batch-a's notes).
FF_COM = bytes.fromhex(
    "31c9803e8000007403b90200b44eba6801cd21721dba7201b409cd21a09500e82800be9e00ac08c07414"
    "88c2b402cd21ebf350ba7901b409cd2158e80c00ba8001b409cd21b8004ccd2150b104d2e8e8030058240f"
    "04303c397602040788c2b402cd21b220cd21c34b52594e4e2e43464700666f756e6420246572726f722024"
    "0d0a24")


def port_setup(work, keys):
    """The port's text screens at each key read [(rows, cursor)], its crt:
    lines and the file it wrote."""
    out, _ = A.run_probe(A.probe_eclplay(), keys, work, config_text=None)
    screens, rows = [], []
    for line in out.splitlines():
        if line.startswith("acrt: cursor"):
            x, y = map(int, line.split()[2:4])
            screens.append((rows, (x, y)))
            rows = []
        elif line.startswith("acrt: "):
            _, chars, attrs = line[6:].split("|")
            rows.append((bytes.fromhex(chars).decode("cp437"), bytes.fromhex(attrs)))
    crt = [line[5:] for line in out.splitlines() if line.startswith("crt: ")]
    cfg = pathlib.Path(work) / "krynn.cfg"
    return screens, crt, cfg.read_bytes() if cfg.exists() else None


def box_screen(box, address):
    rows = box.text_rows(address)
    col, row = box.read(0x450, 2)
    return rows, (col, row)


def judge_text(v, label, mine, port):
    if port is None:
        v.differ(label, "the port has no key read there")
        return
    (rows_a, cur_a), (rows_b, cur_b) = mine, port
    diffs = [r for r in range(25) if rows_a[r][0] != rows_b[r][0]]
    attrs = [r for r in range(25) if rows_a[r][1] != rows_b[r][1]]
    used = sorted({a for chars, at in rows_a for a in at})
    detail = (f"rows differ {diffs}, attribute rows differ {attrs}, cursor {cur_a} / port "
              f"{cur_b}" + "".join(f"; row {r}: {rows_a[r][0].rstrip()!r} / port "
                                   f"{rows_b[r][0].rstrip()!r}" for r in diffs[:4]))
    v.check(not diffs and not attrs and cur_a == cur_b, label,
            f"same text, attributes and cursor; attributes on screen {[hex(a) for a in used]}"
            if not diffs and not attrs and cur_a == cur_b else detail)


def screens(v, work, machine, verbose):
    print(f"screens, machine {machine}: keys {KEYS}")
    port, crt, port_cfg = port_setup(work / "port", KEYS)
    with cokbox.Box(workdir=work / "box", machine=machine, cfg=False, verbose=verbose) as box:
        box.wait_input(seconds=60)
        address = box._until(lambda: box.find_text("GRAPHICS ADAPTER TYPE:"), 30, "the setup")
        # Ctrl-Break through X: the BIOS's handler sees it.
        before = box_screen(box, address)
        box.press("ctrl+Pause")
        time.sleep(1.0)
        box.wait_input(seconds=30)
        v.check(box_screen(box, address) == before, f"{machine}: Ctrl-Break at the first screen "
                "changes nothing", f"the original waits at {box.where()}")
        items = [i for i in cokbox.parse_keys(KEYS) if i[0] == "key"]
        for n, (_, word) in enumerate(items):
            box.wait_input(seconds=60)
            time.sleep(0.2)
            judge_text(v, f"{machine}: read {n:2d} (before key {word:#06x})",
                       box_screen(box, address), port[n] if n < len(port) else None)
            box.push_key(word)
            box._until(lambda: not box.buffer(), 30, "the key to be taken", 0.01)
        # The note to Tandy users, for 2 seconds, still in the text mode.
        note = None
        end = time.monotonic() + 4
        while time.monotonic() < end:
            rows = box.text_rows(address)
            if "TANDY 1000 USERS" in rows[0][0]:
                note = rows
                break
            time.sleep(0.05)
        want = next((c for c in crt if "TANDY 1000 USERS" in c), None)
        v.value(f"{machine}: the note to Tandy users on row 0", note[0][0].rstrip() if note else None,
                want)
        if note:
            v.check(set(note[0][1]) == {0x07}, f"{machine}: the note's attributes",
                    f"{sorted({hex(a) for a in note[0][1]})}")
        box.wait_input(seconds=60)
        cfg = (box.game / "KRYNN.CFG").read_bytes()
    v.value(f"{machine}: KRYNN.CFG", cfg, port_cfg)


def interrupt(v, work, verbose):
    print("interrupt: the setup closed at the path")
    with cokbox.Box(workdir=work / "box", cfg=False, verbose=verbose) as box:
        box.keys("343")
        box.wait_input(seconds=60)
        print(f"  waiting at the path ({box.where()}); DOSBox-X closed")
    size = (work / "box" / "c" / "COK" / "KRYNN.CFG").stat().st_size
    v.check(size == 0, "the setup closed at the path leaves KRYNN.CFG empty (the original, "
            "recorded; the port removes the file it made)", f"{size} bytes")
    # The next start with that empty file, against eclplay's.
    out, shots = A.run_probe(A.probe_eclplay(), r"\w2.\k\r", work / "port", config_text=b"",
                             wav=True, shots=True)
    probe = A.Probe(out.split("aprobe: read shot 1 ")[0])
    with cokbox.Box(workdir=work / "box2", cfg=b"", verbose=verbose) as box:
        box.wait_input(seconds=60, settle=0.5)
        shot = box.screenshot(work / "empty-cfg-title.png")
        v.value("the empty file: the display DS:4b76 (0 CGA)", box.ds_byte(0x4B76),
                probe.get("display"))
        v.value("the empty file: the sound device DS:577a (5 none)", box.ds_byte(0x577A),
                probe.get("577a"))
        v.screen("the empty file: the title's first screen", shot, cokbox.Screen.load(shots[0]),
                 work=work)


def hidden(v, work, verbose):
    print("hidden: ATTRIB +H KRYNN.CFG before START.EXE (the port is not compared: see above)")
    for kind in ("local", "image"):
        w = work / kind
        img = "hd.img"  # in DOSBox-X's directory (the box's): DOS lines stop at 127 characters
        pre = ["FF > C:\\F1.TXT", "ATTRIB +H KRYNN.CFG", "ATTRIB KRYNN.CFG > C:\\AT.TXT",
               "FF > C:\\F2.TXT"]
        if kind == "image":
            pre = [f"IMGMAKE {img} -t hd -size 8 > C:\\MK.TXT", f"IMGMOUNT D {img} > C:\\IM.TXT",
                   "MD D:\\COK",
                   "COPY C:\\COK\\*.* D:\\COK > NUL", "D:", "CD \\COK"] + pre
        with cokbox.Box(workdir=w, pre_commands=pre, files={"FF.COM": FF_COM},
                        verbose=verbose) as box:
            box.wait_input(seconds=90)
            setup = box.find_text("GRAPHICS ADAPTER TYPE:")
            error = box.ds_word(0x8A58)
            print(f"  {kind}: waits at {box.where()} (2f55: the title)")
        out = {f: (w / "c" / f).read_text(errors="replace").strip() for f in
               ("F1.TXT", "AT.TXT", "F2.TXT")}
        print(f"    FindFirst CX 0 before: {out['F1.TXT']!r}; ATTRIB: {out['AT.TXT']!r}; "
              f"FindFirst CX 0 after: {out['F2.TXT']!r}")
        words = out["F2.TXT"].split()
        v.check("H" in out["AT.TXT"].split()[:2], f"hidden, {kind}: ATTRIB +H took",
                out["AT.TXT"])
        v.check(words[:1] == ["found"] and len(words) > 2 and int(words[2], 16) & 2,
                f"hidden, {kind}: DOSBox-X's FindFirst (CX 0) still returns the hidden file "
                "(recorded)", out["F2.TXT"])
        v.check(not setup and error == 0, f"hidden, {kind}: the original goes to the title, "
                "no setup (recorded: DOSBox-X's DOS, not the port's question)",
                f"setup {'shown' if setup else 'not shown'}, DosError DS:8a58 {error}")


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=("screens", "interrupt", "hidden"))
    parser.add_argument("--machine", action="append", choices=("tandy", "vga", "ega", "cga"))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = checklib.workdir(args.workdir, "setup")
    v = checklib.Verdict()
    parts = args.part or ["screens", "interrupt", "hidden"]
    if "screens" in parts:
        for machine in args.machine or ["tandy", "vga"]:
            screens(v, work / f"screens-{machine}", machine, args.verbose)
    if "interrupt" in parts:
        interrupt(v, work / "interrupt", args.verbose)
    if "hidden" in parts:
        hidden(v, work / "hidden", args.verbose)
    print(f"files in {work}")
    return v.finish()


if __name__ == "__main__":
    sys.exit(checklib.run(main))
