#ifndef COK_ARENA_H
#define COK_ARENA_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* The combat screen: the map's drawing of overlay 6beb, the icons of
 * overlay 6d21 and 4b6d, and the side panel, messages, missiles and
 * flashes of overlay 6346.
 *
 * The screen is the frame of 1128:04c1, the map in the 3D view's place
 * and the side panel in cells 23-38 by 1-21. The map shows 7 by 7 cells of
 * 24 by 24 pixels from the view's origin (cok_combat.view_x, view_y). It
 * is drawn into a buffer of 21 units by 168 rows (DS:4b78, the 3D view's,
 * cok_combat.buffer), which is clipped there, and shown (127f:12e8) one
 * unit right and one cell down on the screen, cells 1-21 across and down.
 * A cell's terrain is the tile of its value (cok_combat_terrain) from the
 * combat tile set, opaque (6d21:00f7); icons are masked over them
 * (6d21:04b0). Text goes straight to the screen.
 *
 * The routines draw only while the actor's moves are shown
 * (cok_combat.show_actions, DS:71ad) where the original tests it, and
 * the panel only when it is due (cok_combat.panel, DS:71ac). Delays go
 * through the adventure's delay hook; sounds, the sound driver's
 * commands (17e8:0020), are logged as "sound". Each returns false, with
 * game->vm.status set and the run ended, where the original would read
 * or write what it does not hold: a record that is not a combatant, an
 * icon slot past the table, a cell outside the map's memory. */

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_arena_tables[];
extern const size_t cok_arena_table_count;

/* Draw the combat screen anew (6346:300f): the frame (1128:04c1) and the
 * map around the view's centre, forced (cok_arena_centre with margin
 * 0xff). */
bool cok_arena_redraw(cok_adventure *game);

/* Centre the screen on x, y moved a step in dir (0-7, 8 for none)
 * (6beb:096b): if that is outside the centre's margin, or always for
 * margin 0xff, scroll the view toward it (6beb:07a9, cok_combat_scroll),
 * drawing every terrain tile shown and then every combatant on the map,
 * partly shown, that can act or whose status is 10 (6d21:04b0, ready, by
 * its facing). Then redraw cell x, y (cok_arena_redraw_cell), and the
 * terrain of the cells of the cursor's footprint (map +5) at the target,
 * clamped onto the map if it is not shown, with the cursor's box (icon
 * slot 25) if it is on (map +4), and the icon of the target cell's
 * combatant (6beb:00fc); and show the map. */
bool cok_arena_centre(cok_adventure *game, int8_t x, int8_t y, uint8_t margin, uint8_t dir);

/* Redraw cell x, y (6beb:02aa): erase it (cok_arena_erase with its screen
 * position and no combatant), then draw its combatant's icon if it is
 * partly shown. The map is not shown. */
bool cok_arena_redraw_cell(cok_adventure *game, int8_t x, int8_t y);

/* Draw the terrain over combatant n (6beb:0500), at its screen position
 * as last worked out, on each cell of its footprint that is shown; for
 * n 0, over the combatant on the map's cell at screen position xs, ys,
 * or with none there the terrain of that cell if it is shown. */
bool cok_arena_erase(cok_adventure *game, int8_t xs, int8_t ys, uint8_t n);

/* Turn c to face pose (0-7) and draw it in image (0 ready, 1 attacking)
 * (6beb:0ad8): while its moves are shown, centre on it unless it is shown
 * whole, and erase it when it turns from one side to the other (pose / 4
 * changes), or for an image or hide; then its facing (combat record +9)
 * is pose, and unless hide, while shown and partly shown, its icon is
 * drawn and the map shown. */
bool cok_arena_pose(cok_adventure *game, cok_character *c, uint8_t pose, uint8_t image,
                    bool hide);

/* Show c's turn (6beb:12ef): the cursor on (map +4 cursor) in c's
 * footprint, and while its moves are shown, the screen centred on it with
 * margin; then the cursor off and its footprint one cell again. c need
 * be a combatant only while the moves are shown. */
bool cok_arena_turn(cok_adventure *game, cok_character *c, uint8_t margin, bool cursor);

/* What the byte 6beb:0e08 tests as the index of a body holds: the stack
 * left by its caller's calls before it (see cok_arena_kill). */
enum { COK_ARENA_STALE_UNKNOWN = -1 };

/* The record pointers outside the body table that 6beb:0e08's stale byte
 * b can read as a body's (DS:69ee + 7b): the exploding list's entries 1,
 * 8 and 15 (DS:6b45, 6b61, 6b7d; b 0x31, 0x35, 0x39) and the spell
 * targets' entries 1, 8, ..., 64 (DS:6feb + 28j; b 0xdb + 4j). Neither is
 * cleared with the bodies. The caller fills in what it holds (NULL for
 * none): the first from combat.exploding[1], [8] and [15]; the spell
 * targets are not held yet. */
typedef struct {
    const cok_character *exploding[3];
    const cok_character *targets[10];
} cok_arena_reachable;

/* c drops (6beb:0e08). Outside combat, the death's sound (5) and a pause
 * of speed * 100 ms. In combat, unless the entry the stale byte reads
 * holds c (see below), centre on c if it is not shown whole, erase it,
 * sound 5, flash the skull (icon slot 24, ready then attacking) over each
 * cell of its footprint that is shown nine times, 10 ms apart, masked;
 * then a party member (combat record +0x13 clear) leaves a body (DS:69ee)
 * where its top-left cell is, the cell becoming 0x1f unless it is a green
 * cloud; after a pause of speed * 100 ms c is erased and taken off the
 * map (size 0), the occupants are rebuilt, the view's centre cell redrawn
 * (cok_arena_centre with margin 3), and its initiative, movement, spell
 * and guard cleared.
 *
 * The original's search for a body of c indexes the table with a local
 * it never set ([bp-3]), the byte of the stack its caller's last call at
 * the same depth left there:
 *   - 432f:0678, 60f4:017f, 60f4:20d2: 60f4:1440's [bp-3] (through
 *     60f4:057c, which writes no local): 0, or the low byte of the segment
 *     of c's effect 0x4d node if it had one when 1440 looked;
 *   - 432f:1425: after 6346:1883, the high byte of the far return's
 *     segment: of the code segment overlay 432f is loaded at, of 432f's
 *     stub segment, or 0 when the call came through INT 3Fh (19f0:02e6
 *     leaves the stub's IP 0x0075): unknown;
 *   - 60f4:24df (in 60f4:2375): 0x24, the high byte of the return address
 *     24bf, after a 60f4:1db7 in the pass, else 0, the high byte of the
 *     map's offset pushed for 6b30:08d8.
 * An interrupt between the call that left the byte and 0e08 could leave
 * another on any path. stale is the byte, or COK_ARENA_STALE_UNKNOWN.
 * Entries 1-8 are the bodies, kept from earlier battles past the count
 * (setup clears only the count); 0 reads the pointers of combatants 71
 * and 72 and 0x24 the targets 62 and 63 listed (DS:6a30), which never
 * hold a heap pointer, so neither finds c; 0x31, 0x35, 0x39 and 0xdb +
 * 4j read the lists of also (NULL: empty). Unknown, the port stops if c
 * is in any of those, where the byte could find it, and else takes it as
 * not found. A body entry of a record since freed can match a new record
 * the allocator put at its address, which stops the port too. */
bool cok_arena_kill(cok_adventure *game, cok_character *c, int stale,
                    const cok_arena_reachable *also);

/* Copy the map's buffer to the screen (127f:12e8). False, ending the run,
 * where a byte the original would draw from the heap would show (see
 * restore in arena.c). */
bool cok_arena_show(cok_adventure *game);

/* Draw icon image (0 ready, 1 attacking) of slot (6172 + 8 * slot) at
 * cell x, y of the map's buffer, masked, mirrored for a facing above 3
 * (6d21:04b0); nothing for an empty slot. A large icon is drawn whole from
 * its top-left cell and clipped at the buffer's edges. */
bool cok_arena_icon(cok_adventure *game, int8_t x, int8_t y, uint8_t facing, uint8_t image,
                    uint8_t slot);

/* Load the icons of slot (6d21:01d0): records id and id + 0x80 of name,
 * ready and attacking, masked with colour 0 transparent (127f:0111). For
 * "CHEAD" or "CBODY" followed by a letter, the letter is dropped and a T
 * (or t) adds 0x40 to id; "COMSPR" loads as it is; any other name gets
 * the ECL file's digit (DS:5782, game->vm.file) and has colour 8 drawn
 * black (127f:15fd from DS:1e26 to 1e36, outside CGA mode). A record that
 * is not there leaves the picture empty, as the original does; a file
 * that is not there makes the original ask for its disk, and fails. */
bool cok_arena_load_icon(cok_adventure *game, const char *name, uint8_t id, uint8_t slot);

/* Free the icons of slot (6d21:0156). */
void cok_arena_free_icon(cok_adventure *game, uint8_t slot);

/* Lay head over body (4b6d:0784): for each byte of head's first frame,
 * OR its pixels into body's and AND its mask into body's, at the same
 * byte. Returns false where the original would read or write past a
 * picture: a picture without pixels or mask, or a head larger than the
 * body's frame. */
bool cok_arena_compose(const cok_picture *head, cok_picture *body);

/* Build a player character's icons in its slot (+0x137) (4b6d:0817): its
 * body (+0x136) from CBODY and its head (+0x135) from CHEAD into slot 11,
 * small or large by +0x138 (1 S, 2 T: ids + 0x40), the head laid over the
 * body (cok_arena_compose) ready and attacking, colour 8 drawn black,
 * then if colours each of the six colour bytes from +0x139 gives the
 * template's colours (DS:390b: 1, 2, 3, 4, 6 and 7) its low nibble and
 * theirs + 8 its high one (127f:15fd); slot 11 is freed. Fails for a size
 * past 2, whose letter the original reads from the strings after DS:0877,
 * and where a picture is missing. */
bool cok_arena_party_icon(cok_adventure *game, cok_character *c, bool colours);

/* The icons of every record in the list as a saved game's characters join
 * (4b6d:1b34, after 4b6d:1989): a player character's built
 * (cok_arena_party_icon with colours), an NPC's (+0xe7 0x80 and up)
 * CPIC record +0x115 of file, the saved game's. */
bool cok_arena_join(cok_adventure *game, uint8_t file);

/* The side panel for c (6346:0af6), if it is due (cok_combat.panel),
 * which it clears: cells 23-38 by 1-21 cleared, c's name on row 1,
 * "Hitpoints" on row 3 and "AC" on row 5 at column 23 in light green, its
 * hit points at column 33, yellow below the maximum, and its armour class
 * at column 26 (6346:0984); its readied weapon's name (6346:0488) wrapped
 * in columns 23-38 from row 7; then two rows below the text's last, in
 * white, its status (DS:1330) if it cannot act, "(Helpless)" if it has
 * effect 0x1f or 0x33-0x35 (6346:0cdb), or "(Casting)" while it casts a
 * spell. */
bool cok_arena_panel(cok_adventure *game, cok_character *c);

/* Say text about c (6346:1883): in combat its name (6346:199d) at column
 * 23 of row, the text wrapped below in light green in columns 23-38 to
 * row 21, cleared first; outside combat as cok_camp_say, whatever row.
 * With wait, a pause and the text cleared (cok_arena_clear_text). From a
 * row past 20 in combat the original prints over the frame: the port
 * stops. */
bool cok_arena_say(cok_adventure *game, cok_character *c, const char *text, uint8_t row,
                   bool wait);

/* Clear the text (6346:196a): in combat the panel's rows 10-21, outside
 * it rows 18-22. */
void cok_arena_clear_text(cok_adventure *game);

/* Say text about c with a flash on it (6346:228c): in combat the flash's
 * pictures are built from icon slot 0x16 (kind not 0) or 0x17
 * (cok_arena_missile_frames), the screen centred on c unless it is shown
 * whole, sound 4 or 3, the text said on row 10 (cok_arena_say, no wait),
 * then the four pictures drawn masked at c's top-left cell, shown 70 ms
 * each and erased, once, or speed + 1 times for a kind not 0; with no
 * repeat a pause of speed * 100 ms after. Outside combat it is said on row
 * 10 with a wait. */
bool cok_arena_flash(cok_adventure *game, cok_character *c, uint8_t kind, const char *text);

/* Build the four pictures of a missile or flash (DS:719e) from icon slot
 * (6346:1b5b, 6346:1a26): ready, ready mirrored, attacking mirrored and
 * attacking. The slot's pictures must be 24 by 24, as COMSPR's are. */
bool cok_arena_missile_frames(cok_adventure *game, uint8_t slot);

/* Fly a missile from cell x0, y0 to x1, y1 (6346:1ba6), along the line
 * between them in steps of 8 pixels (6b30:01a5, 6b30:024c). The screen is
 * centred, forced, on the view's centre if both ends are shown, else on
 * their midpoint if they are at most 6 cells apart, or else on the view's
 * centre until the missile leaves the view and then, near the target, from
 * where it comes in. Each step draws the missile's pictures in turn,
 * frames of them, masked at its place, shows the map, waits delay ms and
 * erases it; with a delay of 0 only where it is on a cell's edge across or
 * down. It ends drawn at the
 * target, shown and erased after delay ms, or left drawn for a delay of
 * 0. A missile to its own cell is not drawn. One whose walk back comes in
 * where it leaves again, as one to a target on an edge of the map from
 * more than 6 cells away can, never lands in the original: the port stops
 * after 8 rounds. */
bool cok_arena_missile(cok_adventure *game, int x0, int y0, int x1, int y1, uint8_t frames,
                       uint8_t delay);

#endif
