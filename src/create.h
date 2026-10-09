#ifndef COK_CREATE_H
#define COK_CREATE_H

#include "adventure.h"

#include <stdbool.h>
#include <stdint.h>

/* Creating a character (4def:06dd), from the start menu: race, gender,
 * class, deity and alignment from lists, the rolls, training to the
 * levels its experience allows, the name, its combat icon, and saving it
 * to the roster. The character never joins the party. */
void cok_create(cok_adventure *game);

/* 4def:3c61: the default head (+0x135) and body (+0x136) of the combat
 * icon: head 3 for a kender, else 9 or 7 for a woman, 5 or 0 for a man,
 * large (+0x138 2) or small; body 0x17 for a cleric, else 1 for a ranger,
 * 0x18 for a knight or fighter, 0x1d for a mage, 5 for anyone else. */
void cok_create_icon(uint8_t *record);

/* 4def:5534: a knight's Plate Mail, Long Sword and Shield, not readied,
 * linked after the first item as the original links them: Plate Mail,
 * Shield, Long Sword; the item count (+0x142) becomes 3. Returns false
 * when out of memory. */
bool cok_create_knight_items(cok_character *character);

#endif
