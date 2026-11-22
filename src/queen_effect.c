#include "dark_queen.h"

#include "queen.h"
#include "arena.h"
#include "attack.h"
#include "cast.h"
#include "combat.h"
#include "effect.h"
#include "field.h"
#include "party.h"
#include "round.h"

/* The Dark Queen of Krynn's own effect routines (dqk 78e4 and 7e76), the handlers the descriptor's
 * table maps to the kinds of spells.h's COK_FX_QUEEN_*: effect.c has no handler for them, so it runs
 * them through the game's own hook (effect.h's own), which is cok_dark_queen_effect here. Each case is
 * one routine, cited by its address in the descriptor's row (rules_dark_queen_magic.c), with the
 * original's behaviour read from its disassembly. The damage is the word rolls.amount16 (DS:ae55), with
 * its byte rolls.amount kept as the handlers read it (set_damage). */

/* 78e4:00a1 (id): the cancel. Id 0, or the pending effect is id: the damage (the word and its byte) and
 * the pending effect are cleared. */
static void cancel(cok_adventure *game, uint8_t id)
{
    cok_rolls *r = &game->effects.rolls;
    if (id == 0 || r->pending == id) {
        r->amount16 = 0;
        r->amount = 0;
        r->pending = 0;
    }
}

/* The damage word and its byte together (the writes of DS:ae55 and of the byte the handlers read). */
static void set_damage(cok_adventure *game, int16_t v)
{
    game->effects.rolls.amount16 = v;
    game->effects.rolls.amount = (uint8_t)v;
}

/* The level of the spell being cast (DS:27e9 + 0x16 * spell + 2). */
static uint8_t spell_level(cok_adventure *game)
{
    return queen_spell_byte(game, game->effects.rolls.spell, 2);
}

/* The effects' pool's count (19c2's): the nodes linked in the party's records (effect.c's pooled). */
static unsigned pooled(cok_adventure *game)
{
    unsigned n = 0;
    for (size_t i = 0; i < game->party.count; ++i)
        for (const cok_effect *e = game->party.members[i]->effects; e != NULL; e = e->next) ++n;
    return n;
}

/* 148f:19c2 (c, id, minutes, value, flag): the effect added. An id of 0 adds nothing. A full pool stops,
 * cited as effect.c's add does (the rules' pool_size nodes, pool_unported); out of memory fails. */
static bool add_raw(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes, uint8_t value,
                    bool on_remove)
{
    const cok_rules_effects *E = &cok_game_rules(game)->effects;
    if (id == 0) return true;
    if (E->pool_size != 0 && pooled(game) >= E->pool_size) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "effect 0x%02x: the effects' pool is full (%u nodes; %s)",
                           id, E->pool_size, E->pool_unported);
        return false;
    }
    if (cok_character_add_effect(c, id, minutes, value, on_remove) != NULL) return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "out of memory adding effect 0x%02x", id);
    return false;
}

/* 148f:19c2 for band c's Enlarge (queen_band_c.c): add_raw. */
bool queen_add_raw(cok_adventure *game, cok_character *c, uint8_t id, uint16_t minutes, uint8_t value, bool on_remove)
{
    return add_raw(game, c, id, minutes, value, on_remove);
}

/* 78e4:00bf (c, id, value, minutes), "again": while the curing flag (DS:b184) is set, nothing is added
 * (*added false); otherwise the effect is added with flag 1 (add_raw). */
static bool __attribute__((unused)) again(cok_adventure *game, cok_character *c, uint8_t id,
                                          uint8_t value, uint16_t minutes, bool *added)
{
    *added = false;
    if (game->effects.rolls.curing != 0) return true;
    if (!add_raw(game, c, id, minutes, value, true)) return false;
    *added = true;
    return true;
}

/* c's combat record: NULL outside a battle, where the 7e76 routines return at once (c->combat NULL). */
static cok_combat_record *block(cok_character *c)
{
    return c->combat;
}

/* effects-3: images, haste, slow, prayer, missiles, 0x38 */

/* A handler that reads its node, run with none (cok_effects_run's NULL): the port stops, cited. */
static bool no_node(cok_adventure *game, uint8_t id, unsigned cite)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "effect 0x%02x (dqk 78e4:%04x) is run with no effect", id, cite);
    return false;
}

/* The selected member (vm.character, DS:ad67), NULL with none. */
static cok_character *selected_member(cok_adventure *game)
{
    if (game->vm.character == NULL) return NULL;
    size_t i = cok_party_index(&game->party, game->vm.character);
    return i < game->party.count ? game->party.members[i] : NULL;
}

/* 78e4:0580 (0x1c, Mirror Image; ev 5, 6, 9; applied_only). In mode 5 a d(value >> 5 + 1) (one draw, always) above
 * 1 takes an image, unless the images flag (rolls.images) is set and the spell being cast is none or its
 * SPELL_ALLIES word is not 0. The damage and the pending effect are cancelled, "lost an image" (row 10, waited),
 * and the value's high nibble counts down: the first 0x1c goes at 0, else the nibble is the new count. */
static bool images(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    enum { SPELL_ALLIES = 0x13 }; /* queen_cast.c's SPELL_ALLIES: a word, 0 for the caster's own side */
    cok_rolls *r = &game->effects.rolls;
    if (effect == NULL) return no_node(game, 0x1c, 0x0580);
    if (game->vm.mode != 5) return true;
    uint8_t d = cok_dice(&game->vm.seed, 1, (uint8_t)((effect->value >> 5) + 1));
    if (d <= 1) return true;
    if (r->images != 0 && (r->spell == 0 || queen_spell_word(game, r->spell, SPELL_ALLIES) != 0)) return true;
    cancel(game, 0);
    if (!cok_arena_say(game, c, "lost an image", 10, true)) return false;
    uint8_t n = (uint8_t)((effect->value >> 5) - 1);
    if (n == 0) return cok_effects_remove(&game->effects, c, NULL, 0x1c) || cok_adventure_effect_failed(game);
    effect->value = (uint8_t)((n << 5) | (effect->value & 0x1f));
    return true;
}

/* 78e4:06e8 (0x27, haste; ev 0x12; applied_only). The first time (bit 5 of the value clear) the value gains 0x20,
 * the holder "ages" (row 10, waited), its age (+0x52 word) goes up a year and the battle's aging is asked for.
 * Every time, the movement and the rate are doubled. */
static bool haste(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_rolls *r = &game->effects.rolls;
    if (effect == NULL) return no_node(game, 0x27, 0x06e8);
    if ((effect->value & 0x20) == 0) {
        effect->value = (uint8_t)(effect->value + 0x20);
        if (!cok_arena_say(game, c, "ages", 10, true)) return false;
        cok_field_set16(L, c->record, COK_FIELD_AGE, (uint16_t)(cok_field_u16(L, c->record, COK_FIELD_AGE) + 1));
        game->effects.aging = true;
    }
    r->movement = (uint8_t)(r->movement << 1);
    r->rate = (uint8_t)(r->rate << 1);
    return true;
}

/* 78e4:0877 (0x2a, slow; ev 0x12; applied_only). Unless the holder holds effect 0x96 (177e:1734), the movement and the
 * rate are halved. */
static void slow(cok_adventure *game, cok_character *c)
{
    cok_rolls *r = &game->effects.rolls;
    if (cok_character_find_effect(c, 0x96) != NULL) return;
    r->movement = (uint8_t)(r->movement >> 1);
    r->rate = (uint8_t)(r->rate >> 1);
}

/* 78e4:09d3 (0x31, prayer; ev 0x0a, 0x0c, shared within 6; applied_only). The side in bit 7 of the value (0 or 1)
 * against the holder's side word (+0x66): equal, the save and the attack roll 1 better (011f); else both 1 worse. */
static bool prayer(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    cok_rolls *r = &game->effects.rolls;
    if (effect == NULL) return no_node(game, 0x31, 0x09d3);
    unsigned side = (unsigned)(effect->value & 0x80) >> 7;
    if (cok_combat_field_word(cok_game_rules(game), c->record, COK_FIELD_HOSTILE) == side) {
        r->save_roll = (uint8_t)(r->save_roll + 1);
        r->attack_roll = (uint8_t)(r->attack_roll + 1);
    } else {
        r->attack_roll = (uint8_t)(r->attack_roll - 1);
        r->save_roll = (uint8_t)(r->save_roll - 1);
    }
    return true;
}

/* 78e4:07d4 (a): the item the selected member a strikes with against a missile: NULL with no readied weapon (slot 0),
 * else the ammunition (51c2:10c3, cok_combat_ammunition) when it has one, else the weapon. The original calls it
 * twice (the NULL test and the bonus); the lookup draws nothing, so once is the same. */
static bool missile_item(cok_adventure *game, cok_character *a, const uint8_t **m)
{
    *m = NULL;
    size_t weapon = a->slots[0];
    if (weapon == 0 || weapon > a->item_count) return true;
    size_t item = 0;
    bool has = false;
    if (!cok_combat_ammunition(game, a, &item, &has)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "an item type past ITEMS reads past the table "
                                                    "(dqk 51c2:10c3, 78e4:07d4)");
        return false;
    }
    *m = a->items[(item != 0 ? item : weapon) - 1];
    return true;
}

/* 78e4:076e (a, c, 100): the attacker's readied weapon, and a distance (51c2:0b26, the hook) above 1, then one d100
 * (drawn; a chance of 100 is never exceeded): "Avoids it" (row 10, waited), the damage 0, the attack roll 0xff and
 * one hit less (rolls.hits[0]). */
static bool avoid(cok_adventure *game, cok_character *a, cok_character *c)
{
    cok_rolls *r = &game->effects.rolls;
    if (a->slots[0] == 0) return true;
    char why[200] = "";
    uint8_t d;
    if (!game->effects.distance(&game->effects, a->record, c->record, &d, why, sizeof why, game)) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", why);
        return false;
    }
    if (d <= 1) return true;
    (void)cok_dice(&game->vm.seed, 1, 100);
    if (!cok_arena_say(game, c, "Avoids it", 10, true)) return false;
    set_damage(game, 0);
    r->attack_roll = 0xff;
    --r->hits[0];
    return true;
}

/* 78e4:0832 (0x29, Protection from Normal Missiles; ev 5; applied_only). The selected member's missile item (07d4)
 * with no bonus (+0x30 byte 0) avoids the blow from a square more than 1 away (076e). No selected member is read
 * through NULL in the original: the port stops, cited. */
static bool missiles(cok_adventure *game, cok_character *c)
{
    cok_character *a = selected_member(game);
    if (a == NULL) {
        cok_adventure_fail(game, COK_ECL_UNDEFINED, "dqk 78e4:0832 reads the selected character (DS:ad67) "
                                                    "with none");
        return false;
    }
    const uint8_t *m;
    if (!missile_item(game, a, &m)) return false;
    if (m == NULL || m[cok_game_rules(game)->item_record.bonus] != 0) return true;
    return avoid(game, a, c);
}

/* 78e4:0acf (0x38, add invisibility; ev 8; not applied_only). Removed: in battle (mode 5) the first 0x19 goes. Not
 * removed: 0x19 for a minute, value 0x0c, flag 0 (148f:19c2). */
static bool add_invisible(cok_adventure *game, cok_character *c, bool removing)
{
    if (removing) {
        if (game->vm.mode != 5) return true;
        return cok_effects_remove(&game->effects, c, NULL, 0x19) || cok_adventure_effect_failed(game);
    }
    return add_raw(game, c, 0x19, 1, 0x0c, false);
}

/* effects-4: the 7e76 four */

/* 51c2:0920 (cok_combat_count_sides): a record on another side fails the run (dqk queen_ai.c's stop). */
static bool count_sides(cok_adventure *game)
{
    if (cok_combat_count_sides(&game->combat, &game->party, cok_game_rules(game), game->ambush))
        return true;
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "a record on a side other than 0 or 1 (+0x18a) counts "
                                                "past DS:6b2d (dqk 51c2:0920)");
    return false;
}

/* 7e76:007a (0x0b, charm; ev 0x0f, 0x11, 0x13, not applied_only). Removing puts c back on the side in
 * bit 6 of the value (the side word +0x66), and an NPC the charm made (+0x9b 0xb3) back to a player
 * (computer 0). Not removing, the first time (bit 5 of the value clear) takes hold: the value gains
 * c's side in bit 6 and 0x20 (a byte sum), the side becomes the value's bit 7, c is computer
 * controlled, an NPC (+0x9b bit 0x80 clear) becomes 0xb3, its block's target is NULL and the sides
 * are counted. Either way but removing, the morale is 100. With no block, nothing. */
static bool charm(cok_adventure *game, bool removing, cok_effect *effect, cok_character *c)
{
    const cok_rules *rules = cok_game_rules(game);
    const cok_character_layout *L = rules->character;
    cok_combat_record *cr = block(c);
    if (cr == NULL) return true;
    if (removing) {
        cok_combat_set_field_word(rules, c->record, COK_FIELD_HOSTILE, (unsigned)(effect->value & 0x40) >> 6);
        if (cok_field_u8(L, c->record, COK_FIELD_NPC) == 0xb3) {
            cok_field_set8(L, c->record, COK_FIELD_NPC, 0);
            cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 0);
        }
        return true;
    }
    if ((effect->value & 0x20) == 0) {
        effect->value = (uint8_t)(effect->value +
                                  (uint8_t)(cok_field_u8(L, c->record, COK_FIELD_HOSTILE) << 6) + 0x20);
        cok_combat_set_field_word(rules, c->record, COK_FIELD_HOSTILE, effect->value >> 7);
        cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 1);
        if ((cok_field_u8(L, c->record, COK_FIELD_NPC) & 0x80) == 0)
            cok_field_set8(L, c->record, COK_FIELD_NPC, 0xb3);
        cr->target = NULL;
        if (!count_sides(game)) return false;
    }
    game->effects.rolls.morale = 100;
    return true;
}

/* 7e76:012c (0x15, silence; ev 0x0f, applied_only; shared with the party within 1 by the holder's node).
 * A block that may use items is said "is Silenced" (row 10, waited, "is %s" DS:8674 with DS:6a42);
 * a pending spell is forgotten (cok_combat_forget, 177e:1346); the spell, may_use and may_cast are 0.
 * No turn ends and no spell is "lost". */
static bool silence(cok_adventure *game, cok_character *c)
{
    cok_combat_record *cr = block(c);
    if (cr == NULL) return true;
    if (cr->may_use != 0 && !cok_arena_say(game, c, "is Silenced", 10, true)) return false;
    if (cr->spell > 0) cok_combat_forget(cok_game_rules(game), c->record, cr->spell);
    cr->spell = 0;
    cr->may_use = 0;
    cr->may_cast = 0;
    return true;
}

/* 7e76:033f (0x23, confusion; ev 0x15, applied_only). One d100 by 7e76:0385's table: 1-10 runs away (the
 * effect 0x23 removed, the block forced and computer controlled, its target NULL, and 0x6f added for
 * ten minutes, "runs away"); 11-60 "is confused" and the turn ends; 61-80 berserk: 0x4d added, "goes
 * berserk", and 0x4d run (7e76:081f, its cited stop here); 81-100 "is enraged". Then the save (type 4,
 * bonus -2) made removes the 0x23. */
static bool confused(cok_adventure *game, cok_character *c)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat_record *cr = block(c);
    if (cr == NULL) return true;
    uint8_t d = cok_dice(&game->vm.seed, 1, 100);
    if (d <= 10) {
        if (!cok_effects_remove(&game->effects, c, NULL, 0x23)) return cok_adventure_effect_failed(game);
        cr->forced = 1;
        cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 1);
        if ((cok_field_u8(L, c->record, COK_FIELD_NPC) & 0x80) == 0)
            cok_field_set8(L, c->record, COK_FIELD_NPC, 0xb3);
        cr->target = NULL;
        if (!cok_cast_add_effect(game, c, 0x6f, 10, 0, true, 1, false, "runs away")) return false;
    } else if (d <= 60) {
        if (!cok_arena_flash(game, c, 1, "is confused")) return false;
        cok_arena_clear_text(game);
        cok_combat_end_turn(c);
    } else if (d <= 80) {
        if (!cok_cast_add_effect(game, c, 0x4d, 1, cok_field_u8(L, c->record, COK_FIELD_HOSTILE), true,
                                 1, false, "goes berserk"))
            return false;
        if (!cok_effects_run(&game->effects, c, 0x4d, NULL)) return cok_adventure_effect_failed(game);
    } else {
        if (!cok_arena_flash(game, c, 1, "is enraged")) return false;
        cok_arena_clear_text(game);
    }
    bool made;
    if (!queen_save(game, c, 4, 0xfe, &made)) return false;
    if (!made) return true;
    return cok_effects_remove(&game->effects, c, NULL, 0x23) || cok_adventure_effect_failed(game);
}

/* 7e76:0b3c (0x6f, terror; no event, applied_only is not its): only removing does anything: an NPC
 * the charm made (+0x9b 0xb3) goes back to a player (computer 0), and the block's forced is 0. With no
 * block, nothing (h_terror fails there). */
static bool terror(cok_adventure *game, cok_character *c, bool removing)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_combat_record *cr = block(c);
    if (cr == NULL || !removing) return true;
    if (cok_field_u8(L, c->record, COK_FIELD_NPC) == 0xb3) {
        cok_field_set8(L, c->record, COK_FIELD_NPC, 0);
        cok_field_set8(L, c->record, COK_FIELD_COMPUTER, 0);
    }
    cr->forced = 0;
    return true;
}

/* effects-5: timers. These run when their node is removed (its count-down) with flag 1, or when
 * called (0x22 by its spell); they read the node's value, so a run with no
 * node (cok_effects_run's NULL) is a stop: the original reads 0000:0003. */
static bool no_timer_node(cok_adventure *game, const char *routine)
{
    return cok_adventure_not_yet(game, "a timer run with no effect (its value is read at 0000:0003)",
                                 routine);
}

/* The timers' point of damage (poison 023d and wasting 0920): 1 of type 0, not saved, through
 * queen_cast_damage (148f:2214); then outside a battle the party list is drawn again, which stops
 * (5e22:0065). The caller has checked that the character has more than one hit point (+0x198). */
static bool ticking_damage(cok_adventure *game, cok_character *c)
{
    game->effects.rolls.damage_type = 0;
    if (!queen_cast_damage(game, c, 1, 0, false)) return false;
    if (game->vm.mode != 5)
        return cok_adventure_not_yet(game, "the party list drawn again after a poison's point",
                                     "dqk 5e22:0065");
    return true;
}

/* 78e4:023d (0x0f, poison; either flag): again(c, 0x1a, value, 10) (the original re-adds 0x1a, ROARK's
 * +1-to-hit id, not 0x0f; reproduced); added, and a character with more than one hit point takes the
 * ticking damage. */
static bool poison(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    if (effect == NULL) return no_timer_node(game, "dqk 78e4:023d");
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool added;
    if (!again(game, c, 0x1a, effect->value, 10, &added)) return false;
    if (!added || cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS) <= 1) return true;
    return ticking_damage(game, c);
}

/* 78e4:0696 (0x22, cause disease; applied_only): the handlers of 0x2b and 0x2c run with flag 0 and this
 * node (58e5, cok_effects_run), 0x2b first. */
static bool disease(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    if (!cok_effects_run(&game->effects, c, 0x2b, effect)) return cok_adventure_effect_failed(game);
    if (!cok_effects_run(&game->effects, c, 0x2c, effect)) return cok_adventure_effect_failed(game);
    return true;
}

/* 78e4:08ba (0x2b, strength drain; either flag): again(c, 0x2b, value, 60); added, a strength (+0x79)
 * above 3 "is weakened" (row 10, waited) and loses a point; else 0x1f for good (148f:19c2, with flag 0
 * and no test for the character holding it: COK's h_drain tests it). */
static bool drain(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    if (effect == NULL) return no_timer_node(game, "dqk 78e4:08ba");
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool added;
    if (!again(game, c, 0x2b, effect->value, 60, &added)) return false;
    if (!added) return true;
    if (cok_field_u8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH) > 3) {
        /* Outside a battle (a 0x2b counted down while walking or resting) 177e:156e says it in the
         * camp's shape (dark_queen.c's member_say, colour 7), not cok_arena_say's COK camp text. */
        if (game->vm.mode != 5)
            return cok_adventure_not_yet(game, "the drain's text outside combat (the camp's text and wait)",
                                         "dqk 177e:156e outside combat");
        if (!cok_arena_say(game, c, "is weakened", 10, true)) return false;
        cok_field_add8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_STRENGTH, -1);
        return true;
    }
    return add_raw(game, c, 0x1f, 0, 0xff, false);
}

/* 78e4:0920 (0x2c, wasting; either flag): again(c, 0x2c, value, 10); added, a character with more than
 * one hit point takes the ticking damage, one with a hit point or none gets 0x1f (no test for holding
 * it). */
static bool wasting(cok_adventure *game, cok_character *c, cok_effect *effect)
{
    if (effect == NULL) return no_timer_node(game, "dqk 78e4:0920");
    const cok_character_layout *L = cok_game_rules(game)->character;
    bool added;
    if (!again(game, c, 0x2c, effect->value, 10, &added)) return false;
    if (!added) return true;
    if (cok_field_u8(L, c->record, COK_FIELD_HIT_POINTS) > 1) return ticking_damage(game, c);
    return add_raw(game, c, 0x1f, 0, 0xff, false);
}

bool cok_dark_queen_effect(cok_adventure *game, uint8_t kind, bool removing, cok_effect *effect,
                           cok_character *c)
{
    cok_effects *fx = &game->effects;
    const cok_rules_effect *h = &cok_game_rules(game)->effects.handlers[fx->running];
    cok_rolls *r = &fx->rolls;
    const cok_character_layout *L = cok_game_rules(game)->character;
    (void)removing, (void)effect;
    switch (kind) {
    /* effects-2: damage and rolls */
    case COK_FX_QUEEN_TYPE_CANCEL:
        /* 78e4:0184 -> 00f7(4): the damage of the entry's type (DS:bebd & the value) cancelled. */
        if ((r->damage_type & h->value) != 0) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_RESIST: {
        /* 78e4:01f4 (0x0a, value 2, cold) and 0313 (0x14, value 1, fire): the damage of the type
         * halved as a signed word (idiv), and the save 3 better. */
        if ((r->damage_type & h->value) != 0) {
            set_damage(game, (int16_t)(r->amount16 / 2));
            r->save_roll = (uint8_t)(r->save_roll + 3);
        }
        return true;
    }
    case COK_FX_QUEEN_BARKSKIN:
        /* 78e4:021d (0x0d): the save 1 better unless the damage is magic (type 8); the attack roll
         * 1 worse, always. */
        if ((r->damage_type & 8) == 0) r->save_roll = (uint8_t)(r->save_roll + 1);
        r->attack_roll = (uint8_t)(r->attack_roll - 1);
        return true;
    case COK_FX_QUEEN_SHIELD:
        /* 78e4:02b9 (0x11): the holder's AC raised to at least 0x39 (+0x18e, unsigned), the save 1
         * better, and Magic Missile's (spell 0x0f) damage word zeroed (the pending effect kept). */
        if (cok_field_u8(L, c->record, COK_FIELD_AC) < 0x39) cok_field_set8(L, c->record, COK_FIELD_AC, 0x39);
        r->save_roll = (uint8_t)(r->save_roll + 1);
        if (r->spell == 0x0f) set_damage(game, 0);
        return true;
    case COK_FX_QUEEN_QUARTER_LESS:
        /* 78e4:0654 (0x1d): the damage a quarter less (the quarter as a signed word: idiv). */
        set_damage(game, (int16_t)(r->amount16 - r->amount16 / 4));
        return true;
    case COK_FX_QUEEN_GLOBE:
        /* 78e4:073b (0x28): a spell of level below 8 (the byte, unsigned) cancelled, if one is cast. */
        if (r->spell != 0 && spell_level(game) < 8) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_GLOBE_MAGIC:
        /* 78e4:0b11 (0x39): a spell of level 4 or below (the byte, zero-extended and compared as
         * a signed word: 0 to 4) with the magic bit (type 8) cancelled. */
        if (r->spell > 0 && spell_level(game) <= 4 && (r->damage_type & 8) != 0) cancel(game, 0);
        return true;
    case COK_FX_QUEEN_RESISTANT: {
        /* 78e4:0a22 (0x32, value 2: the damage of cold is saved; fire, the other, doubled when not
         * saved) and 0a7e (0x36, value 1: the mirror). A saved type adds 2 to the save roll (a higher
         * roll saves), and its damage is zeroed when the save was made, else halved. */
        unsigned other = 3 - (unsigned)h->value;
        if ((r->damage_type & h->value) != 0) {
            r->save_roll = (uint8_t)(r->save_roll + 2);
            if (r->save_made != 0) set_damage(game, 0);
            else set_damage(game, (int16_t)(r->amount16 / 2));
        } else if ((r->damage_type & other) != 0 && r->save_made == 0) {
            set_damage(game, (int16_t)(r->amount16 * 2));
        }
        return true;
    }
    /* effects-3: images, haste, slow, prayer, missiles, 0x38 */
    case COK_FX_QUEEN_IMAGES:
        return images(game, c, effect);
    case COK_FX_QUEEN_HASTE:
        return haste(game, c, effect);
    case COK_FX_QUEEN_SLOW:
        slow(game, c);
        return true;
    case COK_FX_QUEEN_PRAYER:
        return prayer(game, c, effect);
    case COK_FX_QUEEN_MISSILES:
        return missiles(game, c);
    case COK_FX_QUEEN_ADD_INVISIBLE:
        return add_invisible(game, c, removing);
    /* effects-4: the 7e76 four */
    case COK_FX_QUEEN_CHARM:
        return charm(game, removing, effect, c);
    case COK_FX_QUEEN_SILENCE:
        return silence(game, c);
    case COK_FX_QUEEN_CONFUSED:
        return confused(game, c);
    case COK_FX_QUEEN_TERROR:
        return terror(game, c, removing);
    /* effects-5: timers */
    case COK_FX_QUEEN_POISON:
        return poison(game, c, effect);
    case COK_FX_QUEEN_DISEASE:
        return disease(game, c, effect);
    case COK_FX_QUEEN_DRAIN:
        return drain(game, c, effect);
    case COK_FX_QUEEN_WASTING:
        return wasting(game, c, effect);
    default:
        cok_adventure_fail(game, COK_ECL_UNDEFINED,
                           "effect kind %u (dqk %04x) has no Dark Queen routine", kind, h->address);
        return false;
    }
}
