# funcmatch: routines of two games, matched

`funcmatch.py` (Python 3, standard library only) pairs the routines of two
of the games by Ghidra's decompiled C of their merged images, and says how
alike each pair is. It measured what [DARK-QUEEN.md](../../DARK-QUEEN.md)
says the games share; a later part uses it to find a cited routine's
counterpart in Dark Queen before checking the two against each other.

    python3 tools/funcmatch/funcmatch.py match DKKAssets/death-START_FULL.EXE.c \
        DQKAssets/DQK_FULL.EXE.c --out build/dkk_dqk.tsv
    python3 tools/funcmatch/funcmatch.py cites DKKAssets/death-START_FULL.EXE.c \
        build/dkk_dqk.tsv --game dkk
    python3 tools/funcmatch/funcmatch.py noise COKAssets/START_FULL.EXE.c \
        DQKAssets/DQK_FULL.EXE.c

`match` takes about 25 seconds for Death Knights against Dark Queen.

## Why the decompiled C

Champions and Death Knights are Turbo Pascal, Dark Queen Borland C++: the
same routine compiles to different instructions (another calling
convention, register use, inlining), so a byte or instruction match finds
one routine in common. Ghidra's C of both is closer: the same statements,
in the same order, with the same small constants.

## The method

1. **Routines.** Each function Ghidra defined (`FUN_seg_off`), without its
   thunks, repeats or Turbo Pascal's runtime segment `3000`.
2. **Tokens.** The body's C tokens, with what may differ between two
   versions of one routine made alike: constants of 16 and up become `N`
   (data segment offsets and table sizes move; flags and small counts stay),
   types `T`, every called routine `FUN`, Ghidra's `DAT_`, labels, local,
   parameter and temporary names their kind. A strict variant keeps
   constants below 256 for the "identical" class.
3. **Exact pairs.** A routine of 30 tokens or more whose token sequence is
   unique in each game pairs with its double.
4. **Similar pairs.** Three rounds, greedy, at thresholds 0.6, 0.35, 0.2:
   for each unpaired A routine and each unpaired B routine of comparable
   length (the shorter at least 0.4 of the longer), the Jaccard similarity
   of their 4-token shingles (above 0.12), plus 0.3 times the share of A's
   paired neighbours (callers and callees) whose partners are B's
   neighbours. Each round's pairs help the next one's neighbour scores.
5. **Classes.** A pair's ratio is `difflib`'s over the two token sequences:
   **identical** (the same tokens), **minor** (0.9 and up), **substantial**
   (0.5 and up: the same algorithm, re-coded), **rewritten** (below 0.5),
   and **none** for an A routine no B routine paired with.

`cites` finds the port's citations in `src/*.c` (`seg:off`, Champions'
3-digit segments counted from `0x1000`; `dkk seg:off` and `dqk seg:off`
for the others, a `dkk`/`dqk` before an address on its line counting
until a `)` closes it), the A routine each falls in (the last routine
starting at or below it), and sums the classes of their pairs per source
file. A few citations fall below the first routine of their segment
(Champions' 50: data, the runtime) and are counted apart.

## What the numbers mean (the noise floor)

`noise` scores random pairs of routines of 30 tokens or more with the
ratio the classes use. Measured on 2026-10-26, 2000 pairs each:

| Games | Median | 90th percentile | 99th |
| --- | --- | --- | --- |
| Champions, Death Knights | 0.23 | 0.38 | 0.51 |
| Champions, Dark Queen | 0.22 | 0.37 | 0.52 |
| Death Knights, Dark Queen | 0.22 | 0.37 | 0.50 |

So any two routines score about 0.2 from C's own grammar, and one in a
hundred unrelated pairs reaches 0.5. A pair the matcher chose (by
shingles and neighbours, not by the ratio) at 0.5 or more is very likely
the same routine; "rewritten" pairs (below 0.5) are often but not always
the routine's counterpart, and need reading. "identical" and "minor" are
reliable.

The same matcher gives, for Champions against Death Knights (a game the
port already plays from one engine): 477 identical, 167 minor, 221
substantial, 70 rewritten, 75 none of Champions' 1,010 routines; of the
665 distinct routines the port cites, 58% identical or minor. For Death
Knights against Dark Queen: 74, 32, 448, 285 and 247 of 1,086; of the cited
Champions routines in Dark Queen, 43 of 665 (6%) identical or minor.
