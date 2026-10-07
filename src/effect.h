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
 * spells, text output) are not ported: a call that reaches one fails with
 * the handler's address in the error. So does anything the original
 * mishandles, such as an effect with no handler (the original calls
 * 0000:0000). */

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
                           * until the camp menu returns; the port does not save. */
    uint8_t round;        /* DS:714b: the combat round, 0 outside combat. */
} cok_rolls;

enum {
    COK_EFFECT_IDS = 0x79,   /* Handler table, DS:6b94-6d77. */
    COK_EFFECT_TIMED = 0x48, /* DS:46b4. */
};

typedef struct {
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
} cok_effects;

/* Set up fx for the game's VM, party and item types. */
void cok_effects_init(cok_effects *fx, cok_ecl *vm, cok_party *party, const cok_item_types *types);
void cok_effects_free(cok_effects *fx);

/* Run the handlers of target's effects for event 1-0x18 (60f4:057c), each
 * with flag 0: the event's effect ids in order, each for target's first
 * effect with the id, or for ids 0x15, 0x2d, 0x2e and 0x31 the first party
 * member's that has one when target has none. Returns false with fx->error
 * set when a handler is not ported or the original would misbehave. */
bool cok_effects_dispatch(cok_effects *fx, cok_character *target, uint8_t event);

/* Remove effect from character, or if it is NULL the first effect with id
 * (60f4:01e9), first running the handler of id with flag 1 if the effect
 * asks for it. Removing id 0x0e then recomputes charisma and 0x0c or 0x26
 * strength from the base scores, items and effects (60f4:1743). The effect
 * is freed. Returns false with fx->error set as cok_effects_dispatch does,
 * or if effect is not in the list, where the original writes to 0000:0005. */
bool cok_effects_remove(cok_effects *fx, cok_character *character, cok_effect *effect,
                        uint8_t id);

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

/* How many of each clock unit make the next (DS:3874), 0x4bc6 on. */
extern const uint16_t cok_clock_units[7];

#endif
