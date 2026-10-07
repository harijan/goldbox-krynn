#ifndef COK_SHOP_H
#define COK_SHOP_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Shops and the temple, from overlays 36d0 and 340d, with Appraise from
 * overlay 58e7. COMBAT opens a shop instead of a battle when a script has
 * set var 0x7f6c, its prices scaled by var 0x7f6d, and the temple when it
 * has set var 0x7ee2; the pool's items are the shop's stock. Both run in
 * mode 1, with the pool's money routines of treasure.h. */

/* The shop (36d0:07da): "Buy View Take Pool Share Appraise Exit" with
 * money in the pool, else "Buy View Pool Appraise Exit", until Exit with
 * the pool empty of money, or No to going back for it. Stops with
 * game->vm.status set where it cannot be carried out. */
void cok_shop(cok_adventure *game);

/* The temple (340d:0ea9): "Heal View Take Pool Share Appraise Exit" with
 * money in the pool, else "Heal View Pool Appraise Exit", as the shop. */
void cok_temple(cok_adventure *game);

/* Appraise (58e7:1929) the selected character's gems and jewelry: each
 * one valued on dice, then sold for steel (58e7:01f2) or kept as an item.
 * *shown is cleared when it had none, "No Gems or Jewelry". Returns false,
 * ending the run, where it cannot be carried out. */
bool cok_shop_appraise(cok_adventure *game, bool *shown);

/* An item's price for value (+0x3a) at the price factor of var 0x7f6d, as
 * the shop's list shows it (36d0:004e): 1, 2, 4 and 8 divide the value by
 * 16, 8, 4 and 2, 0x20, 0x40 and 0x80 multiply it by 2, 4 and 8, as words;
 * any other factor shows the value. Returns false for such a factor, where
 * Buy (36d0:0484) charges what its stack holds (see cok_shop). */
bool cok_shop_price(uint16_t value, uint16_t factor, uint16_t *price);

/* A character's money in steel (546c:3424): coins 0-4 at 1, 5, 10, 25 and
 * 50 to a steel piece, the sum divided by 50, a LongInt. */
int32_t cok_shop_money(const uint8_t *c);

#endif
