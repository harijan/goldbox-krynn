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

#endif
