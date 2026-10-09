#ifndef COK_COMBAT_H
#define COK_COMBAT_H

#include "party.h"
#include "picture.h"
#include "view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The battlefield: combat setup (3cb2:1c58 and the rest of overlay 3cb2),
 * which builds the map, gives every record its combat record and places
 * the combatants, the map's lookups of overlay 6beb, and the tables of
 * combatants the original keeps in its data segment. The later parts of
 * combat (the rounds, the screen, attacks, the AI and spells) work on
 * these.
 *
 * The map (DS:6a2e, 0x4e9 bytes from GetMem) is 50 cells across by 25
 * down, one terrain value a cell (cok_combat_terrain); a 3D square
 * becomes a block of 6 by 5 cells, each row of blocks five cells right of
 * the one above it. Combatants are numbered from 1 in list order, the
 * party first; a combatant's footprint (+0xcf & 7, DS:1fe4) is 1 cell,
 * two down (2), two across (3) or 2 by 2 (4) from its top-left cell. */

typedef struct cok_adventure cok_adventure;

enum {
    COK_COMBAT_WIDTH = 50,
    COK_COMBAT_HEIGHT = 25,
    COK_COMBATANTS = 72,        /* DS:68d1: 72 far pointers before the bodies. */
    COK_COMBAT_BODIES = 8,      /* DS:69ee: 7-byte entries from 1. */
    COK_COMBAT_TILES = 40,      /* DS:616a: 24 by 24 frames. */
    COK_COMBAT_TERRAINS = 0x42, /* DS:1ee4: the values the builders write. */
    COK_COMBAT_SIZES = 8,       /* DS:1fe4: the footprints sizes 0-7 read. */
    COK_COMBAT_FLOOR = 0x17,    /* Plain floor: tile 0x16. */
    COK_COMBAT_BODY = 0x1f,     /* A fallen party member: RANDCOM's last frame. */
};

/* A terrain value's entry in DS:1ee4 (constant): the cost of moving onto
 * it (0xff where one cannot), the eye height of one standing there, the
 * height that blocks sight past it, and its tile in the combat tile set:
 * 0x01-0x19 DUNGCOM's frames, 0x1a-0x1f RANDCOM's (table, chair, white
 * cloud, puddle, green cloud, body) and 0x20-0x41 WILDCOM's. Value 0
 * stands for off the map. */
typedef struct {
    uint8_t cost, eye, block, tile;
} cok_terrain;
extern const cok_terrain cok_combat_terrain[COK_COMBAT_TERRAINS];

/* Combatant n's entry: DS:623f + 4n (x, y, n, size), DS:68d1 + 4n (its
 * record) and DS:6362 + n, 63aa + n (where it is on the screen). */
typedef struct {
    int8_t x, y;              /* Its top-left cell. */
    uint8_t size;             /* Its footprint; 0 while it is not on the map. */
    cok_character *character; /* NULL for none. */
    /* x and y less the view's origin when 6beb:0375 or 6beb:0077 last
     * worked them out, which the screen's routines read. */
    int8_t screen_x, screen_y;
} cok_combatant;

/* A party member that fell where it stands (DS:69ee + 7k): the body tile
 * replaces the cell's terrain, kept here to be put back. */
typedef struct {
    cok_character *character; /* NULL once taken back (6beb:10f3). */
    int8_t x, y;
    uint8_t cell;
} cok_combat_body;

/* A combatant listed around a cell (DS:6a30 + 3k, 6b30:08d8). */
typedef struct {
    uint8_t index;     /* Its combatant. */
    uint8_t distance;  /* In half cells: 2 a straight step, 3 a diagonal one. */
    uint8_t direction; /* 0-7 from north, clockwise; 8 for any. */
} cok_combat_listing;

typedef struct {
    bool active;             /* DS:6a2e is not NULL: a battle is set up. */
    int8_t view_x, view_y;   /* Map +2, +3: the top-left cell of the 7 by 7 shown. */
    uint8_t cursor;          /* Map +4: the cursor is shown. */
    uint8_t cursor_size;     /* Map +5: the cursor's footprint. */
    uint8_t see_all;         /* Map +6: sight is not blocked (6b30:03f1). */
    uint8_t cells[COK_COMBAT_HEIGHT][COK_COMBAT_WIDTH]; /* Map +7: terrain values. */
    /* DS:63f3: the combatant on each cell, 0 for none (6beb:0375). */
    uint8_t occupant[COK_COMBAT_HEIGHT][COK_COMBAT_WIDTH];
    /* Combatants 1 to count - 1; entry count is the empty one after
     * them, whose size is 0. Entry 0 stands for a record that is not a
     * combatant (6beb:0c43 returns 0): its x and y are 0 and its size is
     * read as count, as the original reads DS:6242. */
    cok_combatant combatant[COK_COMBATANTS + 1];
    uint8_t count;           /* DS:6242. */
    cok_combat_body body[COK_COMBAT_BODIES]; /* DS:69ee + 7, ... */
    uint8_t bodies;          /* DS:6a2d. */
    /* DS:6b2c, 6b2d: the records on the party's side and against it that
     * can act (6346:268a). */
    uint8_t sides[2];
    /* DS:7197: the enemies' hit points as a percentage of their most, in
     * fives (432f:2df3); kept from the last combat that worked it out. */
    uint8_t enemy_health;
    uint8_t round_limit;     /* DS:714c: rounds without an attack before it ends. */
    bool magic_on;           /* DS:7198: Auto characters cast (Alt-M). */
    uint8_t *yelled;         /* DS:71a7: the kender who yelled this battle (60f4:0023). */
    /* The rounds (see round.h). DS:71ae: the turn order, combatants' records
     * from entry 1 to 72 (3995:02a0), NULL after the last; DS:72ce the
     * entry whose turn it is. */
    cok_character *turn[COK_COMBATANTS + 1];
    uint8_t turn_index;
    bool panel;              /* DS:71ac: the side panel is to be drawn (6346:0af6). */
    bool show_actions;       /* DS:71ad: the actor's moves are shown (3995:040b). */
    /* DS:71ab: the computer's target is within reach (3afb:0d49), then
     * whether it has ammunition; read by its next step, kept from turn to
     * turn and battle to battle. */
    bool in_reach;
    /* DS:43c8, 43c9, 43ca: the computer's last step's direction (8 none)
     * and how often it has been stuck (3afb:095a), and a byte 3afb:0d49
     * clears that nothing reads. */
    uint8_t last_step, stuck, stuck_more;
    /* DS:6a30 + 3k, k from 1 (count DS:6a32): the combatants around a cell
     * (6b30:08d8), nearest first. */
    cok_combat_listing listed[COK_COMBATANTS + 1];
    uint8_t listed_count;
    /* DS:714d on: the enemies of the last 6346:26e2, as combatants, from 1. */
    uint8_t enemies[COK_COMBATANTS + 1];
    /* DS:6b45, 6b95: the dead that explode after the turn (effect 0x44),
     * at most 20, from 1; DS:6b96 while they do (60f4:2375). */
    cok_character *exploding[21];
    uint8_t exploding_count;
    bool exploding_now;
    /* DS:7195, 7196: the attacks made with slots 1 and 2 in the last
     * attack (432f:1579), whose ammunition 432f:1a45 then uses up; [0] is
     * DS:7194, which a helpless target's blow from slot 0 counts, the byte
     * the list of enemies (DS:714d) holds its 72nd in. */
    uint8_t swings[3];
    /* DS:616a: the tile set, 40 frames of 24 by 24, which each setup loads
     * DUNGCOM or WILDCOM into from frame 0 and RANDCOM into from 0x22
     * (6d21:002c); frames it does not load keep what an earlier battle
     * loaded there. tiles_loaded has bit n set once frame n has been. */
    cok_picture tiles;
    uint64_t tiles_loaded;
    /* The screen's pictures (see arena.h): DS:4b78, the map's buffer, 21
     * units by 168 rows; DS:4b7c, what a masked draw that saves covered,
     * 24 by 24, laid out as the original saves (127f:27af), under_set
     * marking the bytes a save has written; DS:719e, the four pictures of
     * a missile or flash, 24 by 24, masked. unknown marks the buffer's
     * bytes drawn back from a save's byte never written, until covered
     * (arena.c's restore). */
    cok_picture buffer, under, flash;
    bool under_set[24 * 12];
    uint8_t unknown[21 * 4 * 168];
} cok_combat;

/* The constant tables of the data segment that the port's code holds,
 * to check against the executable. */
extern const cok_ds_table cok_combat_tables[];
extern const size_t cok_combat_table_count;

/* Setup (3cb2:1c58), as the battle (3995:0172) starts it, the mode first
 * becoming 5: frees the small and big pictures, says "A battle begins..."
 * on row 24 after a pause, clears the battle's state, builds the map
 * (3cb2:1029), gives every record its stats and combat record
 * (3cb2:10d9), places them (3cb2:17f7), sets the view's origin on the
 * first record, works out the screen positions (6346:300f, which the
 * port does not draw), runs effect events 8 and 0x16 for every record
 * and works out the enemies' health (432f:2df3). Between the stages, and
 * before each record is placed, a key waiting is read and dropped
 * (1614:0479). Returns false, with game->vm.status set, where it cannot
 * be carried out. */
bool cok_combat_setup(cok_adventure *game);

/* The end of the battle (3995:004b): charmed records that are okay run
 * if more than one enemy could act at the last count, every record loses
 * the first effect of each id that lasts only through the battle
 * (60f4:1440), a berserk turned record goes back to the party's side,
 * the map is freed and spells pick their targets outside combat again
 * (DS:6e3a). The lists of clouds and the flash picture, which the
 * original frees too, are not ported. Returns false, with
 * game->vm.status set, where an effect's removal cannot be carried out. */
bool cok_combat_end(cok_adventure *game);

/* The map's pieces, with explicit inputs. */

/* Build the map from the 3D map around square x, y (3cb2:08cd): each
 * square 6 east and west and 2 north and south of it becomes a block of
 * floor and walls from its sides (cok_view_passage, cok_view_wall); a
 * room whose square byte has 0x40, with a wall, no door, and walls on the
 * other two sides where two opposite sides are walls, gets tables and
 * chairs on d10s from seed. Every cell is written, but a room's tables
 * read cells not yet built, as the map's memory holds them: setup fills
 * them with RANDCOM's record, as the heap normally leaves them. Returns
 * false where those cells were read. */
bool cok_combat_dungeon(cok_combat *combat, const cok_view *view, int8_t x, int8_t y,
                        uint32_t *seed);

/* Build a map of open ground (3cb2:0fc8): floor, then a river, trees and
 * scatter by the flags of a terrain type (DS:0432), about 2,700 rolls from
 * seed, column by column. The game always takes type 15, whose flags, 4,
 * give no river, though its d100 is rolled. */
void cok_combat_wilderness(cok_combat *combat, uint8_t flags, uint32_t *seed);

/* Give every record in party its stats (6346:0d20) and a combat record,
 * zeroed (3cb2:10d9): +0x13 set past party_size (var 0x7f3e), facing the
 * combat direction of the party's direction (DS:03da), or the other way
 * against the party (+0x18a 1); an ally past the party (+0x18a 0) whose
 * morale (+0xe7 & 0x7f) is 0 or above 0x66 gets var 0x7ec6 + 0x80. A
 * previous combat record is freed. Returns false with error set where
 * the stats cannot be worked out (see cok_character_stats), leaving the
 * records from that one on as they were. */
bool cok_combat_records(cok_party *party, const cok_item_types *types, uint16_t party_size,
                        uint8_t direction, uint16_t morale, char *error, size_t error_size);

/* Place every record (3cb2:17f7) on the built map; see combat.c. A
 * monster with no place is removed (4def:3b0a); a party member without
 * one stays off the map. Returns false, with game->vm.status set, where
 * it cannot be carried out. */
bool cok_combat_place_all(cok_adventure *game);

/* 6346:268a: count the records that can act on each side (+0x18a 0 or
 * 1). Returns false for a record on another side, which the original
 * counts, as a signed byte, in the data around DS:6b2c. */
bool cok_combat_count_sides(cok_combat *combat, const cok_party *party);

/* 432f:2df3: the enemies' (+0x18a 1) hit points, of those that can act,
 * as a percentage of all their most, in fives: (20 * hp as a word) /
 * most * 5, as a byte. Unchanged when the most is 0. */
void cok_combat_enemy_health(cok_combat *combat, const cok_party *party);

/* The lookups of overlay 6beb. */

/* Cell k (0-3) of footprint size (6beb:000f), for sizes below
 * COK_COMBAT_SIZES; false for size 0 or a cell the footprint lacks. */
bool cok_combat_footprint(unsigned size, unsigned k, int8_t *dx, int8_t *dy);

/* The combatant whose record is record (6beb:0c43), from 1, or 0 for
 * none: entries 1 to the count, so also the one after the last, which
 * holds the record being placed while setup places them. Afterwards the
 * original's holds a record of an earlier battle, freed, or NULL; the
 * port's NULL. */
uint8_t cok_combat_index(const cok_combat *combat, const uint8_t *record);

/* The x, y and size of record's combatant (6beb:0bcb, 0bf3, 0c1b): for
 * none, 0, 0 and the count. */
int8_t cok_combat_x(const cok_combat *combat, const uint8_t *record);
int8_t cok_combat_y(const cok_combat *combat, const uint8_t *record);
uint8_t cok_combat_size(const cok_combat *combat, const uint8_t *record);

/* The terrain and combatant of cell x, y (6beb:0493), both 0 off the
 * map. */
void cok_combat_cell(const cok_combat *combat, int8_t x, int8_t y, uint8_t *occupant,
                     uint8_t *terrain);

/* Rebuild the occupants (6beb:0375): each combatant on the map fills the
 * cells of its footprint, and its screen position is worked out. A
 * footprint past the right edge runs into the next row, as in the
 * original. Returns false where one would be written outside the
 * table, or a size is past the footprints, which the original reads
 * after them. */
bool cok_combat_occupy(cok_combat *combat);

/* What record's footprint shifted one step in dir (0-7, 8 for none)
 * stands on (6beb:0c9d): *occupant the last other combatant found;
 * *terrain the cell of highest cost (the later on ties), starting from
 * plain floor, which cells of green cloud (0x1e) and puddle (0x1d) do not
 * count toward, and 0 once a cell is off the map; *cloud and *puddle
 * whether a cell is one. Returns false for a size past the footprints. */
bool cok_combat_probe(const cok_combat *combat, const uint8_t *record, uint8_t dir,
                      uint8_t *occupant, uint8_t *terrain, bool *cloud, bool *puddle);

/* Put character on the map at x, y (6beb:10f3), in combat (mode 5;
 * otherwise *placed is true and nothing changes): its size from +0xcf &
 * 0x7f, and if nothing stands there and it can be walked on, *placed;
 * with restore, a party member's bodies are taken back and the cell gets
 * the terrain the last of them covered, unless another body lies there,
 * else the terrain the probe found. The occupants are then rebuilt.
 * Returns false where the original reads past the footprints, writes
 * outside the occupants or the map, or for a record that is not a
 * combatant, whose size it would write over the count. */
bool cok_combat_place(cok_combat *combat, uint8_t mode, cok_character *character, uint8_t x,
                      uint8_t y, bool restore, bool *placed);

/* Whether x, y, relative to the view's origin, is in the 7 by 7 shown
 * (6beb:06be). */
bool cok_combat_on_view(int8_t x, int8_t y);

/* Whether record's footprint is on the view (6beb:06ef), from its
 * screen position: any cell of it, or with all, every cell. False for
 * a combatant not on the map. Returns false where the original reads
 * past the footprints. */
bool cok_combat_visible(const cok_combat *combat, const uint8_t *record, bool all,
                        bool *visible);

/* Scroll the view toward x, y (6beb:07a9) unless it is within margin of
 * the centre, or always for margin 0xff: the centre moves onto it a cell
 * at a time while it is within 3-0x2e across and 3-0x15 down; then the
 * screen positions are worked out (6beb:0077). Returns whether it
 * scrolled, when the original redraws the map, which the port does not.
 * 6beb:096b, which centres the screen, scrolls toward x, y moved one
 * step in its direction. */
bool cok_combat_scroll(cok_combat *combat, int8_t x, int8_t y, uint8_t margin);

#endif
