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

/* A wait of ms as the game's 1df9:052a does it (one random(1000) first for ms != 0): the wait of
 * ADD EP (1a62:0949 -> 1df9:052a, var 0x12's 100 ms times). */
void cok_dark_queen_wait(cok_adventure *game, unsigned ms);

/* The random scroll (6c70:1a83, 1a4e): its 17 bytes into entry, drawing from *seed (RandSeed).
 * TREASURE ITEMS' id 0xff and STOCK TREASURE's stock 0xff (part 40) make it. */
void cok_dark_queen_random_scroll(const cok_rules *rules, uint32_t *seed,
                                  uint8_t entry[COK_ITEM_ENTRY]);

/* The treasure branch of COMBAT with no monsters (75cc:135c; rules.h's no_monsters_treasure). */
void cok_dark_queen_no_monsters_treasure(cok_adventure *game);

/* The round's and battle's hooks of rules.h's combat (5045:0f6c, 5045:0001, 5045:059d's tail):
 * the clock (1396:044d(1, 1)), "Your Teammate is Dying" (177e:153c), the Continue question
 * (1bdb:0424: true for Yes), the combat icons' empty slots (204c:004d) and the colour cycles
 * (261b:05be) after a turn. */
bool cok_dark_queen_round_clock(cok_adventure *game);
void cok_dark_queen_teammate_dying(cok_adventure *game);
bool cok_dark_queen_continue_battle(cok_adventure *game);
void cok_dark_queen_battle_icons(cok_adventure *game);
void cok_dark_queen_turn_tail_cycles(cok_adventure *game);

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
/* The camp's say of a member (member_say: its name line on row 18, the text from row 19), for the
 * tests: the stoned and Order's texts reach it through the camp's menus only. */
void cok_dark_queen_member_say_for_test(cok_adventure *game, const uint8_t *c, const char *text);

/* The small picture's frame (DS:b20e) and whether the menu waiting now animates it. */
unsigned cok_dark_queen_picture_frame(const cok_adventure *game);
bool cok_dark_queen_picture_animating(const cok_adventure *game);

#endif
