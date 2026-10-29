#include "queen_create.h"

#include "party.h"
#include "queen.h"
#include "queen_create_data.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The form of race, gender, class and alignment (dqk 66e5:2a6c): a dialog
 * of radio buttons in groups (2aa6: a group item, 0aad, holding the
 * variable and the callback, then its buttons, 0c21) and the bar's Done and
 * Exit. The original's callbacks (66e5:2581, 26a1, 26c2, 26c7, 26d4)
 * enable and disable buttons by sending them messages (2aa6:188f). */

enum {
    F_DISABLED = 0x01, F_HIDDEN = 0x02, F_SELECTED = 0x04,
};

/* The items by their index in the original's dialog: groups at 0, 8, 11,
 * 29 and 33, each followed by its buttons. */
enum {
    ITEMS = 37,
    G_RACE = 0, G_GENDER = 8, G_CLASS = 11, G_LAW = 29, G_GOOD = 33,
    FIRST_CLASS = 12, CLASSES = 17,
};

typedef struct {
    uint8_t flags;
    int row, col; /* pixels */
    const char *text;
} radio;

typedef struct {
    cok_adventure *game;
    cok_character *character;
    radio item[ITEMS];
    /* The groups' variables (DS:c785, c787, c789, c78b, c78d: the index
     * of the button selected within its group, from 1) and the item
     * selected (the group's +0x18). */
    int race, law, good, class_, gender;
    int selected[ITEMS];
    uint8_t shown[ITEMS]; /* the flags each was drawn with: 0xff, never */
    uint8_t dwarf; /* DS:7276: 0x11 for the dwarves, 0 */
    int column, row; /* DS:c750, c74e: the keyboard's place, with no mouse */
    bool exit;     /* DS:c783 */
    bool done;     /* a button with its bit 4 was taken */
} form;

static void set_flag(form *f, int index, uint8_t flag, bool on)
{
    if (on)
        f->item[index].flags |= flag;
    else
        f->item[index].flags &= (uint8_t)~flag;
}

static void group_select(form *f, int group, int index);

static uint8_t *rec(form *f)
{
    return f->character->record;
}

static int *group_var(form *f, int group)
{
    switch (group) {
    case G_RACE: return &f->race;
    case G_GENDER: return &f->gender;
    case G_CLASS: return &f->class_;
    case G_LAW: return &f->law;
    default: return &f->good;
    }
}

static unsigned word_at(const uint8_t *p)
{
    return (unsigned)(p[0] | p[1] << 8);
}

/* The row of the alignments the class allows (DS:0563 + (class + DS:7276)
 * * 0x15). */
static const uint8_t *alignments(form *f)
{
    unsigned class_ = word_at(rec(f) + 0x5a) + f->dwarf;
    return queen_class_alignments[class_ < 34 ? class_ : 0];
}

static void set_alignment(form *f)
{
    /* 66e5:230c */
    uint8_t *c = rec(f);
    unsigned a = (unsigned)((f->law - 1) * 3 + f->good - 1);
    c[0x62] = (uint8_t)a;
    c[0x63] = (uint8_t)(a >> 8);
}

static bool allowed(form *f, unsigned alignment)
{
    const uint8_t *row = alignments(f);
    for (unsigned j = 0; j < row[0]; ++j)
        if (word_at(row + 1 + 2 * j) == alignment) return true;
    return false;
}

/* 66e5:2459: the second axis's Good and Neutral on where the class allows
 * the alignment with the first axis' button selected. */
static void enable_second_axis(form *f)
{
    for (int a = 1; a <= 2; ++a) {
        bool found = allowed(f, (unsigned)((f->law - 1) * 3 + a - 1));
        set_flag(f, 0x21 + a, F_DISABLED, !found);
    }
}

/* 66e5:2532: the alignment a class starts with. */
static void default_alignment(form *f)
{
    int class_ = f->class_ - 1;
    if (class_ < 0 || class_ > 16) return;
    bool neutral = class_ == 6 || class_ == 12 || class_ >= 14;
    f->law = f->good = neutral ? 2 : 1;
}

/* 66e5:2323: the alignment of the buttons selected where the class
 * allows it; else the first Good or Neutral that it allows; else the
 * class's default. */
static void validate_alignment(form *f)
{
    uint8_t *c = rec(f);
    bool found = false;
    for (unsigned i = 0; i < alignments(f)[0] && !found; ++i) {
        unsigned a = (unsigned)((f->law - 1) * 3 + f->good - 1);
        c[0x62] = (uint8_t)a;
        c[0x63] = (uint8_t)(a >> 8);
        if (word_at(alignments(f) + 1 + 2 * i) == a) found = true;
    }
    if (found) return;
    bool ok = false;
    f->good = 1;
    while (f->good <= 2 && !ok) {
        unsigned a = (unsigned)((f->law - 1) * 3 + f->good - 1);
        c[0x62] = (uint8_t)a;
        c[0x63] = (uint8_t)(a >> 8);
        ok = allowed(f, a);
        ++f->good;
    }
    --f->good;
    if (!ok) default_alignment(f);
}

/* 66e5:2581: the race's classes on, the others off, and a class that is
 * no longer allowed replaced by the first. */
static void race_chosen(form *f)
{
    uint8_t *c = rec(f);
    c[0x58] = (uint8_t)(f->race - 1);
    c[0x59] = 0;
    const uint8_t *row = queen_race_classes[(f->race - 1) < 7 ? f->race - 1 : 0];
    int first = 0xff;
    bool selected_off = false;
    for (int k = 0; k < CLASSES; ++k) {
        bool found = false;
        for (unsigned j = 0; j < row[0] && !found; ++j)
            if ((int)word_at(row + 1 + 2 * j) == k) {
                found = true;
                if (first == 0xff) first = k;
            }
        if (!found && k == f->class_ - 1) selected_off = true;
        set_flag(f, FIRST_CLASS + k, F_DISABLED, !found);
    }
    if (selected_off) group_select(f, G_CLASS, first + 1);
    f->dwarf = (f->race - 1 == 4 || f->race - 1 == 3) ? 0x11 : 0;
}

/* 66e5:26d4: the class chosen: the first axis' buttons by it, the
 * alignment kept to one it allows. */
static void class_chosen(form *f)
{
    uint8_t *c = rec(f);
    c[0x5a] = (uint8_t)(f->class_ - 1);
    c[0x5b] = 0;
    int class_ = f->class_ - 1;
    if (class_ <= 16 && class_ != 7) {
        bool lawful_only = class_ == 1 || class_ == 3;
        set_flag(f, 0x1e, F_DISABLED, false);
        set_flag(f, 0x1f, F_DISABLED, lawful_only);
        set_flag(f, 0x20, F_DISABLED, lawful_only);
    }
    const uint8_t *row = alignments(f);
    bool found = false;
    int axis = 1;
    for (; axis <= 2 && !found; ++axis)
        for (unsigned j = 0; j < row[0] && !found; ++j) {
            unsigned a = (unsigned)((f->law - 1) * 3 + axis - 1);
            c[0x62] = (uint8_t)a;
            c[0x63] = (uint8_t)(a >> 8);
            if (word_at(row + 1 + 2 * j) == a) found = true;
        }
    if (found) {
        validate_alignment(f);
    } else {
        bool other = false;
        int law = 1;
        for (; law <= 3 && !other; ++law)
            for (unsigned j = 0; j < row[0] && !other; ++j) {
                unsigned a = (unsigned)((law - 1) * 3 + f->good - 1);
                c[0x62] = (uint8_t)a;
                c[0x63] = (uint8_t)(a >> 8);
                if (word_at(row + 1 + 2 * j) == a) other = true;
            }
        --law;
        if (other)
            f->law = law;
        else
            default_alignment(f);
    }
    enable_second_axis(f);
    group_select(f, G_LAW, f->law);
    group_select(f, G_GOOD, f->good);
}

/* A group's callback (item +4) with the button picked. */
static void group_callback(form *f, int group)
{
    switch (group) {
    case G_RACE: race_chosen(f); break;
    case G_GENDER: rec(f)[0x60] = (uint8_t)(f->gender - 1), rec(f)[0x61] = 0; break;
    case G_CLASS: class_chosen(f); break;
    case G_LAW:
        /* 66e5:26a1 */
        enable_second_axis(f);
        validate_alignment(f);
        group_select(f, G_GOOD, f->good);
        set_alignment(f);
        break;
    default: set_alignment(f); break; /* 66e5:26c2 */
    }
}

/* 2aa6:0aad, message 4: button index of the group (from 1) selected, the
 * one before it not, the variable stored, the callback called. */
static void group_select(form *f, int group, int index)
{
    int old = f->selected[group];
    if (old != index) {
        if (old != 0) set_flag(f, group + old, F_SELECTED, false);
        f->selected[group] = index;
        *group_var(f, group) = index;
    }
    if (index != 0) set_flag(f, group + index, F_SELECTED, true);
    group_callback(f, group);
}

/* A radio button (2aa6:0c21 and 0095): its record of ALWAYS.TLB (16
 * unselected, 17 selected, 18 off), the text 12 pixels on. */
static void draw_radio(form *f, int index)
{
    const radio *r = &f->item[index];
    f->shown[index] = r->flags;
    if (r->flags & F_HIDDEN) return;
    unsigned state = (r->flags & F_DISABLED) ? 2 : (r->flags & F_SELECTED) ? 1 : 0;
    queen_draw_always(f->game, 16 + state, r->row, r->col);
    queen_text(f->game, r->row, r->col + 12, r->text, 0x87);
}

/* 2aa6:0c21's hit test: the button's row, from its column to the end of
 * its text and 16 pixels; a disabled or hidden one is not hit. */
static queen_rect radio_rect(const radio *r)
{
    return (queen_rect){r->row, r->col, r->row + 8, r->col + 8 * (int)strlen(r->text) + 16};
}

static const char *const race_names[7] = {"SILVANESTI ELF", "QUALINESTI ELF", "HALF-ELF",
                                          "MOUNTAIN DWARF", "HILL DWARF", "KENDER", "HUMAN"};
static const char *const class_names[CLASSES] = {
    "CLERIC", "KNIGHT", "FIGHTER", "PALADIN", "RANGER", "MAGIC-USER", "THIEF", "MONK",
    "CLERIC/FIGHTER", "CLERIC/FIGHTER/M-U", "CLERIC/RANGER", "CLERIC/MAGIC-USER", "CLERIC/THIEF",
    "FIGHTER/MAGIC-USER", "FIGHTER/THIEF", "FIGHTER/M-U/THIEF", "MAGIC-USER/THIEF"};

static void build_form(form *f)
{
    memset(f->item, 0, sizeof f->item);
    memset(f->selected, 0, sizeof f->selected);
    for (int k = 0; k < 7; ++k)
        f->item[G_RACE + 1 + k] = (radio){0, 24 + 8 * k, 12, race_names[k]};
    f->item[G_GENDER + 1] = (radio){0, 164, 12, "MALE"};
    f->item[G_GENDER + 2] = (radio){0, 172, 12, "FEMALE"};
    for (int k = 0; k < CLASSES; ++k) {
        /* The first seven by rows from 24; Monk with Thief; then 80. */
        int row = k <= 6 ? 24 + 8 * k : k == 7 ? 72 : 80 + 8 * (k - 8);
        f->item[FIRST_CLASS + k] = (radio){0, row, 148, class_names[k]};
    }
    f->item[FIRST_CLASS + 7].flags = F_HIDDEN;
    f->item[G_LAW + 1] = (radio){0, 100, 12, "LAWFUL"};
    f->item[G_LAW + 2] = (radio){0, 108, 12, "NEUTRAL"};
    f->item[G_LAW + 3] = (radio){0, 116, 12, "CHAOTIC"};
    f->item[G_GOOD + 1] = (radio){0, 128, 12, "GOOD"};
    f->item[G_GOOD + 2] = (radio){0, 136, 12, "NEUTRAL"};
    f->item[G_GOOD + 3] = (radio){F_HIDDEN, 144, 12, "EVIL"};
    f->race = 7;
    f->law = f->good = f->gender = 1;
    f->class_ = 3;
    f->dwarf = 0;
}

/* The form's screen: the window, the four headings (1102:03a0 at 12, 12;
 * 88, 12; 152, 12; 12, 144 in 0x8c), the buttons. */
static void draw_form(form *f)
{
    cok_adventure *game = f->game;
    queen_full_window(game);
    queen_text(game, 12, 12, "PICK RACE", 0x8c);
    queen_text(game, 88, 12, "PICK ALIGNMENT", 0x8c);
    queen_text(game, 152, 12, "PICK GENDER", 0x8c);
    queen_text(game, 12, 144, "PICK CLASS", 0x8c);
    memset(f->shown, 0xff, sizeof f->shown);
}

static bool is_group(int i);

/* The buttons not drawn as they now are (2aa6:151f: those a message
 * left unmarked). */
static void draw_changed(form *f)
{
    for (int i = 1; i < ITEMS; ++i)
        if (!is_group(i) && f->shown[i] != f->item[i].flags) draw_radio(f, i);
}

/* Done and Exit on the bar (1bdb:008e): at 8 and 48. */
static const struct {
    const char *text;
    int col;
} form_buttons[2] = {{"Done", 8}, {"Exit", 48}};

static void bar(form *f)
{
    queen_bottom_bar(f->game);
    for (int i = 0; i < 2; ++i)
        queen_button(f->game, QUEEN_BAR_ROW, form_buttons[i].col, form_buttons[i].text, 4, 0);
}

static bool is_group(int i)
{
    return i == G_RACE || i == G_GENDER || i == G_CLASS || i == G_LAW || i == G_GOOD;
}

/* A click at x, y on a button of a group (2aa6:15e3's first item hit, its
 * message 4): true where one took it. */
static bool click_radio(form *f, int x, int y)
{
    for (int i = 1; i < ITEMS; ++i) {
        if (is_group(i) || (f->item[i].flags & (F_DISABLED | F_HIDDEN))) continue;
        queen_rect r = radio_rect(&f->item[i]);
        if (!queen_inside(&r, x, y)) continue;
        int group = i > G_GOOD ? G_GOOD : i > G_LAW ? G_LAW : i > G_CLASS ? G_CLASS
                    : i > G_GENDER ? G_GENDER : G_RACE;
        group_select(f, group, i - group);
        return true;
    }
    return false;
}

/* 66e5:2967, the form's key handler where there is no mouse: Left and
 * Right change the column, Up and Down the row (skipping the gaps: the
 * left column's rows 0-6, 10-14 and 18-19, the right's 0-15), Return
 * clicks where the pointer is, at the row's text. Another key is not its.
 * True where the key was. */
static bool form_keys(form *f, int key)
{
    if (key == 0x106 || key == 0x102)
        f->column = 1 - f->column;
    else if (key == 0x108 && f->row != 0)
        --f->row;
    else if (key == 0x104)
        ++f->row;
    else if (key != 0x0d)
        return false;
    if (f->column != 0) {
        if (f->row > 0xf) f->row = 0xf;
    } else if (f->row == 7) {
        f->row = 10;
    } else if (f->row == 9) {
        f->row = 6;
    } else if (f->row == 0xf) {
        f->row = 0x12;
    } else if (f->row == 0x11) {
        f->row = 0xe;
    } else if (f->row > 0x13) {
        f->row = 0x13;
    }
    if (key == 0x0d) click_radio(f, f->column != 0 ? 152 : 16, 26 + 8 * f->row);
    return true;
}

/* The dialog's polling (2aa6:1586, 15e3): a click on a button of a group
 * selects it, one on Done or a key of it ends the dialog by its callback,
 * Exit's by the flag. Returns false where the keys ran out. */
static bool run_form(form *f, bool (*done)(form *))
{
    cok_adventure *game = f->game;
    queen_rect buttons[2];
    for (int i = 0; i < 2; ++i) buttons[i] = queen_button_rect(QUEEN_BAR_ROW, form_buttons[i].col, 4);
    for (;;) {
        queen_controls_clear(game);
        for (int i = 1; i < ITEMS; ++i)
            if (!is_group(i) && !(f->item[i].flags & (F_DISABLED | F_HIDDEN)))
                queen_control_add(game, radio_rect(&f->item[i]));
        for (int i = 0; i < 2; ++i) queen_control_add(game, buttons[i]);
        draw_changed(f);
        queen_input in;
        int key = queen_read_input(game, &in);
        if (key < 0) {
            queen_controls_clear(game);
            return false;
        }
        int pick = -1; /* 0 Done, 1 Exit */
        if (in.click) {
            if (click_radio(f, in.x, in.y)) continue;
            for (int i = 0; i < 2 && pick == -1; ++i)
                if (queen_inside(&buttons[i], in.x, in.y)) pick = i;
        } else if (queen_hotkey('D', in.key)) {
            pick = 0;
        } else if (queen_hotkey('E', in.key) || queen_hotkey('#', in.key)) {
            pick = 1;
        } else if (!queen_mouse(game) && form_keys(f, in.key)) {
            continue;
        }
        if (pick == -1) {
            queen_beep(game);
            continue;
        }
        queen_controls_clear(game);
        if (pick == 0) {
            if (!done(f)) return false;
        } else {
            f->exit = true;
        }
        return true;
    }
}

/* Creation. */

/* What the levels' module (dqk 8bcc: 000f cleric spells, 0af3 thief skills,
 * 0c72 saving throws; 86b8:380e hit points; 6be8:0022 Train) gives:
 * stopped, cited, until that module is ported. */
static bool modify(cok_adventure *game, cok_character *c)
{
    (void)c;
    cok_adventure_not_yet(game, "Modify", "dqk 6a87:1293");
    return false;
}

static bool levels_not_yet(cok_adventure *game, const char *what, const char *where)
{
    (void)game, (void)what, (void)where; /* TEMP */
    return true;
}

static bool levels_cleric_spells(cok_adventure *game, cok_character *c)
{
    (void)c;
    return levels_not_yet(game, "a cleric's first spells", "dqk 8bcc:000f");
}

static bool levels_thief_skills(cok_adventure *game, cok_character *c)
{
    (void)c;
    return levels_not_yet(game, "a thief's skills", "dqk 8bcc:0af3");
}

static bool levels_saving_throws(cok_adventure *game, cok_character *c)
{
    (void)c;
    return levels_not_yet(game, "saving throws", "dqk 8bcc:0c72");
}

static bool levels_hit_points(cok_adventure *game, cok_character *c, unsigned count)
{
    (void)c, (void)count;
    return levels_not_yet(game, "the hit points of a level", "dqk 86b8:380e");
}

static bool train_silently(cok_adventure *game, cok_character *c)
{
    (void)c;
    return levels_not_yet(game, "Train", "dqk 6be8:0022");
}

static void add_effect(cok_character *character, uint8_t id)
{
    cok_character_add_effect(character, id, 0, 0xff, false); /* 148f:19c2 */
}

/* 66e5:1839: the race's size (+0xc6) and effects. */
static void race_effects(cok_character *ch)
{
    uint8_t *c = ch->record;
    switch (c[0x58]) {
    case 5: c[0xc6] = 1; add_effect(ch, 0x5c), add_effect(ch, 0x5e); break;
    case 3: case 4: c[0xc6] = 1; add_effect(ch, 0x5e), add_effect(ch, 0x1a), add_effect(ch, 0x2f); break;
    case 0: case 1: c[0xc6] = 2; add_effect(ch, 0x5f); break;
    case 2: c[0xc6] = 2; add_effect(ch, 0x12); break;
    default: c[0xc6] = 2; break;
    }
}

static void set_word(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

/* 66e5:0dbf: the class's first level or levels, the experience (500,000
 * for two classes, 333,333 for three, else 1,000,001 set before), and the
 * class's effects: a paladin 8 and its +0x88, a ranger 0x69. */
static void class_levels(cok_character *ch)
{
    uint8_t *c = ch->record;
    unsigned class_ = word_at(c + 0x5a);
    switch (class_) {
    case 0: case 2: case 5: case 6: c[0xa5 + class_] = 1; break;
    case 3: c[0x88] = 1, c[0xa8] = 1; add_effect(ch, 8); break;
    case 4: c[0xa9] = 1; add_effect(ch, 0x69); break;
    case 1:
        c[0xa6] = 1;
        set_word(c + 0x44, 0x4241), set_word(c + 0x46, 0x000f);
        c[0x19a] = 1;
        break;
    case 8: c[0xa5] = c[0xa7] = 1; goto two;
    case 9: c[0xa5] = c[0xa7] = c[0xaa] = 1; goto three;
    case 10: c[0xa5] = c[0xa9] = 1; add_effect(ch, 0x69); goto two;
    case 11: c[0xa5] = c[0xaa] = 1; goto two;
    case 12: c[0xa5] = c[0xab] = 1; goto two;
    case 13: c[0xa7] = c[0xaa] = 1; goto two;
    case 14: c[0xa7] = c[0xab] = 1; goto two;
    case 15: c[0xa7] = c[0xaa] = c[0xab] = 1; goto three;
    case 16: c[0xaa] = c[0xab] = 1; goto two;
    default: break;
    }
    return;
three:
    set_word(c + 0x46, 5), set_word(c + 0x44, 0x1615);
    return;
two:
    set_word(c + 0x46, 7), set_word(c + 0x44, 0xa120);
}

/* 66e5:220a, Done's callback: the race's effects, 1,000,001 experience
 * and level 1 as the highest, the class's levels, a thief's skills, no
 * usable classes and base THAC0 0 raised to each class's at its level, the
 * saving throws. */
static bool finish_form(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    race_effects(ch);
    set_word(c + 0x46, 0x000f), set_word(c + 0x44, 0x4241);
    c[0x91] = 1;
    class_levels(ch);
    if (c[0xab] > 0 && !levels_thief_skills(game, ch)) return false;
    c[0xbf] = 0;
    c[0x87] = 0;
    const cok_rules *rules = cok_game_rules(game);
    for (unsigned k = 0; k < 7; ++k) {
        if (c[0xa5 + k] == 0) continue;
        uint8_t t = queen_thac0[k][c[0xa5 + k] < 0x16 ? c[0xa5 + k] : 0x15];
        if (t > c[0x87]) c[0x87] = t;
        c[0xbf] = (uint8_t)(c[0xbf] + rules->characters.class_usable[k]);
    }
    return levels_saving_throws(game, ch);
}

static bool form_done(form *f)
{
    return finish_form(f->game, f->character);
}

/* 66e5:2a6c: the form; the character's race, gender, class and alignment
 * set, and what Done sets. 1 where Done was taken, 0 for Exit, -1 where the
 * run stopped. */
static int pick_form(cok_adventure *game, cok_character *ch)
{
    form *f = calloc(1, sizeof *f);
    if (f == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
        return -1;
    }
    f->game = game;
    f->character = ch;
    build_form(f);
    draw_form(f);
    draw_changed(f);
    bar(f);
    for (int g = 0; g < ITEMS; ++g)
        if (is_group(g)) {
            int *v = group_var(f, g);
            group_select(f, g, *v);
        }
    bool ok = run_form(f, form_done);
    int result = !ok ? -1 : f->exit ? 0 : 1;
    free(f);
    return result;
}

/* The deity (66e5:36b0): none for a character with no cleric levels
 * (+0xa5), Reorx for a dwarf's, else picked from a list by the alignment:
 * the four of the good (0, 3, 6: Paladine, Majere, Kiri-Jolith,
 * Mishakal) or the three of the neutral (1, 4, 7: Sirrion, Reorx,
 * Shinare); then what the deity gives (66e5:393f). The list's Select takes
 * it, Escape ends the creation, Exit does too without a mouse (with one it
 * asks again). 1, 0 where it ended, -1 where the run stopped. */
static const char *const deities[8] = {"None",     "Paladine", "Majere", "Kiri-Jolith",
                                       "Mishakal", "Sirrion",  "Reorx",  "Shinare"};
static const uint8_t bit_of[8] = {1, 2, 4, 8, 0x10, 0x20, 0x40, 0x80}; /* DS:68bc */

static void deity_gifts(cok_character *ch)
{
    uint8_t *c = ch->record;
    switch ((int)word_at(c + 0x19b)) {
    case 1: c[0x15e] |= bit_of[0]; break;
    case 2: c[0x15e] |= bit_of[2]; break;
    case 3: c[0x15e] |= bit_of[3], ++c[0x87]; break;
    case 4: c[0x15e] |= bit_of[4], c[0x15e] |= bit_of[5], c[0x15e] |= bit_of[6]; break;
    case 5: c[0x15e] |= bit_of[7]; break;
    case 6:
        if (word_at(c + 0x58) == 3 || word_at(c + 0x58) == 4) ++c[0x87];
        break;
    case 7: c[0x15e] |= bit_of[6]; break;
    default: break;
    }
}

static int pick_deity(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    queen_full_window(game);
    if (c[0xa5] == 0) {
        set_word(c + 0x19b, 0);
        return 1;
    }
    if (word_at(c + 0x58) == 3 || word_at(c + 0x58) == 4) {
        set_word(c + 0x19b, 6);
        deity_gifts(ch);
        return 1;
    }
    unsigned alignment = word_at(c + 0x62);
    if (alignment > 7 || alignment % 3 == 2) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "a cleric of alignment %u: the deity's list is not built (dqk "
                           "66e5:36b0 reads a list it has not made)", alignment);
        return -1;
    }
    bool good = alignment % 3 == 0;
    char text[4][24];
    const char *items[4];
    size_t count = good ? 4 : 3;
    for (size_t i = 0; i < count; ++i) {
        snprintf(text[i], sizeof text[i], "\t%s", deities[(good ? 0u : 4u) + 1 + i]);
        items[i] = text[i];
    }
    static const char *const bar[2] = {"Select", "Exit"};
    cok_adventure_log(game, "list", "Pick Diety");
    for (size_t i = 0; i < count; ++i) cok_adventure_log(game, "item", text[i]);
    size_t at = 0;
    for (;;) {
        queen_say(game, 1, 2, "Pick Diety", 0x0c);
        int key = queen_list_menu(game, bar, 2, items, count, &at, 3, 0x16, 0x26);
        if (key < 0) return -1;
        bool mouse = queen_mouse(game);
        if (key == 0) break;
        if ((mouse && key == 0x1b) || (!mouse && key != 0)) return 0;
    }
    set_word(c + 0x19b, (uint16_t)((good ? 0 : 4) + at + 1));
    cok_adventure_log(game, "choice", deities[word_at(c + 0x19b)]);
    deity_gifts(ch);
    return 1;
}

/* The rolls: 66e5:1a90 and 0f47. */

static void mark_known(uint8_t *c, unsigned id)
{
    c[0x157 + (id - 1) / 8] |= bit_of[(id - 1) % 8];
}

/* 66e5:0f47: score a (0 strength ... 5 charisma) changed by the age
 * (DS:04b7: the ages from its second word), kept within the race's
 * limits (DS:02b8), then at least the class's (DS:04fd); strength 18 of a
 * knight, fighter, paladin or ranger gets an exceptional one, 1-100 at
 * most the race's. */
static void age_score(cok_adventure *game, uint8_t *c, unsigned a)
{
    unsigned race = word_at(c + 0x58) < 7 ? word_at(c + 0x58) : 0, gender = word_at(c + 0x60) & 1,
             age = word_at(c + 0x52);
    const uint8_t *limits = queen_score_limits[race], *ages = queen_age_limits[race];
    bool old[4];
    for (unsigned i = 0; i < 4; ++i) old[i] = age > word_at(ages + 2 + 2 * i);
    uint8_t *s = &c[0x79 + 2 * a];
    uint8_t low, high;
    switch (a) {
    case 0:
        if (*s > 0 && old[0]) ++*s;
        if (*s > 0 && old[1]) --*s;
        if (*s > 0 && old[2]) *s = (uint8_t)(*s - 2);
        if (*s > 0 && old[3]) --*s;
        low = limits[gender], high = limits[2 + gender];
        break;
    case 1:
        if (old[1]) ++*s;
        if (old[3]) ++*s;
        low = limits[6], high = limits[7];
        break;
    case 2:
        if (!old[0]) --*s;
        if (old[1]) ++*s;
        if (old[2]) ++*s;
        if (old[3]) ++*s;
        low = limits[8], high = limits[9];
        break;
    case 3:
        if (old[2]) *s = (uint8_t)(*s - 2);
        if (old[3]) --*s;
        low = limits[10], high = limits[11];
        break;
    case 4:
        if (old[1]) --*s;
        if (old[2]) --*s;
        if (old[3]) --*s;
        low = limits[12], high = limits[13];
        break;
    default:
        low = limits[14], high = limits[15];
        break;
    }
    if (*s < low) *s = low;
    if (*s > high) *s = high;
    unsigned class_ = word_at(c + 0x5a);
    uint8_t least = queen_class_minimums[class_ < 17 ? class_ : 0][a];
    if (*s < least) *s = least;
    /* 66e5:1523: a wisdom under 13 of a class above 8 and below 12 in the
     * same test (which no class passes) is 13. */
    if (a == 2 && *s < 13 && class_ <= 8 && class_ >= 12) *s = 13;
    if (a == 0 && *s == 18 && (c[0xa7] > 0 || c[0xa9] > 0 || c[0xa8] > 0 || c[0xa6] > 0)) {
        c[0x84] = (uint8_t)(cok_tp_random(&game->vm.seed, 100) + 1);
        if (limits[4 + gender] < c[0x84]) c[0x84] = limits[4 + gender];
    }
}

/* 66e5:1a90: each class at level 1, the six scores (the best of six 3d6+1,
 * each changed by the race: dwarves +1 constitution and -1 charisma, elves
 * +1 dexterity and -1 constitution, half-elves +2 dexterity, kenders -1
 * strength and +2 dexterity), the base combat fields, the mage's order,
 * the spells a day and the spells known, 200 steel, the hit points of
 * 86b8:380e and Train until nothing more can train. False where it
 * stopped. */
static bool roll(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    uint32_t *seed = &game->vm.seed;
    for (unsigned k = 0; k <= 6; ++k)
        if (c[0xa5 + k] > 0) c[0xa5 + k] = 1;
    c[0x84] = 0;
    unsigned race = word_at(c + 0x58);
    for (unsigned a = 0; a < 6; ++a) {
        c[0x79 + 2 * a] = 0;
        for (unsigned t = 1; t <= 6; ++t) {
            uint8_t v = (uint8_t)(cok_dice(seed, 3, 6) + 1);
            switch (race) {
            case 0: case 1:
                if (a == 3) ++v;
                if (a == 4) --v;
                break;
            case 2:
                if (a == 3) v = (uint8_t)(v + 2);
                break;
            case 3: case 4:
                if (a == 4) ++v;
                if (a == 5) --v;
                break;
            case 5:
                if (a == 0) --v;
                if (a == 3) v = (uint8_t)(v + 2);
                break;
            default: break;
            }
            if (c[0x79 + 2 * a] < v) c[0x79 + 2 * a] = v;
        }
        age_score(game, c, a);
        c[0x78 + 2 * a] = c[0x79 + 2 * a];
        if (a == 0) c[0x85] = c[0x84];
    }
    c[0x198] = c[0x89];
    c[0xb3] = 2, c[0xb5] = 1, c[0xb7] = 2;
    c[0x90] = 12;
    unsigned alignment = word_at(c + 0x62);
    if (alignment <= 7) {
        if (alignment % 3 == 0 && c[0xaa] > 0) c[0x19d] = 1;
        if (alignment % 3 == 1 && c[0xaa] > 0) c[0x19d] = 2;
    }
    memset(c + 0x167, 0, 0x24);
    unsigned classes = 0;
    for (unsigned k = 0; k <= 6; ++k) {
        if (c[0xa5 + k] == 0) continue;
        if (k == 0) c[0x167] = (int16_t)word_at(c + 0x19b) <= 4 ? 1 : 2;
        if (k == 5) c[0x179] = 1;
        if (k == 0) {
            if (!levels_cleric_spells(game, ch)) return false;
            for (unsigned i = 1; i <= 10 && queen_cleric_spells[i] != 0; ++i)
                mark_known(c, queen_cleric_spells[i]);
        } else if (k == 5) {
            c[0x159] |= bit_of[2], c[0x158] |= bit_of[2], c[0x158] |= bit_of[3];
            c[0x158] |= bit_of[6], c[0x158] |= bit_of[7], c[0x159] |= bit_of[1];
            c[0x159] |= bit_of[2];
        }
        ++classes;
    }
    set_word(c + 0x4c, 200);
    c[0x89] = c[0xc0] = c[0x198] = 0;
    if (!levels_hit_points(game, ch, classes)) return false;
    return train_silently(game, ch);
}

/* The sheet with Done, Reroll Stats, Modify and Exit on the bar
 * (66e5:19b8, 1f25): 0 Done, 1 Reroll Stats, 2 Modify, 3 Exit, -1 where
 * the keys ran out. */
static int sheet_bar(cok_adventure *game)
{
    static const struct {
        const char *text;
        int col, key;
    } buttons[4] = {{"Done", 168, 'D'}, {"Reroll Stats", 8, 'R'}, {"Modify", 112, 'M'},
                    {"Exit", 208, 'E'}};
    queen_rect rects[4];
    queen_bottom_bar(game);
    queen_controls_clear(game);
    for (int i = 0; i < 4; ++i) {
        int width = (int)strlen(buttons[i].text);
        queen_button(game, QUEEN_BAR_ROW, buttons[i].col, buttons[i].text, width, 0);
        rects[i] = queen_button_rect(QUEEN_BAR_ROW, buttons[i].col, width);
        queen_control_add(game, rects[i]);
    }
    int result = -1;
    for (;;) {
        queen_input in;
        int key = queen_read_input(game, &in);
        if (key < 0) break;
        int pick = -1;
        for (int i = 0; i < 4 && pick < 0; ++i)
            if (in.click ? queen_inside(&rects[i], in.x, in.y)
                         : (queen_hotkey(buttons[i].key, in.key) ||
                            (i == 3 && queen_hotkey('#', in.key))))
                pick = i;
        if (pick < 0) {
            queen_beep(game);
            continue;
        }
        result = pick;
        break;
    }
    queen_controls_clear(game);
    return result;
}

static void log_roll(cok_adventure *game, const uint8_t *c)
{
    char line[96];
    snprintf(line, sizeof line, "%u %u %u %u %u %u (%u) age %u", c[0x79], c[0x7b], c[0x7d], c[0x7f],
             c[0x81], c[0x83], c[0x84], word_at(c + 0x52));
    cok_adventure_log(game, "scores", line);
}

/* 66e5:1f25: the age (the race's dice at the class, DS:03f3: a base word, a
 * count of dice and their sides, + 10; a class of more than one has the most
 * of its first class's dice, none rolled), the roll and its sheet, then the
 * bar until Done (the scores kept as the base scores) or Exit. 1, 0 for
 * Exit, -1 where the run stopped. */
static int roll_loop(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    unsigned race = word_at(c + 0x58) < 7 ? word_at(c + 0x58) : 0, class_ = word_at(c + 0x5a);
    uint32_t *seed = &game->vm.seed;
    const uint8_t *dice = queen_age_dice[race];
    if (class_ <= 6) {
        const uint8_t *d = dice + 4 * class_;
        set_word(c + 0x52, (uint16_t)(word_at(d) + cok_dice(seed, d[2], d[3]) + 10));
    } else if (class_ >= 8 && class_ <= 16) {
        /* The most of one class's dice, none rolled: the cleric's for
         * classes 8-12, the fighter's for 14, the mage's for the others. */
        const uint8_t *d = dice + 4 * (class_ <= 12 ? 0 : class_ == 14 ? 2 : 5);
        set_word(c + 0x52, (uint16_t)(word_at(d) + d[2] * d[3]));
    }
    if (!roll(game, ch) || !queen_sheet(game, ch)) return -1;
    for (;;) {
        log_roll(game, c);
        int pick = sheet_bar(game);
        if (pick < 0) return -1;
        if (pick == 2) {
            if (!modify(game, ch) || !queen_sheet(game, ch)) return -1;
        } else if (pick == 1) {
            if (!roll(game, ch) || !queen_sheet(game, ch)) return -1;
        } else {
            if (pick == 3) return 0;
            if (!queen_sheet(game, ch)) return -1;
            for (unsigned i = 0; i < 6; ++i) c[0x78 + 2 * i] = c[0x79 + 2 * i];
            c[0x85] = c[0x84];
            return 1;
        }
    }
}

/* 66e5:193f: the name (15 characters at most) asked on the bar until it
 * is a file name's start (not empty, not a digit); stored at +0x68. 1,
 * 0 where Escape ended it, -1. */
static int ask_name(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    for (;;) {
        char line[40];
        int done = queen_input_line(game, "Character name:", 0, 8, line, 15);
        if (done < 0) return -1;
        for (char *p = line; *p != '\0'; ++p)
            if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 0x20);
        memset(c + 0x68, 0, 16);
        memcpy(c + 0x68, line, strlen(line) < 15 ? strlen(line) : 15);
        char base[16];
        size_t n = 0;
        for (const char *p = line; *p != '\0' && n < 8; ++p)
            if (strchr(" =+<>\"[].*,?/\\:;|", *p) == NULL) base[n++] = *p;
        base[n] = '\0';
        if (base[0] == '\0' && done == 1) continue;
        if (base[0] >= '0' && base[0] <= '9') continue;
        cok_adventure_log(game, "input", line);
        return done;
    }
}

/* 66e5:30d3: the spells known: all of the mage's first level and by the
 * order and the levels of spells a day those it gets; 34e4: the items of
 * the race and class (DS:180d), the first weapon readied. */
static void known_spells(cok_character *ch)
{
    uint8_t *c = ch->record;
    if (c[0xaa] > 0)
        for (unsigned id = 1; id <= 0x7e; ++id)
            if (queen_spell_levels[id][0] == 2 && queen_spell_levels[id][1] == 0 &&
                queen_spell_levels[id][2] == 1)
                mark_known(c, id);
    /* By the spells a day of level 2-9 (+0x17a-+0x181: those that are
     * more than 0), the bytes (+0x157 on) and bits it makes known. */
    typedef struct {
        unsigned a_day;
        struct {
            unsigned at, bit;
        } set[6];
    } order_spells;
    static const order_spells white[7] = {
        {0x17a, {{0x15a, 4}, {0x15b, 0}, {0x15b, 1}, {0, 8}}},
        {0x17b, {{0x15c, 5}, {0x15c, 6}, {0x15d, 0}, {0x15d, 3}, {0x15d, 5}}},
        {0x17c, {{0x161, 1}, {0x161, 7}, {0x161, 0}, {0x161, 4}, {0x161, 6}}},
        {0x17d, {{0x162, 2}, {0x162, 3}}},
        {0x17e, {{0x164, 7}}},
        {0x17f, {{0x165, 2}, {0x165, 4}}},
        {0x180, {{0x166, 0}}},
    }, other[7] = {
        {0x17a, {{0x15a, 5}, {0x15a, 6}, {0x15a, 7}, {0x15b, 1}, {0x15b, 2}}},
        {0x17b, {{0x15c, 4}, {0x15c, 6}, {0x15c, 7}, {0x15d, 1}, {0x15d, 2}, {0x15d, 6}}},
        {0x17c, {{0x161, 3}, {0x161, 2}, {0x161, 4}, {0x161, 6}}},
        {0x17d, {{0x162, 3}, {0x165, 5}, {0x165, 6}}},
        {0x17e, {{0x165, 0}, {0x165, 1}, {0x164, 6}}},
        {0x17f, {{0x165, 4}}},
        {0x180, {{0x166, 2}}},
    };
    const order_spells *table = c[0x19d] == 1 ? white : other;
    for (unsigned i = 0; i < 7; ++i) {
        if (c[table[i].a_day] == 0) continue;
        for (unsigned k = 0; k < 6 && table[i].set[k].bit < 8 && table[i].set[k].at != 0; ++k)
            c[table[i].set[k].at] |= bit_of[table[i].set[k].bit];
    }
    if (c[0x181] > 0)
        for (unsigned id = 1; id <= 0x7e; ++id)
            if (queen_spell_levels[id][0] == 2 && queen_spell_levels[id][1] == 0)
                mark_known(c, id);
}

static bool kit(cok_adventure *game, cok_character *ch)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_rules_item_record *I = &rules->item_record;
    uint8_t *c = ch->record;
    unsigned race = word_at(c + 0x58) < 7 ? word_at(c + 0x58) : 0,
             class_ = word_at(c + 0x5a) < 17 ? word_at(c + 0x5a) : 0;
    c[0xc9] = 0;
    bool weapon = false;
    for (unsigned i = 0; i < 5; ++i) {
        uint8_t id = queen_kits[race][class_][2 * i];
        if (id == 0 || game->item_table[id][0] == 0xff) continue;
        ++c[0xc9];
        uint8_t item[COK_ITEM_SIZE] = {0};
        memcpy(item + I->file_at, game->item_table[id], COK_ITEM_ENTRY);
        item[0x33] &= 0xf8; /* the words identified */
        if (id == 0x50 || id == 0x56 || id == 0x5b) item[I->count] = 0x28;
        item[I->readied] = 0;
        if (id != 0x52 && id != 0x53 && id != 0x54 && id != 0x56 && id != 0x5d) {
            bool arm = game->item_types.type[item[I->type]][0] == 0;
            if (!arm || !weapon) item[I->readied] = 1;
            if (arm) weapon = true;
        }
        if (!cok_character_insert_item(ch, 0, item)) {
            cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
            return false;
        }
    }
    char error[300];
    if (!cok_character_stats(ch, rules, &game->item_types, error, sizeof error)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", error);
        return false;
    }
    return true;
}

/* The icon's choice (66e5:0cf1, 0b85, 0570, 0372, 040e, 09e4): the 49
 * bodies of CBODY.TLB in a grid of 7 by 7 cells of 24 pixels from 8, 8,
 * the selected one framed, the character's name, gender, race and class
 * on the right, and the selected one drawn as it stands ready (CBODY's id)
 * and in action (its id + 128). A click on a cell, or an arrow key, makes
 * it the selection (+0xc4) and draws the screen again; Done or Exit (E,
 * Escape) end it. 1 for Done, 0 for Exit, -1 where the run stopped. */
static const char *const genders[3] = {"Male", "Female", "Steel"};
static const char *const races[8] = {"Silvanesti Elf", "Qualinesti Elf", "Half-Elf",
                                     "Mountain Dwarf", "Hill Dwarf", "Kender", "Human", "Monster"};
static const char *const classes[18] = {
    "Cleric", "Knight", "Fighter", "Paladin", "Ranger", "Magic-User", "Thief", "Monk",
    "Cleric/Fighter", "Cleric/Fighter/M-U", "Cleric/Ranger", "Cleric/Magic-User", "Cleric/Thief",
    "Fighter/Magic-User", "Fighter/Thief", "Fighter/M-U/Thief", "Magic-User/Thief", "Illegal type"};

/* A cell's backdrop (204c:00df makes it, slot 0x1e with ink 0xf and slot
 * 0x1f with 0x10): 24 by 24, the top row and the left two columns in the
 * highlight, the bottom row and the right two columns black. A cell not
 * selected has colour 0x57 (DUNGCOM's grey-blue) and a white highlight; the
 * selected one (and the preview of the action) colour 8 and the grey-blue
 * for a highlight. */
static void backdrop(cok_adventure *game, int row, int col, bool selected)
{
    uint8_t fill = selected ? 8 : 0x57, high = selected ? 0x57 : 0x0f;
    queen_fill(game, row, col, row + 24, col + 24, fill);
    queen_fill(game, row, col, row + 1, col + 23, high);
    queen_fill(game, row, col, row + 24, col + 1, high);
    queen_fill(game, row + 1, col + 1, row + 23, col + 2, high);
    queen_fill(game, row + 1, col + 22, row + 23, col + 24, 0);
    queen_fill(game, row, col + 23, row + 1, col + 24, 0);
    queen_fill(game, row + 23, col + 1, row + 24, col + 24, 0);
}

static void picker_cell(cok_adventure *game, unsigned icon, bool selected)
{
    int row = 8 + 24 * (int)(icon / 7), col = 8 + 24 * (int)(icon % 7);
    backdrop(game, row, col, selected);
    queen_draw_library(game, QUEEN_LIB_CBODY, icon, row, col);
}

static void picker_screen(cok_adventure *game, cok_character *ch, unsigned selected)
{
    uint8_t *c = ch->record;
    /* 66e5:0570 and 20b2:02fa: the window cleared, then FRAME's pieces 1,
     * 2, 3 and 5, the bar's (4) and 6. */
    queen_clear_cells(game, 1, 1, 0x26, 0x16);
    for (unsigned id = 1; id <= 5; ++id)
        if (id != 4) queen_draw_frame(game, id, 0, 0);
    queen_bottom_bar(game);
    queen_draw_frame(game, 6, 0, 0);
    queen_set_palette(game, QUEEN_LIB_DUNGCOM, 1, 0);
    for (unsigned i = 0; i < 49; ++i) picker_cell(game, i, i == selected);
    char line[48];
    queen_say(game, 0x17, 8, (const char *)c + 0x68, 7);
    unsigned gender = word_at(c + 0x60), race = word_at(c + 0x58), class_ = word_at(c + 0x5a);
    queen_say(game, 0x17, 9, genders[gender < 3 ? gender : 0], 7);
    queen_say(game, 0x17, 0x0a, races[race < 8 ? race : 7], 7);
    snprintf(line, sizeof line, "%s", classes[class_ < 18 ? class_ : 17]);
    queen_say(game, 0x17, 0x0c, line, 7);
    queen_text(game, 16, 188, "ready    action", 0x87);
    backdrop(game, 32, 200, false);
    queen_draw_library(game, QUEEN_LIB_CBODY, selected, 32, 200);
    backdrop(game, 32, 272, true);
    queen_draw_library(game, QUEEN_LIB_CBODY, selected + 128, 32, 272);
}

static int pick_icon(cok_adventure *game, cok_character *ch)
{
    uint8_t *c = ch->record;
    unsigned selected = c[0xc4] < 49 ? c[0xc4] : 0;
    queen_rect done = queen_button_rect(QUEEN_BAR_ROW, 8, 4), exit = queen_button_rect(QUEEN_BAR_ROW, 48, 4),
               grid = {8, 8, 8 + 168, 8 + 168};
    for (;;) {
        picker_screen(game, ch, selected);
        queen_bottom_bar(game);
        queen_button(game, QUEEN_BAR_ROW, 8, "Done", 4, 0);
        queen_button(game, QUEEN_BAR_ROW, 48, "Exit", 4, 0);
        queen_controls_clear(game);
        queen_control_add(game, grid);
        queen_control_add(game, done);
        queen_control_add(game, exit);
        for (;;) {
            queen_input in;
            int key = queen_read_input(game, &in);
            if (key < 0) return -1;
            unsigned next = selected;
            bool moved = false;
            if (in.click) {
                if (queen_inside(&done, in.x, in.y)) return c[0xc4] = (uint8_t)selected, 1;
                if (queen_inside(&exit, in.x, in.y)) return 0;
                if (queen_inside(&grid, in.x, in.y)) {
                    next = (unsigned)((in.y - 8) / 24 * 7 + (in.x - 8) / 24);
                    moved = true;
                }
            } else if (queen_hotkey('D', in.key)) {
                return c[0xc4] = (uint8_t)selected, 1;
            } else if (queen_hotkey('E', in.key) || queen_hotkey('#', in.key)) {
                return 0;
            } else {
                /* 66e5:09e4: Right, Down, Left and Up, wrapping. */
                unsigned x = selected % 7, y = selected / 7;
                switch (in.key) {
                case 0x102: x = x == 6 ? 0 : x + 1; moved = true; break;
                case 0x104: y = y == 6 ? 0 : y + 1; moved = true; break;
                case 0x106: x = x == 0 ? 6 : x - 1; moved = true; break;
                case 0x108: y = y == 0 ? 6 : y - 1; moved = true; break;
                default: break;
                }
                next = y * 7 + x;
            }
            if (!moved) {
                queen_beep(game);
                continue;
            }
            selected = next;
            c[0xc4] = (uint8_t)selected;
            break;
        }
    }
}

bool queen_create(cok_adventure *game)
{
    cok_character *ch = calloc(1, sizeof *ch);
    if (ch == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory");
        return false;
    }
    uint8_t *c = ch->record;
    /* 66e5:2f95: base armour class and THAC0, active, size 1, the icon's slot. */
    c[0xbb] = 0x32;
    c[0x87] = 0x28;
    c[0x18b] = 1;
    c[0x8a] = 1;
    c[0xc5] = 0x0d;
    bool ok = true;
    { char t[32]; snprintf(t, sizeof t, "%08x", game->vm.seed); cok_adventure_log(game, "seed", t); } /* TEMP */
    int step = pick_form(game, ch);
    if (step == 1) step = pick_deity(game, ch);
    if (step == 1) step = roll_loop(game, ch);
    if (step == 1) step = ask_name(game, ch);
    if (step == 1) {
        cok_character_default_body(cok_game_rules(game)->character, c);
        step = pick_icon(game, ch);
    }
    if (step == 1) {
        c[0xc7] = c[0xc8] = 2;
        char prompt[48];
        snprintf(prompt, sizeof prompt, "Save %s?", (const char *)c + 0x68);
        int yes = queen_yes_no(game, prompt);
        if (yes < 0)
            step = -1;
        else if (yes) {
            known_spells(ch);
            ok = kit(game, ch) && queen_write_character(game, ch);
        }
    }
    cok_character_free(ch);
    free(ch);
    return ok && step >= 0;
}
