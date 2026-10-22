#ifndef COK_ADVENTURE_H
#define COK_ADVENTURE_H

#include "combat.h"
#include "config.h"
#include "debug.h"
#include "ecl.h"
#include "effect.h"
#include "input.h"
#include "menu.h"
#include "party.h"
#include "picture.h"
#include "rules.h"
#include "sound.h"
#include "text.h"
#include "view.h"

#include <assert.h>
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
    COK_BATTLE_SP = 0x3dd4, /* SP at 3995:0172 for a script 2fd3:3c28 runs. */
    /* How much deeper PROGRAM 9's camp runs its vectors than the script
     * running PROGRAM (2fd3:3add's SP at the inner run, from 3403, against
     * the outer, through 386d and 3473), as the emulator measured it. */
    COK_PROGRAM_CAMP_DEPTH = 0x128,
    /* The icon slots a game may have (its startup.icon_slots): Champions' 26
     * (DS:6172: 0-7 the party's, 8 on monsters', 13-25 missiles), Death
     * Knights' 35 (dkk DS:b474, missiles 13-34). */
    COK_ICON_SLOTS_MAX = 35,
    COK_MONSTERS_MAX = 63, /* DS:43be: records LOAD MONSTER adds between CLEARMONSTERS. */
    COK_COINS = 7,
    /* The item table (a game's treasure.table, dkk DS:a2ca): entries of an
     * item's +0x2e-+0x3e, ids 1-254 (DS:a2b9 + 17 * id). */
    COK_ITEM_ENTRY = 17,
    COK_ITEM_ENTRIES = 255,
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

/* How eclplay's --combat resolves a battle, or plays the player's turns
 * (3995:0573, command.h). */
typedef enum {
    COK_COMBAT_UNPORTED, /* The rounds: the computer plays, the player's turns pass. */
    COK_COMBAT_WON,      /* Every record against the party (+0x18a 1) drops. */
    COK_COMBAT_FLED,     /* The whole party flees. */
    COK_COMBAT_LOST,     /* The whole party dies. */
    COK_COMBAT_GODS,     /* The original's Helm cheat (432f:41e2) ends it. */
    COK_COMBAT_MELEE,    /* Every turn attacks the nearest enemy (cok_combat_melee). */
    COK_COMBAT_AUTO,     /* The player's first turn presses Alt-Q: the computer plays all. */
    COK_COMBAT_PASS,     /* Every turn passes, the computer's too. */
    COK_COMBAT_PLAYER,   /* The rounds: the player's commands read the keyboard. */
} cok_combat_stub;

typedef struct {
    /* An opcode not carried out here, with its operands decoded in
     * game->vm. NULL ignores it. */
    void (*unported)(cok_adventure *game, void *context);
    /* Text as it is printed, menus as they are shown (items with their ~
     * marks, or a yes/no prompt), input as it is read, pictures that fail
     * to load, the party's square and facing as "X,Y,DIR" after it moves
     * or turns, its place on the overland map likewise after each step
     * there, and commands of the adventure loop and camp that are not
     * ported, the name of each character WHO picks, the rows of spell
     * lists, quitting to DOS, the monsters loaded, the encounter's sprite
     * and money robbed, the end of a fight, and the coins and items
     * TREASURE adds, the overhead map going on and off, the characters ADD
     * NPC and DUMP add and remove or that join from the roster, one saved
     * to the roster, the sound driver's commands, and the debug mode's
     * switching, traces and View (see debug.h) and the lines it writes to
     * DEBUG.TXT: kind is "print", "menu", "list", "item", "heading",
     * "choice", "input", "error", "at", "overland", "unported", "who",
     * "quit", "monster", "combat", "treasure", "area", "party", "roster",
     * "sound", "debug" or "debug.txt". */
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
    /* Discard the keys waiting (1614:045c), as the menus do once they have
     * their key, paging does after its key, and the title before and after
     * each wait. NULL keeps them. */
    void (*flush)(cok_adventure *game, void *context);
    /* The sound driver's calls, as the original makes them (see sound.h
     * and DEVELOPERS.md's Sound): its commands (17e8:0020, those logged as
     * "sound" and the ones that are not: a step, a turn, a DAX record
     * read), its install and the uninstall around each overlay's load, the
     * title's music, Ctrl-S read by 1614:025b, quitting. A front end
     * passes them to its cok_sound. NULL ignores them. */
    void (*sound)(cok_adventure *game, cok_sound_call call, unsigned value, void *context);
    /* The game polls the mouse or the joystick in a loop (waiting for the
     * buttons to come up after a menu's key, 67b5:0c2d; the joystick's
     * calibration): let up to ms pass, or less if they change. NULL lets
     * nothing pass. */
    void (*device_wait)(cok_adventure *game, unsigned ms, void *context);
    void *context;
} cok_adventure_hooks;

struct cok_adventure {
    cok_ecl vm;
    /* The game played (rules.h): its files' names and what else differs
     * between the games. cok_adventure_open sets it before any file is
     * read. */
    const cok_rules *rules;
    char assets[512];      /* Directory holding the DAX files. */
    /* DS:4b76, KRYNN.CFG's display (11c7:031a). In CGA mode the screen and
     * every picture hold CGA colours (cok_picture.cga); EGA mode draws as
     * the Tandy's. */
    cok_display display;
    cok_picture screen;    /* 40 units by 200 rows. */
    cok_font font;         /* 8X8D1.DAX record 201. */
    /* The 3D view: map, wall sets, tile sets (set 4, 8X8D1.DAX record 202,
     * is the frame's; see screen.h) and sky pictures. */
    cok_view view;
    /* DS:6d8a: for wall sets 1-3, the WALLDEF record and the slot it was
     * loaded at, or -1 and -1; (0, 1) and none at startup (3e99:005b). By
     * slot from the game's first (rules->view.first_slot; dkk DS:c078 has
     * slots 0-3, Champions' saves 1-3). */
    int16_t wall_ids[COK_VIEW_WALL_SLOTS], wall_slots[COK_VIEW_WALL_SLOTS];

    /* The party (DS:609a). The selected character is vm.character
     * (DS:6096), which must be a member's record or NULL. */
    cok_party party;
    cok_item_types item_types; /* DS:5886, from ITEMS. */
    /* The item table (dkk DS:a2ca, 4f9a:0006), entry id at [id], ids 1-254;
     * entry 0 unused. All 0xff where the game has none. */
    uint8_t item_table[COK_ITEM_ENTRIES][COK_ITEM_ENTRY];
    /* The party's spell effects: the rolls they change and their timers. */
    cok_effects effects;

    /* The small picture (DS:6da2): frames from PIC<file>.DAX, each with its
     * delay in hundredths of a second, drawn at cell 3, 3. */
    cok_picture frames[COK_ADVENTURE_FRAMES];
    uint32_t delays[COK_ADVENTURE_FRAMES];
    size_t frame_count;
    size_t frame;          /* DS:6da3, 0-based here. */
    bool frame_unset;      /* DS:6da3 0: the slot freed, nothing loaded since. */
    uint8_t picture_id;    /* DS:6e00, COK_ADVENTURE_NO_PICTURE if none. */
    bool picture_sprite;   /* DS:6dee: the slot holds a SPRIT record, not a PIC. */
    cok_picture big;       /* BIGPIC<file>.DAX, drawn at cell 1, 1 (DS:6e02). */
    /* The portrait (6961:05b9): a head from HEAD<file>.DAX and a body from
     * BODY<file>.DAX (DS:6de5, 6dea), and the records they hold (DS:6de4,
     * 6de9), 0xff for none, as at startup (3e99:0477) and once a step or
     * combat setup has freed them; and the portrait last shown (DS:4b55, 4b56), which shops in 3D
     * areas show again. */
    cok_picture head, body;
    uint8_t head_id, body_id;
    uint8_t portrait_head, portrait_body;
    /* The overland map's mark of the party (4877:0005): CURSOR.DAX record
     * 1, loaded at startup with colour 13 transparent (DS:8965), and the
     * screen cell it covers, saved when it is drawn and put back before
     * the party moves (DS:6162, one unit by eight rows, zeroed at
     * startup). */
    cok_picture cursor, under;
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
    bool overhead;         /* DS:6d84: the view shows the overhead map (Area). */
    bool helm;             /* Started with Helm: ParamStr(2) is DS:896a's string. */
    bool woof;             /* Started with Woof: ParamStr(1) is 1614:024c's 'Woof', and
                            * 1614:025b takes the debug keys (see debug.h). */
    bool debug;            /* DS:4b51, the debug flag (Ctrl-D). */
    cok_debug_file debug_file; /* DEBUG.TXT, DS:8864. */
    uint8_t sky;           /* DS:6d80: the view's sky colour, as 6945:00ba last set it. */
    /* DS:6d81-6d83: the backdrop's line, horizon and ground below the sky,
     * which 6945:00ba sets in CGA mode only (6d83 8 in the others). */
    uint8_t backdrop[3];
    bool text_shown;       /* DS:884e clear: text to clear after a command. */
    bool door_tries[3];    /* DS:7146-7148: Bash, Pick and Knock may be tried. */
    bool party_killed;     /* DAMAGE left no character able to act. */

    uint8_t speed;         /* DS:4b38, the game speed; 4 by default. */
    /* DS:4b59: running text is typed a character at a time, the speed's
     * delay after each (see cok_adventure_wrap). Set at startup and for a
     * new game (3e99:005b, 0843), by PRINT while it prints (2fd3:0acf), by
     * ENCOUNTER MENU while it runs (2fd3:23e5) and at the end of a battle
     * (3995:0297, 351b:1b00); cleared after PRINT and ENCOUNTER MENU and by
     * combat setup (3cb2:1c5e). */
    bool text_delay;
    bool animate;          /* DS:4b4f: load every frame of a picture. */
    uint8_t pictures;      /* DS:4b4d, Alter's "Pics", 1 at startup; nothing else reads it. */
    uint8_t selected;      /* DS:6e0f, the menu item selected. */
    /* dkk DS:c0f8: whether the last menu by index (6a69:0f1b, the lists,
     * the Yes/No and the pick that read it) took a special key; the
     * temple's View takes it for its flag (see shop.h). */
    bool menu_special;
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
    /* Where the game imports another's roster and saved games from (dkk
     * DS:99a4, DKK.CFG's fifth line: Champions'); empty for nowhere. */
    char import_dir[512];
    /* Whose files the last Save, Load, Add or roster write meant (dkk
     * DS:c468: own_files 1, the imported game's 0), set in a game that
     * imports; own_files_set clear until then: DS:c468 lies past the
     * image EXEPACK unpacks (0x141a paragraphs, DS from 0xbce: its data
     * ends at DS:84c0) and System's startup (1a07:0000) clears none. */
    bool own_files, own_files_set;
    /* KRYNN.CFG, which Initialize Mouse/Joystick reads (config_path) and
     * writes (config_write; see cok_config_find); empty for none. */
    char config_path[1024], config_write[1024];
    uint8_t *spell_target; /* DS:710b: the last member a spell was cast on, or NULL. */
    /* DS:6fea, 6feb: the targets of the spell being cast or the last one,
     * spell_targets[1] to [spell_target_count] (DS:6fe7 + 4i), NULL where a
     * handler took one off. Nothing clears them: entries past the count
     * keep earlier casts' records, freed ones too, which 6beb:0e08 can
     * read (see arena.h). DS:710f, 7110: the cell a spell in combat aims
     * at. */
    cok_character *spell_targets[COK_PARTY_RECORDS + 1];
    uint8_t spell_target_count;
    int8_t spell_x, spell_y;
    uint8_t *trade_partner; /* DS:46b0: whom View's trades start from. */
    uint8_t scroll_spells; /* DS:4838: the spells of scroll lists, since Scribe's last. */
    bool quit;             /* The player quit to DOS (1614:0000); the run was aborted. */
    /* The vault (a game whose rules' saves.vault names one): the letter of
     * the saved game its file is, 'Q' (none) at startup and after a game
     * (dkk DS:fb79); and whether its menu left it in saves.vault_temp,
     * which the next save moves (DS:fb78). */
    char vault_letter;
    bool vault_temp;
    /* The start menu emptied the party and ended the game (dkk DS:8c5b):
     * the adventure starts over from a new game's block once it returns. */
    bool new_game;

    /* Monsters and encounters (see monster.h). */
    uint8_t monsters;      /* DS:43be: records LOAD MONSTER added since CLEARMONSTERS. */
    uint8_t icon_slot;     /* DS:72eb: the icon slot of the next LOAD MONSTER. */
    bool monsters_loaded;  /* DS:8851: COMBAT fights. */
    uint8_t undead;        /* DS:8859: LOAD MONSTERs of undead (+0xda), for Turn. */
    /* DS:6172: each slot's two combat icons, ready and attacking, from
     * CPIC<file> records N and N + 0x80 (6d21:01d0), the game's
     * startup.icon_slots of them; nothing draws them yet. */
    cok_picture icons[COK_ICON_SLOTS_MAX][2];
    uint8_t sprite_id;     /* DS:72e9: the SPRIT<file> record of the encounter. */
    uint8_t closeup_id;    /* DS:72ea: the PIC<file> record shown at distance 0. */
    bool sprite_loaded;    /* DS:8830: the sprite is in the small picture slot. */
    bool closeup_shown;    /* DS:8831: the close-up picture replaced it. */
    bool sprite_shown;     /* DS:884d: a sprite is drawn over the view. */
    bool in_encounter;     /* DS:8853: ENCOUNTER MENU runs. */
    uint8_t closeup_head;  /* DS:8854: 0x7ee1 when the close-up was shown. */
    /* dkk DS:c4cc: SETUP MONSTERS' fourth operand, the directions an
     * ambush comes from (bits 0-3, a quadrant each); a game whose SETUP
     * MONSTER has none never sets it. */
    uint8_t ambush;
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
    /* DS:883c (dkk db62): a shop or the temple redraws its frame (see
     * shop.h); clear for its first redraw. */
    bool shop_frame;
    /* The battle's map, combatants and tile set (see combat.h). */
    cok_combat combat;
    /* DS:6e3a: spells pick their targets with the combat routine
     * (432f:2337, see cast.h) rather than 5b04:127e; set while a battle
     * runs (3995:0172 to 004b). */
    bool combat_targets;
    bool restoring;        /* The block's vectors run; DS:43bf is restored after them. */
    /* The stack pointer 3995:0172 starts a battle at, which the path the
     * script was run by fixes (from the MZ header's 0x4000, main's frame
     * and the calls on the way): COK_BATTLE_SP for a vector 2fd3:3c28 runs
     * (an emulator booting the game measured it in the demonstration's
     * battle), 6 less through 3b47 (entering a block), 8 less through 3403
     * (Encamp in the adventure loop), and COK_PROGRAM_CAMP_DEPTH less than
     * the script's for PROGRAM 9's camp, in the middle of a script. A
     * dragon's cone reads a byte of the stack below it (see
     * cok_cast_breath). */
    uint16_t battle_sp;

    /* The start menu (see start.h). */
    bool demo;             /* DS:4b4b: the demonstration plays. */
    bool free_training;    /* DS:7140: Helm's free training. */
    bool timed;            /* A menu that times out reads keys (DS:6e11 set). */
    uint16_t timeout;      /* DS:6e11: how long it waits, in hundredths of a second. */
    /* 67b5:03e2's flag ([bp+0xa]): the menu waiting for a key animates the
     * small picture, each frame shown for its delay * 10 hundredths, as the
     * camp's, treasure's and the scripts' menus over a picture do. The port
     * sets it while such a menu reads; a front end that waits does the
     * animating (cok_adventure_show_frame with game->frame). */
    bool menu_animates;
    bool title;            /* The title or its menu shows. */
    /* A record outside the list that the selection (DS:6096) may name: the
     * character being created (4def:06dd), or NULL. */
    cok_character *outside;
    /* DS:5885 is game->effects.rolls.saved; DS:4b52 is vm.keep_vars. */

    /* The mouse and the joystick (input.h, src/devices.c). mouse.driver is
     * the machine's driver, which the front end installs and moves; port
     * its game port. pointer is what the menus see: the click (DS:8987,
     * 8989, 898b) and the list's columns and rows. */
    cok_mouse mouse;
    cok_stick stick;
    cok_game_port port;
    cok_pointer pointer;
    /* While the game waits for a key with the mouse or the joystick on
     * (1614:01ad, 00fa), which: a front end's read then gives
     * COK_KEY_DEVICE once cok_adventure_device_ready says so. */
    enum { COK_WAIT_KEYS, COK_WAIT_MOUSE, COK_WAIT_STICK } device_wait;
    /* DS:71a5: combat's aiming cursor is up, at map cell DS:71a3, 71a4
     * (432f:33ce); DS:71a6: combat's Move runs (3995:0d9c). */
    bool aiming, combat_moving;
    int8_t aim_x, aim_y;
    /* DS:7141: Modify's sheet is up (4def:28fa); DS:6e35: the field a
     * click picked, 0-5 the scores, 6 the name, 7 the hit points, 0xff
     * none, as Modify sets it before its sheet (4def:29d8). */
    bool modifying;
    uint8_t modify_field;
    /* DS:6e31-6e34: the party list's columns and rows as last drawn
     * (6346:07ba); 0 before it is first drawn, as fresh memory holds. */
    uint8_t party_left, party_right, party_top, party_bottom;
    /* DS:6e08: a click on the party list picks into DS:6e09 (pick), not
     * the selection (6346:32c7). */
    bool pick_into;
    uint8_t *pick;

    cok_keyboard keys;
    cok_adventure_hooks hooks;
    char error[600];       /* Why the last call failed. */
};

/* The game's rules (game->rules), which every read of them goes through:
 * a game set up without them (cok_adventure_open names them) stops here,
 * on whatever path reaches them first, rather than reading through NULL. */
static inline const cok_rules *cok_game_rules(const cok_adventure *game)
{
    assert(game->rules != NULL);
    return game->rules;
}

/* The game's icon slots (cok_rules_startup's icon_slots). */
static inline unsigned cok_icon_slots(const cok_adventure *game)
{
    return cok_game_rules(game)->startup.icon_slots;
}

/* A named effect's or spell's id in the game's rules (spells.h,
 * cok_effect_id and cok_spell_id). */
static inline uint8_t cok_game_effect(const cok_adventure *game, cok_named_effect name)
{
    return cok_effect_id(cok_game_rules(game), name);
}

static inline uint8_t cok_game_spell(const cok_adventure *game, cok_named_spell name)
{
    return cok_spell_id(cok_game_rules(game), name);
}

/* Load the font and item types from assets and set up the VM with this module's hooks,
 * for the game rules describes (never NULL: every caller names the game).
 * keys and hooks may be NULL. Returns false with game->error set. Free with
 * cok_adventure_close even on failure, which also frees the party and
 * closes DEBUG.TXT (cok_debug_end). */
bool cok_adventure_open(cok_adventure *game, const cok_rules *rules, const char *assets,
                        const cok_keyboard *keys, const cok_adventure_hooks *hooks);
/* As cok_adventure_open, in the display mode given (cok_adventure_open's
 * is the Tandy's). */
bool cok_adventure_open_display(cok_adventure *game, const cok_rules *rules, const char *assets,
                                cok_display display, const cok_keyboard *keys,
                                const cok_adventure_hooks *hooks);
/* cok_picture_create for the game's display: CGA pictures in CGA mode.
 * Every picture the game makes holds its mode's colours, so all are made
 * so, though only fills, text, recolouring and exports read the flag (the
 * combat buffer, its tiles and the overland mark's cell take none). */
cok_picture_status cok_adventure_create_picture(const cok_adventure *game, cok_picture *picture,
                                                size_t units, size_t height, size_t frames,
                                                int masked);
void cok_adventure_close(cok_adventure *game);

/* Load ECL block from ECL<game->vm.file>.DAX and start it (3775:0361,
 * 3775:01e8), resetting picture state as the original does. */
cok_ecl_status cok_adventure_load(cok_adventure *game, uint8_t block);

/* Load block and start it as 2fd3:3c28 does, for a new game or a loaded
 * one: run only its load vector (2fd3:3cc7), entering the next block as
 * 2fd3:3b47 does if that runs NEWECL. The after-move and location vectors
 * wait for the first command (cok_adventure_play), and the last mode
 * (DS:4b4a) stays as it was unless a NEWECL ran. */
cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block);

/* Load block and enter it as 2fd3:3b47 enters a block NEWECL names, for
 * eclplay's block: run the load vector, show the view if the block loaded
 * its files or stays in 3D, then run the after-move and location vectors,
 * the first at the stack depth of 3c28's own, and start over from the load
 * vector whenever NEWECL switches blocks. */
cok_ecl_status cok_adventure_enter_block(cok_adventure *game, uint8_t block);

/* Run the adventure loop of 2fd3:3c28 in the block entered: until input
 * ends or a run fails, take a command from the adventure menu (475c:09ec),
 * run the after-move vector, take a step (475c:0e77), show the view and run
 * the location vector. NEWECL enters the new block as 2fd3:3b47 does
 * (cok_adventure_enter_block's loop). In 3D areas the party walks the map; outside them (mode 3) it
 * travels the overland map while 0x4cf7 is set (475c:08d5), and without
 * it the original's command is unset: the run stops with
 * COK_ECL_UNDEFINED. Clears game->vm.abort on return, as the original
 * clears DS:4b57. */
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

/* Draw the game's frame of screen (screen.h) with the moons' phases
 * (0x4cf9-0x4cfb). */
void cok_adventure_screen(cok_adventure *game, cok_screen screen);

/* Show the 3D view from the party's square, or the overhead map while
 * game->overhead is set, which an area hiding the square (0x4bfb) turns
 * off first; or with no 3D view in the area (0x4be6 and 0x4c38 both 0) the
 * big picture if game->redraw is set (6945:00ba). Sets the party's square
 * (0xc04f) and clears game->redraw. Once the party is destroyed
 * (game->party_killed, DS:4b57) it does nothing. */
void cok_adventure_view(cok_adventure *game);

/* Add the characters of a saved game to the party, as 4b6d:1b34 does: for
 * each name it holds, the character files of that name (see
 * cok_party_file_name) in the saved game's directory, skipping names with
 * no .SAV file. Each is counted in 0x7f3e, and the first becomes the
 * selected character. Returns false with game->error set if the saved
 * game or a character cannot be read, or the party is full. */
bool cok_adventure_load_party(cok_adventure *game, const char *path);

/* Add the characters in files paths[0..count) to the party as
 * cok_adventure_load_party adds a saved game's: each a record file
 * (DIR/BASE.EXT: a saved game's .SAV, the roster's .WHO, Death Knights'
 * .GAM or .PC) with its other files beside it (cok_character_read_file),
 * then their combat icons; the first becomes the selected character.
 * Returns false with game->error set if one cannot be read or the party
 * is full. */
bool cok_adventure_add_characters(cok_adventure *game, const char *const *paths, size_t count);

/* Load a saved game (4b6d:1b34; dkk 46b2:1ef0), in the game's layout: the
 * variables, the party's square and facing, the modes and wall sets, the
 * speed (0x4bfc) and animation (0x4bff), then the party as
 * cok_adventure_load_party does, and the ECL file from 0x7f12. In a 3D
 * area it reloads the map (if wall set 1's record was above 0) and the
 * wall sets the layout restores; outside them it loads the overland map,
 * big picture 0x79 (dkk 0x70), without drawing it. The last mode becomes
 * the saved mode, or stays the saved last mode where the layout keeps it;
 * a game with a vault takes the saved game's letter's (DS:fb79). The next
 * block entered keeps the variables NEWECL would clear (DS:4b52), and the
 * adventure loop then redraws the screen. Play resumes from block 0x4bf2,
 * or the game's first block when that is 0 (2fd3:3c28). */
bool cok_adventure_restore(cok_adventure *game, const char *path);

/* Load another game's saved game, as a game that imports its characters
 * does (dkk 46b2:1ef0 with DS:c468 0, Champions' SAVGAM<letter>.DAT): of
 * the file, the party's names, read as the other game's layout, and the
 * savings variable (COK_VAR_SAVINGS: Champions' bank at 0x4c33); each
 * character's record file (the other game's extension) imported
 * (cok_character_import) and joined unless it is an NPC (which it never
 * is: the import leaves the flag 0), counted in
 * 0x7f3e; then their combat icons, the first selected, the mode 0. The
 * other variables, the square, the modes and the walls stay as they are,
 * and the first block clears its variables as a new game's does. Returns
 * false with game->error set. */
bool cok_adventure_import_game(cok_adventure *game, const char *path);

/* Turn the overhead map off (DS:6d84) and, if it was on, show the view or
 * the big picture again (6945:00ba with DS:713a set), as the first sprite
 * of an encounter does (3775:0575). */
void cok_adventure_overhead_off(cok_adventure *game);

/* Mark the party on the overland map (4877:0005): save the screen cell at
 * the party's place there, 0x4bc3 + 1 across and 0x4bc4 + 1 down, in
 * game->under, then draw the cursor over it. A cell off the screen, which
 * the original reads and writes outside its rows, stops the run with
 * COK_ECL_UNDEFINED. */
void cok_adventure_mark(cok_adventure *game);

/* Travel a square on the overland map (475c:08d5): put back the cell the
 * mark covered (4877:00d6), keep the party's place in 0x4bf0 and 0x4bf1,
 * step the way it faces (DS:1ed6, DS:1edf), within 0-37 across and 0-14
 * down, log it as "overland", mark it there and pass twelve hours. The step
 * is added to the low byte of each word. A cell off the screen stops the
 * run as cok_adventure_mark does. */
void cok_adventure_travel(cok_adventure *game);

/* The selected character (DS:6096): the member whose record vm.character
 * is, or game->outside's; NULL for none. */
cok_character *cok_adventure_selected(cok_adventure *game);

/* Draw the party list beside the view (6346:07ba), unless the area has no
 * 3D view or a big picture is shown. */
void cok_adventure_party(cok_adventure *game);

/* Draw the status line on row 15 (6346:2d75): the party's square unless the
 * overhead map is on (0x4bfb), its facing, the time, and "search" while
 * searching. Not drawn outside 3D areas. */
void cok_adventure_status(cok_adventure *game);

/* For the camp and other menus outside the scripts. */

/* Show text on row 24 in colour fg for speed * 100 ms, then clear the row
 * (1521:0af1; 6346:1827 with colour 10). */
void cok_adventure_notice(cok_adventure *game, const char *text, uint8_t fg);
/* Log text of kind (see cok_adventure_hooks.log). */
void cok_adventure_log(cok_adventure *game, const char *kind, const char *text);
/* The keyboard, which ends the run (game->vm.abort, game->input_ended) when
 * keys run out. */
cok_keyboard cok_adventure_keyboard(cok_adventure *game);
/* Wait ms milliseconds through hooks.delay. */
void cok_adventure_wait(cok_adventure *game, unsigned ms);
/* Whether a key is waiting (hooks.key_pending, 1614:03c2). */
bool cok_adventure_key_pending(cok_adventure *game);
/* Discard the keys waiting (hooks.flush, 1614:045c). */
void cok_adventure_flush(cok_adventure *game);
/* "Loading...Please Wait" in light green on row 24, cleared first, as the
 * original shows it while it reads the disk (67b5:0c7b or 1521:030a, then
 * 1521:0353): loading a block (3775:0361), a small picture or sprite
 * with animation on (6961:00e4), at startup (3e99:005b), reading a
 * character or a saved game (4b6d:11e5, 1b34; roster.h). */
void cok_adventure_loading(cok_adventure *game);
/* Text input's keyboard: Crt.ReadKey's keys (1521:0739), which do not
 * turn the sound on and off as 1614:025b's do. */
cok_keyboard cok_adventure_keyboard_raw(cok_adventure *game);
/* A call to the sound driver (hooks.sound). */
void cok_adventure_sound(cok_adventure *game, cok_sound_call call, unsigned value);
/* The pair of calls around an overlay's load (if DS:577c, 17e8:0020(0xff);
 * then unless DS:577c, 17e8:00da), as main and COMBAT make them. */
void cok_adventure_overlay(cok_adventure *game);
/* Quit to DOS (1614:0000): the sound stopped, DEBUG.TXT closed while the
 * debug flag is set, game->quit and the run aborted. */
void cok_adventure_quit(cok_adventure *game);
/* A key read as 1614:025b reads it, without waiting for one first: in the
 * demonstration 0 at once when none is pending. */
int cok_adventure_wait_key(cok_adventure *game);
/* Carry each full unit of clock into the next once (57e4:0459), moving
 * the moons on a new day and aging the party while the years are full, as
 * cok_adventure_pass_time does. */
void cok_adventure_carry(cok_adventure *game, uint16_t clock[7]);
/* Redraw the screen for the mode (6346:2c17): in camp the frame, the party
 * list and status line, and PIC record 0x3b loaded as the small picture;
 * for treasure (mode 6) PIC record 0x3c and no status line; in shops and
 * the temple (mode 1) the frame unless game->shop_frame is clear, the
 * small picture's first frame outside 3D areas (in them the portrait last
 * shown, cok_adventure_portrait, where the shops section's portrait is
 * set), the party list and the status line; in a game's vault (mode 7,
 * dkk) the same and its line; in the party menu (mode 0) the cleared
 * frame of 1128:0000; in combat (mode 5) nothing. */
void cok_adventure_redraw(cok_adventure *game);
/* Load PIC<file> record id as the small picture unless it is loaded
 * (6961:00e4), with "Loading...Please Wait" while animation is on; a
 * record that is not there says "PIC not found" and waits for a key, a
 * file that is not there stops the run, and a record that cannot be
 * decoded logs an error and leaves the slot empty. */
void cok_adventure_load_picture(cok_adventure *game, uint8_t id);
/* Load SPRIT<file> record id into the small picture slot unless it is
 * there (6961:00e4 with mode 1): every group, colour 0 transparent and 13
 * drawn black, as cok_picture_load_sprite loads them; not for 0xff. As
 * cok_adventure_load_picture for the message and what is not there. */
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
/* While a menu waits for a key with game->menu_animates set (67b5:03e2):
 * whether the small picture animates, and then in *ms how long its current
 * frame stays, its delay * 10 hundredths of a second, at least one
 * hundredth (a delay of 0, which the original redraws as fast as its
 * loop runs, as every sprite's groups have). Nothing animates
 * where the slot was freed and nothing loaded since (DS:6da3 0) or the
 * picture has fewer than two frames. */
bool cok_adventure_menu_animates(const cok_adventure *game, uint32_t *ms);
/* The next frame of the small picture (DS:6da3 + 1, the first after the
 * last), drawn at cell 3, 3, as 67b5:03e2 shows it once the current one's
 * time is up. */
void cok_adventure_menu_frame(cok_adventure *game);
/* Show the portrait of head and body (3775:0538): keep them as the one
 * last shown (DS:4b55, 4b56), load them unless they are loaded
 * (6961:05b9) and draw them, the head at cell 3, 3 and the body at 3, 8
 * (6961:06bd); the view is not replaced (DS:884b). A record that is not
 * there says "head not found" on row 24 in yellow and waits for a key; a
 * file that is not there, where the original asks for its disk, fails and
 * ends the run. 0xff loads nothing and keeps what is loaded. */
void cok_adventure_portrait(cok_adventure *game, uint8_t head, uint8_t body);
/* Free the portrait's pictures, so that the next one shown loads them
 * again (DS:6de4 and 6de9 0xff), as a step (475c:0e77) and combat setup
 * (3cb2:1c58) do. */
void cok_adventure_forget_portrait(cok_adventure *game);
/* A menu of the ECL opcodes on row 24 (3775:1885): prompt in light
 * magenta, items in normal with the hotkeys and the selection in white;
 * special keys pick a character and redraw the party list. Logs the items
 * as "menu". Returns the item picked, or -1 if input ended. */
int cok_adventure_horizontal(cok_adventure *game, const char *prompt, const char *items,
                             uint8_t normal, bool enter_returns);
/* Pick a member with prompt and "Select", and "Exit" if exit_item, which
 * picks none, starting from who (6346:32c7): the party list shows the
 * pick, and up and down (8 and 2) move it; in camp the small picture shows
 * too. Returns the pick; *ended is set when input ended. */
uint8_t *cok_adventure_pick(cok_adventure *game, const char *prompt, uint8_t *who, bool exit_item,
                           bool *ended);
/* Print text wrapped in window from game->vm.cursor in fg on 0, paging as
 * PRINT does (1521:04ac), typed while game->text_delay is set. */
void cok_adventure_print(cok_adventure *game, const char *text, cok_text_window window,
                         uint8_t fg, bool clear);
/* A routine of the game's the port does not carry out for it yet, what it
 * does and the routine cited ("dkk 3e05:08af"): the run stops there with
 * COK_ECL_UNPORTED, "not yet ported: <game>'s <what> (<routine>)". Returns
 * false. */
bool cok_adventure_not_yet(cok_adventure *game, const char *what, const char *routine);
/* Set game->error, log it, set game->vm.status to status and end the run. */
void cok_adventure_fail(cok_adventure *game, cok_ecl_status status, const char *format, ...);
/* A spell effect could not be carried out: end the run with
 * COK_ECL_EFFECT_FAILED and the effects' error, unless a hook the effect
 * ran (a flash, say) has already ended it, whose status and error stay.
 * Returns false. */
bool cok_adventure_effect_failed(cok_adventure *game);

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
/* Show text on row 24 in colour fg and wait for a key (1521:096c), not
 * logged. */
void cok_adventure_prompt_colour(cok_adventure *game, const char *text, uint8_t fg);
/* Show text on row 24 in colour fg, logged as "print", and wait for a key
 * (1521:096c). */
void cok_adventure_alert(cok_adventure *game, const char *text, uint8_t fg);

/* The mouse and the joystick (src/devices.c). */

/* KRYNN.CFG's third line as startup takes it (11c7:0aa8): the mouse
 * (DS:8986) or the joystick (DS:89ce) on, then 177c:0000, which clears the
 * stick's thresholds and turns it off when it does not read, and with it
 * still on its thresholds x and y. The front end sets game->port and the
 * driver (game->mouse.driver.installed) first. */
void cok_adventure_devices(cok_adventure *game, bool mouse, bool joystick,
                           const uint16_t x[7], const uint16_t y[7]);
/* Startup's part for the mouse (3e99:0085-0134), with KRYNN.CFG's M: see
 * cok_mouse_start; the click's place the screen's middle, the pointer
 * moved there. */
void cok_adventure_mouse_startup(cok_adventure *game);
/* The menus' click and wait for the buttons (game->pointer), set up when
 * the game opens. */
void cok_adventure_pointer_init(cok_adventure *game);
/* Either on. */
bool cok_adventure_devices_on(const cok_adventure *game);
/* While game->device_wait says the game waits for a key with the mouse or
 * the joystick on (1614:01ad, 00fa): whether they give it one now, a
 * mouse button down, or the stick pushed or a button of it down. A front
 * end's read gives COK_KEY_DEVICE then. */
bool cok_adventure_device_ready(cok_adventure *game);
/* The key they give (1614:01ad, 00fa): with the mouse, its left button
 * Enter, the right Escape, both 0, and the click and where kept in
 * game->pointer; with the joystick, a push its keypad digit (DS:4358),
 * else button 1 Enter, 2 Escape. -1 when none is down any more. */
int cok_adventure_device_key(cok_adventure *game);
/* 1614:03c2's part for them: the mouse's position kept and a button down,
 * or the stick's button or push. */
bool cok_adventure_device_pending(cok_adventure *game);
/* Let the game's polling of them pass (hooks.device_wait). */
void cok_adventure_device_wait(cok_adventure *game, unsigned ms);
/* The character a special key from a menu selects (546c:3334, see
 * cok_party_special); the selection as it is after a click, which picked
 * from the party list already. */
uint8_t *cok_adventure_special(cok_adventure *game, uint8_t scan);
/* Print running text (1521:04ac, cok_text_wrap) with the mouse's cursor
 * hidden while it prints and pages (1743:0051 at 1521:04c7); shown again
 * after it (054d, 072e), except where its window check returns first.
 * page (cok_text_hooks.page, given game) pages it. After each character
 * the game waits the speed times 3 ms while game->text_delay is set
 * (1521:042e, DS:4b59): the text delay is the flag's alone, never the
 * caller's. */
void cok_adventure_wrap(cok_adventure *game, cok_text_cursor *cursor, const char *text,
                        cok_text_window window, uint8_t fg, bool clear,
                        void (*page)(void *context));
/* The text window, cells 1-38 by 17-22, where PRINT and most running text
 * go. */
extern const cok_text_window cok_adventure_text_window;

#endif
