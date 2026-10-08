#ifndef COK_ROSTER_H
#define COK_ROSTER_H

#include "adventure.h"

#include <stdbool.h>
#include <stddef.h>

/* The roster: characters kept between games in the save directory
 * (DS:5784, game->save_dir) as NAME.WHO, NAME.STF and NAME.SFX, from the
 * start menu (overlays 4def and 4b6d). */

/* A character in the roster (4b6d:008b): its file's base name and its
 * name as listed, padded to 15 columns. */
typedef struct {
    char base[256];
    char shown[41];
} cok_roster_entry;

/* List the roster (4b6d:0453): every file NAME.WHO in game->save_dir of
 * exactly 409 bytes whose byte +0xf7 (the jewelry's low byte, where the
 * NPC flag +0xe7 was surely meant) is below 0x80 and whose name no record
 * in the list has. DOS lists them in directory order; the port in order
 * of their file names. Returns the count, at most max; 0 with no
 * directory. */
size_t cok_roster_list(cok_adventure *game, cok_roster_entry *entries, size_t max);

/* Save character to the roster (4b6d:0bed with no base): as NAME.WHO, its
 * name's dots, then the characters 169c:05da removes, taken out
 * (cok_party_roster_name). If that file is there, "Overwrite NAME? "
 * asks; No asks "New file name: " until one is typed, which becomes the
 * character's name and file. The disk and free-space checks are left
 * out. Returns false when input ended or nothing could be written, which
 * is logged. */
bool cok_roster_save(cok_adventure *game, cok_character *character);

/* Erase the roster's copy of the character of record (4b6d:0a80):
 * NAME.WHO, .STF and .SFX by its name (cok_party_file_name), those that
 * are there. */
void cok_roster_erase(cok_adventure *game, const uint8_t *record);

/* Add Character to Party (4def:37a2): pick roster characters to join,
 * each marked "* " in the list, until Exit, Escape or the party is full;
 * a duplicate, a seventh player character, a ninth record, a paladin with
 * an evil member, a fourth ranger, or an evil character with a paladin
 * is turned away. */
void cok_roster_add(cok_adventure *game);

/* Drop Character (4def:2668): pick one, then "Drop NAME forever? " and
 * "Are you sure? "; Yes to both removes it from the party and its roster
 * copy. */
void cok_roster_drop(cok_adventure *game);

/* Remove Character from Party (4def:01b4, R): pick one; a player
 * character goes back to the roster (cok_roster_save) and leaves the
 * party; an NPC is dropped instead (cok_roster_drop, which picks again). */
void cok_roster_remove(cok_adventure *game);

/* Load Saved Game from the start menu (4b6d:1b34): "Load Which Game: "
 * over the letters whose SAVGAM<letter>.DAT is there (nothing at all with
 * none), Escape cancelling; then the game is loaded
 * (cok_adventure_restore) and the roster copies of its characters erased.
 * Returns true if a game was loaded. */
bool cok_roster_load(cok_adventure *game);

#endif
