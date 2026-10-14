#!/usr/bin/env python3
"""Print the text screen's font (src/crt.c's pc_font) from unscii-8.hex.

unscii 2.1's 8x8 font, by viznut, in the Public Domain
(http://viznut.fi/unscii/unscii-8.hex), taken for each code page 437
character through its Unicode meaning (00-1f and 7f as the PC's ROM showed
them: smileys, card suits, arrows, the house). The four that unscii lacks
are drawn here in its style. Usage: tools/pcfont.py unscii-8.hex
"""
import sys

# 00-1f as pictures, as the PC's text mode showed them.
LOW = [0x0000, 0x263A, 0x263B, 0x2665, 0x2666, 0x2663, 0x2660, 0x2022,
       0x25D8, 0x25CB, 0x25D9, 0x2642, 0x2640, 0x266A, 0x266B, 0x263C,
       0x25BA, 0x25C4, 0x2195, 0x203C, 0x00B6, 0x00A7, 0x25AC, 0x21A8,
       0x2191, 0x2193, 0x2192, 0x2190, 0x221F, 0x2194, 0x25B2, 0x25BC]

# What unscii-8 lacks, drawn for the port.
OWN = {
    0x263C: ["...##...",  # sun
             "#.####.#",
             ".##..##.",
             "##....##",
             ".##..##.",
             "#.####.#",
             "...##...",
             "........"],
    0x2302: ["........",  # house
             "...##...",
             "..####..",
             ".##..##.",
             ".##..##.",
             ".##..##.",
             ".######.",
             "........"],
    0x2310: ["........",  # reversed not sign: unscii's U+00AC mirrored
             ".######.",
             ".##.....",
             ".##.....",
             "........",
             "........",
             "........",
             "........"],
    0x2219: ["........",  # bullet operator: unscii's U+00B7, two rows high
             "........",
             "........",
             "...##...",
             "...##...",
             "........",
             "........",
             "........"],
}


def unicode_of(c):
    if c < 0x20:
        return LOW[c]
    if c == 0x7F:
        return 0x2302
    return ord(bytes([c]).decode("cp437"))


def main():
    glyphs = {}
    with open(sys.argv[1]) as f:
        for line in f:
            line = line.strip()
            if line:
                code, bits = line.split(":")
                glyphs[int(code, 16)] = bytes.fromhex(bits)
    for code, rows in OWN.items():
        glyphs[code] = bytes(int(r.replace("#", "1").replace(".", "0"), 2) for r in rows)
    for c in range(256):
        u = unicode_of(c)
        bits = bytes(8) if u in (0, 0x20, 0xA0) else glyphs[u]
        assert len(bits) == 8, (hex(c), hex(u))
        own = " drawn here" if u in OWN else ""
        print("    {%s}, /* %02x U+%04X%s */" % (", ".join("0x%02x" % b for b in bits), c, u, own))


if __name__ == "__main__":
    main()
