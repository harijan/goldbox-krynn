#include "screen.h"

#include <assert.h>

/* One frame tile, by its value (6e22:01ab, unmasked; dkk 70eb:01f5). */
static void tile(cok_picture *dst, const cok_picture *tiles, const cok_rules *rules,
                 unsigned value, int x, int y)
{
    cok_picture_draw(dst, tiles, value - rules->view.tile_first[4], x, y, 0, NULL);
}

static void line(cok_picture *dst, const cok_picture *tiles, const cok_rules *rules,
                 const cok_rules_frame_line *l, const uint16_t moons[3])
{
    const cok_rules_moons *m = &rules->moons;
    for (unsigned at = l->first; at <= l->last; ++at) {
        unsigned value = rules->frame.base + (unsigned)(l->tiles != NULL ? l->tiles[at - l->first]
                                                                          : l->tile);
        for (size_t i = 0; l->moons && i < 3; ++i)
            if (at == m->column[i]) value = m->tile_base + ((moons[i] + m->tile[i]) & 0xffu);
        if (l->column)
            tile(dst, tiles, rules, value, l->at, (int)at);
        else
            tile(dst, tiles, rules, value, (int)at, l->at);
    }
}

void cok_screen_draw(cok_picture *dst, const cok_picture *tiles, const cok_rules *rules,
                     cok_screen screen, const uint16_t moons[3])
{
    assert(rules != NULL && (unsigned)screen < COK_SCREENS);
    const cok_rules_screen *s = &rules->frame.screens[screen];
    if (s->under != COK_SCREENS) cok_screen_draw(dst, tiles, rules, s->under, moons);
    if (s->right != 0) /* 1128:07e6 */
        cok_picture_fill(dst, s->left, s->top * 8, (size_t)(s->right - s->left + 1),
                         (size_t)(s->bottom - s->top + 1) * 8, 0);
    for (unsigned i = 0; i < s->line_count; ++i) {
        assert(!s->lines[i].moons || moons != NULL);
        line(dst, tiles, rules, &s->lines[i], moons);
    }
}
