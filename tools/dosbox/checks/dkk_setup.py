#!/usr/bin/env python3
"""Death Knights of Krynn's setup (dkk 1187:011b, DEVELOPERS.md "Startup and
the setup"): START.EXE without DKK.CFG in DOSBox-X against eclplay --game
death-knights --start-menu without one, on the same keys.

At each key read the original's 80x25 text screen (characters and
attributes, from DOSBox-X's video memory) and the BIOS cursor (0040:0050)
against the port's (a_common's probe build prints its text screen at each
key read); then the note to Tandy users, and DKK.CFG byte for byte.

Flows:
- tandy (machine tandy): Ctrl-C among the digits, [3] Tandy, [4] PC, [2]
  Mouse (written N with a Tandy), a path taken, Champions' path refused
  ('USING DEFAULT PATH: C:\\KRYNN\\SAVE' and 2 seconds).
- six (machine ega): [2] EGA, the sound's 6 (no line written), [2] Mouse,
  a path on drive C, the second path's default. The original goes on to
  the title with the mouse's flag (DS:dbaa) never set; the port stops after
  the note, citing the read, so only the note's first row is compared.

    python3 tools/dosbox/checks/dkk_setup.py [--workdir DIR] [--flow NAME ...]

Ends with VERDICT: AGREES, or VERDICT: DIFFERS new (...)."""
import argparse
import pathlib
import shutil
import subprocess
import sys
import time

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import a_common as A  # noqa: E402
import checklib  # noqa: E402

cokbox = checklib.cokbox
ROOT = cokbox.ROOT

FLOWS = {
    "tandy": ("tandy", r"3\cC42c:\\dkk\\mine\rq\r", False),
    "six": ("ega", r"262c:\\x\r\r", True),
}


def port_setup(work, keys):
    """The port's text screens at each key read [(rows, cursor)], its crt:
    lines and the DKK.CFG it wrote."""
    work = pathlib.Path(work)
    shutil.rmtree(work, ignore_errors=True)
    (work / "saves").mkdir(parents=True)
    cfg = work / "DKK.CFG"
    args = [str(A.probe_eclplay()), "--game", "death-knights", "--start-menu", "--config",
            str(cfg), "--saves", str(work / "saves"), "--keys", keys, str(ROOT / "DKKAssets")]
    run = subprocess.run([*cokbox.BELOW_BOXES, *args], capture_output=True, text=True,
                         timeout=300)
    (work / "log.txt").write_text(run.stdout + run.stderr)
    screens, rows = [], []
    for line in run.stdout.splitlines():
        if line.startswith("acrt: cursor"):
            x, y = map(int, line.split()[2:4])
            screens.append((rows, (x, y)))
            rows = []
        elif line.startswith("acrt: "):
            _, chars, attrs = line[6:].split("|")
            rows.append((bytes.fromhex(chars).decode("cp437"), bytes.fromhex(attrs)))
    crt = [line[5:] for line in run.stdout.splitlines() if line.startswith("crt: ")]
    return screens, crt, cfg.read_bytes() if cfg.exists() else None, run


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
    detail = (f"rows differ {diffs}, attribute rows differ {attrs}, cursor {cur_a} / port "
              f"{cur_b}" + "".join(f"; row {r}: {rows_a[r][0].rstrip()!r} / port "
                                   f"{rows_b[r][0].rstrip()!r}" for r in diffs[:4]))
    v.check(not diffs and not attrs and cur_a == cur_b, label,
            "same text, attributes and cursor" if not diffs and not attrs and cur_a == cur_b
            else detail)


def run_flow(v, work, flow, verbose):
    machine, keys, stops = FLOWS[flow]
    print(f"{flow}, machine {machine}: keys {keys}", flush=True)
    port, crt, port_cfg, run = port_setup(work / f"port-{flow}", keys)
    if stops:
        v.check(run.returncode == 1 and "dkk 4f9a:0123" in run.stderr,
                f"{flow}: the port stops after the note, citing the read", run.stderr.strip())
    with cokbox.Box(workdir=work / f"box-{flow}", game="death-knights", machine=machine,
                    cfg=False, verbose=verbose) as box:
        box.wait_input(seconds=60)
        address = box._until(lambda: box.find_text("GRAPHICS ADAPTER TYPE:"), 30, "the setup")
        items = [i for i in cokbox.parse_keys(keys) if i[0] == "key"]
        for n, (_, word) in enumerate(items):
            box.wait_input(seconds=60)
            time.sleep(0.2)
            judge_text(v, f"{flow}: read {n:2d} (before key {word:#06x})",
                       box_screen(box, address), port[n] if n < len(port) else None)
            box.push_key(word)
            box._until(lambda: not box.buffer(), 30, "the key to be taken", 0.01)
        note = None
        end = time.monotonic() + 4
        while time.monotonic() < end:
            rows = box.text_rows(address)
            if "Tandy 1000 users" in rows[0][0]:
                note = rows
                break
            time.sleep(0.05)
        want = next((c for c in crt if "Tandy 1000 users" in c), None)
        v.value(f"{flow}: the note to Tandy users on row 0",
                note[0][0].rstrip() if note else None, want)
        if note:
            v.check(set(note[0][1]) == {0x07}, f"{flow}: the note's attributes",
                    f"{sorted({hex(a) for a in note[0][1]})}")
        box.wait_input(seconds=60)
        cfg = (box.game / "DKK.CFG").read_bytes()
    v.value(f"{flow}: DKK.CFG", cfg, port_cfg)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--workdir", default=str(ROOT / "build/dosbox-checks/dkk_setup"))
    parser.add_argument("--flow", "--flows", action="append", choices=list(FLOWS))
    parser.add_argument("-v", "--verbose", action="store_true")
    opts = parser.parse_args()
    work = pathlib.Path(opts.workdir).resolve()
    work.mkdir(parents=True, exist_ok=True)
    v = checklib.Verdict()
    for flow in opts.flow or list(FLOWS):
        run_flow(v, work, flow, opts.verbose)
    return v.finish()


if __name__ == "__main__":
    sys.exit(main())
