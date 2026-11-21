#ifndef COK_QUEEN_H
#define COK_QUEEN_H

#include "adventure.h"
#include "command.h"

/* The Dark Queen of Krynn's window library as dark_queen.c has it, for the
 * parts of its game that the camp's buttons call (queen_create.c's
 * Create Character, queen_modify.c's Modify Character, queen_train.c's
 * Train Character and Knight new Order): their screens and menus are made
 * of the same buttons, bottom bar and lists. Every one draws to the
 * game's screen as the original draws to its page (see dark_queen.h).
 * Positions are pixels (rows then columns) unless a name says cells (8 by 8). */

/* A rectangle of the screen, rows top to bottom and columns left to right,
 * the ends excluded. */
typedef struct {
    int top, left, bottom, right;
} queen_rect;

/* What a read gave (2aa6:15e3): a key's code (251e:00e7's: the ASCII byte,
 * or an extended key from 0x100: Up 0x108, Down 0x104, PgUp 0x101, PgDn
 * 0x103, Home 0x107, End 0x105, Left 0x106, Right 0x102, Alt and a
 * letter 0xff + the letter), or a click of either button at x, y. */
typedef struct {
    int key;
    bool click;
    int x, y;
} queen_input;

enum {
    QUEEN_BUTTON_DISABLED = 0x01, /* sunken in 0x80, no hotkey */
    QUEEN_BUTTON_PRESSED = 0x08,
    QUEEN_BUTTON_WHITE = 0x40,    /* its text white, with no hotkey letter */
    QUEEN_BAR_ROW = 188,          /* the bottom bar's buttons */
    QUEEN_WINDOW = 0x17,          /* the window's grey */
};

/* Read a key or a click: its code, 0 for a click, -1 when the keys ran out
 * (COK_KEY_TIMEOUT where a timed menu's time passed). */
int queen_read_input(cok_adventure *game, queen_input *in);
/* Whether hotkey h (2aa6:000e's '@' Return, '#' Escape, '*' any) takes key. */
bool queen_hotkey(int h, int key);
bool queen_inside(const queen_rect *r, int x, int y);
/* A key or click nothing takes (2102:0193). */
void queen_beep(cok_adventure *game);
/* The controls the mouse's cursor knows (the sword over one, the shield
 * elsewhere), cleared with the screen's. */
void queen_controls_clear(cok_adventure *game);
void queen_control_add(cok_adventure *game, queen_rect r);

/* A string at pixel row, col in the attribute's colours (2102:03a0: the
 * background in the high nibble); the same at cell x, y in colour fg on
 * the window's grey (1a62:0046). */
void queen_text(cok_adventure *game, int row, int col, const char *s, unsigned attribute);
void queen_say(cok_adventure *game, int x, int y, const char *s, unsigned fg);
/* Rows y0 to y1 and columns x0 to x1 (ends excluded) in a colour
 * (22b7:0078), and cells x1-x2 by y1-y2 (inclusive) in the window's grey
 * (1a62:09a1). */
void queen_fill(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t colour);
void queen_clear_cells(cok_adventure *game, int x1, int y1, int x2, int y2);
/* The full window (20b2:0030) and the bottom bar (1bdb:036c). */
void queen_full_window(cok_adventure *game);
void queen_bottom_bar(cok_adventure *game);
/* A framed button of width characters (1bdb:008e) and the place a click on
 * it takes. */
void queen_button(cok_adventure *game, int row, int col, const char *s, int width,
                  unsigned flags);
queen_rect queen_button_rect(int row, int col, int width);
/* The prompt boxed on the bar from cell x + 1 (1a62:0046 on row 24). */
void queen_prompt_box(cok_adventure *game, int x, const char *s, unsigned fg, unsigned bg);
/* The bottom bar's menu (1bdb:1e29): the prompt and a button an item,
 * read by its first letter or a click; the index picked, a filtered key's
 * code (Escape; with dirs the extended keys and Alt's), or -1. */
int queen_bar_menu(cok_adventure *game, const char *prompt, const char *const *items,
                   size_t count, bool dirs);
/* The command menu of a Dark Queen party member's turn (dqk 5045:0cd7, 1bdb:1595): the items on
 * the bottom bar in one row (BAR_ROW) when split < 0, else in two, the items up to split (the index
 * of the '@' item) on row 178 and the rest from column 8 on row 190; no prompt, the extended keys,
 * Escape and '`' as codes, and Space as 0x20 (no beep). With the mouse on (the port's gate: the
 * original adds the areas to a fresh list with or without one, and no click comes without one), the
 * eight areas round the combatant's cell (p, q: its row and column on the view, as bytes) give codes
 * 0x81-0x88 on a click. prompt is boxed before the items (queen_bar_menu's; "" for none): the Move
 * menu's "Move/Attack, Move Left = N" (dqk 5045:1099). pressed, if not 0, is a scan code the first
 * key read gives after the menu is drawn (the original draws the menu first, then reads: 0d26's
 * 0x8ef, 0x801): 0x10 Alt-Q, 0x2d Alt-X. It returns the item's index, a filtered key's or an area's
 * code, or -1 when the keys ran out. */
int queen_combat_menu(cok_adventure *game, const char *prompt, const char *const *items,
                      size_t count, int split, int p, int q, uint8_t pressed);
/* The command menu taken off (1bdb:03d7): the bottom bar, then FRAME 6 over the whole window. */
void queen_combat_menu_close(cok_adventure *game);
/* The command menu's Speed (dqk 5045:166c): "game speed: (N) " on the bar over Faster, Slower and
 * Exit, until Exit or Escape. False where the keys ran out. */
bool queen_speed(cok_adventure *game);
/* View in a battle (dqk 5045:0af8: 86b8:0b0c(&done), then the mode 5 tail): the sheet and its menu,
 * as the camp's View. done is set where Items ends the turn (a readied item of 0x8c, 86b8:16bc),
 * else false (Use, the other writer, is a stop here). False where the run stopped; true, with
 * vm.abort set, where the keys ran out. */
bool queen_view_combat(cok_adventure *game, bool *done);
/* queen_bar_menu with areas of the screen after its buttons (1bdb:1429): a
 * click on one gives QUEEN_AREA + its index. */
enum { QUEEN_AREA = 0x100 };
int queen_bar_menu_areas(cok_adventure *game, const char *prompt, const char *const *items,
                         size_t count, bool dirs, const queen_rect *areas, size_t area_count);
/* A message boxed on the bar from cell x + 1 in fg on bg (1a62:0904, the
 * x of 0 and colours of its callers), for the speed's time; the same
 * message the way 177e:153c shows it (white on grey from cell 0). */
void queen_message(cok_adventure *game, const char *s, int x, unsigned fg, unsigned bg);
void queen_bar_message(cok_adventure *game, const char *s);
/* Rows y0 to y1 and columns x0 to x1 (ends excluded) XORed with mask (22b7:0078
 * with 0x100 added to its colour). */
void queen_xor(cok_adventure *game, int y0, int x0, int y1, int x1, uint8_t mask);
/* The mouse's pointer, hidden while a line is edited. */
void queen_cursor_show(cok_adventure *game, bool shown);
/* A question and Yes or No (1bdb:0424): 1, 0 for No or Escape, -1. */
int queen_yes_no(cok_adventure *game, const char *prompt);
/* A line typed on the bar's field (1a62:05c9, the editor 6431:0001), at
 * most `most` characters: 1 for Return, 0 for Escape, -1. */
int queen_edit_line(cok_adventure *game, char *line, size_t most);
/* The field with a prompt of its own (1a62:05c9): the prompt in fg on bg
 * from cell 1, the line typed after it. */
int queen_input_line(cok_adventure *game, const char *prompt, unsigned fg, unsigned bg,
                     char *line, size_t most);
/* Whether a mouse driver is installed. Not DS:c2f5, which 1bdb:1206 sets
 * for a menu's code result (a key below 0x20 or an extended key, a view area's
 * click), not a driver's flag. */
bool queen_mouse(const cok_adventure *game);
/* The character written to the save path as <name>.QCH (604b:23a7), asking
 * about a file there. False where the run stopped. */
bool queen_write_character(cok_adventure *game, const cok_character *character);
/* Add Character's icons for the member just joined (7368:0af4, 0ba7: 604b:05d7): CBODY into its
 * slot (+0xc5, set by the join: cok_party_add), with no NPC test. False where the run stopped. */
bool queen_added_icons(cok_adventure *game, const uint8_t *record);
/* A member removed (177e:1e31's flag 1, 204c:004d): its slot's icons emptied. */
void queen_remove_icons(cok_adventure *game, const uint8_t *record);
/* The combat icon of slot (0-0x25) and image (0 ready, 1 attacking) as the table holds it
 * (dqk 19a4:0881's copy, header included): record and size, NULL and 0 for an empty slot.
 * False for a game that has no such table (not Dark Queen's) or a slot or image past it. For
 * the tests and eclplay --icons. */
bool queen_icon(const cok_adventure *game, unsigned slot, unsigned image, const uint8_t **record,
                size_t *size);
/* The attack of an ally question (dqk 52db:2aef, queen_commands.c's Move into a party member): *go is
 * whether c may attack target: yes where target is an enemy of c (1 where its word +0x66 is 0, else
 * 0, is c's word +0x66) or c is the computer's; else "attack ally? " is asked (Yes: var 0x36 set,
 * the sides counted, go). False where the run stopped. */
bool queen_attack_ally(cok_adventure *game, cok_character *c, cok_character *target, bool *go);
/* Aim (dqk 52db:37dd, queen_aim.c): the cursor's pictures, the targets listed around c (36c6), the
 * menu of Next, Prev, Manual, Target, Center and Exit with the range on row 23 when attacking (2b60),
 * and the range line cleared at the end when attacking (1bdb:03a4). range 0xff is the readied
 * weapon's range (0 or 0xff is 1); attack 1 attacks the target (the strike, 52db:2d46, queen_aim.c's); empty
 * and creatures are the spell's choice of a target (part 28). res is what Target picked (the
 * record, NULL for none, and the cell); done is set where Target ends the turn. False where the run
 * stopped; true, with the turn not ended, when the keys ran out. */
bool queen_aim(cok_adventure *game, cok_character *c, uint8_t range, uint8_t attack, uint8_t empty,
               uint8_t creatures, bool *done, cok_combat_aimed *res);
/* The range line of Aim's end (dqk 1bdb:03a4): rows 176 to 185 redrawn from FRAME 6 through
 * their clip. */
void queen_range_line_clear(cok_adventure *game);
/* A Dark Queen party member's turn (dqk 5045:08cf, queen_commands.c): the command menu until a
 * command ends the turn or a stop is reached (its routine cited by cok_adventure_not_yet). pressed,
 * if not 0, is a scan code the first menu's first key read gives (command.c's; 0x10 is Alt-Q, auto).
 * False where the run stopped; true, with the turn not ended, when the keys ran out. */
bool queen_commands(cok_adventure *game, cok_character *c, uint8_t pressed);
/* The computer's turn's key (dqk 5bb8:1050 reads it through 2093:0078): a key's code as a
 * menu's result gives it (Alt-M 0xcc, Alt-X 0xd7). Once a key is read, every key waiting
 * (queen_key_waiting) is read too, and the last one's code is returned; 0 for none and for a
 * click, -1 when the keys ran out. */
int queen_read_code(cok_adventure *game);
/* Whether a key waits on the keyboard (hooks.key_pending, 1df9:045b): a mouse button does not
 * count. */
bool queen_key_waiting(cok_adventure *game);
/* The undead search (dqk 52db:135e, queen_ai.c): the first of the highest kind of undead c sees
 * (the enemies in sight, not forced; a kind above 0 and no higher than *limit, signed words) into
 * *undead, with *found; *limit is that kind, 0 for none. False where the run stopped. */
bool queen_undead(cok_adventure *game, cok_character *c, cok_character **undead, int16_t *limit,
                  bool *found);
/* Turning undead (dqk 52db:1117, queen_ai.c): c's "turns undead...", the search and the turning
 * until the count runs out, "Nothing Happens..." where none is turned, then c's turn is ended and
 * DS:bd2e set (the command Turn and the computer's turn both). False where the run stopped. */
bool queen_turn_undead(cok_adventure *game, cok_character *c);
/* The computer's spell choice (dqk 5bb8:0306, queen_ai.c): *cast is set where its spell was cast (the
 * turn ends); none where it casts nothing (fleeing, no Magic, a no-cast draw, no memorised spell).
 * False where the run stopped. */
bool queen_spell_choice(cok_adventure *game, cok_character *c, bool *cast);
/* The computer's spell (dqk 5bb8:1b1e): the first memorised spell, in priority order, worth casting
 * (queen_spell_worth above 0); 0 for none. False where the run stopped. */
bool queen_best_spell(cok_adventure *game, cok_character *c, uint8_t *spell);
/* The worth of spell cast by c (dqk 52db:4789), and of spell to one target t (dqk 52db:42cc, the
 * table of the spells by id, which dqk-ai-cast-b reads too): a signed byte, -40 for the other side,
 * 0 for none, 1-4 otherwise. side is the word +0x66 the spell's side has. False where the run stopped. */
bool queen_spell_worth(cok_adventure *game, cok_character *c, uint8_t spell, int8_t *worth);
bool queen_target_worth(cok_adventure *game, cok_character *t, uint8_t spell, unsigned side,
                        int8_t *worth);
/* The side a spell of c's counts for (52db:4789, 1d42, 48aa): c's word +0x66 where the spell's allies
 * word (+0x13) is 0, else the other side (51c2:08fd). */
unsigned queen_spell_side(cok_adventure *game, const cok_character *c, uint8_t spell);
/* The computer's turn (dqk 5bb8:0000, queen_ai.c; ai.c's cok_combat_computer dispatches to it by
 * the descriptor's computer_routine): its steps as far as the port carries them. */
bool queen_computer(cok_adventure *game, cok_character *c);
/* The computer's weapon's needs (dqk 5bb8:1396's aca8-acab, set from the target's effects by
 * queen_weapon_needs_of): need is a weapon's bonus below which it is worth nothing, need_flagged
 * the same for one whose flags have 8 or 0x10; effect_42 and effect_43 halve some weapons. */
typedef struct {
    uint8_t need, need_flagged;
    bool effect_42, effect_43;
} queen_weapon_needs;
/* The needs of a target (NULL for none: all zero). */
void queen_weapon_needs_of(const cok_character *target, queen_weapon_needs *needs);
/* A weapon's worth to a character with hands held (dqk 5bb8:1271): item and its type's 16 bytes;
 * underwater is var 0x3c (COK_VAR_RECORD_EFFECT). */
uint8_t queen_weapon_score(const cok_rules *rules, uint8_t hands, const uint8_t *item,
                           const uint8_t *type, const queen_weapon_needs *needs, bool underwater);
/* The computer's weapon choice (dqk 5bb8:1396, queen_ai.c) for c, with target (a record, or NULL):
 * its ready and stats are the View's. False where the run stopped. */
bool queen_choose_weapon(cok_adventure *game, cok_character *c, const uint8_t *target);
/* The computer's fight of one pass (dqk 5bb8:0b3d, queen_ai.c), with the target queen_computer's
 * loop picked: over is set when the fight is over (its !going). False where the run stopped. */
bool queen_fight(cok_adventure *game, cok_character *c, bool *over);
/* The computer's step (dqk 5bb8:0438, may it step; the descriptor's can_step, queen_ai.c): ok where
 * the square of try's way from dir may be stepped on; offmap where it is off the map. False where the
 * run stopped. */
bool queen_can_step(cok_adventure *game, cok_character *c, uint8_t try_, uint8_t dir, bool *offmap,
                    bool *ok);
/* A frame record (FRAME.TLB) or a record of ALWAYS.TLB drawn at pixel
 * row, col. */
void queen_draw_frame(cok_adventure *game, unsigned id, int row, int col);
void queen_draw_always(cok_adventure *game, unsigned id, int row, int col);

/* The libraries (FRAME and ALWAYS have their own calls above): DUNGCOM.TLB
 * (its sets of tiles and palettes), CBODY.TLB (the party's bodies) and
 * WILDCOM.TLB (the wilderness' sets, the combat ground). */
enum { QUEEN_LIB_DUNGCOM = 12, QUEEN_LIB_CBODY = 13, QUEEN_LIB_WILDCOM = 14 };
/* A record of a library drawn at pixel row, col (as 269b:0a9c), and the
 * palette record of a library (set 0) or of its nested library set. */
void queen_draw_library(cok_adventure *game, unsigned which, unsigned id, int row, int col);
bool queen_set_palette(cok_adventure *game, unsigned which, unsigned set, unsigned record);
/* A message in the bar for the speed's time (177e:153c, 1a62:0904: the
 * text in colour fg), the bar again after. */
void queen_bar_message_colour(cok_adventure *game, const char *s, unsigned fg);
/* DS:bd92, which turns Train and Knight new Order on in the camp wherever
 * the party is: whether set, and set. */
bool queen_train_anywhere(const cok_adventure *game);
void queen_set_train_anywhere(cok_adventure *game, bool on);

/* The character sheet (86b8:0000) of a member or a character in the
 * making, which recomputes it first (177e:0be2). False where that stopped
 * the run. */
bool queen_sheet(cok_adventure *game, cok_character *character);
/* The sheet's score k (0-5) drawn lit or not (86b8:096c), and its lower
 * block after the recompute and the movement's (86b8:065e). */
void queen_sheet_score(cok_adventure *game, const uint8_t *c, int k, bool lit);
bool queen_sheet_derived(cok_adventure *game, cok_character *character);

/* The spell engine (queen_cast.c): the spell record's byte or word at offset off of spell id (DS:27e9
 * + 0x16 * id). */
uint8_t queen_spell_byte(cok_adventure *game, unsigned id, unsigned off);
uint16_t queen_spell_word(cok_adventure *game, unsigned id, unsigned off);
/* 177e:1900: the caster level of spell for caster (a character's record; 6 for a non-caster, by the kind; the item's 6; capped
 * to 0x1f when capped). False, ending the run, where the kind above 4 leaves it unset (undefined),
 * or a magic order past the moons is read. */
bool queen_caster_level(cok_adventure *game, const uint8_t *caster, uint8_t spell, bool capped,
                        uint8_t *level);
/* 849a:0f34: the spell's range for the selected character, as a byte (1 for 0 with a target, and for
 * 0xff, a touch). */
bool queen_spell_range(cok_adventure *game, uint8_t spell, uint8_t *range);
/* 849a:101b: the spell's duration in minutes (the dice through cok_dice). */
bool queen_spell_duration(cok_adventure *game, uint8_t spell, uint16_t *minutes);
/* 148f:1886: the saving throw of t by type (the spell's +0xc word, 0-4) with bonus (0 in the apply),
 * made true when it saves: a holder of effect 0x76 rolls 1, a roll of 1 fails and 20 saves (both
 * with no event 0x0c); else the mage's moon, the save bonus and the event 0x0c on t, then the
 * record's saving throw of that type. False where the run stopped (a magic order past the moons). */
bool queen_save(cok_adventure *game, cok_character *t, uint8_t type, uint8_t bonus, bool *made);
/* 849a:10f9: the spell's apply to the targets (spell_targets, count spell_target_count): the saving
 * throw (queen_save, or saved by status 10 or effect 0x96 with spell 0x37), the touch (the stats,
 * event 0x0b on the slot, the hit roll 148f:1771: a miss zeroes amount for the rest), the damage
 * (through queen_cast_damage) and the effect of the spell for its duration with value (0: the caster
 * level, capped) and text. False where the run stopped. */
bool queen_spell_apply(cok_adventure *game, uint8_t spell, uint8_t value, bool on_remove,
                       uint8_t amount, uint8_t type, const char *text);
/* 148f:2214: damage of amount (the word DS:ae55) to t, saved by save_kind (1 none, 2 half) when saved;
 * its event 6, 0x14 or the save, the text, the panel and flash, the damage in combat, "lost a spell",
 * "Goes Down" and the death. False where the run stopped. */
bool queen_cast_damage(cok_adventure *game, cok_character *t, uint16_t amount, uint8_t save_kind,
                       bool saved);
/* 177e:187a: "is fully healed" or "is partially healed" said on c's row 10 with its wait (in combat). */
bool queen_healed(cok_adventure *game, cok_character *c);
/* 7fd8:00f8, Cure Light Wounds, and 7fd8:050a, Magic Missile, for the selected caster and the targets
 * (rolls.spell is the spell). False where the run stopped. */
bool queen_cure_light(cok_adventure *game);
bool queen_magic_missile(cok_adventure *game);
/* The handler of a spell id the port carries out (CLW 0x03, MM 0x0f, the plain applies, Bless 0x01 and 0x3e,
 * Curse 0x02, Mirror Image 0x20, Prayer 0x2a, Dispel Evil 0x49, Fire Touch 0x76), NULL for any
 * other: the cast takes its handler from here before rules_dark_queen_magic.c's unported table. */
typedef bool queen_spell_fn(cok_adventure *game);
queen_spell_fn *queen_spell_handler(uint8_t id);
/* The plain apply's text of spell (queen_cast.c's table, 7fd8's routines) and the DS offset that
 * routine pushes for it (*ds); NULL, with *ds unset, for a spell with no plain apply. */
const char *queen_plain_text(uint8_t spell, uint16_t *ds);
/* The flight's picture of the Fireball family by facing dir (51c2:0fc8): slot's picture for the facing, one frame
 * (frame 0). The burst (51c2:0def): radius's sparks (3 * radius + 1 rings, from the sine table DS:5976), slot's
 * four pictures and the sound. Both false where the run stopped. */
bool queen_direction_picture(cok_adventure *game, uint8_t dir, uint8_t slot);
bool queen_burst(cok_adventure *game, uint8_t radius, uint8_t slot, uint8_t sound);
/* The caster level of spell for the selected character (177e:1900) and the combatants listed round the spell's
 * cell with radius (5d71:07d7, the targets 1 to the count), for queen_area.c. False where the run stopped. */
bool queen_spell_level(cok_adventure *game, uint8_t spell, bool capped, uint8_t *level);
bool queen_area_targets(cok_adventure *game, uint16_t radius);
/* The band b handlers (queen_area.c's switch): the handler of a spell id it carries out, NULL for any other. The
 * cast takes it before the plain table (queen_spell_handler). */
queen_spell_fn *queen_area_handler(uint8_t id);
/* The Fireball family's routine (dqk 7fd8:1519: Fireball 0x2f, Delayed Blast Fireball 0x73, Necklace of Missiles 0x85;
 * the underwater Lightning 0x33's area is band b3's call of it), for queen_area.c's table and its tests. */
bool queen_fireball(cok_adventure *game);
/* 849a:19ff, the cone from x0, y0 aimed at x1, y1 (queen_area.c; Cone of Cold, and for band b5 and c Meteor and Fear):
 * its targets are spell_targets 1 to spell_target_count, the selected member left out. width 1 is the centre line
 * alone, 2 adds the right side, 3 the left, 0xff the combatants within 3 of the end. len is in cells: the aim goes on
 * along its steps until its walk (2 a straight step, 3 a diagonal one) is 2 * len. Where the centre line meets no one
 * the sides are those of side 0xff (the caller's BP's high byte: 7fd8:299c's and 257a's). False where the run stopped. */
bool queen_cone(cok_adventure *game, int8_t x0, int8_t y0, int8_t x1, int8_t y1, uint8_t width, uint8_t len);

/* 849a:0031: whether the character may cast spell (its low 7 bits) by its class levels, wisdom
 * or intelligence (the spell's kind in DS:27e9): the mage's kind 2 also by the armour and the
 * battle (game's mode 5), with scroll the class 6 level too. */
bool queen_spell_usable(cok_adventure *game, const cok_character *character, uint8_t spell,
                        bool scroll);
/* 849a:2080 for kind 0 in mode 1 (Cast): the list "Choose Spell:" of the memorised spells the
 * character may cast. The spell's id, 0 for Exit, Escape or no spell listed, -1 where the keys
 * ran out. Other modes stop (cok_adventure_not_yet). */
int queen_spell_pick(cok_adventure *game, cok_character *character, int mode);
/* 52db:2543 (the Cast command, queen_cast.c): the picker when spell is 0, the camp-only and underwater
 * refusals, the arena again unless redrawn, then the cast at once (time 0: queen_cast, the turn ended)
 * or "Begins Casting" with the caster moved in the turn list. done is set where the turn ends. False
 * where the run stopped; true, with the turn not ended, where the keys ran out. */
bool queen_cast_command(cok_adventure *game, cok_character *c, uint8_t spell, bool redrawn,
                        bool *done);
/* 849a:145b (queen_cast.c): the cast of spell by the selected member (vm.character) in a battle:
 * the announce (when announce and not an item), the targets (queen_targets) until one is done, its
 * flight, the reveal, the forget (unless an item), the handler (queen_spell_handler, or the cited
 * stop). ai is the computer's (part 29: its aim stops). done is what the targets gave. False where
 * the run stopped; true, with the turn not ended, where the keys ran out. */
bool queen_cast(cok_adventure *game, uint8_t spell, bool ai, bool announce, bool *done);
/* 52db:1f78 (queen_cast.c): the targets of spell for c (the caster) into spell_targets, its count and
 * the cell (spell_x, spell_y); done is cleared where no target is picked. False where the run
 * stopped; true with vm.abort set where the keys ran out. */
bool queen_targets(cok_adventure *game, cok_character *c, uint8_t spell, bool ai, bool *done);

#endif
