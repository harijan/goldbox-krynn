#ifndef COK_DARK_QUEEN_H
#define COK_DARK_QUEEN_H

#include "adventure.h"

/* The Dark Queen of Krynn's own drawing (adventure.h's cok_drawing): VGA's
 * 320x200 in 256 colours (vga.h), its HLIB libraries (hlib.h), its font,
 * its window library's text window, bottom bar and buttons, its screens'
 * frames, its 3D view of pre-drawn pieces and its pictures, as far as
 * eclplay shows its scripts and title headless (DARK-QUEEN.md's step 1).
 * Addresses are build/DQK_FULL.EXE's ("dqk"), its data segment DS 0x422e.
 *
 * Drawing goes straight to the one screen, its palette changing at once:
 * the original draws to a back page and flips it, its palette changes
 * flushed at the flip (dqk 19a4:06d3, 261b:0455), which shows the same
 * once it waits for a key. Its colour cycles (261b:05be) are not run. */
extern const cok_drawing cok_dark_queen_drawing;

#endif
