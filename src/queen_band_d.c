#include "queen.h"

#include "arena.h"
#include "attack.h"
#include "combat.h"
#include "ecl.h"
#include "effect.h"
#include "field.h"
#include "party.h"
#include "queen_view.h"
#include "round.h"

/* The Dark Queen of Krynn's band d spells (part 33): the 7fd8 handlers from 2cae on, as the briefs add them (d1 the kills,
 * then d3 to d6). Each handler reads the spell's record and the targets as queen_cast.c does, and shares the spell
 * engine's routines (queen_save, queen_cast_damage, the run of an effect's handler). queen_band_d_handler is the table: a
 * spell with no handler here is NULL, and queen_spell_handler then takes the plain table or stops at the descriptor's
 * citation. */

/* An undefined stop (the original reads unset memory, a NULL or a freed record): cok_adventure_fail. */
static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* The caster's record (DS:ad67): none is read through NULL, undefined. */
static uint8_t *caster_record(cok_adventure *game, const char *what)
{
    if (game->vm.character == NULL) undefined(game, what);
    return game->vm.character;
}

/* The character of a combatant's record (NULL for none, or a record not in the combat). */
static cok_character *combatant_of(cok_adventure *game, const uint8_t *record)
{
    if (record == NULL) return NULL;
    uint8_t n = cok_combat_index(&game->combat, record);
    return n == 0 ? NULL : game->combat.combatant[n].character;
}

/* The ids and the effect and damage types of band d1. Effect 0x65 is the level drain (7e76:093b, a cited row: part 38);
 * the damage types 0x48 (Destruction, Disintegrate) and 0x40 (Flesh to Stone) are the spells' own. */
enum {
    SPELL_ENERGY_DRAIN = 0x67,
    SPELL_DESTRUCTION = 0x68,
    SPELL_DISINTEGRATE = 0x6f,
    SPELL_FLESH_TO_STONE = 0x72,
    SPELL_ITEM_POWER = 0x8a,
    EFFECT_DRAIN_LEVEL = 0x65,
    DAMAGE_KILL = 0x48,
    DAMAGE_STONE = 0x40,
    KILL_AMOUNT = 0xff,
    SAVE_TYPE_KILL = 4,
    STATUS_DESTROYED = 8,
    STATUS_KILLED = 6,
    STATUS_STONE = 7,
};

/* d1: the kills (7fd8:2ebc, 2eee, 31cc, 344d, 41e7) */

/* 7fd8:2ebc, Energy Drain (0x67; computer only): the caster's combat target (block +0x0b) is set to the first target's
 * record, NULL allowed, then effect 0x65's handler runs on the caster with no node (7e76:093b: a row cited, part 38, so the
 * cast stops there). A caster with no record or no combat record is read through NULL: undefined. The target is stored as
 * it is; a first target freed since (&cok_arena_freed) is read by 093b, where the stop is. */
static bool energy_drain(cok_adventure *game)
{
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 2ebc)");
    if (caster == NULL) return false;
    cok_character *c = combatant_of(game, caster);
    if (c == NULL || c->combat == NULL)
        return undefined(game, "dqk 7fd8:2ec7 writes the target through NULL (DS:ad67 +0x40)");
    cok_character *t1 = game->spell_targets[1];
    c->combat->target = t1 == NULL ? NULL : t1->record;
    return cok_effects_run(&game->effects, c, EFFECT_DRAIN_LEVEL, NULL) || cok_adventure_effect_failed(game);
}

/* The kill's first step (2eee, 31cc, 344d): the damage type, the amount 0xff (the word and its byte) and event 9 on the
 * local copy t (a redirect by a handler changes t, not the slot). A NULL t after is read through NULL: undefined, cited.
 * False where the run stopped. */
static bool kill_event(cok_adventure *game, cok_character **t, uint8_t damage_type, const char *stop)
{
    cok_rolls *r = &game->effects.rolls;
    r->damage_type = damage_type;
    r->amount16 = KILL_AMOUNT;
    r->amount = KILL_AMOUNT;
    if (!cok_effects_dispatch_at(&game->effects, t, 9)) return cok_adventure_effect_failed(game);
    if (*t == NULL) return undefined(game, stop);
    return true;
}

/* 7fd8:2eee, Destruction (0x68; computer only): the first target t (a copy of the slot), damage type 0x48, event 9. A target
 * whose creature flags' second byte has the quiet bit (8: +0xc8, as Slay Living), or whose amount a handler cancelled to 0,
 * is "is unaffected" (row 10, waited, one draw); else the kill (cok_combat_kill, status 8, "is destroyed": no body). Unlike
 * Slay Living, the quiet holder gets "is unaffected", not nothing. */
static bool destruction(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    const cok_rules *R = cok_game_rules(game);
    if (!kill_event(game, &t, DAMAGE_KILL, "dqk 7fd8:2f1d reads +0xc8 through NULL")) return false;
    bool quiet = (cok_field_u8_at(R->character, t->record, COK_FIELD_CREATURE_FLAGS, 1) & R->fight.quiet_flag) != 0;
    if (game->effects.rolls.amount16 == 0 || quiet) return cok_arena_say(game, t, "is unaffected", 10, true);
    return cok_combat_kill(game, t, STATUS_DESTROYED, "is destroyed");
}

/* 7fd8:31cc, Disintegrate (0x6f; the mage's spell, a player's cast too): the first target t, damage type 0x48,
 * event 9. A cancelled amount (0) is "is unaffected", as a save of type 4 (bonus 0, no roll for a 0 amount). Else the kill
 * (cok_combat_kill, status 6, "disintegrates"), then every item the target carries is removed, head first, readied ones
 * too (cok_character_remove_item: no unready, no stats). The removal runs also when the kill returned early (status 6-8). */
static bool disintegrate(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (!kill_event(game, &t, DAMAGE_KILL, "dqk 7fd8:320c reads the target through NULL")) return false;
    if (game->effects.rolls.amount16 == 0) return cok_arena_say(game, t, "is unaffected", 10, true);
    bool saved = false;
    if (!queen_save(game, t, SAVE_TYPE_KILL, 0, &saved)) return false;
    if (saved) return cok_arena_say(game, t, "is unaffected", 10, true);
    if (!cok_combat_kill(game, t, STATUS_KILLED, "disintegrates")) return false;
    while (t->item_count != 0) cok_character_remove_item(t, 0);
    return true;
}

/* 7fd8:344d, Flesh to Stone (0x72; the mage's spell, a player's cast too): the first target t, damage type 0x40, event 9.
 * A cancelled amount, or a save of type 4 (bonus 0), is silent. Else the stone (cok_combat_kill, status 7: the stone tile,
 * "is turned to stone"). Effect 0x41 (the stone's petrified node) is not added here: 7e76:069f is a monster's touch,
 * part 38. */
static bool flesh_to_stone(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (!kill_event(game, &t, DAMAGE_STONE, "dqk 7fd8:348d reads the target through NULL")) return false;
    if (game->effects.rolls.amount16 == 0) return true;
    bool saved = false;
    if (!queen_save(game, t, SAVE_TYPE_KILL, 0, &saved)) return false;
    if (saved) return true;
    return cok_combat_kill(game, t, STATUS_STONE, "is turned to stone");
}

/* 7fd8:41e7, the item power 0x8a (an item's or a monster's; computer and item use only): the caster's combat target, or
 * nothing when it has none (no draws). Two d6 are drawn: the first is the strength damage bonus's sum, which the second
 * damage overwrites, so its value is dead (177e:11f9 is not called: it draws nothing and only feeds that sum). The second
 * d6 plus 9 is the damage to the target through queen_cast_damage, with no save and the damage type left as it stands
 * (the stale one). The caster's record or combat record NULL is read through NULL: undefined (41eb). */
static bool item_power(cok_adventure *game)
{
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 41e7)");
    if (caster == NULL) return false;
    cok_character *c = combatant_of(game, caster);
    if (c == NULL || c->combat == NULL)
        return undefined(game, "dqk 7fd8:41eb reads the caster's combat record through NULL (DS:ad67 +0x40)");
    if (c->combat->target == NULL) return true;
    cok_character *t = combatant_of(game, c->combat->target);
    if (t == NULL) return undefined(game, "dqk 7fd8:41e7 reads the caster's target's combatant through NULL");
    (void)cok_dice(&game->vm.seed, 1, 6); /* 1968(1, 6): the first d6, its sum overwritten below (177e:11f9 is pure) */
    uint8_t d = cok_dice_count(&game->vm.seed, 1, 6, &game->effects.rolls.dice);
    return queen_cast_damage(game, t, (uint16_t)(d + 9), 0, false);
}

/* d3: Death Spell 0x6e (30ba), PW Kill 0x7d (3ea6), item powers 0x8b (4250) and 0x8c (4369) */

/* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */

/* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */

/* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */

enum {
    SPELL_STONE_TO_FLESH = 0x71,
    SPELL_MONSTER_SUMMONING = 0x7e,
    STATUS_WAITING = 9,       /* a record waiting to gate in (the battle's extra monsters) */
    SUMMON_LIMIT = 2,         /* 3fa4: the most gated in by one cast */
};

/* The party list's record after c (the next of DS:c6e5's list), NULL after the last one or for c not in it. */
static cok_character *list_next(cok_adventure *game, const cok_character *c)
{
    size_t n = game->party.count;
    for (size_t i = 0; i + 1 < n; ++i)
        if (game->party.members[i] == c) return game->party.members[i + 1];
    return NULL;
}

/* The status word of c (+0x64, a word in Dark Queen's records; the descriptor's read, as the combat's). */
static unsigned status_word(const cok_adventure *game, const cok_character *c)
{
    return cok_combat_field_word(cok_game_rules(game), c->record, COK_FIELD_STATUS);
}

/* 7fd8:3fa4, Monster Summoning (0x7e; the caster's pending spell, and ELGYNORA's): the battle's waiting records (status
 * word 9: the extra monsters of the fight) gate in, at most two, round the anchor. The anchor starts at the list head
 * (not the caster); from it the eight directions are tried in turn on the cell of the floor (0x17) or, with var 0x22
 * clear, the ground (0x45). A waiting record takes the cell (combatant's size from +0x8a & 7, its hit points +0x89,
 * cok_combat_revive, "gates in") and the next waits; after eight failed directions the anchor moves to the next record,
 * or to the caster (and the walk ends) when the next is none, or when the anchor is waiting. The record's side word
 * (+0x66) is the caster's, each pass. None placed: "summons is not answered" (waited). A cell off the map is the original's
 * hang (3fa4's retry of the same cell, 40aa): undefined. A waiting record with no combatant is undefined (40fa). */
static bool summon(cok_adventure *game)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_combat *combat = &game->combat;
    uint8_t *caster_rec = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 3fa4)");
    if (caster_rec == NULL) return false;
    cok_character *caster = combatant_of(game, caster_rec);
    if (caster == NULL) return undefined(game, "the caster has no combatant (DS:ad67, 3fa4)");
    uint8_t want = cok_ecl_load_var(&game->vm, 0x22) != 0 ? 0x17 : 0x45;
    cok_character *anchor = game->party.count != 0 ? game->party.members[0] : NULL;
    cok_character *cur = NULL;
    for (size_t i = 0; i < game->party.count && cur == NULL; ++i)
        if (status_word(game, game->party.members[i]) == STATUS_WAITING) cur = game->party.members[i];
    unsigned n = 0, k = 0;
    bool done = false;
    for (;;) {
        if (k > 7) {
            cok_character *next = anchor != NULL ? list_next(game, anchor) : NULL;
            if (next == NULL || status_word(game, anchor) == STATUS_WAITING) {
                anchor = caster;
                done = true;
            } else {
                anchor = next;
            }
            k = 0;
        }
        if (cur == NULL || n == SUMMON_LIMIT) {
            done = true;
            break;
        }
        cok_combat_set_field_word(rules, cur->record, COK_FIELD_HOSTILE,
                                  cok_combat_field_word(rules, caster->record, COK_FIELD_HOSTILE));
        int x0 = cok_combat_x(combat, anchor->record), y0 = cok_combat_y(combat, anchor->record);
        bool placed = false;
        do {
            int x = x0 + (int8_t)cok_data_u8_at(rules->data, COK_DATA_STEP_X, k);
            int y = y0 + (int8_t)cok_data_u8_at(rules->data, COK_DATA_STEP_Y, k);
            if (x < 0 || x >= COK_COMBAT_WIDTH || y < 0 || y >= COK_COMBAT_HEIGHT)
                return undefined(game, "gating tries a cell off the map and tries it again: the game hangs "
                                       "(dqk 7fd8:40aa)");
            uint8_t occupant, terrain;
            cok_combat_cell(combat, (int8_t)x, (int8_t)y, &occupant, &terrain);
            if (occupant == 0 && terrain == want) {
                uint8_t i = cok_combat_index(combat, cur->record);
                if (i == 0)
                    return undefined(game, "a waiting record that is no combatant is placed through entry 0, "
                                           "over the count (dqk 7fd8:40fa)");
                combat->combatant[i].size = cok_field_u8(L, cur->record, COK_FIELD_SIZE) & 7;
                combat->combatant[i].x = (int8_t)x;
                combat->combatant[i].y = (int8_t)y;
                if (!cok_combat_revive(game, cur, cok_field_u8(L, cur->record, COK_FIELD_MAX_HIT_POINTS),
                                       "gates in", &placed))
                    return false;
                if (placed) {
                    ++n;
                    cur = list_next(game, cur);
                    if (cur == NULL || status_word(game, cur) != STATUS_WAITING) done = true;
                }
            }
            ++k;
        } while (!placed && k <= 7);
        if (done) break;
    }
    if (n == 0) return cok_arena_say(game, caster, "summons is not answered", 10, true);
    return true;
}

/* The body of a stoned target t taken back (mode 5, 3297's entry search 3316, the count 33e1): its entry out of the
 * battle's bodies (1-8), the map cell back to the terrain it covered, the entries after it shifted down and the count less
 * one; then t is placed on its cell (cok_combat_place, restore), and the screen centred on the spell's cell (margin 0,
 * dir 8) if it fits. */
static bool stone_body(cok_adventure *game, cok_character *t)
{
    const cok_rules *rules = cok_game_rules(game);
    cok_combat *combat = &game->combat;
    unsigned k = 0;
    while (k < COK_COMBAT_BODIES && combat->body[k].character != t) ++k;
    if (k == COK_COMBAT_BODIES)
        return undefined(game, "a body searched for past the bodies' table is matched against whatever follows it "
                               "(dqk 7fd8:3316)");
    if (combat->bodies == 0)
        return undefined(game, "a body taken back with none counted moves the 255 entries after it (dqk 7fd8:33e1)");
    cok_combat_body *b = &combat->body[k];
    if (b->x < 0 || b->x >= COK_COMBAT_WIDTH || b->y < 0 || b->y >= COK_COMBAT_HEIGHT)
        return undefined(game, "a body's cell is off the map (the port's guard: bodies are placed on the map)");
    combat->cells[b->y][b->x] = b->cell;
    for (unsigned j = k; j + 1 < combat->bodies && j + 1 < COK_COMBAT_BODIES; ++j) combat->body[j] = combat->body[j + 1];
    --combat->bodies;
    bool placed = false;
    if (!cok_combat_place(combat, game->vm.mode, t, rules, (uint8_t)cok_combat_x(combat, t->record),
                          (uint8_t)cok_combat_y(combat, t->record), true, &placed))
        return undefined(game, "a record is placed outside the map's tables (6beb:10f3)");
    return !placed || cok_arena_centre(game, game->spell_x, game->spell_y, 0, 8);
}

/* 7fd8:3297, Stone to Flesh (0x71; the mage's spell, a player's cast too): the first target t, read as it is (NULL
 * undefined). A stone (status word 7) with a readied item of type 0x8c (queen_readied_8c) is "spell dosn't work!" with no
 * name line; else the status word is 0, the record active with 1 hit point, "is no longer a statue", and in battle its
 * body is taken back (stone_body). A target not stoned, and the refusal, still get the spell's apply (no effect, no
 * text). Mutants: no 0x8c check (the stone freed); the bodies not shifted. The status byte only (the byte-vs-word status
 * mutant) is EQUIVALENT here, not caught: the word is 7 and its high byte 0, so a byte write gives the same state. */
static bool stone_to_flesh(cok_adventure *game)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (cok_combat_field_word(rules, t->record, COK_FIELD_STATUS) == STATUS_STONE) {
        if (queen_readied_8c(t)) {
            if (!cok_arena_say(game, NULL, "spell dosn't work!", 10, false)) return false;
        } else {
            cok_combat_set_field_word(rules, t->record, COK_FIELD_STATUS, 0);
            cok_field_set8(L, t->record, COK_FIELD_ACTIVE, 1);
            cok_field_set8(L, t->record, COK_FIELD_HIT_POINTS, 1);
            if (!cok_arena_say(game, t, "is no longer a statue", 10, false)) return false;
            if (game->vm.mode == 5 && !stone_body(game, t)) return false;
        }
    }
    return queen_spell_apply(game, SPELL_STONE_TO_FLESH, 0, false, 0, 0, "");
}

queen_spell_fn *queen_band_d_handler(uint8_t id)
{
    switch (id) {
    /* d1: the kills (7fd8:2ebc, 2eee, 31cc, 344d, 41e7) */
    case SPELL_ENERGY_DRAIN: return energy_drain;
    case SPELL_DESTRUCTION: return destruction;
    case SPELL_DISINTEGRATE: return disintegrate;
    case SPELL_FLESH_TO_STONE: return flesh_to_stone;
    case SPELL_ITEM_POWER: return item_power;

    /* d3: Death Spell 0x6e (30ba), PW Kill 0x7d (3ea6), item powers 0x8b (4250) and 0x8c (4369) */
    /* d4: Prot. from Fire 0x6d (301a), PW Stun 0x75 (3569), PW Blind 0x7b (396a) */
    /* d5: Mass Charm 0x78 (372b), Otto's 0x79 (388c) */
    /* d6: Monster Summoning 0x7e (3fa4), Stone to Flesh 0x71 (3297) */
    case SPELL_MONSTER_SUMMONING: return summon;
    case SPELL_STONE_TO_FLESH: return stone_to_flesh;

    default: return NULL;
    }
}
