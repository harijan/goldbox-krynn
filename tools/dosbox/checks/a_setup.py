#!/usr/bin/env python3
"""Check: startup's text screens (README, Checks against the original:
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
and the BIOS cursor (0040:0050) are compared with the port's (a copy of
eclplay printing its text screen at each key read, a_common.delay_eclplay,
ECLPLAY_CRT_DUMP); then the note to Tandy users, and KRYNN.CFG byte for
byte. In machine tandy and svga_s3.
interrupt: the setup closed at the path: KRYNN.CFG's size; then a start
with that empty file: the display (DS:4b76) and the device (DS:577a), and
the title's first screen against eclplay's with the same empty file.
hidden: KRYNN.CFG given the hidden attribute (ATTRIB +H) before START.EXE,
on DOSBox-X's local drive and on a FAT image (IMGMAKE, IMGMOUNT); FF.COM
(INT 21h 4Eh with CX 0, ff.asm below) shows what FindFirst returns.
The joystick's calibration is not run (see the results).
"""
import argparse
import subprocess
import sys
import time

import a_common as A

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


def port_setup(work, keys, config):
    """The port's text screens at each key read, its crt: lines and the file."""
    exe = A.delay_eclplay(work / "tool")
    (work / "saves").mkdir(parents=True, exist_ok=True)
    if config.exists():
        config.unlink()
    run = subprocess.run([str(exe), "--start-menu", "--config", str(config), "--saves",
                          str(work / "saves"), "--keys", keys, str(cokbox.ROOT / "Assets")],
                         env=dict(A.os.environ, ECLPLAY_CRT_DUMP="1"), capture_output=True,
                         text=True, timeout=600)
    screens, rows = [], []
    for line in run.stdout.splitlines():
        if line.startswith("crtdump: cursor"):
            x, y = map(int, line.split()[2:4])
            screens.append((rows, (x, y)))
            rows = []
        elif line.startswith("crtdump: "):
            _, chars, attrs = line[9:].split("|")
            rows.append((bytes.fromhex(chars).decode("cp437"), bytes.fromhex(attrs)))
    crt = [line[5:] for line in run.stdout.splitlines() if line.startswith("crt: ")]
    return screens, crt, config.read_bytes() if config.exists() else None


def box_screen(box, address):
    rows = box.text_rows(address)
    col, row = box.read(0x450, 2)
    return rows, (col, row)


def compare_screens(label, mine, port):
    (rows_a, cur_a), (rows_b, cur_b) = mine, port
    diffs = [r for r in range(25) if rows_a[r][0] != rows_b[r][0]]
    attrs = [r for r in range(25) if rows_a[r][1] != rows_b[r][1]]
    used = sorted({a for chars, at in rows_a for a in at})
    ok = not diffs and not attrs and cur_a == cur_b
    print(f"  {label}: " + ("same text, attributes and cursor" if ok else
          f"rows differ {diffs}, attribute rows differ {attrs}, cursor {cur_a} / port {cur_b}")
          + f"; attributes on screen {[hex(a) for a in used]}")
    for r in diffs[:4]:
        print(f"    row {r}: original {rows_a[r][0].rstrip()!r}\n           port     "
              f"{rows_b[r][0].rstrip()!r}")
    return ok


def screens(work, machine, verbose):
    print(f"screens, machine {machine}: keys {KEYS}")
    port, crt, port_cfg = port_setup(work / "port", KEYS, work / "port" / "krynn.cfg")
    bad = 0
    with cokbox.Box(workdir=work / "box", machine=machine, cfg=False, verbose=verbose) as box:
        box.wait_input(seconds=60)
        address = box._until(lambda: box.find_text("GRAPHICS ADAPTER TYPE:"), 30, "the setup")
        # Ctrl-Break through X: the BIOS's handler sees it.
        box.press("ctrl+Pause")
        time.sleep(1.0)
        box.wait_input(seconds=30)
        print(f"  Ctrl-Break at the first screen: the original waits at {box.where()}")
        items = [i for i in cokbox.parse_keys(KEYS) if i[0] == "key"]
        for n, (_, word) in enumerate(items):
            box.wait_input(seconds=60)
            time.sleep(0.2)
            mine = box_screen(box, address)
            if n < len(port):
                bad += not compare_screens(f"read {n:2d} (before key {word:#06x})", mine, port[n])
            else:
                print(f"  read {n}: the port has no screen here")
                bad += 1
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
        print(f"  the note: original row 0 {note[0][0].rstrip()!r} (attributes "
              f"{sorted({hex(a) for a in note[0][1]})}); the port's crt: {want!r}"
              if note else "  the note: not seen in the original")
        bad += note is None or note[0][0].rstrip() != want
        box.wait_input(seconds=60)
        cfg = (box.game / "KRYNN.CFG").read_bytes()
    print(f"  KRYNN.CFG: original {cfg!r}\n             port     {port_cfg!r}")
    bad += cfg != port_cfg
    return bad


def interrupt(work, verbose):
    print("interrupt: the setup closed at the path")
    with cokbox.Box(workdir=work / "box", cfg=False, verbose=verbose) as box:
        box.keys("343")
        box.wait_input(seconds=60)
        print(f"  waiting at the path ({box.where()}); KRYNN.CFG now "
              f"{(box.game / 'KRYNN.CFG').stat().st_size} bytes; DOSBox-X closed")
    size = (work / "box" / "c" / "COK" / "KRYNN.CFG").stat().st_size
    print(f"  KRYNN.CFG left: {size} bytes")
    bad = size != 0
    # The next start with that empty file, against eclplay's.
    empty = work / "empty.cfg"
    empty.write_bytes(b"")
    (work / "port-saves").mkdir(exist_ok=True)
    shots = work / "port-shots"
    shots.mkdir(exist_ok=True)
    for f in shots.iterdir():
        f.unlink()
    run = subprocess.run([str(cokbox.ROOT / "build/eclplay"), "--start-menu", "--config",
                          str(empty), "--saves", str(work / "port-saves"), "--shots", str(shots),
                          "--keys", r"\w2.\k\r", str(cokbox.ROOT / "Assets")],
                         capture_output=True, text=True, timeout=300)
    ssi = cokbox.Screen.load(sorted(shots.glob("*.bmp"))[0])
    with cokbox.Box(workdir=work / "box2", cfg=b"", verbose=verbose) as box:
        box.wait_input(seconds=60, settle=0.5)
        shot = box.screenshot(work / "empty-cfg-title.png")
        disp, dev = box.ds_byte(0x4B76), box.ds_byte(0x577A)
        count, area = shot.diff(ssi)
        print(f"  started with the empty file: DS:4b76 {disp:#x}, DS:577a {dev:#x}; the title's "
              f"first screen {count} pixels differ from eclplay's with the same file"
              + (f" in {area}" if area else ""))
        bad += count != 0
    print("  port: " + " | ".join(run.stdout.splitlines()[:2]))
    return bad


def hidden(work, verbose):
    print("hidden: ATTRIB +H KRYNN.CFG before START.EXE")
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
            print(f"  {kind}: the game {'shows the setup' if setup else 'does not show the setup'}"
                  f", waits at {box.where()} (2f55: the title), DosError DS:8a58 "
                  f"{box.ds_word(0x8A58)}")
        out = {f: (w / "c" / f).read_text(errors="replace").strip() for f in
               ("F1.TXT", "AT.TXT", "F2.TXT")}
        print(f"    FindFirst CX 0 before: {out['F1.TXT']!r}; ATTRIB: {out['AT.TXT']!r}; "
              f"FindFirst CX 0 after: {out['F2.TXT']!r}")
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir")
    parser.add_argument("--part", action="append", choices=("screens", "interrupt", "hidden"))
    parser.add_argument("--machine", action="append", choices=("tandy", "vga", "ega", "cga"))
    parser.add_argument("-v", "--verbose", action="store_true")
    args = parser.parse_args()
    work = A.workdir(args.workdir, "setup")
    bad = 0
    parts = args.part or ["screens", "interrupt", "hidden"]
    if "screens" in parts:
        for machine in args.machine or ["tandy", "vga"]:
            bad += screens(work / f"screens-{machine}", machine, args.verbose)
    if "interrupt" in parts:
        bad += interrupt(work / "interrupt", args.verbose)
    if "hidden" in parts:
        hidden(work / "hidden", args.verbose)
    print("verdict:", "AGREES" if not bad else "PORT DIFFERS / see above", f"(files in {work})")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
