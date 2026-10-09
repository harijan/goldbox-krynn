#include "icon.h"

#include "arena.h"
#include "camp.h"
#include "screen.h"

#include <stdio.h>
#include <string.h>

enum { SLOT_NEW = 12, SLOT_FRAME = 25 };

/* The template's colours each part's colour byte replaces (DS:390b): body,
 * arm, leg, hair or face, shield and weapon. */
static const uint8_t part_colours[6] = {1, 2, 3, 4, 6, 7};
static const uint8_t same[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static bool fail(cok_adventure *game, const char *what)
{
    cok_adventure_fail(game, COK_ECL_UNDEFINED, "%s", what);
    return false;
}

/* 4b6d:0817 without colours. */
static bool build(cok_adventure *game, cok_character *character)
{
    return cok_arena_party_icon(game, character, false);
}

/* 4def:3e20: copy slot from's pictures over slot to's, the sizes of to's,
 * the masks both by the ready picture's size, then with colours give the
 * template's colours the record's (127f:15fd). */
static bool copy(cok_adventure *game, const uint8_t *c, uint8_t from, uint8_t to, bool colours)
{
    cok_picture *src = game->icons[from], *dst = game->icons[to];
    size_t size = dst[0].frame_size;
    for (int image = 0; image < 2; ++image) {
        if (src[image].pixels == NULL || dst[image].pixels == NULL || src[image].mask == NULL ||
            dst[image].mask == NULL || src[image].frame_size < dst[image].frame_size ||
            src[image].frame_size < size || dst[image].frame_size < size)
            return fail(game, "the icon editor copies icons of other sizes (4def:3e20)");
        memcpy(dst[image].pixels, src[image].pixels, dst[image].frame_size);
        memcpy(dst[image].mask, src[image].mask, size);
    }
    if (!colours) return true;
    uint8_t map[16];
    memcpy(map, same, sizeof map);
    for (size_t k = 0; k < 6; ++k) {
        map[part_colours[k]] = c[0x139 + k] & 15;
        map[part_colours[k] + 8] = c[0x139 + k] >> 4;
    }
    for (int image = 0; image < 2; ++image)
        if (cok_picture_recolor(&dst[image], same, map, 0, NULL) != COK_PICTURE_OK)
            return fail(game, "out of memory");
    return true;
}

/* 4def:3d41: at cell x, y of the view's buffer, 96 by 24 pixels cleared,
 * the frame of COMSPR 25 behind each image, slot 12's ready and attacking
 * images three cells apart (6d21:04b0), and that much shown (127f:12e8),
 * a unit right and a cell down. Nothing else is drawn there, so the port
 * draws on the screen. */
static void pair(cok_adventure *game, int x, int y)
{
    cok_picture_fill(&game->screen, 3 * x + 1, 24 * y + 8, 12, 24, 0);
    const cok_picture *frame = &game->icons[SLOT_FRAME][0];
    for (int k = 0; k < 2; ++k) {
        int at = 3 * (x + 3 * k) + 1;
        if (frame->pixels != NULL)
            cok_picture_draw(&game->screen, frame, 0, at, 3 * y + 1, COK_DRAW_MASKED, NULL);
        const cok_picture *icon = &game->icons[SLOT_NEW][k];
        if (icon->pixels != NULL)
            cok_picture_draw(&game->screen, icon, 0, at, 3 * y + 1, COK_DRAW_MASKED, NULL);
    }
}

static void text(cok_adventure *game, const char *s, int x, int y)
{
    cok_text_string(&game->screen, &game->font, s, x, y, 15, 0);
}

bool cok_icon_edit(cok_adventure *game)
{
    cok_character *character = cok_adventure_selected(game);
    if (character == NULL)
        return fail(game, "the icon editor with no character selected reads through NULL "
                          "(4def:408b)");
    uint8_t *c = character->record;
    uint16_t moons[3];
    for (size_t i = 0; i < 3; ++i) moons[i] = game->vm.mem4b00[0x1f9 + i];
    cok_screen_frame(&game->screen, &game->view.tiles[4], moons, true); /* 1128:0000 */
    static const char *const menus[6] = {
        "", "Parts 1st-color 2nd-color Size Exit", "Head Weapon Exit", "", " Keep Exit",
        "Next Prev Keep Exit",
    };
    /* [bp-0x1a], the level whose key was last taken, is not set before the
     * first: -1 stands for that. */
    int parent = -1;
    for (;;) {
        if (!build(game, character)) return false;
        int level = 1;
        uint8_t colours[6];
        memcpy(colours, c + 0x139, sizeof colours);
        uint8_t slot = c[0x137];
        c[0x137] = SLOT_NEW;
        bool built = build(game, character);
        c[0x137] = slot;
        if (!built) return false;
        uint8_t head = c[0x135], body = c[0x136], size = c[0x138];
        if (!copy(game, c, slot, SLOT_NEW, true)) return false;
        pair(game, 1, 2);
        text(game, "old", 8, 6);
        text(game, "ready   action", 3, 10);
        text(game, "new", 8, 12);
        text(game, "ready   action", 3, 16);
        uint8_t part = 1, edited = 0;
        bool second = false;
        int key;
        for (;;) {
            pair(game, 1, 4);
            char items[48];
            if (level == 3)
                snprintf(items, sizeof items, "Weapon Body %s Shield Arm Leg Exit",
                         second ? "Face" : "Hair");
            else
                snprintf(items, sizeof items, "%s%s",
                         level == 4 ? (c[0x138] == 2 ? "Small" : "Large") : "", menus[level]);
            bool special;
            key = cok_camp_menu(game, "", items, false, false, &special);
            if (key < 0) return false;
            if (special) {
                if (parent < 0 && (key == 0 || key == 'E'))
                    return fail(game, "the icon editor's first key tests a level it never "
                                      "set (4def:408b)");
            } else if (level == 1) {
                parent = 1;
                if (key == 'P') level = 2;
                else if (key == '1' || key == '2') level = 3, second = key == '2';
                else if (key == 'S') level = 4;
            } else if (level == 2) {
                parent = 2;
                if (key == 0 || key == 'E') {
                    level = 1;
                } else {
                    edited = (uint8_t)key;
                    level = 5;
                }
            } else if (level == 3) {
                parent = 3;
                switch (key) {
                case 'W': part = 6; break;
                case 'H': case 'F': part = 4; break;
                case 'S': part = 5; break;
                case 'A': part = 2; break;
                case 'L': part = 3; break;
                default: part = 1; break;
                }
                level = key == 0 || key == 'E' ? 1 : 5;
            } else if (level == 4) {
                if (key == 'L' && !(c[0x138] = 2, build(game, character))) return false;
                if (key == 'S' && !(c[0x138] = 1, build(game, character))) return false;
                if (key == 'K') {
                    size = c[0x138];
                    level = 1;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    c[0x138] = size;
                    level = 1;
                    key = ' ';
                }
                if (!build(game, character)) return false;
            } else if (parent == 2 && (edited == 'H' || edited == 'W')) {
                uint8_t *field = &c[edited == 'H' ? 0x135 : 0x136];
                uint8_t last = edited == 'H' ? 13 : 31, kept = edited == 'H' ? head : body;
                if (key == 'P') {
                    *field = *field > 0 ? (uint8_t)(*field - 1) : last;
                } else if (key == 'N') {
                    *field = *field < last ? (uint8_t)(*field + 1) : 0;
                } else if (key == 'K') {
                    if (edited == 'H') head = *field;
                    else body = *field;
                    level = 2;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    *field = kept;
                    level = 2;
                    key = ' ';
                }
                if (!build(game, character)) return false;
            } else if (parent == 3) {
                uint8_t *byte = &c[0x138 + part];
                uint8_t low = *byte & 15, high = *byte >> 4;
                if (key == 'N' || key == 'P') {
                    int step = key == 'N' ? 1 : 15;
                    if (second) high = (uint8_t)((high + step) & 15);
                    else low = (uint8_t)((low + step) & 15);
                    *byte = (uint8_t)(low + high * 16);
                } else if (key == 'K') {
                    memcpy(colours, c + 0x139, sizeof colours);
                    level = 3;
                    key = ' ';
                } else if (key == 'E' || key == 0) {
                    memcpy(c + 0x139, colours, sizeof colours);
                    level = 3;
                    key = ' ';
                }
            }
            /* Else, after a space in Parts, nothing: the original is stuck
             * at this level for good. */
            if (!copy(game, c, c[0x137], SLOT_NEW, true)) return false;
            if (parent == 1 && (key == 0 || key == 'E')) break;
        }
        c[0x135] = head;
        c[0x136] = body;
        c[0x138] = size;
        memcpy(c + 0x139, colours, sizeof colours);
        if (!copy(game, c, c[0x137], SLOT_NEW, true) || !copy(game, c, SLOT_NEW, c[0x137], false))
            return false;
        cok_text_clear(&game->screen, &game->font, 40, 0, 24, 0); /* 67b5:0c7b */
        cok_arena_free_icon(game, SLOT_NEW);
        int answer = cok_camp_yes_no(game, "Is this icon ok? ", 13);
        if (answer < 0) return false;
        if (answer == 'Y') return true;
    }
}
