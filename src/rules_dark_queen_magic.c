#include "rules.h"
#include "rules_dark_queen_magic.h"

/* The Dark Queen of Krynn's spells and effects (cok_rules_dark_queen's
 * spells and effects sections, rules_dark_queen.c): the spells' names and
 * the spell list headings, the handlers by spell and effect id, and the ids
 * the code names. Every handler is the original's routine, cited (dqk
 * seg:off, build/DQK_FULL.EXE's numbering) and unported: a spell or effect
 * that reaches one stops the run. An effect whose routine is a bare retf
 * (78e4:4f2f and nine more, by their bytes) does nothing: COK_FX_NOTHING.
 * The handler lines are the output of tools/dqk_magic_ids.py, from the
 * stores the fill routines make (7fd8:46bb and 4892 for the spells, 78e4:4fb2
 * and 549e for the effects), following an overlay stub to its routine; the
 * names and headings are read from the same executable's data segment, and
 * tests/test_dark_queen.c checks all of it against the bytes. An entry's
 * address is the stored offset, and its cite the routine's seg:off: 0x75's
 * 10a3 is the stub at 2d99:10a3, which jumps to 52db:4fd1. Ghidra's numbering
 * throughout. A row the port carries out names its kind (spells.h's, not
 * COK_FX_MISSING), and its routine is cited in a comment beside it. */

/* The spells' names by id from 1 (names[id - 1]): far pointers at DS:25c7 +
 * 4 * id, read by the 7fd8 routines that push a name (8438:0009 and kin).
 * 0x41, 0x61 and 0x63 point at an empty string. 0x8a-0x8c have none (0000:0000
 * in the original): "", so the unported messages print no name for them
 * (cast.c's, which the spells reach first); a NULL check on a name does not
 * stop for them. */
const char *const cok_dark_queen_spell_names[0x8c] = {
    "Bless", /* 0x01 */
    "Curse", /* 0x02 */
    "Cure Light Wounds", /* 0x03 */
    "Cause Light Wounds", /* 0x04 */
    "Detect Magic", /* 0x05 */
    "Protection From Evil", /* 0x06 */
    "Protection from Good", /* 0x07 */
    "Resist Cold", /* 0x08 */
    "Burning Hands", /* 0x09 */
    "Charm Person", /* 0x0a */
    "Detect Magic", /* 0x0b */
    "Enlarge", /* 0x0c */
    "Reduce", /* 0x0d */
    "Friends", /* 0x0e */
    "Magic Missile", /* 0x0f */
    "Protection From Evil", /* 0x10 */
    "Protection From Good", /* 0x11 */
    "Read Magic", /* 0x12 */
    "Shield", /* 0x13 */
    "Shocking Grasp", /* 0x14 */
    "Sleep", /* 0x15 */
    "Find Traps", /* 0x16 */
    "Hold Person", /* 0x17 */
    "Resist Fire", /* 0x18 */
    "Silence, 15' Radius", /* 0x19 */
    "Slow Poison", /* 0x1a */
    "Snake Charm", /* 0x1b */
    "Spiritual Hammer", /* 0x1c */
    "Detect Invisibility", /* 0x1d */
    "Invisibility", /* 0x1e */
    "Knock", /* 0x1f */
    "Mirror Image", /* 0x20 */
    "Ray of Enfeeblement", /* 0x21 */
    "Stinking Cloud", /* 0x22 */
    "Strength", /* 0x23 */
    "Heal", /* 0x24 */
    "Cure Blindness", /* 0x25 */
    "Cause Blindness", /* 0x26 */
    "Cure Disease", /* 0x27 */
    "Cause Disease", /* 0x28 */
    "Dispel Magic", /* 0x29 */
    "Prayer", /* 0x2a */
    "Remove Curse", /* 0x2b */
    "Bestow Curse", /* 0x2c */
    "Blink", /* 0x2d */
    "Dispel Magic", /* 0x2e */
    "Fireball", /* 0x2f */
    "Haste", /* 0x30 */
    "Hold Person", /* 0x31 */
    "Invisibility, 10' Radius", /* 0x32 */
    "Lightning Bolt", /* 0x33 */
    "Prot. From Evil, 10' Radius", /* 0x34 */
    "Prot. From Good, 10' Radius", /* 0x35 */
    "Prot. From Normal Missiles", /* 0x36 */
    "Slow", /* 0x37 */
    "Harm", /* 0x38 */
    "Prot. From Evil, 10' Radius", /* 0x39 */
    "Cure Serious Wounds", /* 0x3a */
    "Silence 15' radius", /* 0x3b */
    "Detect Magic", /* 0x3c */
    "Remove Curse", /* 0x3d */
    "Bless", /* 0x3e */
    "Charm Person", /* 0x3f */
    "Burning Hands", /* 0x40 */
    "", /* 0x41 */
    "Cause Serious Wounds", /* 0x42 */
    "Neutralize Poison", /* 0x43 */
    "Poison", /* 0x44 */
    "Prot. From Evil, 10' Radius", /* 0x45 */
    "Sticks to Snakes", /* 0x46 */
    "Cure Critical Wounds", /* 0x47 */
    "Cause Critical Wounds", /* 0x48 */
    "Dispel Evil", /* 0x49 */
    "Flame Strike", /* 0x4a */
    "Raise Dead", /* 0x4b */
    "Slay Living", /* 0x4c */
    "Detect Magic", /* 0x4d */
    "Entangle", /* 0x4e */
    "Faerie Fire", /* 0x4f */
    "Invisibility to Animals", /* 0x50 */
    "Charm Monsters", /* 0x51 */
    "Confusion", /* 0x52 */
    "Dimension Door", /* 0x53 */
    "Fear", /* 0x54 */
    "Fire Shield", /* 0x55 */
    "Fumble", /* 0x56 */
    "Ice Storm", /* 0x57 */
    "Minor Globe Of Invulnerability", /* 0x58 */
    "Remove Curse", /* 0x59 */
    "Barkskin", /* 0x5a */
    "Cloud Kill", /* 0x5b */
    "Cone of Cold", /* 0x5c */
    "Feeblemind", /* 0x5d */
    "Hold Monsters", /* 0x5e */
    "Scroll of Protection", /* 0x5f */
    "Charm Person Or Mammal", /* 0x60 */
    "", /* 0x61 */
    "Cure Light Wounds", /* 0x62 */
    "", /* 0x63 */
    "Bestow Curse", /* 0x64 */
    "Blade Barrier", /* 0x65 */
    "Restoration", /* 0x66 */
    "Energy Drain", /* 0x67 */
    "Destruction", /* 0x68 */
    "Resurrection", /* 0x69 */
    "Cure Disease", /* 0x6a */
    "Neutralize Poison", /* 0x6b */
    "Hold Animal", /* 0x6c */
    "Prot. from Fire", /* 0x6d */
    "Death Spell", /* 0x6e */
    "Disintegrate", /* 0x6f */
    "Globe of Invulnerability", /* 0x70 */
    "Stone to Flesh", /* 0x71 */
    "Flesh to Stone", /* 0x72 */
    "Delayed Blast Fireball", /* 0x73 */
    "Mass Invisibility", /* 0x74 */
    "Power Word Stun", /* 0x75 */
    "Fire Touch", /* 0x76 */
    "Iron Skin", /* 0x77 */
    "Mass Charm", /* 0x78 */
    "Otto's Irr. Dance", /* 0x79 */
    "Mind Blank", /* 0x7a */
    "Power Word Blind", /* 0x7b */
    "Meteor Swarm", /* 0x7c */
    "Power Word Kill", /* 0x7d */
    "Monster Summoning", /* 0x7e */
    "Potion Of Speed", /* 0x7f */
    "Potion Of Frost", /* 0x80 */
    "Javelin Of Lightning", /* 0x81 */
    "Wand Of Paralyzation", /* 0x82 */
    "Potion Of Healing", /* 0x83 */
    "Elixir Of Youth", /* 0x84 */
    "Necklace Of Missiles", /* 0x85 */
    "Wand Of Magic Missiles", /* 0x86 */
    "Scroll of Protection", /* 0x87 */
    "Potion Of Invisibility", /* 0x88 */
    "Potion Of Extra Healing", /* 0x89 */
    "", /* 0x8a: no name (0000:0000) */
    "", /* 0x8b: no name (0000:0000) */
    "", /* 0x8c: no name (0000:0000) */
};

/* The spell list headings by spell level (DS:8b2e + 41 * level, 41 bytes a
 * heading, read by magic.c's list rows). */
const char *const cok_dark_queen_spell_levels[10] = {
    "Special",
    "1st Level",
    "2nd Level",
    "3rd Level",
    "4th Level",
    "5th Level",
    "6th Level",
    "7th Level",
    "8th Level",
    "9th Level",
};

/* The spells' handlers by id, DS:af45 + 4 * id (cok_rules_cast): the original's
 * routine at 7fd8:xxxx. The ids queen_cast.c's queen_spell_handler carries out (CLW,
 * MM, the plain applies, Bless and the other band a spells) are ported there, and take
 * precedence over the COK_CAST_NONE entries below, which keep the cite. */
const cok_rules_cast cok_dark_queen_spell_cast[0x8d] = {
    [0x00] = {0}, /* no store fills it */
    [0x01] = {0x00a2, COK_CAST_NONE, "dqk 7fd8:00a2"},
    [0x02] = {0x00c9, COK_CAST_NONE, "dqk 7fd8:00c9"},
    [0x03] = {0x00f8, COK_CAST_NONE, "dqk 7fd8:00f8"},
    [0x04] = {0x0162, COK_CAST_NONE, "dqk 7fd8:0162"},
    [0x05] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x06] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x07] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x08] = {0x01c8, COK_CAST_NONE, "dqk 7fd8:01c8"},
    [0x09] = {0x01e6, COK_CAST_NONE, "dqk 7fd8:01e6"},
    [0x0a] = {0x022a, COK_CAST_NONE, "dqk 7fd8:022a"},
    [0x0b] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x0c] = {0x02e8, COK_CAST_NONE, "dqk 7fd8:02e8"},
    [0x0d] = {0x0438, COK_CAST_NONE, "dqk 7fd8:0438"},
    [0x0e] = {0x04cd, COK_CAST_NONE, "dqk 7fd8:04cd"},
    [0x0f] = {0x050a, COK_CAST_NONE, "dqk 7fd8:050a"},
    [0x10] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x11] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x12] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x13] = {0x056a, COK_CAST_NONE, "dqk 7fd8:056a"},
    [0x14] = {0x0588, COK_CAST_NONE, "dqk 7fd8:0588"},
    [0x15] = {0x05c4, COK_CAST_NONE, "dqk 7fd8:05c4"},
    [0x16] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x17] = {0x06e9, COK_CAST_NONE, "dqk 7fd8:06e9"},
    [0x18] = {0x073c, COK_CAST_NONE, "dqk 7fd8:073c"},
    [0x19] = {0x075a, COK_CAST_NONE, "dqk 7fd8:075a"},
    [0x1a] = {0x0778, COK_CAST_NONE, "dqk 7fd8:0778"},
    [0x1b] = {0x0813, COK_CAST_NONE, "dqk 7fd8:0813"},
    [0x1c] = {0x08e3, COK_CAST_NONE, "dqk 7fd8:08e3"},
    [0x1d] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x1e] = {0x091b, COK_CAST_NONE, "dqk 7fd8:091b"},
    [0x1f] = {0x0939, COK_CAST_NONE, "dqk 7fd8:0939"},
    [0x20] = {0x0957, COK_CAST_NONE, "dqk 7fd8:0957"},
    [0x21] = {0x09a6, COK_CAST_NONE, "dqk 7fd8:09a6"},
    [0x22] = {0x09c4, COK_CAST_NONE, "dqk 7fd8:09c4"},
    [0x23] = {0x0c83, COK_CAST_NONE, "dqk 7fd8:0c83"},
    [0x24] = {0x0e9f, COK_CAST_NONE, "dqk 7fd8:0e9f"},
    [0x25] = {0x0ff6, COK_CAST_NONE, "dqk 7fd8:0ff6"},
    [0x26] = {0x1026, COK_CAST_NONE, "dqk 7fd8:1026"},
    [0x27] = {0x1044, COK_CAST_NONE, "dqk 7fd8:1044"},
    [0x28] = {0x1068, COK_CAST_NONE, "dqk 7fd8:1068"},
    [0x29] = {0x114e, COK_CAST_NONE, "dqk 7fd8:114e"},
    [0x2a] = {0x13b7, COK_CAST_NONE, "dqk 7fd8:13b7"},
    [0x2b] = {0x13ef, COK_CAST_NONE, "dqk 7fd8:13ef"},
    [0x2c] = {0x14dd, COK_CAST_NONE, "dqk 7fd8:14dd"},
    [0x2d] = {0x14fb, COK_CAST_NONE, "dqk 7fd8:14fb"},
    [0x2e] = {0x114e, COK_CAST_NONE, "dqk 7fd8:114e"},
    [0x2f] = {0x1519, COK_CAST_NONE, "dqk 7fd8:1519"},
    [0x30] = {0x175a, COK_CAST_NONE, "dqk 7fd8:175a"},
    [0x31] = {0x06e9, COK_CAST_NONE, "dqk 7fd8:06e9"},
    [0x32] = {0x091b, COK_CAST_NONE, "dqk 7fd8:091b"},
    [0x33] = {0x1ac3, COK_CAST_NONE, "dqk 7fd8:1ac3"},
    [0x34] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x35] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x36] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x37] = {0x1b11, COK_CAST_NONE, "dqk 7fd8:1b11"},
    [0x38] = {0x1b5c, COK_CAST_NONE, "dqk 7fd8:1b5c"},
    [0x39] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x3a] = {0x1c2e, COK_CAST_NONE, "dqk 7fd8:1c2e"},
    [0x3b] = {0x075a, COK_CAST_NONE, "dqk 7fd8:075a"},
    [0x3c] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x3d] = {0x13ef, COK_CAST_NONE, "dqk 7fd8:13ef"},
    [0x3e] = {0x00a2, COK_CAST_NONE, "dqk 7fd8:00a2"},
    [0x3f] = {0x022a, COK_CAST_NONE, "dqk 7fd8:022a"},
    [0x40] = {0x01e6, COK_CAST_NONE, "dqk 7fd8:01e6"},
    [0x41] = {0, COK_CAST_NONE, "DS:af45 + 0x104 is 0:0, never stored (7fd8:46bb, 7fd8:4892)"},
    [0x42] = {0x1e31, COK_CAST_NONE, "dqk 7fd8:1e31"},
    [0x43] = {0x1e5d, COK_CAST_NONE, "dqk 7fd8:1e5d"},
    [0x44] = {0x1f41, COK_CAST_NONE, "dqk 7fd8:1f41"},
    [0x45] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x46] = {0x1fcb, COK_CAST_NONE, "dqk 7fd8:1fcb"},
    [0x47] = {0x2073, COK_CAST_NONE, "dqk 7fd8:2073"},
    [0x48] = {0x20df, COK_CAST_NONE, "dqk 7fd8:20df"},
    [0x49] = {0x210b, COK_CAST_NONE, "dqk 7fd8:210b"},
    [0x4a] = {0x2157, COK_CAST_NONE, "dqk 7fd8:2157"},
    [0x4b] = {0x219f, COK_CAST_NONE, "dqk 7fd8:219f"},
    [0x4c] = {0x223e, COK_CAST_NONE, "dqk 7fd8:223e"},
    [0x4d] = {0x018c, COK_CAST_NONE, "dqk 7fd8:018c"},
    [0x4e] = {0x22f5, COK_CAST_NONE, "dqk 7fd8:22f5"},
    [0x4f] = {0x2396, COK_CAST_NONE, "dqk 7fd8:2396"},
    [0x50] = {0x23a7, COK_CAST_NONE, "dqk 7fd8:23a7"},
    [0x51] = {0x23c5, COK_CAST_NONE, "dqk 7fd8:23c5"},
    [0x52] = {0x243f, COK_CAST_NONE, "dqk 7fd8:243f"},
    [0x53] = {0x24f0, COK_CAST_NONE, "dqk 7fd8:24f0"},
    [0x54] = {0x257a, COK_CAST_NONE, "dqk 7fd8:257a"},
    [0x55] = {0x26b4, COK_CAST_NONE, "dqk 7fd8:26b4"},
    [0x56] = {0x27ef, COK_CAST_NONE, "dqk 7fd8:27ef"},
    [0x57] = {0x28fb, COK_CAST_NONE, "dqk 7fd8:28fb"},
    [0x58] = {0x2960, COK_CAST_NONE, "dqk 7fd8:2960"},
    [0x59] = {0x13ef, COK_CAST_NONE, "dqk 7fd8:13ef"},
    [0x5a] = {0x297e, COK_CAST_NONE, "dqk 7fd8:297e"},
    [0x5b] = {0x09c4, COK_CAST_NONE, "dqk 7fd8:09c4"},
    [0x5c] = {0x299c, COK_CAST_NONE, "dqk 7fd8:299c"},
    [0x5d] = {0x2a48, COK_CAST_NONE, "dqk 7fd8:2a48"},
    [0x5e] = {0x06e9, COK_CAST_NONE, "dqk 7fd8:06e9"},
    [0x5f] = {0, COK_CAST_NONE, "DS:af45 + 0x17c is 0:0, never stored (7fd8:46bb, 7fd8:4892)"},
    [0x60] = {0x2b3a, COK_CAST_NONE, "dqk 7fd8:2b3a"},
    [0x61] = {0, COK_CAST_NONE, "DS:af45 + 0x184 is 0:0, never stored (7fd8:46bb, 7fd8:4892)"},
    [0x62] = {0x2c32, COK_CAST_NONE, "dqk 7fd8:2c32"},
    [0x63] = {0, COK_CAST_NONE, "DS:af45 + 0x18c is 0:0, never stored (7fd8:46bb, 7fd8:4892)"},
    [0x64] = {0x2cae, COK_CAST_NONE, "dqk 7fd8:2cae"},
    [0x65] = {0x09c4, COK_CAST_NONE, "dqk 7fd8:09c4"},
    [0x66] = {0x2ccc, COK_CAST_NONE, "dqk 7fd8:2ccc"},
    [0x67] = {0x2ebc, COK_CAST_NONE, "dqk 7fd8:2ebc"},
    [0x68] = {0x2eee, COK_CAST_NONE, "dqk 7fd8:2eee"},
    [0x69] = {0x2f60, COK_CAST_NONE, "dqk 7fd8:2f60"},
    [0x6a] = {0x1044, COK_CAST_NONE, "dqk 7fd8:1044"},
    [0x6b] = {0x1e5d, COK_CAST_NONE, "dqk 7fd8:1e5d"},
    [0x6c] = {0x2fd4, COK_CAST_NONE, "dqk 7fd8:2fd4"},
    [0x6d] = {0x301a, COK_CAST_NONE, "dqk 7fd8:301a"},
    [0x6e] = {0x30ba, COK_CAST_NONE, "dqk 7fd8:30ba"},
    [0x6f] = {0x31cc, COK_CAST_NONE, "dqk 7fd8:31cc"},
    [0x70] = {0x3279, COK_CAST_NONE, "dqk 7fd8:3279"},
    [0x71] = {0x3297, COK_CAST_NONE, "dqk 7fd8:3297"},
    [0x72] = {0x344d, COK_CAST_NONE, "dqk 7fd8:344d"},
    [0x73] = {0x1519, COK_CAST_NONE, "dqk 7fd8:1519"},
    [0x74] = {0x34b9, COK_CAST_NONE, "dqk 7fd8:34b9"},
    [0x75] = {0x3569, COK_CAST_NONE, "dqk 7fd8:3569"},
    [0x76] = {0x36d6, COK_CAST_NONE, "dqk 7fd8:36d6"},
    [0x77] = {0x370d, COK_CAST_NONE, "dqk 7fd8:370d"},
    [0x78] = {0x372b, COK_CAST_NONE, "dqk 7fd8:372b"},
    [0x79] = {0x388c, COK_CAST_NONE, "dqk 7fd8:388c"},
    [0x7a] = {0x01aa, COK_CAST_NONE, "dqk 7fd8:01aa"},
    [0x7b] = {0x396a, COK_CAST_NONE, "dqk 7fd8:396a"},
    [0x7c] = {0x3b99, COK_CAST_NONE, "dqk 7fd8:3b99"},
    [0x7d] = {0x3ea6, COK_CAST_NONE, "dqk 7fd8:3ea6"},
    [0x7e] = {0x3fa4, COK_CAST_NONE, "dqk 7fd8:3fa4"},
    [0x7f] = {0x1bf9, COK_CAST_NONE, "dqk 7fd8:1bf9"},
    [0x80] = {0x1c9a, COK_CAST_NONE, "dqk 7fd8:1c9a"},
    [0x81] = {0x1d20, COK_CAST_NONE, "dqk 7fd8:1d20"},
    [0x82] = {0x1d39, COK_CAST_NONE, "dqk 7fd8:1d39"},
    [0x83] = {0x1d57, COK_CAST_NONE, "dqk 7fd8:1d57"},
    [0x84] = {0x1d97, COK_CAST_NONE, "dqk 7fd8:1d97"},
    [0x85] = {0x1519, COK_CAST_NONE, "dqk 7fd8:1519"},
    [0x86] = {0x1e05, COK_CAST_NONE, "dqk 7fd8:1e05"},
    [0x87] = {0x2b1c, COK_CAST_NONE, "dqk 7fd8:2b1c"},
    [0x88] = {0x2c14, COK_CAST_NONE, "dqk 7fd8:2c14"},
    [0x89] = {0x2c6e, COK_CAST_NONE, "dqk 7fd8:2c6e"},
    [0x8a] = {0x41e7, COK_CAST_NONE, "dqk 7fd8:41e7"},
    [0x8b] = {0x4250, COK_CAST_NONE, "dqk 7fd8:4250"},
    [0x8c] = {0x4369, COK_CAST_NONE, "dqk 7fd8:4369"},
};

/* The spell ids the code names (spells.h). A name the game has once is the
 * id of that name (Entangle is 0x4e here, not Champions' 0x4f); a name the
 * game has more than once (Bless, Remove Curse) takes Champions' number, where
 * the game has that name there. 0: none of the game's names is that spell
 * (Animate Dead, Paralyze, Giant Strength). A role the game gives its own id:
 * an item's Fireball is the Necklace of Missiles (0x85, tested at 7fd8:1545),
 * a deity's granted Bless is kind 3's Bless (0x3e, the granted powers' row
 * at DS:27e9; Bless 0x01 is the spell). */
const uint8_t cok_dark_queen_spell_ids[COK_SPELL_NAMES] = {
    [COK_SPELL_BLESS] = 0x01,
    [COK_SPELL_CURE_LIGHT_WOUNDS] = 0x03,
    [COK_SPELL_MAGIC_MISSILE] = 0x0f,
    [COK_SPELL_HOLD_PERSON] = 0x17,
    [COK_SPELL_KNOCK] = 0x1f,
    [COK_SPELL_REMOVE_CURSE] = 0x2b,
    [COK_SPELL_FIREBALL] = 0x2f,
    [COK_SPELL_HOLD_PERSON_MAGE] = 0x31,
    [COK_SPELL_LIGHTNING_BOLT] = 0x33,
    [COK_SPELL_CURE_SERIOUS_WOUNDS] = 0x3a,
    [COK_SPELL_FIREBALL_ITEM] = 0x85,
    [COK_SPELL_CURE_CRITICAL_WOUNDS] = 0x47,
    [COK_SPELL_ENTANGLE] = 0x4e,
    [COK_SPELL_CHARM_MONSTERS] = 0x51,
    [COK_SPELL_CONFUSION] = 0x52,
    [COK_SPELL_DIMENSION_DOOR] = 0x53,
    [COK_SPELL_FEAR] = 0x54,
    [COK_SPELL_FIRE_SHIELD] = 0x55,
    [COK_SPELL_FUMBLE] = 0x56,
    [COK_SPELL_BLESS_POWER] = 0x3e,
};

/* The effects' handlers by id, DS:bee8 + 4 * id (cok_rules_effect). */
const cok_rules_effect cok_dark_queen_effect_handlers[0xe6] = {
    [0x00] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x01] = {0x0132, COK_FX_BLESS, .applied_only = true}, /* 78e4:0132: morale and attack roll */
    [0x02] = {0x0146, COK_FX_CURSE, .applied_only = true}, /* 78e4:0146: morale and attack roll */
    [0x03] = {0x0009, COK_FX_SNAKES}, /* 7e76:0009: the snakes' count (effect.c's h_snakes, flag 0; flag 1 stops, cited there) */
    [0x04] = {0x0168, COK_FX_EVIL_AVOID, .applied_only = true}, /* 78e4:0168: attacker of flag 1, -7 */
    [0x05] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x06] = {0x0184, COK_FX_QUEEN_TYPE_CANCEL, .value = 4, .applied_only = true}, /* 78e4:0184: 00f7(4) */
    [0x07] = {0x0075, COK_FX_NOTHING}, /* does nothing: retf */
    [0x08] = {0x0198, COK_FX_PROTECTION_EVIL, .applied_only = true}, /* 78e4:0198: alignment 2, 5, 8 */
    [0x09] = {0x01c6, COK_FX_PROTECTION_GOOD, .applied_only = true}, /* 78e4:01c6: alignment 0, 3, 6 */
    [0x0a] = {0x01f4, COK_FX_QUEEN_RESIST, .value = 2, .applied_only = true}, /* 78e4:01f4: cold halved, save +3 */
    [0x0b] = {0x007a, COK_FX_QUEEN_CHARM}, /* 7e76:007a: charm in battle, its side and NPC */
    [0x0c] = {0x0218, COK_FX_NOTHING}, /* does nothing: retf */
    [0x0d] = {0x021d, COK_FX_QUEEN_BARKSKIN, .applied_only = true}, /* 78e4:021d: save +1 unless magic, attack -1 */
    [0x0e] = {0x0238, COK_FX_NOTHING}, /* does nothing: retf */
    [0x0f] = {0x023d, COK_FX_QUEEN_POISON}, /* 78e4:023d: poison, its 0x1a (sic) for ten minutes, the damage */
    [0x10] = {0x02b4, COK_FX_NOTHING}, /* does nothing: retf */
    [0x11] = {0x02b9, COK_FX_QUEEN_SHIELD, .applied_only = true}, /* 78e4:02b9: AC 0x39, save +1, MM cancelled */
    [0x12] = {0x02e6, COK_FX_RESIST_CHARM_30, .applied_only = true}, /* 78e4:02e6: d100 30 or less */
    [0x13] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x14] = {0x0313, COK_FX_QUEEN_RESIST, .value = 1, .applied_only = true}, /* 78e4:0313: fire halved, save +3 */
    [0x15] = {0x012c, COK_FX_QUEEN_SILENCE, .applied_only = true}, /* 7e76:012c: silence in battle */
    [0x16] = {0x0337, COK_FX_QUEEN_SLOW_POISON, .applied_only = true}, /* 78e4:0337: a poisoned holder dies, its 0x0f cured (queen_effect.c) */
    [0x17] = {0x0397, COK_FX_QUEEN_HAMMER}, /* 78e4:0397: the spiritual hammer (queen_effect.c); event 0x13 */
    [0x18] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x19] = {0x054a, COK_FX_INVISIBLE}, /* 78e4:054a: tests the removing flag itself */
    [0x1a] = {0x01c7, COK_FX_TARGET_BONUS, .battle = true, .applied_only = true}, /* 7e76:01c7 */
    [0x1b] = {0x010d, COK_FX_HELD}, /* 78e4:010d: ends the holder's turn in battle */
    [0x1c] = {0x0580, COK_FX_QUEEN_IMAGES, .applied_only = true}, /* 78e4:0580: an image lost, the value counts down */
    [0x1d] = {0x0654, COK_FX_QUEEN_QUARTER_LESS, .applied_only = true}, /* 78e4:0654: a quarter less */
    [0x1e] = {0x01fb, COK_FX_MISSING, "dqk 7e76:01fb"},
    [0x1f] = {0x010d, COK_FX_HELD}, /* 78e4:010d */
    [0x20] = {0x028c, COK_FX_MISSING, "dqk 7e76:028c"},
    [0x21] = {0x0672, COK_FX_BLIND, .applied_only = true}, /* 78e4:0672: attack, save and AC */
    [0x22] = {0x0696, COK_FX_QUEEN_DISEASE, .applied_only = true}, /* 78e4:0696: cause disease, runs 0x2b and 0x2c */
    [0x23] = {0x033f, COK_FX_QUEEN_CONFUSED, .applied_only = true}, /* 7e76:033f: confusion in battle */
    [0x24] = {0x06d3, COK_FX_MINUS_FOUR, .applied_only = true}, /* 78e4:06d3: attack and save */
    [0x25] = {0x058c, COK_FX_MISSING, "dqk 7e76:058c"},
    [0x26] = {0x0218, COK_FX_NOTHING}, /* does nothing: retf */
    [0x27] = {0x06e8, COK_FX_QUEEN_HASTE, .applied_only = true}, /* 78e4:06e8: the movement and rate doubled, aging */
    [0x28] = {0x073b, COK_FX_QUEEN_GLOBE, .applied_only = true}, /* 78e4:073b: spells of level below 8 cancelled */
    [0x29] = {0x0832, COK_FX_QUEEN_MISSILES, .applied_only = true}, /* 78e4:0832: "Avoids it" for a weapon of no bonus */
    [0x2a] = {0x0877, COK_FX_QUEEN_SLOW, .applied_only = true}, /* 78e4:0877: the movement and rate halved */
    [0x2b] = {0x08ba, COK_FX_QUEEN_DRAIN}, /* 78e4:08ba: strength drain, "is weakened", or 0x1f */
    [0x2c] = {0x0920, COK_FX_QUEEN_WASTING}, /* 78e4:0920: wasting, the damage, or 0x1f */
    [0x2d] = {0x0198, COK_FX_PROTECTION_EVIL, .applied_only = true}, /* 78e4:0198 */
    [0x2e] = {0x01c6, COK_FX_PROTECTION_GOOD, .applied_only = true}, /* 78e4:01c6 */
    [0x2f] = {0x09b2, COK_FX_AGAINST_FLAG4, .applied_only = true}, /* 78e4:09b2: flag 4 -4 */
    [0x30] = {0x09ce, COK_FX_NOTHING}, /* does nothing: retf */
    [0x31] = {0x09d3, COK_FX_QUEEN_PRAYER, .applied_only = true}, /* 78e4:09d3: attack and save by the side */
    [0x32] = {0x0a22, COK_FX_QUEEN_RESISTANT, .value = 2, .applied_only = true}, /* 78e4:0a22: cold saved, fire doubled */
    [0x33] = {0x010d, COK_FX_HELD}, /* 78e4:010d */
    [0x34] = {0x010d, COK_FX_HELD}, /* 78e4:010d */
    [0x35] = {0x010d, COK_FX_HELD}, /* 78e4:010d */
    [0x36] = {0x0a7e, COK_FX_QUEEN_RESISTANT, .value = 1, .applied_only = true}, /* 78e4:0a7e: fire saved, cold doubled */
    [0x37] = {0x0aca, COK_FX_NOTHING}, /* does nothing: retf */
    [0x38] = {0x0acf, COK_FX_QUEEN_ADD_INVISIBLE}, /* 78e4:0acf: 0x19 added, or removed in battle */
    [0x39] = {0x0b11, COK_FX_QUEEN_GLOBE_MAGIC, .applied_only = true}, /* 78e4:0b11: level 4 or below, magic, cancelled */
    [0x3a] = {0x0b50, COK_FX_MISSING, "dqk 78e4:0b50"},
    [0x3b] = {0x0d30, COK_FX_NOTHING}, /* does nothing: retf */
    [0x3c] = {0x0d8c, COK_FX_MISSING, "dqk 78e4:0d8c"},
    [0x3d] = {0x0d99, COK_FX_MISSING, "dqk 78e4:0d99"},
    [0x3e] = {0x0df6, COK_FX_MISSING, "dqk 78e4:0df6"},
    [0x3f] = {0x0e3e, COK_FX_MISSING, "dqk 78e4:0e3e"},
    [0x40] = {0x0e7d, COK_FX_MISSING, "dqk 78e4:0e7d"},
    [0x41] = {0x069f, COK_FX_MISSING, "dqk 7e76:069f"},
    [0x42] = {0x0e98, COK_FX_MISSING, "dqk 78e4:0e98"},
    [0x43] = {0x0eeb, COK_FX_MISSING, "dqk 78e4:0eeb"},
    [0x44] = {0x06f6, COK_FX_QUEEN_FEEBLEMIND, .applied_only = true}, /* 7e76:06f6: intelligence and wisdom 3, the spells forgotten */
    [0x45] = {0x0f33, COK_FX_MISSING, "dqk 78e4:0f33"},
    [0x46] = {0x0f6b, COK_FX_MISSING, "dqk 78e4:0f6b"},
    [0x47] = {0x10a6, COK_FX_ARMOUR, .value = 0xfe, .applied_only = true}, /* 78e4:10a6: AC and AC behind 2 worse */
    [0x48] = {0x10c0, COK_FX_NOTHING}, /* does nothing: retf */
    [0x49] = {0x10c5, COK_FX_MISSING, "dqk 78e4:10c5"},
    [0x4a] = {0x10e0, COK_FX_MISSING, "dqk 78e4:10e0"},
    [0x4b] = {0x07c8, COK_FX_MISSING, "dqk 7e76:07c8"},
    [0x4c] = {0x10f3, COK_FX_MISSING, "dqk 78e4:10f3"},
    [0x4d] = {0x081f, COK_FX_QUEEN_BERSERK}, /* 7e76:081f: the berserk turn at the listed[2] combatant */
    [0x4e] = {0x1106, COK_FX_MISSING, "dqk 78e4:1106"},
    [0x4f] = {0x1115, COK_FX_MISSING, "dqk 78e4:1115"},
    [0x50] = {0x11d1, COK_FX_MISSING, "dqk 78e4:11d1"},
    [0x51] = {0x1214, COK_FX_MISSING, "dqk 78e4:1214"},
    [0x52] = {0x1257, COK_FX_MISSING, "dqk 78e4:1257"},
    [0x53] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0x54] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0x55] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0x56] = {0x129a, COK_FX_MISSING, "dqk 78e4:129a"},
    [0x57] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x58] = {0x12b5, COK_FX_MISSING, "dqk 78e4:12b5"},
    [0x59] = {0x12f3, COK_FX_MISSING, "dqk 78e4:12f3"},
    [0x5a] = {0x0ebf, COK_FX_MISSING, "dqk 7e76:0ebf"},
    [0x5b] = {0x10ab, COK_FX_MISSING, "dqk 7e76:10ab"},
    /* The racial effects 0x5c, 0x5e and 0x5f (cancel, constitution save, resist charm 90;
     * Death Knights' 504d:2162, 21bd and 223d, with the same constants). DQK's three test the
     * removing flag first (applied_only). The racial nodes are made with the flag 0 (queen_create.c
     * 148f:19c2), so they never run removing. The cancel zeroes the damage word and its byte, as
     * 78e4:00a1 does. */
    [0x5c] = {0x1334, COK_FX_CANCEL, NULL, 0x6f, .applied_only = true}, /* 78e4:1334 cancels 0x6f (00a1) */
    [0x5d] = {0x1347, COK_FX_MISSING, "dqk 78e4:1347"},
    [0x5e] = {0x1355, COK_FX_CONSTITUTION_SAVE, .applied_only = true}, /* 78e4:1355: saves of types 0, 2, 4 */
    [0x5f] = {0x13df, COK_FX_RESIST_CHARM_90, .applied_only = true}, /* 78e4:13df: a d100 of 90 or less, 0x0b, 0x35 */
    [0x60] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0x61] = {0x140c, COK_FX_MISSING, "dqk 78e4:140c"},
    [0x62] = {0x148c, COK_FX_MISSING, "dqk 78e4:148c"},
    [0x63] = {0x14a6, COK_FX_MISSING, "dqk 78e4:14a6"},
    [0x64] = {0x14d1, COK_FX_PRESENCE, NULL, 0x78, "is terrified"}, /* DS:83bc */
    [0x65] = {0x093b, COK_FX_MISSING, "dqk 7e76:093b"},
    [0x66] = {0x0960, COK_FX_MISSING, "dqk 7e76:0960"},
    [0x67] = {0x1a95, COK_FX_MISSING, "dqk 78e4:1a95"},
    [0x68] = {0x0985, COK_FX_MISSING, "dqk 7e76:0985"},
    [0x69] = {0x1aa2, COK_FX_QUEEN_RANGER},
    [0x6a] = {0x1af9, COK_FX_HELD}, /* 78e4:1af9: 010d byte for byte, the turn ends */
    [0x6b] = {0x09e8, COK_FX_MISSING, "dqk 7e76:09e8"},
    [0x6c] = {0x0ad9, COK_FX_MISSING, "dqk 7e76:0ad9"},
    [0x6d] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x6e] = {0x1b0b, COK_FX_MISSING, "dqk 78e4:1b0b"},
    [0x6f] = {0x0b3c, COK_FX_QUEEN_TERROR}, /* 7e76:0b3c: terror in battle, its block cleared on removal */
    [0x70] = {0x1b39, COK_FX_MISSING, "dqk 78e4:1b39"},
    [0x71] = {0x1bd5, COK_FX_MISSING, "dqk 78e4:1bd5"},
    [0x72] = {0x1c12, COK_FX_MISSING, "dqk 78e4:1c12"},
    [0x73] = {0x0b74, COK_FX_MISSING, "dqk 7e76:0b74"},
    [0x74] = {0x1df6, COK_FX_MISSING, "dqk 78e4:1df6"},
    [0x75] = {0x10a3, COK_FX_MISSING, "dqk 52db:4fd1"},
    [0x76] = {0x2025, COK_FX_ARMOUR, .value = 0xfc}, /* 78e4:2025: AC and AC behind 4 worse, no flag test (not applied_only) */
    [0x77] = {0x2039, COK_FX_QUEEN_FIRE_ABSORB}, /* 78e4:2039: fire absorbed at 12 a point of value (queen_effect.c) */
    [0x78] = {0x20ab, COK_FX_MISSING, "dqk 78e4:20ab"},
    [0x79] = {0x0ad9, COK_FX_MISSING, "dqk 7e76:0ad9"},
    [0x7a] = {0x20c9, COK_FX_MISSING, "dqk 78e4:20c9"},
    [0x7b] = {0x20dd, COK_FX_MISSING, "dqk 78e4:20dd"},
    [0x7c] = {0x11d6, COK_FX_MISSING, "dqk 7e76:11d6"},
    [0x7d] = {0x210a, COK_FX_MISSING, "dqk 78e4:210a"},
    [0x7e] = {0x2117, COK_FX_MISSING, "dqk 78e4:2117"},
    [0x7f] = {0x4f34, COK_FX_MISSING, "dqk 78e4:4f34"},
    [0x80] = {0}, /* no store fills it */
    [0x81] = {0x0bc0, COK_FX_MISSING, "dqk 7e76:0bc0"},
    [0x82] = {0x2128, COK_FX_MISSING, "dqk 78e4:2128"},
    [0x83] = {0x0c28, COK_FX_MISSING, "dqk 7e76:0c28"},
    [0x84] = {0x213a, COK_FX_MISSING, "dqk 78e4:213a"},
    [0x85] = {0x2152, COK_FX_MISSING, "dqk 78e4:2152"},
    [0x86] = {0x2214, COK_FX_MISSING, "dqk 78e4:2214"},
    [0x87] = {0x2228, COK_FX_MISSING, "dqk 78e4:2228"},
    [0x88] = {0x227f, COK_FX_MISSING, "dqk 78e4:227f"},
    [0x89] = {0x2391, COK_FX_MISSING, "dqk 78e4:2391"},
    [0x8a] = {0x24a1, COK_FX_MISSING, "dqk 78e4:24a1"},
    [0x8b] = {0x24ae, COK_FX_MISSING, "dqk 78e4:24ae"},
    [0x8c] = {0x24c3, COK_FX_NOTHING}, /* does nothing: retf */
    [0x8d] = {0x24c8, COK_FX_MISSING, "dqk 78e4:24c8"},
    [0x8e] = {0x24dd, COK_FX_MISSING, "dqk 78e4:24dd"},
    [0x8f] = {0x24ea, COK_FX_MISSING, "dqk 78e4:24ea"},
    [0x90] = {0}, /* no store fills it */
    [0x91] = {0x0d07, COK_FX_MISSING, "dqk 7e76:0d07"},
    [0x92] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x93] = {0x0d36, COK_FX_MISSING, "dqk 7e76:0d36"},
    [0x94] = {0x250b, COK_FX_MISSING, "dqk 78e4:250b"},
    [0x95] = {0x2515, COK_FX_MISSING, "dqk 78e4:2515"},
    [0x96] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0x97] = {0x259d, COK_FX_MISSING, "dqk 78e4:259d"},
    [0x98] = {0x287d, COK_FX_MISSING, "dqk 78e4:287d"},
    [0x99] = {0x2949, COK_FX_MISSING, "dqk 78e4:2949"},
    [0x9a] = {0x2b1e, COK_FX_MISSING, "dqk 78e4:2b1e"},
    [0x9b] = {0x213a, COK_FX_MISSING, "dqk 78e4:213a"},
    [0x9c] = {0x2d38, COK_FX_MISSING, "dqk 78e4:2d38"},
    [0x9d] = {0}, /* no store fills it */
    [0x9e] = {0x2e53, COK_FX_MISSING, "dqk 78e4:2e53"},
    [0x9f] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xa0] = {0x2e60, COK_FX_MISSING, "dqk 78e4:2e60"},
    [0xa1] = {0x2f61, COK_FX_MISSING, "dqk 78e4:2f61"},
    [0xa2] = {0x30a9, COK_FX_MISSING, "dqk 78e4:30a9"},
    [0xa3] = {0x30c2, COK_FX_MISSING, "dqk 78e4:30c2"},
    [0xa4] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xa5] = {0x316c, COK_FX_MISSING, "dqk 78e4:316c"},
    [0xa6] = {0x317f, COK_FX_MISSING, "dqk 78e4:317f"},
    [0xa7] = {0x3441, COK_FX_MISSING, "dqk 78e4:3441"},
    [0xa8] = {0x35ca, COK_FX_MISSING, "dqk 78e4:35ca"},
    [0xa9] = {0x35e8, COK_FX_MISSING, "dqk 78e4:35e8"},
    [0xaa] = {0x3620, COK_FX_MISSING, "dqk 78e4:3620"},
    [0xab] = {0x36db, COK_FX_MISSING, "dqk 78e4:36db"},
    [0xac] = {0x37aa, COK_FX_MISSING, "dqk 78e4:37aa"},
    [0xad] = {0x3842, COK_FX_MISSING, "dqk 78e4:3842"},
    [0xae] = {0x386c, COK_FX_MISSING, "dqk 78e4:386c"},
    [0xaf] = {0x38c4, COK_FX_MISSING, "dqk 78e4:38c4"},
    [0xb0] = {0x38d5, COK_FX_MISSING, "dqk 78e4:38d5"},
    [0xb1] = {0x3a1f, COK_FX_MISSING, "dqk 78e4:3a1f"},
    [0xb2] = {0x0dc6, COK_FX_MISSING, "dqk 7e76:0dc6"},
    [0xb3] = {0x3a4d, COK_FX_MISSING, "dqk 78e4:3a4d"},
    [0xb4] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0xb5] = {0x3baa, COK_FX_MISSING, "dqk 78e4:3baa"},
    [0xb6] = {0x3bda, COK_FX_MISSING, "dqk 78e4:3bda"},
    [0xb7] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xb8] = {0x3d76, COK_FX_MISSING, "dqk 78e4:3d76"},
    [0xb9] = {0x3e15, COK_FX_MISSING, "dqk 78e4:3e15"},
    [0xba] = {0x3e36, COK_FX_MISSING, "dqk 78e4:3e36"},
    [0xbb] = {0x3f58, COK_FX_MISSING, "dqk 78e4:3f58"},
    [0xbc] = {0x4147, COK_FX_MISSING, "dqk 78e4:4147"},
    [0xbd] = {0x41a7, COK_FX_MISSING, "dqk 78e4:41a7"},
    [0xbe] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xbf] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xc0] = {0x4207, COK_FX_MISSING, "dqk 78e4:4207"},
    [0xc1] = {0x4223, COK_FX_MISSING, "dqk 78e4:4223"},
    [0xc2] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xc3] = {0x4949, COK_FX_KIN, NULL, 0xcb},
    [0xc4] = {0x445c, COK_FX_MISSING, "dqk 78e4:445c"},
    [0xc5] = {0x4596, COK_FX_MISSING, "dqk 78e4:4596"},
    [0xc6] = {0x45e2, COK_FX_MISSING, "dqk 78e4:45e2"},
    [0xc7] = {0x4830, COK_FX_MISSING, "dqk 78e4:4830"},
    [0xc8] = {0x0ded, COK_FX_MISSING, "dqk 7e76:0ded"},
    [0xc9] = {0x4873, COK_FX_MISSING, "dqk 78e4:4873"},
    [0xca] = {0x12a1, COK_FX_MISSING, "dqk 7e76:12a1"},
    [0xcb] = {0x425b, COK_FX_MISSING, "dqk 78e4:425b"},
    [0xcc] = {0x0ebf, COK_FX_MISSING, "dqk 7e76:0ebf"},
    [0xcd] = {0x49c2, COK_FX_MISSING, "dqk 78e4:49c2"},
    [0xce] = {0x49e4, COK_FX_MISSING, "dqk 78e4:49e4"},
    [0xcf] = {0x4a68, COK_FX_MISSING, "dqk 78e4:4a68"},
    [0xd0] = {0x4a8d, COK_FX_MISSING, "dqk 78e4:4a8d"},
    [0xd1] = {0x4bec, COK_FX_MISSING, "dqk 78e4:4bec"},
    [0xd2] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xd3] = {0x4def, COK_FX_MISSING, "dqk 78e4:4def"},
    [0xd4] = {0x4e63, COK_FX_MISSING, "dqk 78e4:4e63"},
    [0xd5] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xd6] = {0x0e24, COK_FX_MISSING, "dqk 7e76:0e24"},
    [0xd7] = {0x4ee7, COK_FX_MISSING, "dqk 78e4:4ee7"},
    [0xd8] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xd9] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xda] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xdb] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xdc] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xdd] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xde] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xdf] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe0] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe1] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe2] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe3] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe4] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
    [0xe5] = {0x4f2f, COK_FX_NOTHING}, /* does nothing: retf */
};

/* The effect ids the code names (spells.h). Each is the id the original's routine tests:
 * CHARMED and BERSERK by the battle's end (5045:0001 and 1b66's test), INVISIBLE by the attack,
 * YELLED_AT by a yell (148f:0005's add; View's Lay tests it too, 86b8:314e: that routine stops),
 * and ASLEEP by the resist-charm 90 (78e4:13df).
 * Every other name is 0 (the game has none under it yet, read as false by cok_effect_is and
 * cok_character_find_named; cok_effect_id asserts), so a routine that finds one of those names
 * must be ported with its id first (dqk-effects-a..e). */
const uint8_t cok_dark_queen_effect_ids[COK_EFFECT_NAMES] = {
    [COK_EFFECT_CHARMED] = 0x0b,   /* 5045:0001's charm test */
    [COK_EFFECT_SEES_INVISIBLE] = 0x18, /* 78e4:054a's test: the selected character without it cannot see an invisible one */
    [COK_EFFECT_INVISIBLE] = 0x19, /* 148f:1b2a removes every 0x19; the attack's invisible */
    [COK_EFFECT_BERSERK] = 0x4d,   /* 1b66's test (75cc, the battle's end) */
    [COK_EFFECT_ENLARGED] = 0x0c,  /* 017d's 1dfc(rec, 0) for Enlarge's */
    [COK_EFFECT_FRIENDLY] = 0x0e,  /* 017d's 1dfc(rec, 5) for Friends' */
    [COK_EFFECT_STRENGTH] = 0x26,  /* 017d's 1dfc(rec, 0) for Strength's */
    /* 148f:0005's 252c(m, 0x8c, ...). attack.c:854 and round.c:726 read it for the computer's
     * targets (part 15) and cast.c's picks read it through round.c:697 (part 28): unchecked here. */
    [COK_EFFECT_YELLED_AT] = 0x8c,
    /* 52db:1b26: the spiritual hammer's 0x17 (a thrown weapon with it is not put in the pool). */
    [COK_EFFECT_HAMMER] = 0x17,
    /* 78e4:13df's cancel (0x5f): the sleep, as in Champions (rules.c). */
    [COK_EFFECT_ASLEEP] = 0x35,
    /* 7e76:0009, Sticks to Snakes' effect (the row 0x03 above; h_snakes finds it by this id). */
    [COK_EFFECT_SNAKES] = 0x03,
};

/* The event lists (148f:0512, a0512.txt): by event, the ids its handlers run, 0-ended, in
 * order; events 0x16 and 0x17 have none. Event 2 goes on with event 3's tail from 8b and event
 * 4 with event 0x0b's last id (d6), as the original's cases do. The lists are data: the
 * dispatch (effect.c) runs 02ed before event 6 and 9's list and 2a6d after event 6's. */
static const uint8_t dqk_ev1[] = {0x19, 0x45, 0x25, 0};
static const uint8_t dqk_ev2[] = {0x56, 0x65, 0x66, 0x79, 0x41, 0x73, 0x20, 0x58, 0xaa, 0xb0,
                                  0x84, 0x81, 0x8b, 0xb8, 0xce, 0xd0, 0xd3, 0xd4, 0xc7, 0};
static const uint8_t dqk_ev3[] = {0x40, 0x56, 0x65, 0x66, 0x79, 0x41, 0x20, 0x68, 0x6c, 0xaa,
                                  0xb0, 0x84, 0x8b, 0xb8, 0xce, 0xd0, 0xd3, 0xd4, 0xc7, 0};
static const uint8_t dqk_ev4[] = {0x1d, 0x69, 0x78, 0x4b, 0x74, 0x95, 0x83, 0x93, 0xba, 0xc8,
                                  0xc9, 0xd6, 0};
static const uint8_t dqk_ev5[] = {0x1c, 0x29, 0x3c, 0x42, 0x43, 0x50, 0x51, 0x70, 0x52, 0x67,
                                  0x7d, 0xa8, 0xa9, 0x9f, 0xa5, 0xae, 0xaf, 0xbb, 0xc1, 0xc5, 0};
static const uint8_t dqk_ev6[] = {0x0a, 0x11, 0x14, 0x1c, 0x06, 0x28, 0x32, 0x36, 0x3d, 0x49,
                                  0x4f, 0x5d, 0x62, 0x3f, 0x39, 0xa8, 0xa5, 0xad, 0xae, 0x86,
                                  0x87, 0xc0, 0xcd, 0x77, 0};
static const uint8_t dqk_ev7[] = {0x03, 0x1b, 0x1f, 0x34, 0x35, 0x6a, 0x33, 0x82, 0};
static const uint8_t dqk_ev8[] = {0x38, 0x59, 0x64, 0xab, 0xc3, 0};
static const uint8_t dqk_ev9[] = {0x12, 0x1c, 0x28, 0x39, 0x3f, 0x4c, 0x4f, 0x5c, 0x5f, 0x7b,
                                  0x9e, 0xa5, 0x5d, 0x8d, 0x8f, 0x8a, 0x8e, 0};
static const uint8_t dqk_ev10[] = {0x01, 0x02, 0x1a, 0x21, 0x24, 0x31, 0x78, 0xa2, 0x83, 0x94, 0};
static const uint8_t dqk_ev11[] = {0x04, 0x08, 0x09, 0x0d, 0x11, 0x1e, 0x21, 0x2d, 0x2e, 0x76,
                                   0x47, 0x7a, 0xd6, 0};
static const uint8_t dqk_ev12[] = {0x08, 0x09, 0x0a, 0x0d, 0x11, 0x14, 0x21, 0x24, 0x2d, 0x2e,
                                   0x31, 0x32, 0x36, 0x3d, 0x5e, 0x61, 0x7e, 0x8f, 0xcd, 0};
static const uint8_t dqk_ev13[] = {0x9a, 0x99, 0x98, 0xa0, 0x9c, 0xb6, 0xbc, 0xbd, 0xb3, 0};
static const uint8_t dqk_ev14[] = {0x46, 0x53, 0x54, 0x55, 0x5a, 0x5b, 0x60, 0x3a, 0x75, 0x72,
                                   0x98, 0xb3, 0xb4, 0xcb, 0xc6, 0xca, 0x58, 0xcc, 0xd1, 0};
static const uint8_t dqk_ev15[] = {0x0b, 0x15, 0x1e, 0x44, 0x52, 0xa1, 0xa7, 0xb1, 0x89, 0};
static const uint8_t dqk_ev16[] = {0x19, 0x45, 0x25, 0x2f, 0x30, 0x59, 0xcf, 0};
static const uint8_t dqk_ev17[] = {0x01, 0x0b, 0};
static const uint8_t dqk_ev18[] = {0x4a, 0x27, 0x2a, 0x63, 0xb2, 0};
static const uint8_t dqk_ev19[] = {0x0b, 0x17, 0xb5, 0};
static const uint8_t dqk_ev20[] = {0x32, 0x36, 0};
static const uint8_t dqk_ev21[] = {0x23, 0};
static const uint8_t dqk_ev24[] = {0x64, 0};

const uint8_t *const cok_dark_queen_event_lists[0x19] = {
    NULL,        dqk_ev1,  dqk_ev2,  dqk_ev3,  dqk_ev4,  dqk_ev5,  dqk_ev6,  dqk_ev7,  dqk_ev8,
    dqk_ev9,     dqk_ev10, dqk_ev11, dqk_ev12, dqk_ev13, dqk_ev14, dqk_ev15, dqk_ev16, dqk_ev17,
    dqk_ev18,    dqk_ev19, dqk_ev20, dqk_ev21, NULL,     NULL,     dqk_ev24,
};
/* The ids the party shares (0369: a target without one takes a holder's), 0-ended; 0x31
 * (Prayer) reaches 6 squares in combat, the others 1 (dqk 0369, scope.md 3.1). */
const uint8_t cok_dark_queen_shared[] = {0x15, 0x2d, 0x2e, 0x31, 0};
/* 02ed's resisted pending effects (the same as Champions'), 0-ended. */
const uint8_t cok_dark_queen_resisted_pending[] = {0x5b, 0x52, 0};

