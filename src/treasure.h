#ifndef COK_TREASURE_H
#define COK_TREASURE_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Treasure and the end of combat: the ECL opcode TREASURE (2fd3:1d21),
 * the random items of overlay 58e7, the end of combat of overlay 351b
 * with its results and treasure screens, and the money pool's routines
 * in 58e7. */

/* Carry out vm.opcode if it is TREASURE, or a later game's TREASURE ITEMS
 * (dkk 3632:1933) or STOCK (dkk 3632:28d2), with its operands decoded.
 * Returns false for any other opcode. */
bool cok_treasure_opcode(cok_adventure *game);

/* Make a random item of type into item (58e7:1039), drawing from seed: a
 * magic weapon, armour, shield, ring or bracers with a bonus of +1, or +2
 * on a d10 of 10 (58e7:1005); a scroll of 1-3 spells; or a potion or wand
 * from the templates at DS:0b88. Other types are left blank but for the
 * type. */
void cok_treasure_item(const cok_rules *rules, uint32_t *seed, uint8_t type,
                       uint8_t item[COK_ITEM_SIZE]);

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

/* The vault's menu (dkk 3948:000e), which COMBAT opens where the game has a
 * vault and var COK_VAR_VAULT is 1: mode 7 and its screen (65cd:2db0), the
 * pool's items freed and the vault read into the pool (46b2:2e1d), then
 * "View Take Pool Money Items Exit" as the pool and the selected character
 * allow (Take with money or items in the pool, Money with coins, Items
 * with items), the selection kept, Escape for Exit, up and down picking a
 * character: Take is the treasure menu's, Pool pools the party's money,
 * Money deposits the character's coins in the pool (580d:26d0); View and
 * Items, which the port does not carry out for the game yet, stop the
 * run. Exit writes the pool to the temporary vault (46b2:302d), which the
 * next save makes the saved game's. */
void cok_treasure_vault(cok_adventure *game);

/* Give a copy of item to the selected character (36d0:034c; dkk
 * 3b89:033b), after its items, and recompute its stats, unless it would be
 * overloaded (546c:32b0), which says the game's word ("OverLoaded") and
 * sets *failed. Where the game's taken items join (dkk), one with a count
 * joins its kind (580d:1be6) and is not refused for 16 items when it
 * would (580d:2a0a). Returns false, ending the run, where it cannot be
 * carried out. */
bool cok_treasure_take_item(cok_adventure *game, const uint8_t *item, bool *failed);

/* Remove record index from the list as 4def:3b0a does, freeing its icon
 * slot's pictures (6d21:0156) if free_icon, and unless keep, counting it
 * out of the party's size (0x7f3e); the record before it is then
 * selected, or the first. The spell target and trade partner the
 * original would keep pointing at it are forgotten. The selection kept
 * in DS:43bf is restored by EXIT after LOAD CHARACTER (DS:43ba) and when a
 * block's vectors end (2fd3:3b47); where either would select the freed
 * record, the original would go on with it, and this returns false,
 * ending the run, unless destroyed, the party destroyed, where the run
 * ends before either restore. */
bool cok_treasure_remove_record(cok_adventure *game, size_t index, bool keep, bool free_icon,
                                bool destroyed);

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
/* Pool (58e7:0511; dkk 5c72:03b9): every player character's money, turned
 * ones too (+0xe7 0 or 0xb3), into the pool, its weight lightened as words.
 * NPCs keep theirs. This and Share go over the kinds of coin the game's
 * records hold (the layout's COINS: dkk steel, gems and jewelry). */
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
/* Whether amount more would overload c (58e7:006d): its weight + amount,
 * a word that wraps, past its allowance + 1500 (58e7:0019); then *fits is
 * what it may still take, 0 if it is over already or not overloaded.
 * Returns false, ending the run, where c's allowance cannot be had. */
bool cok_pool_overloaded(cok_adventure *game, const uint8_t *c, uint16_t amount,
                         bool *overloaded, uint16_t *fits);
/* Take money (58e7:0d01): in the closed frame, a list of the coins in the
 * pool, jewelry first, each its name and amount, in cells 2-15 by 2-8,
 * "Select type of coin " and "Select"; a coin picked asks how much the
 * selected character takes, at most the pool's low word, and takes it
 * (58e7:0a81). Again until the pool has no coins or none is picked.
 * Returns false, ending the run, where it cannot be carried out. */
bool cok_pool_take_money(cok_adventure *game);
/* Drop amount of c's coin k (58e7:09fa; dkk 5c72:0891), into the pool in
 * treasure, shops and the vault (modes 6, 1 and 7). */
void cok_pool_drop(cok_adventure *game, uint8_t *c, uint16_t amount, int k);
/* The worth in steel of coins 0-4 (58e7:00d3): 1, 5, 10, 25 and 50 times
 * each, as a LongInt that wraps, divided by 50. */
int32_t cok_pool_value(const uint32_t coins[COK_COINS]);
/* Leave only steel in the pool (58e7:018f), or in c's coins (58e7:0155,
 * its weight unchanged), as shops pay. */
void cok_pool_set_steel(cok_pool *pool, uint16_t steel);
void cok_pool_pay(uint8_t *c, const cok_rules *rules, uint16_t steel);
/* Give c steel (58e7:01f2): what would overload it goes to the pool, with
 * "Overloaded.  Money will be put in Pool.". Returns false, ending the
 * run, where c's allowance cannot be had. */
bool cok_pool_give_steel(cok_adventure *game, uint8_t *c, uint16_t steel);

/* dkk 65cd:38c7, a game's that keeps whether one may train (rules'
 * combat.may_train): whether record c may train, from class 0 on, the
 * first class that may deciding: one of level 1-17 that has the experience
 * its row (cok_train_row, a paladin's characters.paladin_row) needs for the
 * next level, that experience above 0, and its race's highest level in the
 * class (COK_DATA_LEVEL_LIMITS, DS:82d4) above its level. Returns false
 * where the original reads what the port does not hold: the stack's byte
 * for a row the routine has none for (a knight of no order), a byte past
 * the table for a race past it (a monster's, 7). */
bool cok_treasure_may_train(const cok_rules *rules, const uint8_t *c, bool *may);

#endif
