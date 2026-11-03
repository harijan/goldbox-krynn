#ifndef COK_DARK_QUEEN_H
#define COK_DARK_QUEEN_H

#include "adventure.h"

/* The Dark Queen of Krynn's own drawing (adventure.h's cok_drawing): VGA's
 * 320x200 in 256 colours (vga.h), its HLIB libraries (hlib.h), its font,
 * its window library's text window, bottom bar, buttons, lists and input
 * field, its screens' frames, its 3D view of pre-drawn pieces, its small
 * and big pictures, its title, main and camp menus, its demonstration and
 * its ending (DARK-QUEEN.md's steps 1 and 2). Addresses are
 * build/DQK_FULL.EXE's ("dqk"), its data segment DS 0x422e.
 *
 * Drawing goes to the one screen: the original draws to a back page and
 * flips it, its palette's changes flushed at the flip (dqk 19a4:05ee,
 * 06d3, 261b:0455), always before it waits, so that at every wait the
 * screen is all drawn so far, as here. Its colour cycles (261b:05be) run
 * as the game's time passes. */
extern const cok_drawing cok_dark_queen_drawing;

/* Whether a load is asked (DS:b7a7): the overland's mark after a fight is not made then
 * (dqk 5f07:0974; rules.h's combat load_asked). */
bool cok_dark_queen_load_asked(cok_adventure *game);

/* The treasure branch of COMBAT with no monsters (75cc:135c; rules.h's no_monsters_treasure). */
void cok_dark_queen_no_monsters_treasure(cok_adventure *game);

/* The end-of-combat flags the checks read (eclplay --state): DS:af3f, c7de, c7df, b1da, b7a7 and
 * the experience's share (DS:ae4f); all zero for a game with none. */
typedef struct {
    bool standing, fled, monsters_present, b7a7;
    uint8_t out;
    uint32_t share;
} cok_dark_queen_end_state;
cok_dark_queen_end_state cok_dark_queen_end_state_of(cok_adventure *game);
/* Sets DS:c7df, which 11b0 (part 19c) will set; the tests need it set before the end of combat. */
void cok_dark_queen_set_monsters_present_for_test(cok_adventure *game, bool present);

#endif
