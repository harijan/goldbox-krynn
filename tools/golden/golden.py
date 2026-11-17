#!/usr/bin/env python3
"""Golden transcripts: the helpers behind tools/golden/run.sh, compare.sh and
go.sh (stdlib only; run as python3 -I).

  record KIND REAL OUT TREE TMP -- ARGS...
      Run REAL with ARGS, passing its stdout, stderr and status through, and
      write OUT/KIND/NNNNN.txt: the run's args, status, every file the run
      wrote under TMP or TREE/build (sha256), then its stdout and stderr.
      run.sh puts this in front of build/eclplay, build/krynn and the tools
      the suites run (daximages, daxcompose, ecldump, daxcheck).
  compare BASE HEAD
      Pair the runs of two outputs by their args line and print the runs that
      differ or are on one side only, and a summary per kind. Exit 1 when
      anything differs, except krynn runs without --seed (the clock's Random)
      and krynn runs that follow the real clock (see real_time): their output
      is not repeatable even within one tree, so they are listed, not counted.
  mk MAKEFILE OUT
      Write OUT/golden.mk: the Makefile's sanitize recipe lines that run
      ./build/eclplay_sanitize, against ./build/eclplay.
"""
import hashlib
import os
import re
import signal
import stat
import subprocess
import sys
import threading

RUN_KINDS = ("eclplay", "krynn")
TOOL_KINDS = ("daximages", "daxcompose", "ecldump", "daxcheck")


def normalise(text, tree, tmp):
    """Paths in a string: the tree is <ROOT>; a test's temp folder (the first
    name under TMP, or a test_krynn- folder in /dev/shm) is <TMP>, so that
    its random name drops out. The program cuts its "crt:" message about a
    missing KRYNN.CFG to 80 columns, which leaves the temp path's random
    name in, so that line is <CFG>."""
    text = re.sub(re.escape(tmp) + r"/[^/\s'\"]+", "<TMP>", text)
    text = re.sub(r"/dev/shm/test_krynn-[^/\s'\"]+", "<TMP>", text)
    text = re.sub(r"(?m)^crt: [^\n]*KRYNN\.CFG", "crt: <CFG>", text)
    return text.replace(os.path.realpath(tree) + "/", "<ROOT>/").replace(tree + "/", "<ROOT>/")


def scan(roots):
    """{path: (size, mtime_ns)} for the regular files under roots (a root may
    also be a file)."""
    files = {}
    for root in roots:
        if os.path.isfile(root):
            walked = [(os.path.dirname(root), [], [os.path.basename(root)])]
        else:
            walked = os.walk(root)
        for dirpath, _dirs, names in walked:
            for name in names:
                path = os.path.join(dirpath, name)
                try:
                    st = os.stat(path, follow_symlinks=False)
                except OSError:
                    continue
                if stat.S_ISREG(st.st_mode):
                    files[path] = (st.st_size, st.st_mtime_ns)
    return files


def watched(argv, tree, tmp):
    """The folders a run may write: those its args name under TMP or the
    tree's build folder, and the folders they sit in (below TMP or build
    themselves). Scanning only these keeps one test's runs from seeing
    another's files when the tests run at once."""
    build = os.path.join(tree, "build")
    tops = (tmp, build)
    roots = set()
    for arg in argv:
        path = os.path.abspath(arg)
        for top in tops:
            if path == top or not path.startswith(top + os.sep):
                continue
            roots.add(path)
            parent = os.path.dirname(path)
            if parent != top and parent.startswith(top + os.sep):
                roots.add(parent)
    return sorted(roots)


def digest(data, tree, tmp):
    """sha256 of a file's bytes; a text file's with its paths normalised
    (a page written by a tool can name the tree)."""
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return hashlib.sha256(data).hexdigest()
    return hashlib.sha256(normalise(text, tree, tmp).encode("utf-8")).hexdigest()


def die_with_parent():
    """Linux: the child is killed if this recorder is, so that a test which
    kills the recorder (SIGKILL cannot be forwarded) leaves no krynn running."""
    import ctypes
    ctypes.CDLL(None, use_errno=True).prctl(1, signal.SIGKILL)  # PR_SET_PDEATHSIG


def run_child(real, argv):
    """Run REAL, passing its output through as it comes (the tests read a
    running krynn's output and signal it), and forwarding SIGTERM, SIGINT and
    SIGHUP to it. Return (status, stdout, stderr)."""
    proc = subprocess.Popen([real, *argv], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            preexec_fn=die_with_parent if sys.platform == "linux" else None)
    chunks = {"out": [], "err": []}

    def forward(sig, _frame):
        proc.send_signal(sig)

    def pump(pipe, target, sink):
        while True:
            data = pipe.read1(65536)
            if not data:
                break
            sink.append(data)
            target.write(data)
            target.flush()

    for sig in (signal.SIGTERM, signal.SIGINT, signal.SIGHUP):
        signal.signal(sig, forward)
    readers = [threading.Thread(target=pump, args=(proc.stdout, sys.stdout.buffer, chunks["out"])),
               threading.Thread(target=pump, args=(proc.stderr, sys.stderr.buffer, chunks["err"]))]
    for reader in readers:
        reader.start()
    status = proc.wait()
    for reader in readers:
        reader.join()
    return status, b"".join(chunks["out"]), b"".join(chunks["err"])


def real_time(argv):
    """Whether a krynn run's output follows the real clock: --log-times
    stamps, --sound-clock real, or SDL's disk audio (written in real time)."""
    if "--log-times" in argv or os.environ.get("SDL_AUDIODRIVER") == "disk":
        return True
    return any(a == "--sound-clock" and argv[i + 1:i + 2] == ["real"]
               for i, a in enumerate(argv))


def record(kind, real, out, tree, tmp, argv):
    tree = os.path.abspath(tree)
    tmp = os.path.abspath(tmp)
    build = os.path.join(tree, "build")
    roots = watched(argv, tree, tmp)
    before = scan(roots)
    status, stdout, stderr = run_child(real, argv)
    after = scan(roots)
    if "--log-times" in argv:
        # Each line of --log starts with SDL's clock: wall time, not the game.
        stderr = re.sub(rb"(?m)^\d+ ", b"", stderr)

    lines = ["args: " + " ".join(repr(normalise(a, tree, tmp)) for a in argv),
             "status: %d" % status]
    if kind == "krynn" and real_time(argv):
        lines.append("clock: real-time")
    for path in sorted(after):
        if before.get(path) == after[path]:
            continue
        if path.startswith(build + os.sep):
            shown = os.path.relpath(path, tree)
        else:
            shown = normalise(path, tree, tmp)
        try:
            with open(path, "rb") as f:
                lines.append("file: %s %s" % (shown, digest(f.read(), tree, tmp)))
        except FileNotFoundError:
            lines.append("file: %s gone" % shown)
    body = "\n".join(lines) + "\n"
    body += "stdout:\n" + normalise(stdout.decode("utf-8", "backslashreplace"), tree, tmp)
    body += "\nstderr:\n" + normalise(stderr.decode("utf-8", "backslashreplace"), tree, tmp)

    folder = os.path.join(out, kind)
    os.makedirs(folder, exist_ok=True)
    n = 0
    while True:
        try:
            with open(os.path.join(folder, "%05d.txt" % n), "x", encoding="utf-8",
                      errors="backslashreplace") as f:
                f.write(body)
            break
        except FileExistsError:
            n += 1

    if status < 0:
        # Die as the child did, so that the caller sees the same signal.
        signal.signal(-status, signal.SIG_DFL)
        os.kill(os.getpid(), -status)
    return status


def make_golden(makefile, out):
    with open(makefile, encoding="utf-8") as f:
        lines = f.read().split("\n")
    start = next(i for i, line in enumerate(lines) if line.startswith("sanitize:"))
    recipe = []
    for line in lines[start + 1:]:
        if line.startswith("\t"):
            recipe.append(line)
        elif line.strip():
            break
    picked = [line.replace("eclplay_sanitize", "eclplay")
              for line in recipe if "eclplay_sanitize" in line]
    with open(out, "w", encoding="utf-8") as f:
        f.write("include Makefile\n\ngolden-sanitize:\n" + "\n".join(picked) + "\n")
    return len(picked)


def load_runs(folder):
    """{args line: [record paths in name order]} for one kind's folder."""
    runs = {}
    if not os.path.isdir(folder):
        return runs
    for name in sorted(os.listdir(folder)):
        path = os.path.join(folder, name)
        with open(path, "rb") as f:
            key = f.readline().decode("utf-8", "backslashreplace").rstrip("\n")
        runs.setdefault(key, []).append(path)
    return runs


def header(data):
    return data.split(b"\nstdout:\n", 1)[0]


def compare_kind(kind, base, head):
    """Print the differing runs of one kind; return (identical, differing,
    unseeded differing, real-time differing)."""
    base_runs = load_runs(os.path.join(base, kind))
    head_runs = load_runs(os.path.join(head, kind))
    same = differ = unseeded = clocked = 0
    for key in sorted(set(base_runs) | set(head_runs)):
        left = base_runs.get(key, [])
        right = head_runs.get(key, [])
        for i in range(max(len(left), len(right))):
            if i >= len(right):
                print("only in base: %s: %s" % (left[i], key))
                differ += 1
                continue
            if i >= len(left):
                print("only in head: %s: %s" % (right[i], key))
                differ += 1
                continue
            with open(left[i], "rb") as f:
                a = f.read()
            with open(right[i], "rb") as f:
                b = f.read()
            if a == b:
                same += 1
            elif kind == "krynn" and b"clock: real-time" in header(a) + header(b):
                clocked += 1
                print("differs, real-time clock (--log-times, --sound-clock real or disk "
                      "audio): %s vs %s: %s" % (left[i], right[i], key))
            elif kind == "krynn" and "'--seed'" not in key:
                unseeded += 1
                print("differs, unseeded (the clock's Random): %s vs %s: %s"
                      % (left[i], right[i], key))
            else:
                differ += 1
                print("differs: %s vs %s: %s" % (left[i], right[i], key))
    return same, differ, unseeded, clocked


def compare(base, head):
    failed = False
    for kind in RUN_KINDS:
        same, differ, unseeded, clocked = compare_kind(kind, base, head)
        line = "%s runs compared: %d identical, %d differing, %d unseeded differing" % (
            kind, same, differ, unseeded)
        if kind == "krynn":
            line += ", %d real-time differing" % clocked
        print(line)
        failed = failed or differ > 0
    same = differ = 0
    for kind in TOOL_KINDS:
        s, d, _, _ = compare_kind(kind, base, head)
        same += s
        differ += d
    print("tool outputs compared: %d identical, %d differing" % (same, differ))
    failed = failed or differ > 0
    return 1 if failed else 0


def main(argv):
    if len(argv) >= 1 and argv[0] == "record" and len(argv) >= 7 and argv[6] == "--":
        kind, real, out, tree, tmp = argv[1:6]
        return record(kind, real, out, tree, tmp, argv[7:])
    if len(argv) == 3 and argv[0] == "compare":
        return compare(argv[1], argv[2])
    if len(argv) == 3 and argv[0] == "mk":
        print("golden.mk: %d sanitize eclplay lines" % make_golden(argv[1], argv[2]))
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
