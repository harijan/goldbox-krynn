#include "queen.h"

#include "arena.h"
#include "cast.h"
#include "combat.h"
#include "ecl.h"
#include "effect.h"
#include "field.h"
#include "party.h"
#include "round.h"

/* The Dark Queen of Krynn's band c spells (part 32): the 7fd8 handlers with code of their own, the holds and charms
 * (c1), then the rest of 7fd8 below 2cae as the briefs add them. Each handler reads the spell's record and the
 * targets as queen_cast.c does, and shares the spell engine's routines (queen_save, queen_spell_apply, the add of an
 * effect, the run of an effect's handler). queen_band_c_handler is the table: a spell with no handler here is NULL, and
 * queen_spell_handler then takes the plain table or stops at the descriptor's citation. */

/* The spell record's offsets (DS:27e9 + 0x16 * id): the save kind word, the saving throw's type word and the effect byte.
 * The effects the holds and charms add or test: the charm (0x0b, 7e76:007a), held (0x33, 78e4:010d) and 0x96 (a holder
 * is saved against the holds, 849a:1e0d). The target list has room for 0x44 targets (DS:c5df to c6f2): past that undefined. */
enum {
    SPELL_SAVE_KIND = 0x0a,
    SPELL_TYPE = 0x0c,
    SPELL_EFFECT = 0x0e,
    EFFECT_CHARMED = 0x0b,
    EFFECT_HELD = 0x33,
    EFFECT_96 = 0x96,
    SNAKE_TARGETS_LIMIT = 0x44,
};

/* An undefined stop (the original reads unset memory, a NULL or a freed record): cok_adventure_fail. */
static bool undefined(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* c1: the holds and charms (849a:1e0d; 7fd8:06e9, 2fd4, 2396, 23c5, 022a, 2b3a, 0813) */

/* 849a:1e0d (bonus, text): the holds and charms of the battle's spells over the targets from the count down to 1 (the
 * count read once, its targets read at each pass). Every target but the first one handled (i below the count) takes a
 * missile from the caster (sound 3, the flight's frames 0x13 and 4 frames at delay 0x1e). Then a saving throw of the
 * spell's type with bonus (148f:1886) is made, except that the first handled one of 0x4f (Faerie Fire) and 0x51
 * (Charm Monsters) is saved without a roll, kind 1 ("is Unaffected"). A save is forced for a race 7 under Hold
 * Person (0x17, 0x31), for Hold Animal (0x6c) on a creature without flag 0x40, and for Hold Person, Hold Monsters
 * (0x5e), on a holder of effect 0x96. The value is the caster level (capped), and
 * Charm Monsters' is with the caster's side in bit 7 and on remove. The effect (the spell's byte +0xe) is added for the
 * spell's duration with text (148f:252c). Returns false where the run stopped. */
static bool holds(cok_adventure *game, uint8_t bonus, const char *text)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    cok_combat *combat = &game->combat;
    uint8_t spell = game->effects.rolls.spell;
    uint8_t count = game->spell_target_count;
    bool on_remove = false;
    for (unsigned i = count; i >= 1; --i) {
        cok_character *t = game->spell_targets[i];
        if (t == NULL) continue;
        if (i < count) {
            uint8_t *caster = game->vm.character;
            if (caster == NULL)
                return undefined(game, "the caster is read through NULL with no character selected (DS:ad67, 849a:1e0d)");
            cok_arena_sound(game, 3);
            if (!cok_arena_missile_frames(game, 0x13)) return false;
            if (!cok_arena_missile(game, cok_combat_x(combat, caster), cok_combat_y(combat, caster),
                                   cok_combat_x(combat, t->record), cok_combat_y(combat, t->record), 4, 0x1e))
                return false;
        }
        bool saved = false;
        uint8_t kind = 0;
        if ((spell == 0x4f || spell == 0x51) && i == count) {
            saved = true;
            kind = 1;
        } else {
            if (!queen_save(game, t, (uint8_t)queen_spell_word(game, spell, SPELL_TYPE), bonus, &saved))
                return false;
            kind = (uint8_t)queen_spell_word(game, spell, SPELL_SAVE_KIND);
        }
        if ((int16_t)cok_combat_field_word(R, t->record, COK_FIELD_RACE) == 7 && (spell == 0x17 || spell == 0x31))
            saved = true;
        if ((cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 0x40) == 0 && spell == 0x6c)
            saved = true;
        if (cok_character_find_effect(t, EFFECT_96) != NULL && (spell == 0x31 || spell == 0x17 || spell == 0x5e))
            saved = true;
        uint8_t lv = 0, value;
        if (!queen_spell_level(game, spell, true, &lv)) return false;
        if (spell == 0x51) {
            value = (uint8_t)(lv + (uint8_t)(cok_field_u8(L, game->vm.character, COK_FIELD_HOSTILE) << 7));
            on_remove = true;
        } else {
            value = lv;
        }
        uint16_t minutes = 0;
        if (!queen_spell_duration(game, spell, &minutes)) return false;
        if (!cok_cast_add_effect(game, t, queen_spell_byte(game, spell, SPELL_EFFECT), minutes, value, on_remove,
                                 kind, saved, text))
            return false;
    }
    return true;
}

/* The run of the charm on t (cast.c's charm_person's shape): the effect 0x0b's handler on the holder's node, if t holds
 * it. */
static bool run_charmed(cok_adventure *game, cok_character *t)
{
    cok_effect *node = cok_character_find_effect(t, EFFECT_CHARMED);
    if (node == NULL) return true;
    return cok_effects_run(&game->effects, t, EFFECT_CHARMED, node) || cok_adventure_effect_failed(game);
}

/* The charm's apply of spell to t (queen_spell_apply, value the caster level (capped) with the caster's side in bit 7,
 * on remove, kind 1 and the record's type), with text, then the run of 0x0b. */
static bool charm_apply(cok_adventure *game, cok_character *t, const char *text)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t lv = 0;
    if (!queen_spell_level(game, game->effects.rolls.spell, true, &lv)) return false;
    uint8_t value = (uint8_t)(lv + (uint8_t)(cok_field_u8(L, game->vm.character, COK_FIELD_HOSTILE) << 7));
    if (!queen_spell_apply(game, game->effects.rolls.spell, value, true, 0, 0, text)) return false;
    return run_charmed(game, t);
}

/* 7fd8:06e9, Hold Person (0x17, 0x31) and Hold Monsters (0x5e): the bonus by the count (1: 0xfe for 0x17, else 0xfd; 2:
 * 0xff; 3 and 4: 0), "is held". No target: nothing. Five or more leave the bonus unset (a local never written, 06e9's
 * bp-1): undefined. */
static bool hold(cok_adventure *game)
{
    uint8_t bonus = 0;
    switch (game->spell_target_count) {
    case 0: return true;
    case 1: bonus = game->effects.rolls.spell == 0x17 ? 0xfe : 0xfd; break;
    case 2: bonus = 0xff; break;
    case 3:
    case 4: bonus = 0; break;
    default: return undefined(game, "dqk 7fd8:06e9's bonus for 5 or more targets is a local never written (bp-1)");
    }
    return holds(game, bonus, "is held");
}

/* 7fd8:2fd4, Hold Animal (0x6c): the bonus by the count (1: 0xfc, 2: 0xfe, 3: 0xff, else 0), "is held". */
static bool hold_animal(cok_adventure *game)
{
    uint8_t bonus = 0;
    switch (game->spell_target_count) {
    case 1: bonus = 0xfc; break;
    case 2: bonus = 0xfe; break;
    case 3: bonus = 0xff; break;
    default: bonus = 0; break;
    }
    return holds(game, bonus, "is held");
}

/* 7fd8:2396, Faerie Fire (0x4f): no bonus, "is highlighted" (the AC effect 0x47 is the descriptor's row). */
static bool faerie_fire(cok_adventure *game)
{
    return holds(game, 0, "is highlighted");
}

/* 7fd8:23c5, Charm Monsters (0x51): no bonus, "is charmed" (the holds' value and side), then the run of 0x0b on each
 * target that holds it (the count re-read at each pass; a NULL target is read through NULL: undefined). */
static bool charm_monsters(cok_adventure *game)
{
    if (!holds(game, 0, "is charmed")) return false;
    for (unsigned k = 1; k <= game->spell_target_count; ++k) {
        cok_character *t = game->spell_targets[k];
        if (t == NULL) return undefined(game, "dqk 7fd8:23fc reads a NULL target's effects (DS:c5df)");
        cok_effect *node = cok_character_find_effect(t, EFFECT_CHARMED);
        if (node != NULL && !cok_effects_run(&game->effects, t, EFFECT_CHARMED, node))
            return cok_adventure_effect_failed(game);
    }
    return true;
}

/* 7fd8:022a, Charm Person (0x0a, 0x3f): the first target (NULL: nothing). A race 7 with no flag 2, or a size above 1,
 * is "is unaffected" on row 10 with a wait (lower case: DS:87e8); else the charm of 0x0b, "is charmed". */
static bool charm_person(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    cok_character *t = game->spell_targets[1];
    if (t == NULL) return true;
    bool race7 = (int16_t)cok_combat_field_word(R, t->record, COK_FIELD_RACE) == 7 &&
                 (cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 2) == 0;
    if (race7 || cok_field_u8(L, t->record, COK_FIELD_SIZE) > 1)
        return cok_arena_say(game, t, "is unaffected", 10, true);
    return charm_apply(game, t, "is charmed");
}

/* 7fd8:2b3a, Charm Person or Mammal (0x60): the first target (NULL: nothing). A race of 7 or more with no flag 2 (not
 * 0x10: that is Death Knights') is "is not Charmed" on row 10 with a wait; else the charm, "is Charmed". */
static bool charm_mammal(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    const cok_character_layout *L = R->character;
    cok_character *t = game->spell_targets[1];
    if (t == NULL) return true;
    if ((int16_t)cok_combat_field_word(R, t->record, COK_FIELD_RACE) >= 7 &&
        (cok_field_u8_at(L, t->record, COK_FIELD_CREATURE_FLAGS, 0) & 2) == 0)
        return cok_arena_say(game, t, "is not Charmed", 10, true);
    return charm_apply(game, t, "is Charmed");
}

/* 7fd8:0813, Snake Charm (0x1b): the targets are every party member (the monsters after the party) with the snake flag
 * 0x10 that is active, not held (effect 0x33) and whose hit points are within what is left of the caster's, taken in
 * list order (the caster's hit points, read once, are the allowance; each target takes its hit points off it). The apply
 * of the spell with no save and text "is Charmed"; no target: nothing. */
static bool snake_charm(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t *caster = game->vm.character;
    if (caster == NULL)
        return undefined(game, "the caster is read through NULL with no character selected (DS:ad67, 0813)");
    uint8_t left = cok_field_u8(L, caster, COK_FIELD_HIT_POINTS);
    game->spell_target_count = 0;
    for (size_t i = 0; i < game->party.count; ++i) {
        cok_character *m = game->party.members[i];
        const uint8_t *r = m->record;
        if ((cok_field_u8_at(L, r, COK_FIELD_CREATURE_FLAGS, 0) & 0x10) == 0 ||
            cok_field_u8(L, r, COK_FIELD_ACTIVE) == 0 || cok_character_find_effect(m, EFFECT_HELD) != NULL)
            continue;
        uint8_t hp = cok_field_u8(L, r, COK_FIELD_HIT_POINTS);
        if (hp > left) continue;
        if (game->spell_target_count == SNAKE_TARGETS_LIMIT)
            return undefined(game, "dqk 7fd8:0813 lists a target past DS:c6f2 (the 0x44 targets' room)");
        left = (uint8_t)(left - hp);
        game->spell_targets[++game->spell_target_count] = m;
    }
    return queen_spell_apply(game, game->effects.rolls.spell, 0, false, 0, 0, "is Charmed");
}

/* c3: Fumble 0x56 (7fd8:27ef), Feeblemind 0x5d (2a48) */

/* c3's spell and effect ids: Fumble's held (0x1b) or slowed (0x2a) effect, Feeblemind's effect 0x44. */
enum {
    SPELL_FUMBLE = 0x56,
    SPELL_FEEBLEMIND = 0x5d,
    EFFECT_FUMBLE_HELD = 0x1b,
    EFFECT_SLOW = 0x2a,
    EFFECT_FEEBLEMIND = 0x44,
    SAVE_TYPE_FUMBLE = 4,
};

/* 7fd8:27ef, Fumble (0x56): the first target (queen_first_target's stop). A target with effect 0x96 (177e:1734) is passed
 * over, with nothing said and no draw. Else the damage type is 0x40 for the save's events (it stays set, as the original
 * leaves it), and a saving throw of type 4 (bonus 0) is made: failed, 0x1b ("is clumsy", held) is added for the spell's
 * duration and run; saved, 0x2a ("is slowed") likewise. Both with value 0, on remove 0, kind 0, saved 0. There is no apply
 * after the save (Death Knights' cast.c has one). */
static bool fumble(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    if (cok_character_find_effect(t, EFFECT_96) != NULL) return true;
    game->effects.rolls.damage_type = 0x40;
    bool made = false;
    if (!queen_save(game, t, SAVE_TYPE_FUMBLE, 0, &made)) return false;
    uint16_t minutes = 0;
    if (!queen_spell_duration(game, SPELL_FUMBLE, &minutes)) return false;
    uint8_t id = made ? EFFECT_SLOW : EFFECT_FUMBLE_HELD;
    if (!cok_cast_add_effect(game, t, id, minutes, 0, false, 0, false, made ? "is slowed" : "is clumsy"))
        return false;
    if (cok_character_find_effect(t, id) != NULL && !cok_effects_run(&game->effects, t, id, NULL))
        return cok_adventure_effect_failed(game);
    return true;
}

/* 7fd8:2a48, Feeblemind (0x5d): the first target (queen_first_target's stop). Its saving throw of type 4 (+0x8f) is shifted
 * by its class (the word at +0x5a) for the apply: class 0 takes 1 off, class 5 adds 4, any other adds 2 (byte arithmetic). The apply
 * of 0x5d (value 0, no damage, no text) makes the save with the shifted byte and the spell's effect 0x44 for its duration.
 * Then a holder of 0x44 has its memorised cast bits cleared and its effect run (node NULL). The byte is put back always. */
static bool feeblemind(cok_adventure *game)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    uint8_t was = cok_field_u8_at(L, t->record, COK_FIELD_SAVING_THROW, 4);
    uint8_t shifted;
    size_t klass = cok_field_offset(L, COK_FIELD_CLASS);
    switch (t->record[klass] | t->record[klass + 1] << 8) { /* 2a60: the class as a word */
    case 0: shifted = (uint8_t)(was - 1); break;
    case 5: shifted = (uint8_t)(was + 4); break;
    default: shifted = (uint8_t)(was + 2); break;
    }
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, shifted);
    bool ok = queen_spell_apply(game, SPELL_FEEBLEMIND, 0, false, 0, 0, "");
    if (ok && cok_character_find_effect(t, EFFECT_FEEBLEMIND) != NULL) {
        queen_clear_cast_spells(L, t->record);
        ok = cok_effects_run(&game->effects, t, EFFECT_FEEBLEMIND, NULL) || cok_adventure_effect_failed(game);
    }
    cok_field_set8_at(L, t->record, COK_FIELD_SAVING_THROW, 4, was);
    return ok;
}

/* c4: Haste 0x30 (175a), Slow 0x37 (1b11) and 163f; Enlarge 0x0c (02e8); Reduce 0x0d (0438) */

/* The caster's record (DS:ad67): none is read through NULL, undefined. */
static uint8_t *caster_record(cok_adventure *game, const char *what)
{
    if (game->vm.character == NULL) undefined(game, what);
    return game->vm.character;
}

/* The event's run on target slot i (148f:0512 on &targets[i]: the handlers may change the slot). */
static bool dispatch_slot(cok_adventure *game, unsigned i, uint8_t event)
{
    return cok_effects_dispatch_at(&game->effects, &game->spell_targets[i], event) || cok_adventure_effect_failed(game);
}

/* 7fd8:163f (id, side, text): the Haste and Slow helper. Each target on side (its word +0x66) gets the opposite effect id
 * while the caster's level (uncapped, n) lasts: event 9 with id pending and the magic type 8 first, then, if no handler
 * cancelled id, the cure of id (queen_cure) and one n spent; a holder of id is dropped, and so is any target not on side.
 * A target a handler cancelled (a globe 0x39 on a magic spell) keeps its slot and spends no n. Then the spell's apply
 * with text, and event 0x12 on each target left. The count is read again at every pass; a NULL target is read through
 * NULL: undefined (7fd8:1671). */
static bool hasten(cok_adventure *game, uint8_t id, unsigned side, const char *text)
{
    const cok_rules *R = cok_game_rules(game);
    cok_rolls *r = &game->effects.rolls;
    uint8_t spell = r->spell;
    uint8_t n = 0;
    if (!queen_spell_level(game, spell, false, &n)) return false;
    r->images = 1;
    for (unsigned i = 1; i <= game->spell_target_count; ++i) {
        cok_character *t = game->spell_targets[i];
        if (t == NULL) return undefined(game, "dqk 7fd8:1671 reads a NULL target's side through NULL (DS:c5df)");
        if (cok_combat_field_word(R, t->record, COK_FIELD_HOSTILE) != side || n == 0) {
            game->spell_targets[i] = NULL;
            continue;
        }
        r->pending = id;
        r->damage_type = 8;
        if (!dispatch_slot(game, i, 9)) return false;
        if (r->pending != id) continue;
        --n;
        t = game->spell_targets[i];
        if (t == NULL) return undefined(game, "dqk 7fd8:16be reads the cure's target through NULL (DS:c5df)");
        bool cured = false;
        if (!queen_cure(game, t, id, &cured)) return false;
        if (cured) game->spell_targets[i] = NULL;
    }
    if (!queen_spell_apply(game, spell, 0, false, 0, 0, text)) return false;
    for (unsigned i = 1; i <= game->spell_target_count; ++i)
        if (game->spell_targets[i] != NULL && !dispatch_slot(game, i, 0x12)) return false;
    return true;
}

/* 7fd8:175a, Haste (0x30): the caster's side (word +0x66), "is Hasted"; Slow's effect 0x2a is cured. */
static bool haste(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 175a)");
    if (caster == NULL) return false;
    return hasten(game, 0x2a, cok_combat_field_word(R, caster, COK_FIELD_HOSTILE), "is Hasted");
}

/* 7fd8:1b11, Slow (0x37): the first target, kept whatever the count, holding effect 0x96 is left alone (no draws). Else
 * the other side (51c2:08fd: 1 where the caster's word +0x66 is 0, else 0), "is Slowed"; Haste's effect 0x27 is cured.
 * The first target NULL or freed stops (queen_first_target, cited 7fd8:1b2d). */
static bool slow(cok_adventure *game)
{
    const cok_rules *R = cok_game_rules(game);
    cok_character *t1 = queen_first_target(game);
    if (t1 == NULL) return false;
    if (cok_character_find_effect(t1, EFFECT_96) != NULL) return true;
    uint8_t *caster = caster_record(game, "the caster is read through NULL with no character selected (DS:ad67, 1b11)");
    if (caster == NULL) return false;
    unsigned other = cok_combat_field_word(R, caster, COK_FIELD_HOSTILE) == 0 ? 1 : 0;
    return hasten(game, 0x27, other, "is Slowed");
}

/* 148f:1c87 and 1c24 (rec, str, exc): rec's base strength (+0x78) below str, or str 18 with its base exceptional (+0x85)
 * below exc, is raised: out is 18 with exc + 1 (str 18), else str + 100 (a byte). False where it is not raised. */
static bool enlarges(cok_adventure *game, const cok_character *c, uint8_t str, uint8_t exc, uint8_t *out)
{
    const cok_character_layout *L = cok_game_rules(game)->character;
    uint8_t base = cok_field_u8_at(L, c->record, COK_FIELD_SCORE_BASE, COK_SCORE_STRENGTH);
    if (base >= str && !(str == 0x12 && cok_field_u8(L, c->record, COK_FIELD_STRENGTH_EXCEPTIONAL_BASE) < exc))
        return false;
    *out = str == 0x12 ? (uint8_t)(exc + 1) : (uint8_t)(str + 100);
    return true;
}

/* 7fd8:02e8, Enlarge (0x0c): the first target (queen_first_target), with the strength and exceptional strength by the
 * caster's level (uncapped): 1 to 6 keep 18 with exceptional 0, 1, 0x33, 0x4c, 0x5b and 0x64; 7 to 9 are 19 to 21; 0 and
 * 10 and up are 22. A target not raised, or holding a globe (0x39 or 0x3f), is "is unaffected" (row 10, waited). Else
 * "is stronger", then effect 0x0c added raw (148f:19c2: no event 9, a second node if one is held) for the spell's
 * duration, its value the raised strength, on remove, and the strength recomputed (1dfc). */
static bool enlarge(cok_adventure *game)
{
    cok_character *t = queen_first_target(game);
    if (t == NULL) return false;
    uint8_t spell = game->effects.rolls.spell, lv = 0;
    uint8_t str = 0x12, exc = 0;
    if (!queen_spell_level(game, spell, false, &lv)) return false;
    static const uint8_t exceptional[6] = {0, 1, 0x33, 0x4c, 0x5b, 0x64};
    if (lv >= 1 && lv <= 6) exc = exceptional[lv - 1];
    else if (lv >= 7 && lv <= 9) str = (uint8_t)(0x13 + lv - 7);
    else str = 0x16;
    uint8_t value = 0;
    if (!enlarges(game, t, str, exc, &value) || cok_character_find_effect(t, 0x39) != NULL ||
        cok_character_find_effect(t, 0x3f) != NULL)
        return cok_arena_say(game, t, "is unaffected", 10, true);
    if (!cok_arena_say(game, t, "is stronger", 10, true)) return false;
    uint16_t minutes = 0;
    if (!queen_spell_duration(game, spell, &minutes)) return false;
    if (!queen_add_raw(game, t, 0x0c, minutes, value, true)) return false;
    return cok_effects_ability(&game->effects, t, 0) || cok_adventure_effect_failed(game);
}

/* 7fd8:0438, Reduce (0x0d): the first target (none, or a NULL one: nothing). A save of type 4 (bonus 0) is silent, and so
 * is a target without effect 0x0c. Else the first 0x0c removed (cok_effects_remove with NULL, its handler run), the
 * strength recomputed (1dfc), and "has been reduced" (row 10, waited). */
static bool reduce(cok_adventure *game)
{
    cok_character *t = game->spell_targets[1];
    if (t == NULL || game->spell_target_count == 0) return true;
    bool saved = false;
    if (!queen_save(game, t, 4, 0, &saved)) return false;
    if (saved || cok_character_find_effect(t, 0x0c) == NULL) return true;
    if (!cok_effects_remove(&game->effects, t, NULL, 0x0c)) return cok_adventure_effect_failed(game);
    if (!cok_effects_ability(&game->effects, t, 0)) return cok_adventure_effect_failed(game);
    return cok_arena_say(game, t, "has been reduced", 10, true);
}

/* c5: Spiritual Hammer 0x1c (08e3); Remove Curse 0x2b 0x3d 0x59 (13ef) */

/* c6: Heal 0x24 (0e9f, 0dc0); Sticks to Snakes 0x46 (1fcb); Cause Disease 0x28 (1068); Poison 0x44 (1f41); Harm 0x38 (1b5c); Slay Living 0x4c (223e) */

/* c7: Dispel Magic 0x29 0x2e (114e, 10d9); Dimension Door 0x53 (24f0); Fire Shield 0x55 (26b4) */

queen_spell_fn *queen_band_c_handler(uint8_t id)
{
    switch (id) {
    /* c1: the holds and charms (849a:1e0d; 7fd8:06e9, 2fd4, 2396, 23c5, 022a, 2b3a, 0813) */
    case 0x17:
    case 0x31:
    case 0x5e: return hold;
    case 0x6c: return hold_animal;
    case 0x4f: return faerie_fire;
    case 0x51: return charm_monsters;
    case 0x0a:
    case 0x3f: return charm_person;
    case 0x60: return charm_mammal;
    case 0x1b: return snake_charm;

    /* c3: Fumble 0x56 (7fd8:27ef), Feeblemind 0x5d (2a48) */
    case 0x56: return fumble;
    case 0x5d: return feeblemind;

    /* c4: Haste 0x30 (175a), Slow 0x37 (1b11) and 163f; Enlarge 0x0c (02e8); Reduce 0x0d (0438) */
    case 0x30: return haste;
    case 0x37: return slow;
    case 0x0c: return enlarge;
    case 0x0d: return reduce;

    /* c5: Spiritual Hammer 0x1c (08e3); Remove Curse 0x2b 0x3d 0x59 (13ef) */

    /* c6: Heal 0x24 (0e9f, 0dc0); Sticks to Snakes 0x46 (1fcb); Cause Disease 0x28 (1068); Poison 0x44 (1f41); Harm 0x38 (1b5c); Slay Living 0x4c (223e) */

    /* c7: Dispel Magic 0x29 0x2e (114e, 10d9); Dimension Door 0x53 (24f0); Fire Shield 0x55 (26b4) */

    default: return NULL;
    }
}
