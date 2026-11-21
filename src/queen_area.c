#include "queen.h"

#include "arena.h"
#include "ecl.h"

/* The Dark Queen of Krynn's area and line spells (band b, part 31): the area damage of 7fd8's band b
 * (dqk 7fd8:1519 the Fireball family, 1772 and its hit 1a54 with the Lightning Bolt 1ac3, the Javelin 1d20,
 * Flame Strike 2157, Ice Storm 28fb, Cone of Cold 299c and its cone 19ff, Meteor Swarm 3b99 and its try 3a73).
 * The flight and the bursts of a spell are queen_cast.c's (51c2:0fc8 and 51c2:0def: queen_direction_picture and
 * queen_burst), run before each handler here. queen_area_handler is the table: a spell with no handler yet is
 * NULL, and queen_spell_handler then takes the plain table or stops at the descriptor's citation. */

/* b2: the Fireball family 0x2f 0x73 0x85 (7fd8:1519), Flame Strike 0x4a (2157), Ice Storm 0x57 (28fb). */

/* The bar's words of the underwater refusal (DS:87c9, queen_cast.c's words). */
static const char underwater_text[] = "That has no effect underwater!";

/* Var 0x3c (COK_VAR_RECORD_EFFECT): underwater. */
static bool underwater(cok_adventure *game)
{
    return cok_ecl_var(&game->vm, COK_VAR_RECORD_EFFECT) != 0;
}

/* 148f:199e (count, sides): the sum of the dice, the count stored in rolls.dice (DS:b7f4). */
static uint8_t dice_count(cok_adventure *game, uint8_t count, uint8_t sides)
{
    return cok_dice_count(&game->vm.seed, count, sides, &game->effects.rolls.dice);
}

/* 7fd8:1519 (Fireball 0x2f, Delayed Blast Fireball 0x73, Necklace of Missiles 0x85; Lightning 0x33 underwater calls it
 * too, band b3): underwater the bar says it has no effect (not for 0x33), else the image flag is set and the count is the
 * caster level (the Necklace's d3 first: (2d + 1) of the dice). Underwater (0x33 only) the area is listed again at
 * radius 2, the centre is redrawn, and the damage is nd6 from fire (type 9); the Delayed Blast adds the count to it.
 * Exported for queen_area.c's table and for its tests (the underwater Lightning's path is band b3's). */
bool queen_fireball(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    uint8_t spell = r->spell;
    if (underwater(game) && spell != 0x33) {
        queen_bar_message(game, underwater_text);
        return true;
    }
    r->images = 1;
    uint8_t lv = 0;
    if (!queen_spell_level(game, spell, false, &lv)) return false;
    uint8_t n = lv;
    if (spell == 0x85) n = (uint8_t)(cok_dice(&game->vm.seed, 1, 3) * 2 + 1);
    if (underwater(game) && !queen_area_targets(game, 2)) return false;
    if (!cok_arena_centre(game, game->spell_x, game->spell_y, 0, 8)) return false;
    uint8_t bonus = spell == 0x73 ? n : 0;
    uint8_t amount = (uint8_t)(dice_count(game, n, 6) + bonus);
    return queen_spell_apply(game, spell, 0, false, amount, 9, "");
}

/* 7fd8:2157 (Flame Strike 0x4a): underwater the bar's refusal; else the image flag, and 6d8 of damage from fire (type 9),
 * halved by a save. */
static bool flame_strike(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    if (underwater(game)) {
        queen_bar_message(game, underwater_text);
        return true;
    }
    r->images = 1;
    return queen_spell_apply(game, r->spell, 0, false, dice_count(game, 6, 8), 9, "");
}

/* 7fd8:28fb (Ice Storm 0x57): the image flag first; 3d10 of damage from magic (type 8), one roll for every target, 1d10
 * underwater. No save roll (save kind 0). */
static bool ice_storm(cok_adventure *game)
{
    cok_rolls *r = &game->effects.rolls;
    r->images = 1;
    uint8_t count = underwater(game) ? 1 : 3;
    return queen_spell_apply(game, r->spell, 0, false, dice_count(game, count, 10), 8, "");
}

/* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20). */

/* b4: Cone of Cold 0x5c (7fd8:299c). */

/* b5: Meteor Swarm 0x7c (7fd8:3b99). */

queen_spell_fn *queen_area_handler(uint8_t id)
{
    switch (id) {
    /* b2: the Fireball family 0x2f 0x73 0x85 (7fd8:1519), Flame Strike 0x4a (2157), Ice Storm 0x57 (28fb) */
    case 0x2f:
    case 0x73:
    case 0x85: return queen_fireball;
    case 0x4a: return flame_strike;
    case 0x57: return ice_storm;

    /* b3: Lightning Bolt 0x33 (7fd8:1ac3), Javelin of Lightning 0x81 (1d20) */

    /* b4: Cone of Cold 0x5c (7fd8:299c) */

    /* b5: Meteor Swarm 0x7c (7fd8:3b99) */

    default: return NULL;
    }
}
