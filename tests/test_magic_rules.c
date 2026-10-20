/* The descriptors' spells and effects sections (rules.h, spells.h) against
 * each game's executable: the spell names and the spell lists' headings
 * byte for byte, the handler tables as the routines that fill them at
 * startup store them, the effect ids each event runs as the dispatch calls
 * them, the ids the party shares; and the named ids and lookups. Without
 * build/death-START_FULL.EXE (make death-merged, DKKAssets/) Death Knights'
 * checks of its executable are skipped, with a line saying so. */
#include "rules.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    uint8_t *bytes;
    size_t size, header;
    unsigned ds; /* The data segment, as the decompiled code numbers segments. */
} image;

static bool load(image *m, const char *path, unsigned ds)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return false;
    static uint8_t buffers[2][1 << 20];
    static int used;
    m->bytes = buffers[used++];
    m->size = fread(m->bytes, 1, sizeof buffers[0], f);
    fclose(f);
    CHECK(m->size > 0x20 && m->bytes[0] == 'M' && m->bytes[1] == 'Z');
    m->header = (size_t)(m->bytes[8] | m->bytes[9] << 8) * 16;
    m->ds = ds;
    return true;
}

static const uint8_t *at(const image *m, unsigned segment, unsigned offset)
{
    size_t where = m->header + (size_t)(segment - 0x1000) * 16 + offset;
    CHECK(where < m->size);
    return m->bytes + where;
}

/* The Pascal string at DS:offset is text. */
static bool pascal(const image *m, unsigned offset, const char *text)
{
    const uint8_t *s = at(m, m->ds, offset);
    return s[0] == strlen(text) && memcmp(s + 1, text, s[0]) == 0;
}

static void names(const image *m, const cok_rules *r)
{
    const cok_rules_spells *S = &r->spells;
    for (unsigned id = 1; id < S->count; ++id)
        CHECK(pascal(m, S->names_at + S->name_size * id, S->names[id - 1]));
    for (unsigned level = 0; level < S->level_count; ++level)
        CHECK(pascal(m, S->levels_at + S->level_size * level, S->levels[level]));
}

/* The handler table at DS:base, 4 bytes an id, as the routines at
 * segment:offset store it: mov ax, offset; mov dx, segment; mov [a], ax;
 * mov [a + 2], dx, through a thunk's jmp far. Each id's routine offset is
 * written to out[id] (0 for none). */
static void stores(const image *m, unsigned segment, unsigned offset, unsigned base,
                   unsigned count, uint16_t *out)
{
    const uint8_t *b = at(m, segment, offset);
    unsigned found = 0;
    for (size_t i = 0; i < 0x800; ++i) {
        /* to the routine's end: mov sp, bp or pop bp, then retf */
        if ((b[i] == 0x5d && b[i + 1] == 0xcb) || (b[i] == 0x89 && b[i + 1] == 0xec)) break;
        const uint8_t *p = b + i;
        if (p[0] != 0xb8 || p[3] != 0xba || p[6] != 0xa3 || p[9] != 0x89 || p[10] != 0x16) continue;
        unsigned off = p[1] | p[2] << 8, seg = p[4] | p[5] << 8, a = p[7] | p[8] << 8,
                 a2 = p[11] | p[12] << 8;
        if (a2 != a + 2 || a < base || a >= base + 4 * count || (a - base) % 4 != 0) continue;
        ++found;
        const uint8_t *t = at(m, seg + 0x1000u, off); /* the image's segments from 0 */
        CHECK(t[0] == 0xea); /* a thunk: jmp far to the routine */
        unsigned to = t[1] | t[2] << 8;
        unsigned id = (a - base) / 4;
        if (id == 0) continue; /* the spells' 0, the target routine */
        out[id] = (uint16_t)to;
        i += 12;
    }
    (void)found;
}

/* Every id's address in the descriptor is the routine the original stores
 * for it, and an id the original has no routine for has none. */
static void handlers(const image *m, const cok_rules *r, const unsigned fills[][2], size_t n,
                     unsigned spells_at, unsigned effects_at)
{
    static uint16_t spell[256], effect[256];
    memset(spell, 0, sizeof spell);
    memset(effect, 0, sizeof effect);
    for (size_t k = 0; k < n; ++k) {
        stores(m, fills[k][0], fills[k][1], spells_at, r->spells.count, spell);
        stores(m, fills[k][0], fills[k][1], effects_at, r->effects.count, effect);
    }
    for (unsigned id = 1; id < r->spells.count; ++id) {
        const cok_rules_cast *h = &r->spells.cast[id];
        CHECK(h->address == spell[id] && spell[id] != 0);
        CHECK((h->handler != COK_CAST_NONE) != (h->unported != NULL));
        CHECK(h->handler < COK_CAST_HANDLERS);
    }
    for (unsigned id = 1; id < r->effects.count; ++id) {
        const cok_rules_effect *h = &r->effects.handlers[id];
        CHECK(h->address == effect[id] && effect[id] != 0);
        CHECK((h->handler != COK_FX_MISSING) != (h->unported != NULL));
        CHECK(h->handler < COK_FX_HANDLERS);
    }
    CHECK(r->effects.handlers[0].address == 0 && r->effects.handlers[0].handler == COK_FX_MISSING);
}

/* The effect ids each event runs, as the dispatch at segment:offset calls
 * the routine at call (the original's: cmp al, event; jnz; then mov al,
 * id; push ax; push cs; call near for each), to its ret. */
static void events(const image *m, const cok_rules *r, unsigned segment, unsigned offset,
                   unsigned call)
{
    const uint8_t *b = at(m, segment, offset);
    uint8_t lists[32][32];
    unsigned counts[32] = {0};
    int event = -1;
    size_t i = 0;
    for (; i < 0x1000 && !(b[i] == 0x89 && b[i + 1] == 0xec && b[i + 2] == 0x5d); ++i) {
        if (b[i] == 0x3c && (b[i + 2] == 0x75 || b[i + 2] == 0x74)) {
            event = b[i + 1];
            CHECK(event < 32);
            i += 2;
        } else if (b[i] == 0xb0 && b[i + 2] == 0x50 && b[i + 3] == 0x0e && b[i + 4] == 0xe8) {
            unsigned to =
                (unsigned)(offset + i + 7 + (unsigned)(b[i + 5] | b[i + 6] << 8)) & 0xffff;
            if (to != call) continue;
            CHECK(event >= 0 && counts[event] < 32);
            lists[event][counts[event]++] = b[i + 1];
            i += 6;
        }
    }
    CHECK(i < 0x1000);
    const cok_rules_effects *E = &r->effects;
    for (unsigned e = 0; e < 32; ++e) {
        const uint8_t *list = e < E->event_count ? E->events[e] : NULL;
        unsigned n = 0;
        if (list != NULL)
            for (; list[n] != 0; ++n) CHECK(n < counts[e] && list[n] == lists[e][n]);
        CHECK(n == counts[e]);
    }
}

/* The ids the party shares: the set constant (32 bytes, a bit an id) the
 * dispatch of an id tests. */
static void shared(const image *m, const cok_rules *r, unsigned segment, unsigned offset)
{
    const uint8_t *set = at(m, segment, offset);
    unsigned n = 0;
    for (unsigned id = 0; id < 256; ++id) {
        bool in = (set[id / 8] >> (id % 8) & 1) != 0;
        if (!in) continue;
        CHECK(r->effects.shared[n] == id);
        ++n;
    }
    CHECK(r->effects.shared[n] == 0);
}

/* The moons: the days each is passed (the caller at segment:days, push
 * 0x1fc + i, push days), and its tile and column on the frame (the routine
 * at segment:draw: cmp ax, 0x1fc + i; add ax, tile; mov byte [ss:di-2],
 * column; then the tile set's base added to the phase plus the tile,
 * mov al, [ss:di-3]; add ax, base). */
static void moons(const image *m, const cok_rules *r, unsigned segment, unsigned days,
                  unsigned draw)
{
    const cok_rules_moons *M = &r->moons;
    const uint8_t *b = at(m, segment, days);
    for (unsigned i = 0, k = 0; i < 3; ++i) {
        while (!(b[k] == 0xb8 && b[k + 1] == 0xfc + i && b[k + 2] == 1)) CHECK(++k < 0x60);
        CHECK(b[k + 3] == 0x50 && b[k + 4] == 0xb0 && b[k + 5] == M->days[i]);
    }
    b = at(m, segment, draw);
    size_t k = 0;
    for (unsigned i = 0; i < 3; ++i) {
        while (!(b[k] == 0x3d && b[k + 1] == 0xfc + i && b[k + 2] == 1)) CHECK(++k < 0x200);
        while (!(b[k] == 0x05 && b[k + 2] == 0)) CHECK(++k < 0x200);
        CHECK(b[k + 1] == M->tile[i]);
        while (!(b[k] == 0x36 && b[k + 1] == 0xc6 && b[k + 2] == 0x45 && b[k + 3] == 0xfe))
            CHECK(++k < 0x200);
        CHECK(b[k + 4] == M->column[i]);
    }
    while (!(b[k] == 0x36 && b[k + 1] == 0x8a && b[k + 2] == 0x45 && b[k + 3] == 0xfd))
        CHECK(++k < 0x200);
    k += 4;
    if (b[k] == 0x30 && b[k + 1] == 0xe4) k += 2; /* xor ah, ah (Death Knights') */
    CHECK(b[k] == 0x05 && (b[k + 1] | b[k + 2] << 8) == M->tile_base);
}

/* Byte 7's values: the target routine (Champions' 5b04:127e, Death
 * Knights' 5dc5:1182) compares the byte with the caster's, then one
 * picked's, then the party's (cmp al, value). */
static void targets(const image *m, const cok_rules *r, unsigned segment, unsigned offset)
{
    const uint8_t *b = at(m, segment, offset);
    const uint8_t want[3] = {r->spells.target_caster, r->spells.target_one,
                             r->spells.target_party};
    size_t k = 0;
    for (unsigned n = 0; n < 3; ++n) {
        while (!(b[k] == 0x3c && (b[k + 2] == 0x74 || b[k + 2] == 0x75))) CHECK(++k < 0x100);
        CHECK(b[k + 1] == want[n]);
        k += 2;
    }
}

/* The weapon score's test of an item's byte +0x3d (Champions' 3afb:14ca,
 * Death Knights' at 3f63:14f5): cmp byte [es:di+0x3d], value. */
static void weapon(const image *m, const cok_rules *r, unsigned segment, unsigned offset)
{
    const uint8_t *b = at(m, segment, offset);
    size_t k = 0;
    while (!(b[k] == 0x26 && b[k + 1] == 0x80 && b[k + 2] == 0x7d && b[k + 3] == 0x3d))
        CHECK(++k < 0x200);
    CHECK(b[k + 4] == r->spells.weapon_unscored);
}

/* The list of magic-user spells to learn (Champions' 5b04:0b21's kind 4,
 * Death Knights' 5dc5:0a4e): mov dx, the lists' stride, then the loop
 * over levels to the lists' last (cmp byte [bp-0xd], levels), as
 * magic.c's loop bounds it by COK_DATA_SPELL_LISTS's shape. */
static void learn(const image *m, const cok_rules *r, unsigned segment, unsigned offset)
{
    const cok_data_segment *D = r->data;
    const uint8_t *b = at(m, segment, offset);
    unsigned stride = D->table[COK_DATA_SPELL_LISTS].index[0].stride;
    size_t k = 0;
    while (!(b[k] == 0xba && b[k + 1] == (stride & 0xff) && b[k + 2] == stride >> 8))
        CHECK(++k < 0x400);
    while (!(b[k] == 0x80 && b[k + 1] == 0x7e && b[k + 2] == 0xf3)) CHECK(++k < 0x400);
    CHECK(b[k + 3] == cok_data_count(D, COK_DATA_SPELL_LISTS, 1));
}

/* A spell list's arrays (magic.c) hold the game's lists. */
static void lists(const cok_rules *r)
{
    CHECK(r->spells.list_spells > 0 && r->spells.list_spells <= COK_SPELL_LIST_MOST);
    CHECK(r->spells.list_scrolls > 0 && r->spells.list_scrolls <= COK_SPELL_LIST_MOST);
}

/* Ids with one routine have one handler, in each table. */
static void consistent(const cok_rules *r)
{
    for (unsigned a = 1; a < r->spells.count; ++a)
        for (unsigned b = a + 1; b < r->spells.count; ++b)
            if (r->spells.cast[a].address == r->spells.cast[b].address)
                CHECK(r->spells.cast[a].handler == r->spells.cast[b].handler);
    for (unsigned a = 1; a < r->effects.count; ++a)
        for (unsigned b = a + 1; b < r->effects.count; ++b)
            if (r->effects.handlers[a].address == r->effects.handlers[b].address)
                CHECK(r->effects.handlers[a].handler == r->effects.handlers[b].handler);
}

static void test_champions(void)
{
    const cok_rules *r = &cok_rules_champions;
    /* Named ids: every one Champions has, each the id its comment says the
     * code had (spot checks), and the lookups. */
    for (unsigned n = 0; n < COK_SPELL_NAMES; ++n) CHECK(r->spells.ids[n] != 0);
    for (unsigned n = 0; n < COK_EFFECT_NAMES; ++n) CHECK(r->effects.ids[n] != 0);
    CHECK(cok_spell_id(r, COK_SPELL_KNOCK) == 0x1f && cok_spell_id(r, COK_SPELL_FIREBALL) == 0x2f);
    CHECK(cok_effect_id(r, COK_EFFECT_INVISIBLE) == 0x19 &&
          cok_effect_id(r, COK_EFFECT_TERROR) == 0x6f);
    CHECK(cok_effect_is(r, 0x37, COK_EFFECT_POISONED) &&
          !cok_effect_is(r, 0x36, COK_EFFECT_POISONED));
    CHECK(cok_spell_is(r, 3, COK_SPELL_CURE_LIGHT_WOUNDS) && !cok_spell_is(r, 0, COK_SPELL_KNOCK));
    /* Each named effect has a handler; spells 1-0x6b all have one. */
    for (unsigned n = 0; n < COK_EFFECT_NAMES; ++n)
        CHECK(r->effects.handlers[r->effects.ids[n]].handler != COK_FX_MISSING);
    CHECK(r->spells.count == 0x6c && r->effects.count == 0x79);
    CHECK(r->spells.durations != NULL && r->spells.duration_count == 7);
    /* Every handler the port has is one of Champions' routines or Death
     * Knights', and so is every effect handler. */
    const cok_rules *d = &cok_rules_death_knights;
    for (unsigned k = 1; k < COK_CAST_HANDLERS; ++k) {
        bool used = false;
        for (unsigned id = 1; id < r->spells.count; ++id) used |= r->spells.cast[id].handler == k;
        for (unsigned id = 1; id < d->spells.count; ++id) used |= d->spells.cast[id].handler == k;
        CHECK(used);
    }
    /* Champions' take no constants: their routines are their own. */
    for (unsigned id = 1; id < r->spells.count; ++id)
        CHECK(r->spells.cast[id].value == 0 && r->spells.cast[id].count == 0 &&
              r->spells.cast[id].sides == 0 && r->spells.cast[id].type == 0 &&
              r->spells.cast[id].text == NULL && r->spells.cast[id].handler != COK_CAST_DICE &&
              r->spells.cast[id].handler != COK_CAST_SAYING &&
              r->spells.cast[id].handler != COK_CAST_MISSILES &&
              r->spells.cast[id].handler != COK_CAST_CURE_CRITICAL);
    CHECK(!r->casting.cone_range_length && !r->casting.cone_stack_unknown &&
          r->casting.flame_type == NULL && r->casting.dispel_clouds_unported == NULL &&
          r->casting.cone_side_unknown == NULL);
    for (unsigned k = 1; k < COK_FX_HANDLERS; ++k) {
        bool used = false;
        for (unsigned id = 1; id < r->effects.count; ++id) used |= r->effects.handlers[id].handler == k;
        for (unsigned id = 1; id < d->effects.count; ++id)
            used |= d->effects.handlers[id].handler == k;
        CHECK(used);
    }
    consistent(r);
    lists(r);
    CHECK(r->spells.weapon_unscored == 0x53);
    image m;
    CHECK(load(&m, "build/START_FULL.EXE", 0x1bc6));
    names(&m, r);
    static const unsigned fills[][2] = {{0x5b04, 0x58ed}, {0x3f44, 0x38ea}};
    handlers(&m, r, fills, 2, 0x6e3a, 0x6b94);
    events(&m, r, 0x60f4, 0x057c, 0x0352);
    shared(&m, r, 0x60f4, 0x0332);
    moons(&m, r, 0x57e4, 0x0143, 0x001e);
    targets(&m, r, 0x5b04, 0x127e);
    weapon(&m, r, 0x3afb, 0x14ca);
    learn(&m, r, 0x5b04, 0x0b21);
}

static void test_death_knights(void)
{
    const cok_rules *r = &cok_rules_death_knights;
    CHECK(r->spells.count == 0x73 && r->effects.count == 0x7a);
    CHECK(r->spells.ids[COK_SPELL_ANIMATE_DEAD] == 0);
    CHECK(!cok_spell_is(r, 0x24, COK_SPELL_ANIMATE_DEAD) &&
          !cok_spell_is(r, 0, COK_SPELL_ANIMATE_DEAD));
    CHECK(r->effects.ids[COK_EFFECT_BOLTS] == 0 && !cok_effect_is(r, 0, COK_EFFECT_BOLTS));
    consistent(r);
    lists(r);
    CHECK(r->spells.weapon_unscored == 0x53);
    CHECK(cok_spell_id(r, COK_SPELL_KNOCK) == 0x1f);
    /* Band A: Champions' handlers with the entry's constants (Enlarge,
     * Lightning Bolt, Flame Strike, Ice Storm) or as they are (Confusion);
     * the dice handler only with dice. */
    CHECK(r->spells.cast[0x0c].handler == COK_CAST_ENLARGE && r->spells.cast[0x0c].value == 1);
    CHECK(r->spells.cast[0x33].handler == COK_CAST_LIGHTNING_BOLT &&
          r->spells.cast[0x33].value == 1);
    CHECK(r->spells.cast[0x52].handler == COK_CAST_CONFUSION &&
          cok_effect_id(r, COK_EFFECT_CONFUSED) == 0x23);
    for (unsigned id = 1; id < r->spells.count; ++id) {
        const cok_rules_cast *h = &r->spells.cast[id];
        CHECK((h->handler == COK_CAST_DICE) == (h->count != 0 && h->sides != 0));
        /* The kinds that say only the entry's words have them. */
        if (h->handler == COK_CAST_SAYING || h->handler == COK_CAST_MISSILES)
            CHECK(h->text != NULL && h->text[0] != '\0');
    }
    /* Band B: the ids of one routine share one entry (Fireball's 0x6c, Delay
     * Blast, apart: its routine adds the level for 0x6c alone). */
    static const uint8_t same[][3] = {{0x01, 0x69, 0}, {0x0a, 0x6a, 0}, {0x29, 0x2e, 0},
                                      {0x2b, 0x59, 0x68}, {0x2c, 0x64, 0}, {0x2f, 0x40, 0},
                                      {0x3e, 0x63, 0}};
    for (size_t i = 0; i < sizeof same / sizeof *same; ++i)
        for (size_t k = 1; k < 3 && same[i][k] != 0; ++k) {
            const cok_rules_cast *a = &r->spells.cast[same[i][0]], *b = &r->spells.cast[same[i][k]];
            CHECK(a->handler == b->handler && a->value == b->value &&
                  (a->text == b->text || (a->text != NULL && b->text != NULL &&
                                          strcmp(a->text, b->text) == 0)));
        }
    /* A named effect it has has a handler entry; the rules not ported yet
     * say so. */
    for (unsigned n = 0; n < COK_EFFECT_NAMES; ++n) {
        uint8_t id = r->effects.ids[n];
        CHECK(id == 0 || id == cok_rules_champions.effects.ids[n]);
        if (id != 0)
            CHECK(r->effects.handlers[id].handler != COK_FX_MISSING ||
                  r->effects.handlers[id].unported != NULL);
    }
    /* The saving throw (Champions' but for effect 0x39 and the moon's
     * variable: test_effect, test_death_knights). */
    CHECK(r->effects.save_fail_id == 0x39 && !r->effects.save_moon_wraps);
    CHECK(r->effects.display_spells != NULL && r->spells.display_last == 0x3f);
    image m;
    if (!load(&m, "build/death-START_FULL.EXE", 0x1bce)) {
        puts("magic rules: build/death-START_FULL.EXE not built (make death-merged); Death "
             "Knights' tables not checked");
        return;
    }
    names(&m, r);
    static const unsigned fills[][2] = {{0x5dc5, 0x5622}, {0x504d, 0x2948}};
    handlers(&m, r, fills, 2, 0xc150, 0xbe7e);
    events(&m, r, 0x638c, 0x055b, 0x0327);
    shared(&m, r, 0x638c, 0x0307);
    moons(&m, r, 0x5b7d, 0x0117, 0x0000);
    targets(&m, r, 0x5dc5, 0x1182);
    weapon(&m, r, 0x3f63, 0x1400);
    learn(&m, r, 0x5dc5, 0x0a4e);
}

int main(void)
{
    test_champions();
    test_death_knights();
    puts("magic rules: ok");
    return 0;
}
