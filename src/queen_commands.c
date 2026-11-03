#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "command.h"
#include "dark_queen.h"
#include "ecl.h"
#include "field.h"
#include "round.h"

#include <stdio.h>
#include <string.h>

/* The party member's command menu of Dark Queen (dqk 5045:08cf's loop from 0925, its menu built
 * by 0cd7 and shown by 1bdb:1595): the items of DS:50b3 by index, Move 0 (never shown: only the
 * direction keys give it) to End 12. Each command's routine is ported or a cited stop. */
enum {
    ITEM_MOVE, ITEM_AIM, ITEM_USE, ITEM_CAST, ITEM_TURN, ITEM_GUARD, ITEM_QUICK, ITEM_DELAY,
    ITEM_BANDAGE, ITEM_YELL, ITEM_VIEW, ITEM_SPEED, ITEM_END, ITEM_COUNT,
};
static const char *const item_words[ITEM_COUNT] = {
    "Move", "Aim", "Use", "Cast", "Turn", "Guard", "Quick", "Delay", "Bandage", "Yell", "View",
    "Speed", "End",
};

/* The record's byte offsets the menu reads (Dark Queen's layout, rules_dark_queen.c): the race
 * (0x58, a word with 0x59 0 for a kender); the class levels are read by the field layout. */
enum { RACE = 0x58, RACE_HIGH = 0x59, MEMORIZED_BYTES = 0x89 };

static bool type_past(cok_adventure *game)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED,
                       "an item type past ITEMS reads past the table (DS:5886)");
    return false;
}

/* The class level byte k of c's record as 177e:2234 reads it: the byte unsigned (its max with 0 is
 * the same for an unsigned byte), so 0xff is a level 255. */
static unsigned class_level(const cok_character_layout *L, const uint8_t *r, size_t k)
{
    return cok_field_u8_at(L, r, COK_FIELD_CLASS_LEVEL, k);
}

/* 0cd7: the items the record may choose, in index order (Aim always; Use while it has items;
 * Cast while a memorised spell's byte (0x89 bytes from +0xce, low 7 bits) is set, it may cast
 * this round and var COK_VAR_NO_MAGIC is 0; Turn for a cleric (class level 0 above 0, or class 3
 * above 2) with fewer turns than undead; Guard unless the readied weapon is a missile weapon not
 * thrown; Bandage while cok_combat_bandage would; Yell for a kender (+0x58 5, +0x59 0) while no
 * one has yelled; View, Speed, End and Quick and Delay always). Quick's index is split. */
static bool build(cok_adventure *game, cok_character *c, int ids[ITEM_COUNT], size_t *count,
                  int *split)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    const uint8_t *r = c->record;
    const cok_combat_record *cr = c->combat;
    size_t n = 0;
    ids[n++] = ITEM_AIM;
    if (cok_field_u8(L, r, COK_FIELD_ITEM_COUNT) > 0) ids[n++] = ITEM_USE;
    bool memorised = false;
    size_t at = cok_field_offset(L, COK_FIELD_MEMORIZED);
    for (size_t i = 0; i < MEMORIZED_BYTES; ++i)
        if ((r[at + i] & 0x7f) != 0) memorised = true;
    if (memorised && cr->may_cast != 0 && cok_ecl_var(&game->vm, COK_VAR_NO_MAGIC) == 0)
        ids[n++] = ITEM_CAST;
    if ((class_level(L, r, 0) > 0 || class_level(L, r, 3) > 2) && cr->turns < game->undead)
        ids[n++] = ITEM_TURN;
    bool missile = false, thrown = false;
    if (!cok_combat_missile_weapon(&game->item_types, c, &missile)) return type_past(game);
    if (missile && !cok_combat_thrown(&game->item_types, c, &thrown)) return type_past(game);
    if (!missile || thrown) ids[n++] = ITEM_GUARD;
    ids[n++] = ITEM_QUICK;
    ids[n++] = ITEM_DELAY;
    if (cok_combat_bandage(game, false)) ids[n++] = ITEM_BANDAGE;
    if (cok_field_u8(L, r, COK_FIELD_RACE) == 5 && r[RACE_HIGH] == 0 &&
        game->combat.yelled == NULL)
        ids[n++] = ITEM_YELL;
    ids[n++] = ITEM_VIEW;
    ids[n++] = ITEM_SPEED;
    ids[n++] = ITEM_END;
    *split = -1;
    for (size_t i = 0; i < n; ++i)
        if (ids[i] == ITEM_QUICK) *split = (int)i;
    *count = n;
    return true;
}

/* The menu's text as 0e46 builds it for the log: the words in order, each followed by a blank,
 * or by '@' after Quick (the row break, DS:50b3's '@'); none after the last. */
static void text_of(const int ids[ITEM_COUNT], size_t count, char out[64])
{
    out[0] = '\0';
    for (size_t i = 0; i < count; ++i) {
        strcat(out, item_words[ids[i]]);
        if (i + 1 < count) strcat(out, ids[i] == ITEM_QUICK ? "@" : " ");
    }
}

/* The codes that take a menu's result (0cd7's c2f5 filter: the extended keys' 0x81-0x88 and
 * 0x12, Alt-A and Alt-Q (0xc0, 0xd0), Alt-M (0xcc), Alt-X (0xd7)); others show the menu again. */
static bool taken_code(int code)
{
    return (code >= 0x81 && code <= 0x88) || code == 0x12 || code == 0xc0 || code == 0xd0 ||
           code == 0xcc || code == 0xd7;
}

/* 0c0f (Alt-A and Alt-Q): the turn given back (the list's index less one, initiative 0x14),
 * every record of the party taken by the computer (5045:1791), the bottom bar (1bdb:036c), and
 * 200 ms (1df9:052a, one draw): command.c's quick() with the bar for its clear_row. */
static void quick_all(cok_adventure *game, cok_character *c)
{
    --game->combat.turn_index;
    c->combat->initiative = 0x14;
    for (size_t i = 0; i < game->party.count; ++i)
        cok_combat_auto(game->party.members[i], cok_game_rules(game));
    queen_bottom_bar(game);
    cok_dark_queen_wait(game, 200);
}

/* The turn's command routine by its item (08cf's table at 0925's jump table). done is set
 * where the turn ends; out is set where the keys ran out in the middle of a command (Speed); the
 * stops are cited (part numbers for the ones still to port). */
static bool command(cok_adventure *game, cok_character *c, int item, int code, bool *done,
                    bool *out)
{
    *done = false;
    *out = false;
    if (item < 0) {
        /* A key taken (0b3c's table): Move with a direction, Alt-A and Alt-Q, Alt-M, Alt-X. */
        if (code >= 0x81 && code <= 0x88)
            return cok_adventure_not_yet(game, "Move with a direction key", "dqk 5045:1099 (part 23)");
        if (code == 0xc0 || code == 0xd0) {
            quick_all(game, c); /* 0c0f */
            *done = true;
            return true;
        }
        if (code == 0xcc) {
            /* 0bd7: the magic toggled, its message ("magic on" DS:4a9d, "magic off" DS:4aa6);
             * the turn goes on (c8c). */
            game->combat.magic_on = !game->combat.magic_on;
            queen_bar_message(game, game->combat.magic_on ? "magic on" : "magic off");
            return true;
        }
        if (code == 0xd7)
            return cok_adventure_not_yet(game, "Alt-X (the gods)", "dqk 52db:3feb (part 22b)");
        return true; /* 0x12: nothing, the arena again (c8c) */
    }
    switch (item) {
    case ITEM_MOVE:
        return cok_adventure_not_yet(game, "Move", "dqk 5045:1099 (part 23)");
    case ITEM_AIM: return cok_adventure_not_yet(game, "Aim", "dqk 52db:37dd (part 24)");
    case ITEM_USE: return cok_adventure_not_yet(game, "Use", "dqk 86b8:0f41");
    case ITEM_CAST: return cok_adventure_not_yet(game, "Cast", "dqk 52db:2543 (part 28)");
    case ITEM_TURN: return cok_adventure_not_yet(game, "Turn", "dqk 52db:1117 (part 26)");
    case ITEM_GUARD:
        /* 0a19: 51c2:0c52 (the end, then Guarding unless forced; its message 177e:153c). */
        cok_combat_end_turn(c);
        if (c->combat->forced == 0) {
            c->combat->guarding = 1;
            queen_bar_message(game, "Guarding");
        }
        *done = true;
        return true;
    case ITEM_QUICK:
        /* 0a2c: cok_combat_auto (1791), the bottom bar, the keys flushed (2093:0130), 200 ms
         * (one draw), done; then the computer's turn for c (2d99:0ff9 = 5bb8:0000): its stop. */
        cok_combat_auto(c, cok_game_rules(game));
        queen_bottom_bar(game);
        cok_adventure_flush(game);
        cok_dark_queen_wait(game, 200);
        *done = true;
        return cok_adventure_not_yet(game, "computer's turn",
                                     cok_game_rules(game)->combat.computer_unported);
    case ITEM_DELAY:
        /* 0a60: command.c's delay (initiative 1, the turn list shifted). */
        cok_combat_delay(game, c);
        *done = true;
        return true;
    case ITEM_BANDAGE:
        return cok_adventure_not_yet(game, "Bandage", "dqk 51c2:0d63 (its say 177e:156e, part 22b)");
    case ITEM_YELL: return cok_adventure_not_yet(game, "Yell", "dqk 148f:0005 (part 22b)");
    case ITEM_VIEW: return cok_adventure_not_yet(game, "View", "dqk 86b8:0b0c (part 22b)");
    case ITEM_SPEED:
        /* 166c: the speed menu (queen_speed); the turn goes on (c8c). */
        *out = !queen_speed(game);
        return true;
    case ITEM_END:
        cok_combat_end_turn(c); /* 0b29: 51c2:0c11 */
        *done = true;
        return true;
    default: break;
    }
    return true;
}

bool queen_commands(cok_adventure *game, cok_character *c)
{
    if (c->combat == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "a combatant has no combat record");
        return false;
    }
    bool done = false;
    while (!done) {
        int ids[ITEM_COUNT], split;
        size_t count;
        if (!build(game, c, ids, &count, &split)) return false;
        char text[64];
        text_of(ids, count, text);
        cok_adventure_log(game, "menu", text);
        const char *names[ITEM_COUNT];
        for (size_t i = 0; i < count; ++i) names[i] = item_words[ids[i]];
        int result;
        for (;;) {
            result = queen_combat_menu(game, names, count, split);
            if (result < 0 || game->vm.abort) break;
            if ((size_t)result < count || taken_code(result)) break;
        }
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (result < 0) return true; /* the keys ran out: the turn not ended */
        queen_combat_menu_close(game);
        int item = (size_t)result < count ? ids[result] : -1;
        bool out;
        if (!command(game, c, item, item < 0 ? result : 0, &done, &out)) return false;
        if (game->vm.abort) return game->vm.status == COK_ECL_OK;
        if (out) return true; /* the keys ran out in Speed: the turn not ended */
        if (!done) {
            /* 08cf's c8c: the arena again, the panel, and the menu. */
            if (!cok_arena_turn(game, c, 2, true)) return false;
            game->combat.panel = true;
            if (!cok_arena_panel(game, c)) return false;
        }
    }
    return true;
}
