#ifndef COK_TREASURE_H
#define COK_TREASURE_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Treasure and the end of combat: the ECL opcode TREASURE (2fd3:1d21),
 * the random items of overlay 58e7, the end of combat of overlay 351b
 * with its results and treasure screens, and the money pool's routines
 * in 58e7. */

/* Carry out vm.opcode if it is TREASURE, with its operands decoded.
 * Returns false for any other opcode. */
bool cok_treasure_opcode(cok_adventure *game);

/* Make a random item of type into item (58e7:1039), drawing from seed: a
 * magic weapon, armour, shield, ring or bracers with a bonus of +1, or +2
 * on a d10 of 10 (58e7:1005); a scroll of 1-3 spells; or a potion or wand
 * from the templates at DS:0b88. Other types are left blank but for the
 * type. */
void cok_treasure_item(uint32_t *seed, uint8_t type, uint8_t item[COK_ITEM_SIZE]);

/* The end of combat (351b:1968), after every COMBAT, with a battle or
 * without: weapons lost in combat go back (351b:185f); the party's state
 * after it, its effects that last through combat and its experience
 * (351b:0574, 0037, 0379), or its removal if destroyed; the mode becomes 6
 * (treasure), the enemies are removed (351b:1493) and every record's stats
 * recomputed; then, unless the party was destroyed, the NPCs take their
 * shares (351b:1618), the results show (351b:0b23) and the treasure menu
 * runs (351b:118c), after which the pool's items are gone; else the
 * monsters rejoice and the run ends. The combat's variables are then
 * cleared. Stops with game->vm.status set where it cannot be carried
 * out. */
void cok_treasure_end_of_combat(cok_adventure *game);

/* Give a copy of item to the selected character (36d0:034c), after its
 * items, and recompute its stats, unless it would be overloaded
 * (546c:32b0), which says "OverLoaded" and sets *failed. Returns false,
 * ending the run, where it cannot be carried out. */
bool cok_treasure_take_item(cok_adventure *game, const uint8_t *item, bool *failed);

/* The pool's items and coins, for treasure and shops (58e7). */

/* Insert a copy of item at index of the pool's items, its link cleared,
 * and remove one, keeping the recovered missile on its item. Insert
 * returns false when out of memory. TREASURE and the end of combat put
 * items first. */
bool cok_pool_insert(cok_pool *pool, size_t index, const uint8_t *item);
void cok_pool_remove(cok_pool *pool, size_t index);
/* Free the pool's items; the coins stay. */
void cok_pool_free_items(cok_pool *pool);
/* Whether the pool holds money, any coin not 0, and items (58e7:0fb1). */
void cok_pool_status(const cok_pool *pool, bool *money, bool *items);
/* Pool (58e7:0511): every player character's money, turned ones too
 * (+0xe7 0 or 0xb3), into the pool, its weight lightened as words. NPCs
 * keep theirs. */
void cok_pool_gather(cok_adventure *game);
/* Share (58e7:063a): divide each coin of the pool among the records
 * counted as Pool counts them, as words, each record below 0x80 taking a
 * share and a coin of the remainder, jewelry first, as far as it can
 * carry (58e7:006d); turned characters' shares are lost. Then what is
 * left goes to each record in turn as far as it can carry, and stays in
 * the pool. Returns false, with game->vm.status COK_ECL_DIVIDE_BY_ZERO,
 * where a coin is shared among no one, runtime error 200. */
bool cok_pool_share(cok_adventure *game);
/* Take amount of coin k from the pool for c (58e7:0a81): if it would
 * overload c, "Overloaded" and nothing; else at most what the pool has.
 * Returns false, ending the run, where c's allowance cannot be had. */
bool cok_pool_take(cok_adventure *game, uint8_t *c, uint16_t amount, int k);
/* Drop amount of c's coin k (58e7:09fa), into the pool in treasure and
 * shops (modes 6 and 1). */
void cok_pool_drop(cok_adventure *game, uint8_t *c, uint16_t amount, int k);
/* The worth in steel of coins 0-4 (58e7:00d3): 1, 5, 10, 25 and 50 times
 * each, as a LongInt that wraps, divided by 50. */
int32_t cok_pool_value(const uint32_t coins[COK_COINS]);
/* Leave only steel in the pool (58e7:018f), or in c's coins (58e7:0155,
 * its weight unchanged), as shops pay. */
void cok_pool_set_steel(cok_pool *pool, uint16_t steel);
void cok_pool_pay(uint8_t *c, uint16_t steel);
/* Give c steel (58e7:01f2): what would overload it goes to the pool, with
 * "Overloaded.  Money will be put in Pool.". Returns false, ending the
 * run, where c's allowance cannot be had. */
bool cok_pool_give_steel(cok_adventure *game, uint8_t *c, uint16_t steel);

/* The templates the generator reads (DS:0b88), to check against the
 * executable. */
extern const cok_ds_table cok_treasure_templates;

#endif
