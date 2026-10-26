# Disassembly image

Part of the [developer notes](../DEVELOPERS.md).

`make merged` writes `build/START_FULL.EXE` and `build/START_FULL.map` with
`tools/ovrmerge.py` (Python 3, standard library only); `make test` and
`make sanitize` build them too. The game logic lives in
`GAME.OVR`, a Turbo Pascal overlay file (`FBOV`) that `START.EXE` loads through
35 `INT 3Fh` stub segments, with 506 entry points in all. The tool unpacks
EXEPACK, appends each overlay segment from paragraph `0x1f48` (above the stack
and heap the program reserves), turns the overlay fixups into MZ relocations,
and rewrites every stub entry as a `JMP FAR` to its routine. The stubs cover
`GAME.OVR` exactly; the tool stops if any byte is left over.

Load `START_FULL.EXE` in Ghidra as an MZ executable. Its addresses are the
unpacked ones, so `521:04ac` is `1521:04ac` at Ghidra's default load segment
`0x1000`, with no `0x239` correction. The map lists each overlay's stub, load
segment and `GAME.OVR` offset, and where each stub entry jumps. The output is
for disassembly only and has not been run under DOS.

The sequels get the same treatment, each with its own target:

| Target | Input | Output |
| --- | --- | --- |
| `make merged` | `COKAssets/START.EXE`, `COKAssets/GAME.OVR` | `build/START_FULL.EXE`, `.map` |
| `make death-merged` | `DKKAssets/START.EXE`, `DKKAssets/GAME.OVR` | `build/death-START_FULL.EXE`, `.map` |
| `make dqk-merged` | `DQKAssets/DQK.EXE` | `build/DQK_FULL.EXE`, `.map` |

`DEATH=dir` and `DQK=dir` read the files from elsewhere. Each game's Ghidra
output (`*.EXE.c`, `*.EXE.h`) is kept beside its files.

Death Knights of Krynn is built the same way as this game: an EXEPACK
`START.EXE` and a `FBOV` `GAME.OVR`, 36 overlay segments with 475 entry
points, appended from paragraph `0x25b9`. `tools/ovrmerge.py` also reads a
`START.EXE` that is not packed.

The Dark Queen of Krynn has no `GAME.OVR`: its overlays sit in `DQK.EXE`
itself, after the part DOS loads (file offset `0x218e0` to the end), and a
different overlay manager loads them, in the entry point's segment `1d99`.
`tools/dqkmerge.py` finds its table (`1d99:0c1f`, 13 entries of 18 bytes,
count at `1d99:0c19`): each entry gives the load segment, the file
paragraph (relocations as offset/segment pairs, then the code from the next
paragraph), the paragraphs in memory and read from the file, the relocation
count, the parent overlay and a flag for overlays loaded at start. Overlays
share load areas as a tree (children above their parent, siblings at one
segment), so a segment number in an overlay means that overlay or one of
its parents. Its 182 routines are called through 10-byte stubs in `1d99`,
`CALL 058d` / `JMP FAR seg:off` / `DW overlay`.

The tool leaves the root image and overlay 12 where they run: overlay 12 is
loaded at start and is the data segment and stack (`322e`, so `422e` in
Ghidra, as in `DQK.EXE.c`). Every other overlay gets a segment of its own
from `0x4045`; their relocations are rewritten to the copy each segment
number means, the table's load segments to the copies, and each stub
becomes a `JMP FAR` to its routine. Every stub lands just after a `RETF`,
154 of them on `PUSH BP / MOV BP,SP`. The map lists where each overlay runs
and where it was put, every stub's target, and any segment number in an
overlay area that no overlay owns (only `1d99:0c0f`, the top of memory,
`4024`).
