#!/usr/bin/env python3
"""Match the routines of two games by their decompiled C (stdlib only).

    python3 tools/funcmatch/funcmatch.py match A.c B.c [--out PAIRS.tsv]
    python3 tools/funcmatch/funcmatch.py cites A.c PAIRS.tsv [--game G] [--src DIR]
    python3 tools/funcmatch/funcmatch.py noise A.c B.c [--pairs N] [--seed S]

A.c and B.c are Ghidra's C for two merged images (e.g.
DKKAssets/death-START_FULL.EXE.c and DQKAssets/DQK_FULL.EXE.c). `match`
pairs A's routines with B's and classes each pair (identical, minor,
substantial, rewritten; none for A's routines left alone), printing a
summary and writing the pairs as TSV. `cites` reads the port's citations
of game G's routines in src/*.c (G: cok for the plain `seg:off` ones, dkk
or dqk for those after `dkk`/`dqk`), finds the A routine each falls in,
and sums their classes per source file from PAIRS.tsv. `noise` scores
random pairs, the floor a similarity has to clear to mean anything.
tools/funcmatch/README.md says how it works and what its numbers mean.
"""
import argparse
import bisect
import collections
import difflib
import hashlib
import pathlib
import random
import re
import statistics
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]

NAME = re.compile(r"\b((?:thunk_)?FUN_([0-9a-f]{4})_([0-9a-f]{4})|entry)\s*\(")
TOKEN = re.compile(r"'(?:\\x[0-9a-fA-F]+|\\.|[^'])'|[A-Za-z_]\w*|0x[0-9a-fA-F]+|\d+|"
                   r"==|!=|<=|>=|<<|>>|&&|\|\||->|\+\+|--|[^\s\w]")
CALL = re.compile(r"\b(?:thunk_)?(FUN_[0-9a-f]{4}_[0-9a-f]{4})\s*\(")
TYPES = set("undefined undefined1 undefined2 undefined4 int uint char byte word ulong long "
            "short ushort bool code float double dword uchar".split())
ESCAPES = {"0": 0, "n": 10, "t": 9, "r": 13, "\\": 92, "'": 39, "a": 7, "b": 8, "f": 12, "v": 11}
# A citation: seg:off, the segment of 3 digits (Champions' 521:04ac, its
# 0x1000 left out) or 4.
CITATION = re.compile(r"(?<![\w:])([0-9a-f]{3,4}):([0-9a-f]{4})\b")
GAME_WORD = re.compile(r"\b(dkk|dqk)\b")


# ---------------------------------------------------------------- parsing

def parse(path):
    """{name: body} for each routine defined in Ghidra's C at PATH (a name
    met twice gets #2, #3...)."""
    lines = pathlib.Path(path).read_text(encoding="latin-1").split("\n")
    funcs = {}
    i, n = 0, len(lines)
    while i < n:
        line = lines[i]
        if line and not line[0].isspace() and line[0] not in "{}/#" and "(" in line \
                and not line.rstrip().endswith(";"):
            j, sig = i, [line]
            while j + 1 < n and lines[j + 1] != "{" and j - i < 6 and lines[j + 1].strip():
                j += 1
                sig.append(lines[j])
            while j + 1 < n and not lines[j + 1].strip() and j - i < 8:
                j += 1
            if j + 1 < n and lines[j + 1] == "{":
                m = NAME.search(" ".join(sig))
                k, body = j + 2, []
                while k < n and lines[k] != "}":
                    body.append(lines[k])
                    k += 1
                name = m.group(1) if m else sig[0]
                key, c = name, 1
                while key in funcs:
                    c += 1
                    key = f"{name}#{c}"
                funcs[key] = "\n".join(body)
                i = k + 1
                continue
        i += 1
    return funcs


def _value(t):
    if t.startswith("'"):
        x = t[1:-1]
        if x.startswith("\\x"):
            return int(x[2:], 16)
        if x.startswith("\\"):
            return ESCAPES.get(x[1:], -1)
        return ord(x)
    if t.startswith("0x"):
        return int(t, 16)
    if t.isdigit():
        return int(t)
    return None


def tokens(body, loose=True):
    """The body's tokens with what differs between two compilers' and two
    data segments' versions of one routine named alike: constants of 16
    and up become N (LOOSE; else those of 256 and up become H), types T,
    called routines FUN, data DAT, Ghidra's labels, locals and parameters
    their kind."""
    out = []
    body = re.sub(r"\+\s*-(?=\s*(0x|\d))", "+ ", body)
    for t in TOKEN.findall(body):
        v = _value(t)
        if v is not None:
            out.append((str(v) if 0 <= v < 16 else "N") if loose else
                       (str(v) if 0 <= v < 0x100 else "H"))
            continue
        if loose and t in TYPES:
            t = "T"
        elif t.startswith(("FUN_", "thunk_FUN_")):
            t = "FUN"
        elif t.lstrip("_").startswith("DAT_"):
            t = "DAT"
        elif t.startswith(("LAB_", "code_r", "switchD", "caseD", "PTR_", "UNK_", "s_", "u_")):
            t = t.split("_")[0]
        elif re.match(r"^[a-z]{1,4}Var\d+$", t):
            t = "Var" if loose else re.sub(r"\d+$", "", t)
        elif re.match(r"^[a-z]+Stack_", t):
            t = "local"
        elif re.match(r"^(local|param|in|unaff|extraout|stack)_", t):
            t = t.split("_")[0]
        out.append(t)
    return out


def shingles(toks, k=4):
    return {hash(tuple(toks[i:i + k])) for i in range(max(1, len(toks) - k + 1))}


class Routine:
    def __init__(self, name, body):
        self.name = name
        self.toks = tokens(body)
        strict = tokens(body, loose=False)
        self.hash = hashlib.md5(" ".join(self.toks).encode()).hexdigest()
        self.strict = hashlib.md5(" ".join(strict).encode()).hexdigest()
        self.shingles = shingles(self.toks)
        self.callees = set(CALL.findall(body))
        self.callers = set()


def load(path):
    """{name: Routine} for PATH's routines, but thunks, repeats and the
    runtime's segment 3000."""
    out = {k: Routine(k, b) for k, b in parse(path).items()
           if not k.startswith("thunk_") and "#" not in k and not k.startswith("FUN_3000_")}
    for r in out.values():
        r.callees &= set(out)
        for c in r.callees:
            out[c].callers.add(r.name)
    return out


# ---------------------------------------------------------------- matching

def jaccard(a, b):
    return len(a & b) / len(a | b) if a and b else 0.0


def match(A, B, verbose=False):
    """{a name: b name}: unique exact token matches (30 tokens or more),
    then three rounds of the best shingle similarity plus 0.3 x the share
    of a's matched neighbours (callers and callees) whose partners are b's
    neighbours, taken greedily over thresholds 0.6, 0.35, 0.2."""
    pair, rev = {}, {}
    for key in ("strict", "hash"):
        ha, hb = collections.defaultdict(list), collections.defaultdict(list)
        for k, r in A.items():
            if k not in pair and len(r.toks) >= 30:
                ha[getattr(r, key)].append(k)
        for k, r in B.items():
            if k not in rev and len(r.toks) >= 30:
                hb[getattr(r, key)].append(k)
        for h, aa in ha.items():
            bb = hb.get(h, [])
            if len(aa) == 1 and len(bb) == 1:
                pair[aa[0]], rev[bb[0]] = bb[0], aa[0]
    if verbose:
        print(f"exact token matches: {len(pair)}", file=sys.stderr)

    def neighbours(a, b):
        near = A[a].callees | A[a].callers
        theirs = B[b].callees | B[b].callers
        known = [x for x in near if x in pair]
        return sum(pair[x] in theirs for x in known) / len(known) if known else 0.0

    for threshold in (0.6, 0.35, 0.2):
        cands = []
        left = [b for b in B if b not in rev]
        for a, ra in A.items():
            if a in pair:
                continue
            la = len(ra.toks)
            for b in left:
                lb = len(B[b].toks)
                if min(la, lb) / max(la, lb, 1) < 0.4:
                    continue
                s = jaccard(ra.shingles, B[b].shingles)
                if s > 0.12:
                    cands.append((s + 0.3 * neighbours(a, b), a, b))
        cands.sort(reverse=True)
        for score, a, b in cands:
            if score < threshold:
                break
            if a not in pair and b not in rev:
                pair[a], rev[b] = b, a
        if verbose:
            print(f"after the round at {threshold}: {len(pair)}", file=sys.stderr)
    return pair


def classify(A, B, pair):
    """{a name: (b name or None, class, ratio)}: the token sequences'
    difflib ratio; identical when equal (strict or loose), minor at 0.9 and
    up, substantial at 0.5, rewritten below, none without a partner."""
    out = {}
    for a, ra in A.items():
        b = pair.get(a)
        if b is None:
            out[a] = (None, "none", 0.0)
            continue
        rb = B[b]
        ratio = difflib.SequenceMatcher(None, ra.toks, rb.toks, autojunk=False).ratio()
        if ra.strict == rb.strict or ra.hash == rb.hash:
            cls = "identical"
        else:
            cls = "minor" if ratio >= 0.9 else "substantial" if ratio >= 0.5 else "rewritten"
        out[a] = (b, cls, ratio)
    return out


CLASSES = ("identical", "minor", "substantial", "rewritten", "none")


def read_pairs(path):
    out = {}
    for line in pathlib.Path(path).read_text().splitlines():
        if line.startswith("#") or not line.strip():
            continue
        a, b, cls, ratio = line.split("\t")
        out[a] = (b or None, cls, float(ratio))
    return out


# ---------------------------------------------------------------- citations

def starts(A):
    """Sorted (linear address, name) of A's routines named FUN_seg_off."""
    out = []
    for k in A:
        m = re.match(r"FUN_([0-9a-f]{4})_([0-9a-f]{4})$", k)
        if m:
            out.append((int(m[1], 16) * 16 + int(m[2], 16), k))
    return sorted(out)


def citations(src, game):
    """(file, line number, seg, off) of each citation of GAME's code in
    SRC's *.c: a `dkk`/`dqk` word before it on its line, not closed by a
    `)` since, makes it that game's; none, Champions' (cok)."""
    for f in sorted(pathlib.Path(src).glob("*.c")):
        for n, line in enumerate(f.read_text(errors="replace").split("\n"), 1):
            for m in CITATION.finditer(line):
                before = line[:m.start()]
                words = [w for w in GAME_WORD.finditer(before) if ")" not in before[w.end():]]
                cited = words[-1].group(1) if words else "cok"
                if cited != game:
                    continue
                seg = int(m[1], 16) + (0x1000 if len(m[1]) == 3 else 0)
                yield f.name, n, seg, int(m[2], 16)


def owner(table, seg, off):
    """The routine an address falls in: the last start at or below it."""
    lin = seg * 16 + off
    i = bisect.bisect_right([s for s, _ in table], lin) - 1
    return table[i][1] if i >= 0 else None


# ---------------------------------------------------------------- commands

def cmd_match(args):
    A, B = load(args.a), load(args.b)
    pair = match(A, B, args.verbose)
    res = classify(A, B, pair)
    counts = collections.Counter(cls for _, cls, _ in res.values())
    print(f"{args.a}: {len(A)} routines; {args.b}: {len(B)}")
    print("  ".join(f"{c} {counts[c]}" for c in CLASSES))
    kept = {b for b, cls, _ in res.values() if cls in ("identical", "minor", "substantial")}
    print(f"{len(B) - len(kept)} of B's routines have no counterpart (identical, minor or "
          f"substantial)")
    if args.out:
        with open(args.out, "w") as f:
            f.write("# a\tb\tclass\tratio\n")
            for a in sorted(res):
                b, cls, ratio = res[a]
                f.write(f"{a}\t{b or ''}\t{cls}\t{ratio:.3f}\n")
    return 0


def cmd_cites(args):
    A = load(args.a)
    res = read_pairs(args.pairs)
    table = starts(A)
    per = collections.defaultdict(dict)
    unresolved = 0
    for name, _, seg, off in citations(args.src, args.game):
        k = owner(table, seg, off)
        if k is None or k not in res:
            unresolved += 1
            continue
        per[name][k] = res[k]
    print(f"{'file':16} {'cited':>5} " + " ".join(f"{c:>11}" for c in CLASSES))
    every = {}
    for name in sorted(per):
        c = collections.Counter(cls for _, cls, _ in per[name].values())
        every.update(per[name])
        print(f"{name:16} {len(per[name]):5} " + " ".join(f"{c[k]:11}" for k in CLASSES))
    c = collections.Counter(cls for _, cls, _ in every.values())
    print(f"{'distinct':16} {len(every):5} " + " ".join(f"{c[k]:11}" for k in CLASSES))
    if unresolved:
        print(f"{unresolved} citations fall in no routine of {args.a}")
    return 0


def cmd_noise(args):
    A, B = load(args.a), load(args.b)
    rng = random.Random(args.seed)
    a_names = [k for k, r in A.items() if len(r.toks) >= 30]
    b_names = [k for k, r in B.items() if len(r.toks) >= 30]
    scores = []
    for _ in range(args.pairs):
        ra, rb = A[rng.choice(a_names)], B[rng.choice(b_names)]
        scores.append(difflib.SequenceMatcher(None, ra.toks, rb.toks, autojunk=False).ratio())
    scores.sort()
    print(f"{args.pairs} random pairs of 30 tokens or more: median "
          f"{statistics.median(scores):.2f}, 90th percentile {scores[len(scores) * 9 // 10]:.2f}, "
          f"99th {scores[len(scores) * 99 // 100]:.2f}")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("match", help="pair and class A's routines with B's")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--out", help="write the pairs here (TSV)")
    p.add_argument("--verbose", "-v", action="store_true")
    p = sub.add_parser("cites", help="the port's citations of A's routines, by class")
    p.add_argument("a")
    p.add_argument("pairs", help="match's TSV for A against the other game")
    p.add_argument("--game", default="cok", choices=("cok", "dkk", "dqk"),
                   help="whose citations A's are (default cok: the plain seg:off ones)")
    p.add_argument("--src", default=str(ROOT / "src"))
    p = sub.add_parser("noise", help="similarity of random pairs")
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--pairs", type=int, default=2000)
    p.add_argument("--seed", type=int, default=1)
    args = parser.parse_args(argv)
    return {"match": cmd_match, "cites": cmd_cites, "noise": cmd_noise}[args.command](args)


if __name__ == "__main__":
    sys.exit(main())
