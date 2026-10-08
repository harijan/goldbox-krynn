#ifndef COK_ICON_H
#define COK_ICON_H

#include "adventure.h"

#include <stdbool.h>

/* The combat icon editor (4def:408b), from character creation and the
 * camp's Alter: the selected character's icon, "old" and "new", ready and
 * attacking, and menus to change its head and body ("Weapon"), the
 * colours of its six parts and its size, until "Is this icon ok? " is
 * answered Yes. Its icon slot (+0x137) is left holding the icon. Returns
 * false when the run must stop. */
bool cok_icon_edit(cok_adventure *game);

#endif
