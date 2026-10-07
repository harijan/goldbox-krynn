#ifndef COK_ADVENTURE_H
#define COK_ADVENTURE_H

#include "combat.h"
#include "ecl.h"
#include "effect.h"
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

enum {
    COK_ICON_SLOTS = 26, /* DS:6172: 0-7 the party's, 8 on monsters', 13-25 missiles. */
    COK_MONSTERS_MAX = 63, /* DS:43be: records LOAD MONSTER adds between CLEARMONSTERS. */
    COK_COINS = 7,
};

/* The treasure pool: the coins (DS:6b0c, seven longs: silver, copper,
 * bronze, platinum, steel, gems and jewelry) and the items (DS:6b28, a list
 * of 63-byte items linked at +0x2a, kept here in list order, the head
 * first). TREASURE and the end of combat fill it, and the party's money
 * goes in and out through it in treasure and shops (modes 6 and 1);
 * CLEARMONSTERS empties it. */
typedef struct {
    uint32_t coins[COK_COINS];
    uint8_t (*items)[COK_ITEM_SIZE];
    size_t item_count;
    /* DS:60a2: the last missile combat put in the pool (432f:19b6), as 1 +
     * its index, kept on the same item as items come and go; 0 for none,
     * or once that item is gone. The experience for magic items stops at
     * it (351b:0037). The original keeps a pointer, which only combat setup
     * clears (3cb2:1c58), so that a later item allocated where a freed one
     * was can match it; the port forgets it when its item goes. */
    size_t missile;
} cok_pool;

/* A weapon lost in combat (DS:609e, 71-byte nodes): the item, and the
 * record it goes back to at the end of combat (+0x3f, 351b:185f). */
typedef struct {
    uint8_t item[COK_ITEM_SIZE];
    uint8_t *owner;
} cok_lost_weapon;

/* How eclplay's --combat resolves a battle, which is not ported. */
typedef enum {
    COK_COMBAT_UNPORTED, /* Log COMBAT's battle as unported. */
    COK_COMBAT_WON,      /* Every record against the party (+0x18a 1) drops. */
    COK_COMBAT_FLED,     /* The whole party flees. */
    COK_COMBAT_LOST,     /* The whole party dies. */
    COK_COMBAT_GODS,     /* The original's Helm cheat (432f:41e2) ends it. */
} cok_combat_stub;

typedef struct {
    /* An opcode not carried out here, with its operands decoded in
     * game->vm. NULL ignores it. */
    void (*unported)(cok_adventure *game, void *context);
    /* Text as it is printed, menus as they are shown (items with their ~
     * marks, or a yes/no prompt), input as it is read, pictures that fail
     * to load, the party's square and facing as "X,Y,DIR" after it moves
     * or turns, and commands of the adventure loop and camp that are not
     * ported, the name of each character WHO picks, the rows of spell
     * lists, quitting to DOS, the monsters loaded, the encounter's sprite
     * and money robbed, the end of a fight, and the coins and items
     * TREASURE adds: kind is "print", "menu", "list", "item", "heading",
     * "choice", "input", "error", "at", "unported", "who", "quit",
     * "monster", "combat" or "treasure". */
    void (*log)(cok_adventure *game, const char *kind, const char *text, void *context);
    /* A battle has been set up (3cb2:1c58): game->combat holds its map and
     * combatants. NULL ignores it. */
    void (*battlefield)(cok_adventure *game, void *context);
    /* Before each instruction, as cok_ecl_hooks.trace. */
    void (*trace)(cok_adventure *game, void *context);
    /* Wait ms milliseconds (Crt.Delay): after each printed character, speed
     * * 3, and for DELAY, speed * 100. NULL does not wait. */
    void (*delay)(cok_adventure *game, unsigned ms, void *context);
    /* Whether a key is waiting, without reading it (1614:03c2, KeyPressed),
     * which resting polls. NULL never has one. */
    bool (*key_pending)(cok_adventure *game, void *context);
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
    /* DS:6d8a: for wall sets 1-3, the WALLDEF record and the slot it was
     * loaded at, or -1 and -1; (0, 1) and none at startup (3e99:005b). */
    int16_t wall_ids[3], wall_slots[3];

    /* The party (DS:609a). The selected character is vm.character
     * (DS:6096), which must be a member's record or NULL. */
    cok_party party;
    cok_item_types item_types; /* DS:5886, from ITEMS. */
    /* The party's spell effects: the rolls they change and their timers. */
    cok_effects effects;

    /* The small picture (DS:6da2): frames from PIC<file>.DAX, each with its
     * delay in hundredths of a second, drawn at cell 3, 3. */
    cok_picture frames[COK_ADVENTURE_FRAMES];
    uint32_t delays[COK_ADVENTURE_FRAMES];
    size_t frame_count;
    size_t frame;          /* DS:6da3, 0-based here. */
    uint8_t picture_id;    /* DS:6e00, COK_ADVENTURE_NO_PICTURE if none. */
    uint8_t picture_file;
    bool picture_sprite;   /* DS:6dee: the slot holds a SPRIT record, not a PIC. */
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
    uint8_t pictures;      /* DS:4b4d, Alter's "Pics", 1 at startup; nothing else reads it. */
    uint8_t selected;      /* DS:6e0f, the menu item selected. */
    int list_top;          /* DS:6e0d, the first item a list shows. */
    bool input_ended;      /* The keyboard ran out; the run was aborted. */

    /* Camp (see camp.h). */
    uint16_t rest[7];      /* DS:7120: the time left to rest, laid out as the clock. */
    uint16_t heal_ticks;   /* DS:7136: five minutes rested toward a day's healing. */
    uint8_t learn_ticks[COK_PARTY_MAX]; /* DS:712e: by position, ticks to the next spell. */
    bool resting;          /* DS:7138. */
    bool spells_changed;   /* DS:7142: a granted power was memorized; redraw the list. */
    uint8_t bonus_spells;  /* DS:7144: magic-user spells the moons allow beyond a day's. */
    uint16_t rest_ticks;   /* DS:4b53: five minutes rested toward an encounter roll. */
    char save_dir[512];    /* Where Save writes (DS:5784); empty for nowhere. */
    uint8_t *spell_target; /* DS:710b: the last member a spell was cast on, or NULL. */
    uint8_t *trade_partner; /* DS:46b0: whom View's trades start from. */
    uint8_t scroll_spells; /* DS:4838: the spells of scroll lists, since Scribe's last. */
    bool quit;             /* The player quit to DOS (1614:0000); the run was aborted. */

    /* Monsters and encounters (see monster.h). */
    uint8_t monsters;      /* DS:43be: records LOAD MONSTER added since CLEARMONSTERS. */
    uint8_t icon_slot;     /* DS:72eb: the icon slot of the next LOAD MONSTER. */
    bool monsters_loaded;  /* DS:8851: COMBAT fights. */
    uint8_t undead;        /* DS:8859: LOAD MONSTERs of undead (+0xda), for Turn. */
    /* DS:6172: each slot's two combat icons, ready and attacking, from
     * CPIC<file> records N and N + 0x80 (6d21:01d0); nothing draws them yet. */
    cok_picture icons[COK_ICON_SLOTS][2];
    uint8_t sprite_id;     /* DS:72e9: the SPRIT<file> record of the encounter. */
    uint8_t closeup_id;    /* DS:72ea: the PIC<file> record shown at distance 0. */
    bool sprite_loaded;    /* DS:8830: the sprite is in the small picture slot. */
    bool closeup_shown;    /* DS:8831: the close-up picture replaced it. */
    bool sprite_shown;     /* DS:884d: a sprite is drawn over the view. */
    bool in_encounter;     /* DS:8853: ENCOUNTER MENU runs. */
    uint8_t closeup_head;  /* DS:8854: 0x7ee1 when the close-up was shown. */
    cok_pool pool;
    /* DS:609e: the weapons lost in combat (effect 0x43, 3f44:1dc5), in list
     * order, given back at its end. Nothing fills it yet. */
    cok_lost_weapon *lost_weapons;
    size_t lost_weapon_count;
    /* DS:8840: the experience each character got at the end of the last
     * combat that worked it out (351b:0037), which the results show again
     * when one does not; false until one has. */
    int32_t experience;
    bool experience_known;
    cok_combat_stub combat_stub;
    /* DS:883c: a shop or the temple redraws its frame (see shop.h); clear
     * for its first redraw. */
    bool shop_frame;
    /* The battle's map, combatants and tile set (see combat.h). */
    cok_combat combat;
    bool restoring;        /* The block's vectors run; DS:43bf is restored after them. */

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
 * into larger units and moving the moons on each new day (57e4:0549), then
 * count down the party's spell effects (57e4:0171). Returns false with
 * game->error set, logged as "error", and game->vm.status set to
 * COK_ECL_EFFECT_FAILED if an effect that ends needs what is not ported
 * (see effect.h). */
bool cok_adventure_pass_time(cok_adventure *game, unsigned unit, unsigned count);

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

/* For the camp and other menus outside the scripts. */

/* Log text of kind (see cok_adventure_hooks.log). */
void cok_adventure_log(cok_adventure *game, const char *kind, const char *text);
/* The keyboard, which ends the run (game->vm.abort, game->input_ended) when
 * keys run out. */
cok_keyboard cok_adventure_keyboard(cok_adventure *game);
/* Wait ms milliseconds through hooks.delay. */
void cok_adventure_wait(cok_adventure *game, unsigned ms);
/* Whether a key is waiting (hooks.key_pending, 1614:03c2). */
bool cok_adventure_key_pending(cok_adventure *game);
/* Carry each full unit of clock into the next once (57e4:0459), moving
 * the moons on a new day and aging the party while the years are full, as
 * cok_adventure_pass_time does. */
void cok_adventure_carry(cok_adventure *game, uint16_t clock[7]);
/* Redraw the screen for the mode (6346:2c17): in camp the frame, the party
 * list and status line, and PIC record 0x3b loaded as the small picture;
 * for treasure (mode 6) PIC record 0x3c and no status line; in shops and
 * the temple (mode 1) the frame unless game->shop_frame is clear, the
 * small picture's first frame outside 3D areas (in them the portrait last
 * shown, logged as unported), the party list and the status line. */
void cok_adventure_redraw(cok_adventure *game);
/* Load PIC<file> record id as the small picture unless it is loaded
 * (6961:00e4), logging an error if it fails; and draw its current frame at
 * cell 3, 3, as the camp's menus do while they wait (6961:000a). */
void cok_adventure_load_picture(cok_adventure *game, uint8_t id);
/* Load SPRIT<file> record id into the small picture slot unless it is
 * there (6961:00e4 with mode 1): every group, colour 0 transparent and 13
 * drawn black, as cok_picture_load_sprite loads them; not for 0xff. A
 * record that cannot be had logs an error and leaves the slot empty. */
void cok_adventure_load_sprite(cok_adventure *game, uint8_t id);
/* Free the small picture and load BIGPIC<file> record id as the big
 * picture unless it is loaded (6961:07ed). A record that is not there
 * leaves no big picture, silently, as in the original; a missing file
 * logs an error. */
void cok_adventure_load_big(cok_adventure *game, uint8_t id);
/* Draw group frame 1-3 of the sprite masked over the view (6961:072e), at
 * its header's x and y + 2, which the view's buffer puts at cells x + 3,
 * y + 3; nothing for a group not loaded. */
void cok_adventure_draw_sprite(cok_adventure *game, unsigned frame);
void cok_adventure_show_picture(cok_adventure *game);
/* Free the small picture and forget it (6961:0537), as combat setup does
 * (3cb2:1c58), so that the next load reads it again. */
void cok_adventure_free_picture(cok_adventure *game);
/* Draw frame (from 0) of the small picture at cell 3, 3 (6961:000a). */
void cok_adventure_show_frame(cok_adventure *game, size_t frame);
/* A menu of the ECL opcodes on row 24 (3775:1885): prompt in light
 * magenta, items in normal with the hotkeys and the selection in white;
 * special keys pick a character and redraw the party list. Logs the items
 * as "menu". Returns the item picked, or -1 if input ended. */
int cok_adventure_horizontal(cok_adventure *game, const char *prompt, const char *items,
                             uint8_t normal, bool enter_returns);
/* Type text in the text window (rows 17-22) from game->vm.cursor in fg on
 * 0, a character at a time with the text delay, paging as PRINT does
 * (1521:04ac with DS:4b59 set). Logs it as "print". */
void cok_adventure_type(cok_adventure *game, const char *text, uint8_t fg, bool clear);
/* Pick a member with prompt and "Select", and "Exit" if exit_item, which
 * picks none, starting from who (6346:32c7): the party list shows the
 * pick, and up and down (8 and 2) move it; in camp the small picture shows
 * too. Returns the pick; *ended is set when input ended. */
uint8_t *cok_adventure_pick(cok_adventure *game, const char *prompt, uint8_t *who, bool exit_item,
                           bool *ended);
/* Print text wrapped in window from game->vm.cursor in fg on 0, paging as
 * PRINT does, without a delay between characters (1521:04ac). */
void cok_adventure_print(cok_adventure *game, const char *text, cok_text_window window,
                         uint8_t fg, bool clear);
/* Set game->error, log it, set game->vm.status to status and end the run. */
void cok_adventure_fail(cok_adventure *game, cok_ecl_status status, const char *format, ...);

/* Read a record by id from <name><file>.DAX in the asset directory, as
 * 169c:088e does; the first record with the id wins. Returns a malloc'd
 * buffer, or NULL with game->error set. */
uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size);
/* Read a record by id from <name>.DAX, a full name such as "MON1CHA",
 * also saying whether the archive itself could not be opened, where the
 * original asks for the disk and waits. */
uint8_t *cok_adventure_find_record(cok_adventure *game, const char *name, uint8_t id,
                                   size_t *size, bool *no_file);
/* Load the record of one image or one group of frames, id, from
 * <name>.DAX (a full name such as "CPIC1") into picture (127f:0111), with
 * transparent as for cok_picture_load. The old picture is freed first, so
 * it is left empty when the record cannot be had. Returns false with
 * game->error set. */
bool cok_adventure_load_image(cok_adventure *game, const char *name, uint8_t id, int transparent,
                              cok_picture *picture);
/* Show text on row 24 in white and wait for a key (1521:096c). */
void cok_adventure_prompt_key(cok_adventure *game, const char *text);

#endif
