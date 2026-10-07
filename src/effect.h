#ifndef COK_EFFECT_H
#define COK_EFFECT_H

#include "ecl.h"
#include "party.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Spell effects, from overlays 60f4 (the rolls and the dispatch), 3f44
 * (the handlers) and 57e4 (their timers). A character's effects are a list
 * of cok_effect (party.h). Rolls and other events run the handlers of the
 * effects their target holds (60f4:057c), which change the roll in
 * progress through the working bytes in cok_rolls; removing an effect runs
 * its handler with flag 1 if the effect asks for it (60f4:01e9).
 *
 * Handlers that need systems not yet ported (combat, its map and records,
 * damage and killing with text) are not ported: a call that reaches one
 * fails with the handler's address in the error. Those that only speak do
 * so through the say hook. So does anything the original mishandles fail,
 * such as an effect with no handler (the original calls 0000:0000). */

/* The working bytes of the rolls in the original's data segment, which the
 * handlers read and change. They keep their values between calls, as in
 * the original. */
typedef struct {
    uint8_t save_roll;    /* DS:6b2e: the saving throw, d20 plus bonuses. */
    uint8_t pending;      /* DS:6b2f: the effect a spell is about to add. */
    uint8_t amount;       /* DS:6b30: damage being dealt. */
    uint8_t damage_type;  /* DS:6b31: 1 fire, 2 cold, 4 electricity, 8 magic, 0x10 acid. */
    uint8_t rate;         /* DS:6b32: movement or attacks this round. */
    uint8_t spell;        /* DS:6b33: the spell being cast. */
    uint8_t dice;         /* DS:6b34: dice of the damage. */
    uint8_t untargetable; /* DS:6b37: set when the target cannot be attacked. */
    uint8_t curing;       /* DS:6b38: set while cures remove effects, so that recurring
                           * ones are not added again. */
    uint8_t attack_roll;  /* DS:6b3b: the attack roll, d20 (20 as 100) plus bonuses. */
    uint8_t morale;       /* DS:6b3e. */
    uint8_t save_type;    /* DS:6b43: the saving throw's type. */
    uint8_t save_made;    /* DS:6b44. */
    uint8_t saved;        /* DS:5885: set when the game is saved in camp (4b6d:22de)
                           * until the camp menu returns. */
    uint8_t round;        /* DS:714b: the combat round, 0 outside combat. */
    uint8_t item;         /* DS:711d: set while an item's spell is used (546c:24d7), which
                           * casts it at level 6 (6346:29fe); cleared at startup and by
                           * the next use. */
    uint8_t images;       /* DS:6b39: the spell being cast takes no mirror image, read
                           * (3f44:0a57) whenever a spell (DS:6b33) is cast, in camp
                           * too. */
    uint8_t hits[2];      /* DS:6b3c, 6b3d: the attack's hits with its two slots. */
    uint8_t *target;      /* DS:6b3f: the record a handler takes as the attacker's target. */
} cok_rolls;

enum {
    COK_EFFECT_IDS = 0x79,   /* Handler table, DS:6b94-6d77. */
    COK_EFFECT_TIMED = 0x48, /* DS:46b4. */
};

typedef struct cok_effects cok_effects;

struct cok_effects {
    /* The game: its mode (DS:4b49), selected character (DS:6096), moons
     * (0x4bf8 on), party size (0x7f3e) and Random seed. */
    cok_ecl *vm;
    cok_party *party; /* DS:609a. */
    const cok_item_types *types;
    cok_rolls rolls;
    /* DS:46b4: by party position, whether a member had an effect with time
     * left when they were last counted down. */
    uint8_t timed[COK_EFFECT_TIMED];
    /* Effects removed during a call. They are freed when it returns, so that
     * a handler that removes one the caller still holds is caught. */
    cok_effect **removed;
    size_t removed_count, removed_capacity;
    bool failed;
    char error[300]; /* Why the last call failed. */
    /* Say text about character c in the text window and wait (6346:1883
     * outside combat). The handlers that print fail without it. */
    void (*say)(cok_effects *fx, cok_character *c, const char *text, void *context);
    /* Set while a battle runs (3995:0172 to 3995:004b). A handler, or the
     * part of one, that is not ported is then logged as "unported" and
     * skipped, and effects that end are logged as "effect". */
    bool in_battle;
    /* The event whose handlers run, 0 while an effect is removed or a
     * handler is run directly. */
    uint8_t event;
    /* Log text of kind (see cok_adventure_hooks.log); NULL logs nothing. */
    void (*log)(cok_effects *fx, const char *kind, const char *text, void *context);
    /* Whether target is within radius of holder on the combat map, for an
     * effect the party shares in combat (60f4:0352, 6b30:08d8). Returns
     * false with error set where that cannot be worked out; NULL fails. */
    bool (*in_range)(cok_effects *fx, cok_character *holder, cok_character *target,
                     uint8_t radius, bool *in, char *error, size_t error_size, void *context);
    /* The combatants listed within radius of c on the combat map
     * (6b30:08d8, the list kept as it leaves it), from listed[1] to
     * listed[*count]. Returns false with error set where the list cannot
     * be made; NULL fails. */
    bool (*around)(cok_effects *fx, cok_character *c, uint8_t radius, cok_character **listed,
                   uint8_t *count, char *error, size_t error_size, void *context);
    /* The distance in squares from origin to target on the combat map
     * (6346:2888). Returns false with error set where it cannot be worked
     * out; NULL fails. */
    bool (*distance)(cok_effects *fx, const uint8_t *origin, const uint8_t *target,
                     uint8_t *distance, char *error, size_t error_size, void *context);
    /* How many removals of effects whose handlers run (60f4:01e9) are
     * nested. */
    unsigned removing;
    void *context;
};

/* Set up fx for the game's VM, party and item types. */
void cok_effects_init(cok_effects *fx, cok_ecl *vm, cok_party *party, const cok_item_types *types);
void cok_effects_free(cok_effects *fx);

/* Run the handlers of target's effects for event 1-0x18 (60f4:057c), each
 * with flag 0: the event's effect ids in order, each for target's first
 * effect with the id, or for ids 0x15, 0x2d, 0x2e and 0x31 the first
 * record's in the list, a monster's too, that has one when target has
 * none; in combat (mode 5) the first whose holder has target within 6
 * cells (0x31) or 1 (the others) on the map (fx->in_range). Returns
 * false with fx->error set when a handler is not ported (outside a
 * battle) or the original would misbehave. */
bool cok_effects_dispatch(cok_effects *fx, cok_character *target, uint8_t event);

/* Remove effect from character, or if it is NULL the first effect with id
 * (60f4:01e9), first running the handler of id with flag 1 if the effect
 * asks for it. Removing id 0x0e then recomputes charisma and 0x0c or 0x26
 * strength from the base scores, items and effects (60f4:1743). The effect
 * is freed. Returns false with fx->error set as cok_effects_dispatch does,
 * or if effect is not in the list, where the original writes to 0000:0005. */
bool cok_effects_remove(cok_effects *fx, cok_character *character, cok_effect *effect,
                        uint8_t id);

/* Run the handler of id for character with flag 0 and effect, which may
 * be NULL (60f4:01a8), as Spiritual Hammer does for 0x17 when cast. Fails
 * as cok_effects_dispatch does. */
bool cok_effects_run(cok_effects *fx, cok_character *character, uint8_t id, cok_effect *effect);

/* An attack on target with bonus (60f4:0ffb): a d20, 20 counting as 100,
 * that is not 1 and, after target's effects for event 0x10, is not negative
 * and beats target's AC (+0x18d, as 60 - AC) with bonus. */
bool cok_effects_attack(cok_effects *fx, cok_character *target, uint8_t bonus, bool *hit);

/* Target's saving throw type with bonus (60f4:113a): a d20 that is 20, or
 * not 1 and, plus its bonus (+0x17c), bonus and the moon's -1 or +1 for a
 * mage of an order (+0x5e), after its effects for event 0x0c, reaches the
 * throw at +0xd0 + type. */
bool cok_effects_save(cok_effects *fx, cok_character *target, uint8_t type, uint8_t bonus,
                      bool *made);

/* Count down the party's effects as count units of the clock pass
 * (57e4:0171): unit 1 is minutes, 2 tens of minutes, 3 hours and so on up
 * to 6, years; unit 0 counts as minutes. Ten minutes at a time, each
 * effect with time left loses them, and one with no more than that left is
 * removed as cok_effects_remove does. In camp (mode 2) nothing is counted
 * unless a member had an effect with time left at the last count. */
bool cok_effects_pass_time(cok_effects *fx, unsigned unit, unsigned count);

/* The caster level of spell for the selected character (6346:29fe): 6
 * for one with no cleric or mage level, knight level below 9 and ranger
 * level below 8; otherwise by the spell's class, the best of the cleric
 * level and the knight level - 8 (classes 0 and 2), the ranger level - 7
 * (1), the best of the mage level, changed by the moon of its order, and
 * the ranger level - 8 (3), or 12 (4); former levels count for a human
 * who may use them. While an item is used (rolls.item), it is 6 unless
 * the class is 4. Fails with none selected, where the original reads
 * through NULL, and for a class past 4, where it returns an uninitialized
 * byte. */
bool cok_effects_caster_level(cok_effects *fx, uint8_t spell, uint8_t *level);

/* Recompute ability stat (0 strength ... 5 charisma) of character from its
 * base score, readied items and effects (60f4:1743): strength and
 * charisma as removing an effect does, dexterity with items of power 2, 8
 * and 10, and constitution with the maximum hit points it gives by level
 * and effect 0x3e at 20 and up. Intelligence and wisdom are computed and
 * not stored, as in the original. Fails where the original divides by
 * zero (a constitution recomputed with no class level) or an effect's
 * handler is not ported. */
bool cok_effects_ability(cok_effects *fx, cok_character *character, unsigned stat);

/* How many of each clock unit make the next (DS:3874), 0x4bc6 on. */
extern const uint16_t cok_clock_units[7];

#endif
