#ifndef COK_ADVENTURE_H
#define COK_ADVENTURE_H

#include "ecl.h"
#include "menu.h"
#include "party.h"
#include "picture.h"
#include "text.h"
#include "view.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The adventure screen's side of the ECL opcodes, from the handlers in
 * overlay 2fd3: text, menus, input, pictures, delays, the 3D view's files
 * and the party's characters. Other opcodes go to hooks.unported. The
 * adventure loop moves the party through 3D areas (2fd3:3c28, overlay
 * 475c). The screen is 320x200 in the Tandy layout. */

enum {
    COK_ADVENTURE_FRAMES = 16, /* Small picture frames kept (DS:6da2 holds 8). */
    COK_ADVENTURE_NO_PICTURE = 0xff,
};

typedef struct cok_adventure cok_adventure;

typedef struct {
    /* An opcode not carried out here, with its operands decoded in
     * game->vm. NULL ignores it. */
    void (*unported)(cok_adventure *game, void *context);
    /* Text as it is printed, menus as they are shown (items with their ~
     * marks), input as it is read, pictures that fail to load, the party's
     * square and facing as "X,Y,DIR" after it moves or turns, and commands
     * of the adventure loop that are not ported, and the name of each
     * character WHO picks: kind is "print", "menu", "list", "item",
     * "choice", "input", "error", "at", "unported" or "who". */
    void (*log)(cok_adventure *game, const char *kind, const char *text, void *context);
    /* Before each instruction, as cok_ecl_hooks.trace. */
    void (*trace)(cok_adventure *game, void *context);
    /* Wait ms milliseconds (Crt.Delay): after each printed character, speed
     * * 3, and for DELAY, speed * 100. NULL does not wait. */
    void (*delay)(cok_adventure *game, unsigned ms, void *context);
    void *context;
} cok_adventure_hooks;

struct cok_adventure {
    cok_ecl vm;
    char assets[512];      /* Directory holding the DAX files. */
    cok_picture screen;    /* 40 units by 200 rows. */
    cok_font font;         /* 8X8D1.DAX record 201. */
    /* The 3D view: map, wall sets, tile sets (set 4, 8X8D1.DAX record 202,
     * is the frame's; see screen.h) and sky pictures. */
    cok_view view;
    int16_t wall_ids[3];   /* DS:6d8a: WALLDEF record per wall set, or -1. */

    /* The party (DS:609a). The selected character is vm.character
     * (DS:6096), which must be a member's record or NULL. */
    cok_party party;
    cok_item_types item_types; /* DS:5886, from ITEMS. */

    /* The small picture (DS:6da2): frames from PIC<file>.DAX, each with its
     * delay in hundredths of a second, drawn at cell 3, 3. */
    cok_picture frames[COK_ADVENTURE_FRAMES];
    uint32_t delays[COK_ADVENTURE_FRAMES];
    size_t frame_count;
    size_t frame;          /* DS:6da3, 0-based here. */
    uint8_t picture_id;    /* DS:6e00, COK_ADVENTURE_NO_PICTURE if none. */
    uint8_t picture_file;
    cok_picture big;       /* BIGPIC<file>.DAX, drawn at cell 1, 1 (DS:6e02). */
    uint8_t big_id;        /* DS:6e06, COK_ADVENTURE_NO_PICTURE if none. */
    bool picture_shown;    /* DS:884a: the view holds a picture. */
    bool view_replaced;    /* DS:884b. */
    bool big_shown;        /* DS:4b4e: hides the status line. */
    bool redraw;           /* DS:713a: show the big picture where there is no view. */
    bool files_loaded;     /* DS:43bb: LOAD FILES or LOAD PIECES ran. */
    bool pieces_loaded;    /* DS:43bc. */
    bool map_loaded;       /* DS:43bd. */
    bool frame_pending;    /* DS:8856: redraw the frame once both have run. */

    bool moving;           /* DS:8858: the arrows move the party. */
    bool text_shown;       /* DS:884e clear: text to clear after a command. */
    bool door_tries[3];    /* DS:7146-7148: Bash, Pick and Knock may be tried. */
    bool party_killed;     /* DAMAGE left no character able to act. */

    uint8_t speed;         /* DS:4b38, the game speed; 4 by default. */
    bool animate;          /* DS:4b4f: load every frame of a picture. */
    uint8_t selected;      /* DS:6e0f, the menu item selected. */
    int list_top;          /* DS:6e0d, the first item a list shows. */
    bool input_ended;      /* The keyboard ran out; the run was aborted. */

    cok_keyboard keys;
    cok_adventure_hooks hooks;
    char error[600];       /* Why the last call failed. */
};

/* Load the font and item types from assets and set up the VM with this module's hooks.
 * keys and hooks may be NULL. Returns false with game->error set. Free with
 * cok_adventure_close even on failure, which also frees the party. */
bool cok_adventure_open(cok_adventure *game, const char *assets, const cok_keyboard *keys,
                        const cok_adventure_hooks *hooks);
void cok_adventure_close(cok_adventure *game);

/* Load ECL block from ECL<game->vm.file>.DAX and start it (3775:0361,
 * 3775:01e8), resetting picture state as the original does. */
cok_ecl_status cok_adventure_load(cok_adventure *game, uint8_t block);

/* Load block and enter it as 2fd3:3b47 does: run the load vector, show the
 * view if the block loaded its files or stays in 3D, then run the
 * after-move and location vectors, and start over from the load vector
 * whenever NEWECL switches blocks. */
cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block);

/* Run the adventure loop of 2fd3:3c28 in the block entered: until input
 * ends or a run fails, take a command from the adventure menu (475c:09ec),
 * run the after-move vector, take a step (475c:0e77), show the view and run
 * the location vector. NEWECL enters the new block as cok_adventure_enter
 * does. Only 3D areas are ported: outside them the loop stops and logs
 * "unported". Clears game->vm.abort on return, as the original clears
 * DS:4b57. */
cok_ecl_status cok_adventure_play(cok_adventure *game);

/* Advance the game clock (0x4bc6-0x4bcc) by count of unit 0-6, carrying
 * into larger units and moving the moons on each new day (57e4:0549). */
void cok_adventure_pass_time(cok_adventure *game, unsigned unit, unsigned count);

/* Draw the adventure screen's frame (1128:0242). */
void cok_adventure_frame(cok_adventure *game);

/* Show the 3D view from the party's square, or with no 3D view in the area
 * (0x4be6 and 0x4c38 both 0) the big picture if game->redraw is set
 * (6945:00ba). Sets the party's square (0xc04f) and clears game->redraw. */
void cok_adventure_view(cok_adventure *game);

/* Add the characters of a saved game to the party, as 4b6d:1b34 does: for
 * each name it holds, the character files of that name (see
 * cok_party_file_name) in the saved game's directory, skipping names with
 * no .SAV file. Each is counted in 0x7f3e, and the first becomes the
 * selected character. Returns false with game->error set if the saved
 * game or a character cannot be read, or the party is full. */
bool cok_adventure_load_party(cok_adventure *game, const char *path);

/* Load a saved game (4b6d:1b34): the variables, the party's square and
 * facing, the modes and wall sets, the speed (0x4bfc) and animation
 * (0x4bff), then the party as cok_adventure_load_party does, and the ECL
 * file from 0x7f12. In a 3D area it reloads the map (if wall set 1's
 * record was above 0) and the wall sets. The next block entered keeps the
 * variables NEWECL would clear (DS:4b52), and the adventure loop then
 * redraws the screen. Outside 3D areas the original also shows big
 * picture 0x79, which is not ported. Play resumes from block 0x4bf2, or
 * 0x24 when that is 0 (2fd3:3c28). */
bool cok_adventure_restore(cok_adventure *game, const char *path);

/* Draw the party list beside the view (6346:07ba), unless the area has no
 * 3D view or a big picture is shown. */
void cok_adventure_party(cok_adventure *game);

/* Draw the status line on row 15 (6346:2d75): the party's square unless the
 * overhead map is on (0x4bfb), its facing, the time, and "search" while
 * searching. Not drawn outside 3D areas. */
void cok_adventure_status(cok_adventure *game);

/* Read a record by id from <name><file>.DAX in the asset directory, as
 * 169c:088e does; the first record with the id wins. Returns a malloc'd
 * buffer, or NULL with game->error set. */
uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size);

#endif
