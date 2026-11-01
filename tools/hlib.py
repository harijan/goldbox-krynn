#!/usr/bin/env python3
"""Read The Dark Queen of Krynn's HLIB libraries (.TLB, .GLB; stdlib only).

    python3 tools/hlib.py list FILE...
    python3 tools/hlib.py extract FILE ID OUT
    python3 tools/hlib.py png FILE ID OUT.png [--palette LIB ...] [--scale N]
    python3 tools/hlib.py pngs FILE DIR [--palette LIB ...] [--scale N]

ID is a record's path: its index, or (with an id map) its id, then the
index inside each nested library ("2/1": record 2's record 1). `list`
prints each record (picture, palette, data or nested library), `extract`
writes a record's bytes, `png` renders a picture, `pngs` every picture of
a file. The palette is the --palette libraries' records (in order), then
the palette records of each library on the way to the picture; entries no
palette sets show as grey (index, index, index), transparent pixels as
magenta. Dark Queen's pictures take their base colours from DISK1/ALWAYS.TLB
and DISK3/FRAME.TLB (--palette both).

The format (DARK-QUEEN.md, "Files"):

  library   "HLIB", u32 size, u16 count, u16 flags, 4 bytes type (TILE,
            DATA, DIG4, HLIB), then count + 1 u32 offsets from its start;
            flags 0x100: record 0 maps ids (u16 n, n x (u16 id, u16 index)).
            Records are stored as they are (no compression); a record
            starting "HLIB" is a nested library.
  picture   u16 height, i16 row and i16 column of its hotspot (the game
            draws it at the place it is given less these, dqk 269b:0a9c,
            its column then down to a multiple of 4), u8 width / 4, u8
            format, then the pixels, a byte each (256 colours), plane by
            plane as VGA's unchained planes hold them (pixel x in plane
            x % 4):
            0x10 raw, each plane's rows in turn;
            0x11 as 0x10, twice: bytes the screen is ANDed with, then the
                 pixels XORed on (dqk 10d7:02ba; every mask in the game is
                 0x00 or 0xff, pixel 0 under 0xff: 0xff transparent here);
            0x12 PackBits of the rows (dqk 10d7:0a1c: 0x80 does nothing),
                 each row its four plane-rows in turn;
            0x15 as 0x10, 0xff transparent;
            0x17 the four planes' rows (4 x height), each runs: a byte c,
                 0 ends the row, 0x80 and up skips 256 - c (transparent),
                 below copies c pixels;
            0x19 drawn from the same library's records (dqk 269b:0aec):
                 6-byte entries, u8 record, u8 the entries' count (re-read
                 from each), i16 row, i16 column from the record's own
                 place, each record at its hotspot from there; nothing
                 else is drawn (PICC.TLB's 160/6, 7, 9 and 11, 176x88 with
                 record 2 alone, leave their right half as the screen
                 was), shown here on a transparent canvas.
  palette   a picture header's format byte 0x18 (dqk 2796:0536): u16 type
            (bit 0: first and count hold, else all 256; 2: colour cycles
            follow, 4 bytes each; 4: EGA colours), u16 first, u16 count, u8
            the cycles, u8 0x18, then count 8-bit RGB triples (>> 2 into the
            DAC), the cycles, and count / 2 bytes of EGA colours (two a
            byte).
"""
import argparse
import pathlib
import struct
import sys
import zlib

PALETTE_FORMAT = 0x18
TRANSPARENT_RGB = (255, 0, 255)


class FormatError(Exception):
    pass


class Library:
    """An HLIB library: type (bytes), flags, records (bytes each) and ids
    ({id: index}, or None without an id map)."""

    def __init__(self, data):
        if len(data) < 16 or data[:4] != b"HLIB":
            raise FormatError("not an HLIB library")
        self.size, count, self.flags, self.type = struct.unpack_from("<IHH4s", data, 4)
        offsets = struct.unpack_from(f"<{count + 1}I", data, 16)
        if list(offsets) != sorted(offsets) or offsets[-1] > len(data):
            raise FormatError("offsets out of order or past the end")
        self.records = [bytes(data[offsets[i]:offsets[i + 1]]) for i in range(count)]
        self.ids = None
        if self.flags & 0x100:
            n, = struct.unpack_from("<H", self.records[0])
            self.ids = dict(struct.unpack_from("<HH", self.records[0], 2 + 4 * i) for i in range(n))

    def record(self, key):
        """A record by its id (with an id map) or its index."""
        if self.ids is not None:
            if key not in self.ids:
                raise FormatError(f"no id {key}")
            return self.records[self.ids[key]]
        if not 0 <= key < len(self.records):
            raise FormatError(f"no record {key}")
        return self.records[key]

    def entries(self):
        """(key, record) for each record but the id map: keys are ids with a
        map, indexes without."""
        if self.ids is not None:
            return sorted(((k, self.records[i]) for k, i in self.ids.items()))
        return list(enumerate(self.records))


def kind(record, library=None):
    """library, palette, picture or data (any record of a LIBRARY not of
    type TILE)."""
    if record[:4] == b"HLIB":
        return "library"
    if library is not None and library.type != b"TILE":
        return "data"
    if len(record) >= 8 and record[7] == PALETTE_FORMAT:
        return "palette"
    if len(record) >= 8 and record[7] in (0x10, 0x11, 0x12, 0x15, 0x17, 0x19):
        return "picture"
    return "data"


def palette(record):
    """A palette record: (first, [(r, g, b)], EGA bytes)."""
    _, first, count = struct.unpack_from("<HHH", record)
    rgb = [tuple(record[8 + 3 * i:11 + 3 * i]) for i in range(count)]
    return first, rgb, record[8 + 3 * count:]


def palettes(library):
    """{index: (r, g, b)} from a library's own palette records, in order."""
    colours = {}
    for _, record in library.entries():
        if kind(record, library) == "palette":
            first, rgb, _ = palette(record)
            colours.update({first + i: c for i, c in enumerate(rgb)})
    return colours


def header(record):
    """(width, height, x, y, format) of a picture record."""
    height, x, y, width4, fmt = struct.unpack_from("<HhhBB", record)
    return width4 * 4, height, x, y, fmt


def unpackbits(src, need):
    """PackBits: a byte n below 0x80 copies n + 1 bytes, 0x80 nothing, else
    repeats the next 257 - n times. Returns NEED bytes (or fewer at the end
    of SRC)."""
    out = bytearray()
    i = 0
    while len(out) < need and i < len(src):
        n = src[i]
        i += 1
        if n < 0x80:
            out += src[i:i + n + 1]
            i += n + 1
        elif n == 0x80:
            continue
        else:
            out += bytes([src[i]]) * (257 - n)
            i += 1
    return bytes(out[:need])


def _planar(body, width, height):
    """Rows from plane-major pixels: plane p's rows in turn, a quarter row each."""
    w4 = width // 4
    return [[body[(c % 4) * w4 * height + j * w4 + c // 4] for c in range(width)]
            for j in range(height)]


def picture(record, library=None):
    """A picture record's (width, height, x, y, format, rows): rows of
    palette indexes, None where transparent. LIBRARY: the library holding
    it, for format 0x19's records."""
    width, height, x, y, fmt = header(record)
    w4, body = width // 4, record[8:]
    if fmt in (0x10, 0x15):
        if len(body) < width * height:
            raise FormatError("picture shorter than its size")
        rows = _planar(body, width, height)
        if fmt == 0x15:
            rows = [[None if v == 0xFF else v for v in row] for row in rows]
    elif fmt == 0x11:
        if len(body) < 2 * width * height:
            raise FormatError("masked picture shorter than twice its size")
        mask = _planar(body, width, height)
        pixels = _planar(body[width * height:], width, height)
        rows = [[None if m == 0xFF else v for m, v in zip(mrow, prow)]
                for mrow, prow in zip(mask, pixels)]
    elif fmt == 0x12:
        p = unpackbits(body, width * height)
        if len(p) < width * height:
            raise FormatError("packed picture shorter than its size")
        rows = [[p[j * width + (c % 4) * w4 + c // 4] for c in range(width)]
                for j in range(height)]
    elif fmt == 0x17:
        planes, i = [], 0
        for _ in range(4 * height):
            row = []
            while True:
                c = body[i]
                i += 1
                if c == 0:
                    break
                if c >= 0x80:
                    row += [None] * (256 - c)
                else:
                    row += list(body[i:i + c])
                    i += c
            planes.append((row + [None] * w4)[:w4])
        rows = [[planes[(c % 4) * height + j][c // 4] for c in range(width)]
                for j in range(height)]
    elif fmt == 0x19:
        if library is None:
            raise FormatError("format 0x19 draws its library's records: none given")
        rows = [[None] * width for _ in range(height)]
        for k in range(0, len(body) - 5, 6):
            index, _, py, px = struct.unpack_from("<BBHH", body, k)
            part = library.record(index)
            pw, ph, _, _, _, prows = picture(part, library)
            for j in range(ph):
                for c in range(pw):
                    v = prows[j][c]
                    if v is not None and 0 <= py + j < height and 0 <= px + c < width:
                        rows[py + j][px + c] = v
    else:
        raise FormatError(f"picture format {fmt:#x} unknown")
    return width, height, x, y, fmt, rows


def walk(library, path=(), colours=None):
    """(path, record, colours) for each record of LIBRARY and its nested
    ones; COLOURS: the palette in effect, the library's own records laid
    over the one given."""
    colours = dict(colours or {})
    colours.update(palettes(library))
    for key, record in library.entries():
        if kind(record) == "library":
            yield from walk(Library(record), path + (key,), colours)
        else:
            yield path + (key,), record, colours, library


def find(library, path, colours=None):
    """(record, colours, holding library) at PATH (keys, outermost first)."""
    colours = dict(colours or {})
    colours.update(palettes(library))
    record = library.record(path[0])
    if len(path) == 1:
        return record, colours, library
    return find(Library(record), path[1:], colours)


def write_png(path, rows, colours, scale=1):
    """Rows of indexes (None transparent) as a 24-bit PNG."""
    height, width = len(rows) * scale, len(rows[0]) * scale
    raw = bytearray()
    for row in rows:
        line = bytearray()
        for v in row:
            rgb = TRANSPARENT_RGB if v is None else colours.get(v, (v, v, v))
            line += bytes(rgb) * scale
        raw += (b"\0" + line) * scale

    def chunk(kind_, body):
        return struct.pack(">I", len(body)) + kind_ + body + struct.pack(
            ">I", zlib.crc32(kind_ + body) & 0xFFFFFFFF)
    pathlib.Path(path).write_bytes(
        b"\x89PNG\r\n\x1a\n" +
        chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
        chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))


def describe(record, library):
    k = kind(record, library)
    if k == "picture":
        width, height, x, y, fmt = header(record)
        return f"picture {width}x{height} at ({x}, {y}) format {fmt:#04x}, {len(record)} bytes"
    if k == "palette":
        first, rgb, _ = palette(record)
        return f"palette {first:#04x}-{first + len(rgb) - 1:#04x}"
    if k == "library":
        sub = Library(record)
        return f"library {sub.type.decode('latin-1')}, {len(sub.records)} records"
    return f"data, {len(record)} bytes"


def load(path):
    return Library(pathlib.Path(path).read_bytes())


def replace(data, key, record):
    """The library DATA's bytes with its record KEY (an id, or an index) replaced
    by RECORD: the offsets and the size rewritten, the rest as it was (a copy
    of ECL.GLB with one block changed, for dqk_ecl.py)."""
    lib = Library(data)
    if lib.ids is not None:
        if key not in lib.ids:
            raise FormatError(f"no id {key}")
        index = lib.ids[key]
    else:
        index = key
        if not 0 <= index < len(lib.records):
            raise FormatError(f"no record {key}")
    records = list(lib.records)
    records[index] = bytes(record)
    head = 16 + 4 * (len(records) + 1)
    offsets, body = [], b""
    for r in records:
        offsets.append(head + len(body))
        body += r
    offsets.append(head + len(body))
    return (b"HLIB" + struct.pack("<IHH4s", head + len(body), len(records), lib.flags, lib.type) +
            struct.pack(f"<{len(offsets)}I", *offsets) + body)


def base_colours(paths):
    colours = {}
    for p in paths or ():
        colours.update(palettes(load(p)))
    return colours


def parse_id(text):
    return tuple(int(part, 0) for part in text.split("/"))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("list", help="each record of each FILE")
    p.add_argument("files", nargs="+")
    p = sub.add_parser("extract", help="a record's bytes")
    p.add_argument("file")
    p.add_argument("id")
    p.add_argument("out")
    for name in ("png", "pngs"):
        p = sub.add_parser(name, help="a picture as a PNG" if name == "png" else
                           "every picture of FILE as DIR/ID.png")
        p.add_argument("file")
        if name == "png":
            p.add_argument("id")
        p.add_argument("out")
        p.add_argument("--palette", action="append", help="a library whose palettes come first")
        p.add_argument("--scale", type=int, default=1)
    args = parser.parse_args(argv)
    try:
        if args.command == "list":
            for f in args.files:
                lib = load(f)
                print(f"{f}: {lib.type.decode('latin-1')}, {len(lib.records)} records"
                      + (f", {len(lib.ids)} ids" if lib.ids is not None else ""))
                for path, record, _, holder in walk(lib):
                    print(f"  {'/'.join(map(str, path)):10} {describe(record, holder)}")
        elif args.command == "extract":
            record, _, _ = find(load(args.file), parse_id(args.id))
            pathlib.Path(args.out).write_bytes(record)
        elif args.command == "png":
            record, colours, holder = find(load(args.file), parse_id(args.id),
                                           base_colours(args.palette))
            if kind(record, holder) != "picture":
                raise FormatError(f"record {args.id} is {kind(record, holder)}, not a picture")
            write_png(args.out, picture(record, holder)[5], colours, args.scale)
        else:
            out = pathlib.Path(args.out)
            out.mkdir(parents=True, exist_ok=True)
            done = 0
            for path, record, colours, holder in walk(load(args.file), (),
                                                      base_colours(args.palette)):
                if kind(record, holder) == "picture":
                    name = "-".join(map(str, path))
                    write_png(out / f"{name}.png", picture(record, holder)[5], colours,
                              args.scale)
                    done += 1
            print(f"{done} pictures in {out}")
    except (FormatError, struct.error, IndexError, OSError) as e:
        print(f"hlib: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
