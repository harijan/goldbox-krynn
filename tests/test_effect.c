#include "effect.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_ecl vm;
static cok_party party;
static cok_item_types types;
static cok_effects fx;

/* A party member with strength 12/0 who can act, AC 5 and throws of 15. */
static cok_character *member(void)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    for (size_t i = 0; i < 6; ++i) c->record[0x10 + 2 * i] = c->record[0x11 + 2 * i] = 12;
    c->record[0x189] = 1;
    c->record[0x18d] = 55;
    memset(c->record + 0xd0, 15, 5);
    CHECK(cok_party_add(&party, c));
    return c;
}

/* A fresh game with an empty party, mode 4. */
static void reset(void)
{
    cok_party_free(&party);
    cok_effects_free(&fx);
    memset(&vm, 0, sizeof vm);
    vm.mode = 4;
    cok_effects_init(&fx, &vm, &party, &types);
}

static cok_effect *add(cok_character *c, uint8_t id, uint16_t duration, uint8_t value, bool on_remove)
{
    cok_effect *e = cok_character_add_effect(c, id, duration, value, on_remove);
    CHECK(e != NULL);
    return e;
}

static size_t count(const cok_character *c)
{
    size_t n = 0;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next) ++n;
    return n;
}

/* What the say hook was given, each ended by ";". */
static char said_text[256];

static void said(cok_effects *effects, cok_character *c, const char *text, bool wait,
                 void *context)
{
    (void)effects, (void)c, (void)context, (void)wait;
    size_t used = strlen(said_text);
    snprintf(said_text + used, sizeof said_text - used, "%s;", text);
}

/* The ids of c's effects, as a string of hex pairs. */
static const char *ids(const cok_character *c)
{
    static char out[128];
    out[0] = '\0';
    for (const cok_effect *e = c->effects; e != NULL; e = e->next)
        snprintf(out + strlen(out), sizeof out - strlen(out), "%02x", e->id);
    return out;
}

/* A seed whose first d20 is roll. */
static uint32_t seed_for(uint8_t roll)
{
    for (uint32_t s = 1;; ++s) {
        uint32_t t = s;
        if (cok_tp_random(&t, 20) + 1 == roll) return s;
    }
}

static void test_list(void)
{
    reset();
    cok_character *c = member();
    cok_effect *a = add(c, 0x05, 0, 1, false), *b = add(c, 0x13, 0, 2, false);
    add(c, 0x05, 0, 3, false);
    CHECK(strcmp(ids(c), "051305") == 0 && cok_character_find_effect(c, 0x05) == a);
    /* By pointer, then the first with an id; an id it lacks is no change. */
    CHECK(cok_effects_remove(&fx, c, b, 0x13));
    CHECK(cok_effects_remove(&fx, c, NULL, 0x05) && strcmp(ids(c), "05") == 0);
    CHECK(c->effects->value == 3 && cok_character_find_effect(c, 0x13) == NULL);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x40) && count(c) == 1);
    /* Removing an effect from a list it is not in writes through NULL. */
    cok_character *other = member();
    cok_effect *stray = add(other, 0x05, 0, 0, false);
    CHECK(!cok_effects_remove(&fx, c, stray, 0x05) && strstr(fx.error, "0000:0005") != NULL);
    /* Handlers run on removal only when the effect asks. 0x3a, death
     * throes, then adds 0x3b and 0x3c and removes the first 0x39. */
    add(c, 0x39, 0, 0, false);
    add(c, 0x3a, 6, 0, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x3a) && strcmp(ids(c), "0539") == 0);
    add(c, 0x3a, 6, 0, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x3a) && strcmp(ids(c), "053b3c") == 0);
    CHECK(c->effects->next->next->duration == 3 && c->effects->next->next->on_remove);
    /* Effect 0 has no handler: the original calls 0000:0000. Effect 4's
     * is a monster's attack, which 5b04:58ed installs at startup. */
    add(c, 0x00, 0, 0, true);
    CHECK(!cok_effects_remove(&fx, c, NULL, 0x00) && strstr(fx.error, "0000:0000") != NULL);
    add(c, 0x04, 0, 0, true);
    CHECK(!cok_effects_remove(&fx, c, NULL, 0x04) && strstr(fx.error, "5b04:4ab8") != NULL);
    /* A handler that needs combat is not ported. */
    add(c, 0x1a, 0, 0, true);
    CHECK(!cok_effects_remove(&fx, c, NULL, 0x1a) && strstr(fx.error, "3f44:09e8") != NULL);
}

/* Removing an effect that changes strength or charisma recomputes the
 * score from the base, the readied items and the remaining effects. */
static void test_abilities(void)
{
    reset();
    cok_character *c = member();
    uint8_t *r = c->record;
    r[0x1d] = 40;
    /* 0x0c with value 51 made strength 18/50; without it, 12/40. */
    r[0x11] = 18;
    r[0x1c] = 50;
    add(c, 0x0c, 10, 51, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 12 && r[0x1c] == 40);
    /* A second one stays: its value 0x70 gives 12 (112 - 100), no better. */
    add(c, 0x0c, 10, 51, false);
    add(c, 0x0c, 10, 0x70, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 12 && r[0x1c] == 40);
    /* 0x26 adds its value (here 1, as 18/0) to strength below 19 for a
     * fighter: 12 + 18 is past 18, so 18 with (30 - 18) * 10, past 100. */
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c));
    r[0xfb] = 1;
    add(c, 0x26, 0, 1, false);
    add(c, 0x26, 10, 1, false);
    CHECK(cok_effects_remove(&fx, c, c->effects->next, 0x26) && r[0x11] == 18 && r[0x1c] == 100);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x26));
    /* A former ranger (+0x105) counts as a fighter too. */
    r[0xfb] = 0;
    r[0x105] = 1;
    add(c, 0x26, 0, 1, false);
    add(c, 0x26, 10, 1, false);
    CHECK(cok_effects_remove(&fx, c, c->effects->next, 0x26) && r[0x11] == 18 && r[0x1c] == 100);
    r[0x105] = 0;
    CHECK(cok_effects_remove(&fx, c, NULL, 0x26) && r[0x11] == 12 && r[0x1c] == 40);
    /* Values up to 0x65 give 18 with the value - 1, so 0x65 is 18/100. */
    add(c, 0x0c, 0, 0x65, false);
    add(c, 0x0c, 0, 0x65, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 18 && r[0x1c] == 100);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c));
    /* An 18 with more exceptional strength beats 19 (60f4:15db). */
    r[0x10] = 19;
    add(c, 0x0c, 0, 51, false);
    add(c, 0x0c, 0, 51, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 18 && r[0x1c] == 50);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 19 && r[0x1c] == 40);
    r[0x10] = 12;
    /* Items: power 3 (18/100) and 0x0d (strength 3) readied. */
    c->items = calloc(2, COK_ITEM_SIZE);
    c->item_count = 1;
    c->items[0][0x3e] = 0x83;
    c->items[0][0x34] = 1;
    add(c, 0x26, 0, 0, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x26) && r[0x11] == 18 && r[0x1c] == 100);
    c->items[0][0x3e] = 0x8d;
    add(c, 0x0c, 0, 0, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0c) && r[0x11] == 3 && r[0x1c] == 0);
    /* Charisma: base 12, another 0x0e adding 2, an item of power 6 less 1. */
    c->items[0][0x3e] = 0x86;
    add(c, 0x0e, 5, 4, false);
    add(c, 0x0e, 5, 2, false);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0e) && r[0x1b] == 13);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0e) && r[0x1b] == 11);
}

/* Spiritual hammer: when it ends the hammer goes and the stats are
 * recomputed. */
static void test_hammer(void)
{
    reset();
    cok_character *c = member();
    c->record[0x10f] = 2; /* base dice: 1d2 */
    c->items = calloc(2, COK_ITEM_SIZE);
    c->item_count = 2;
    c->items[0][0x2e] = 0x10;
    c->items[1][0x2e] = 6;
    c->items[1][0x31] = 0x79;
    c->items[1][0x34] = 1;
    char error[200];
    CHECK(cok_character_stats(c, &types, error, sizeof error));
    CHECK(c->slots[0] == 2 && c->record[0x193] == 4 && c->record[0x142] == 2);
    add(c, 0x17, 0, 0, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x17));
    CHECK(c->item_count == 1 && c->items[0][0x2e] == 0x10 && c->slots[0] == 0);
    CHECK(c->record[0x193] == 2 && c->record[0x142] == 1);
    /* With a hammer, or 16 items as last counted (+0x142), it only
     * recomputes the stats. */
    add(c, 0x17, 0, 0, false);
    c->record[0x142] = 16;
    c->record[0x193] = 99;
    CHECK(cok_effects_dispatch(&fx, c, 0x13) && c->record[0x193] == 2 && c->record[0x142] == 1);
    c->items[0][0x2e] = 6;
    c->items[0][0x31] = 0x79;
    c->record[0x193] = 99;
    CHECK(cok_effects_dispatch(&fx, c, 0x13) && c->record[0x193] == 2 && c->item_count == 1);
    /* Without one it adds a hammer at the end, unreadied, and says so; it
     * cannot without a way to print. */
    c->items[0][0x31] = 0;
    CHECK(!cok_effects_dispatch(&fx, c, 0x13) && strstr(fx.error, "3f44:07b5") != NULL);
    CHECK(c->item_count == 2);
    c->item_count = 1;
    fx.say = said;
    said_text[0] = '\0';
    CHECK(cok_effects_dispatch(&fx, c, 0x13) && strcmp(said_text, "Gains an item;") == 0);
    const uint8_t *h = c->items[1];
    CHECK(c->item_count == 2 && h[0x2e] == 6 && h[0x30] == 6 && h[0x31] == 0x79 && h[0x32] == 1);
    CHECK(h[0x3d] == 0x17 && h[0x3e] == 0x80 && h[0x34] == 0 && h[0x39] == 0 && h[0x2f] == 0);
    CHECK(c->slots[0] == 0 && c->record[0x142] == 2);
    fx.say = NULL;
}

/* A readied item of type for c, with bonus. */
static void ready(cok_character *c, uint8_t type, int bonus)
{
    uint8_t (*items)[COK_ITEM_SIZE] = realloc(c->items, (c->item_count + 1) * COK_ITEM_SIZE);
    CHECK(items != NULL);
    c->items = items;
    uint8_t *item = c->items[c->item_count++];
    memset(item, 0, COK_ITEM_SIZE);
    item[0x2e] = type;
    item[0x34] = 1;
    item[0x32] = (uint8_t)bonus;
    char error[200];
    CHECK(cok_character_stats(c, &types, error, sizeof error));
}

static void unready_all(cok_character *c)
{
    c->item_count = 0;
    char error[200];
    CHECK(cok_character_stats(c, &types, error, sizeof error));
}

/* Damage to c (event 5) from the selected character's weapon. */
static uint8_t damage_from(cok_character *c, uint8_t amount)
{
    fx.rolls.amount = amount;
    CHECK(cok_effects_dispatch(&fx, c, 5));
    return fx.rolls.amount;
}

/* Effects on damage by what the attacker strikes with (3f44:13a9): mace
 * 8 (blunt), sword 0x11 (edged), bow 0x16 (piercing, shooting arrows
 * 0x1e), holy water 0x36. */
static void test_weapons(void)
{
    reset();
    cok_character *c = member(), *attacker = member();
    vm.character = attacker->record;
    cok_effect *e = add(c, 0x5d, 0, 0, false);
    CHECK(damage_from(c, 10) == 10);
    ready(attacker, 0x08, 0);
    CHECK(damage_from(c, 10) == 5);
    unready_all(attacker);
    ready(attacker, 0x11, 0);
    CHECK(damage_from(c, 10) == 10);
    e->id = 0x64;
    CHECK(damage_from(c, 10) == 5);
    unready_all(attacker);
    ready(attacker, 0x08, 0);
    CHECK(damage_from(c, 10) == 10);
    unready_all(attacker);
    ready(attacker, 0x16, 0);
    CHECK(damage_from(c, 10) == 5);
    /* 0x67: only an item with a bonus hurts, from a player race or below
     * level 4; for a bow, the arrows'. */
    e->id = 0x67;
    CHECK(damage_from(c, 10) == 0);
    ready(attacker, 0x1e, 1);
    CHECK(damage_from(c, 10) == 10);
    unready_all(attacker);
    ready(attacker, 0x16, 1);
    CHECK(damage_from(c, 10) == 10); /* no arrows: the bow */
    ready(attacker, 0x1e, 0);
    CHECK(damage_from(c, 10) == 0);
    unready_all(attacker);
    ready(attacker, 0x11, 1);
    CHECK(damage_from(c, 10) == 10);
    unready_all(attacker);
    CHECK(damage_from(c, 10) == 0);
    attacker->record[0x5a] = 7;
    attacker->record[0xd6] = 4;
    CHECK(damage_from(c, 10) == 10);
    /* 0x65: holy water as the weapon deals 1d6 + 1. */
    e->id = 0x65;
    CHECK(damage_from(c, 10) == 10);
    ready(attacker, 0x36, 0);
    uint32_t seed = vm.seed;
    uint8_t expected = (uint8_t)(cok_tp_random(&seed, 6) + 2);
    CHECK(damage_from(c, 10) == expected && fx.rolls.dice == 1 && vm.seed == seed);
    /* No character selected: the original reads through NULL. */
    vm.character = NULL;
    fx.rolls.amount = 10;
    CHECK(!cok_effects_dispatch(&fx, c, 5) && strstr(fx.error, "none is selected") != NULL);
}

/* The handlers of the saving throw (event 0x0c) and of attack rolls on the
 * character (0x10), each alone. */
static void test_dispatch(void)
{
    reset();
    cok_character *c = member(), *other = member();
    uint8_t *r = c->record;
    vm.character = other->record;
    cok_rolls *k = &fx.rolls;
    static const struct {
        uint8_t id, value, damage_type, save_type, alignment;
        uint8_t save, attack, amount;
        uint8_t con, made, saved; /* constitution 0 for 12 */
    } cases[] = {
        {0x5e, 0, 0, 0, 0, 50 + 3, 50, 10, 0, 0, 0},    /* constitution 12: +3 */
        {0x5e, 0, 0, 1, 0, 50, 50, 10, 0, 0, 0},        /* but not for type 1 */
        {0x5e, 0, 0, 2, 0, 50 + 4, 50, 10, 17, 0, 0},   /* 14-17: +4 */
        {0x5e, 0, 0, 4, 0, 50 + 5, 50, 10, 18, 0, 0},   /* 18-20: +5 */
        {0x08, 0, 0, 0, 5, 52, 48, 10, 0, 0, 0},        /* protection from evil */
        {0x08, 0, 0, 0, 8, 52, 48, 10, 0, 0, 0},
        {0x08, 0, 0, 0, 7, 50, 50, 10, 0, 0, 0},
        {0x08, 0, 0, 0, 4, 50, 50, 10, 0, 0, 0},
        {0x09, 0, 0, 0, 6, 52, 48, 10, 0, 0, 0},        /* from good */
        {0x0a, 0, 2, 0, 0, 53, 50, 5, 0, 0, 0},         /* resist cold */
        {0x14, 0, 1, 0, 0, 53, 50, 5, 0, 0, 0},         /* resist fire */
        {0x14, 0, 2, 0, 0, 50, 50, 10, 0, 0, 0},
        {0x11, 0, 0, 0, 0, 51, 50, 10, 0, 0, 0},        /* shield */
        {0x21, 0, 0, 0, 0, 46, 46, 10, 0, 0, 0},        /* blindness */
        {0x24, 0, 0, 0, 0, 46, 46, 10, 0, 0, 0},
        {0x31, 0x00, 0, 0, 0, 51, 51, 10, 0, 0, 0},     /* prayer, same side */
        {0x31, 0x10, 0, 0, 0, 49, 49, 10, 0, 0, 0},
        {0x3d, 0, 1, 0, 0, 54, 50, 0, 0, 0, 0},         /* fire resistance, not magical */
        {0x3d, 0, 9, 0, 0, 54, 50, 6, 0, 0, 0},         /* magical: 2 less a die */
        {0x63, 0, 0, 0, 0, 100, 50, 10, 0, 0, 0},       /* type 0 always made */
        {0x63, 0, 0, 3, 0, 50, 50, 10, 0, 0, 0},
        {0x32, 0, 2, 0, 0, 52, 50, 10, 0, 0, 0},        /* cold resistant */
        {0x32, 0, 1, 0, 0, 50, 50, 20, 0, 0, 0},        /* fire vulnerable after a failed save */
        {0x32, 0, 1, 0, 0, 50, 50, 10, 0, 1, 0},        /* not after a save made */
        {0x36, 0, 1, 0, 0, 52, 50, 10, 0, 0, 0},
        {0x36, 0, 2, 0, 0, 50, 50, 20, 0, 1, 0},        /* cold vulnerable, whatever the save, */
        {0x36, 0, 2, 0, 0, 50, 50, 10, 0, 0, 1},        /* unless saved in this camp */
    };
    for (size_t i = 0; i < sizeof cases / sizeof *cases; ++i) {
        while (c->effects != NULL) CHECK(cok_effects_remove(&fx, c, c->effects, c->effects->id));
        add(c, cases[i].id, 0, cases[i].value, false);
        other->record[0x10a] = cases[i].alignment;
        r[0x18d] = 55;
        r[0x19] = cases[i].con != 0 ? cases[i].con : 12;
        *k = (cok_rolls){.save_roll = 50, .attack_roll = 50, .amount = 10, .dice = 2,
                         .damage_type = cases[i].damage_type, .save_type = cases[i].save_type,
                         .save_made = cases[i].made, .saved = cases[i].saved};
        CHECK(cok_effects_dispatch(&fx, c, 0x0c));
        CHECK(k->save_roll == cases[i].save && k->attack_roll == cases[i].attack);
        CHECK(k->amount == cases[i].amount);
    }
    CHECK(r[0x18d] == 55); /* the last case; shield and blindness change it */
    /* Shield and blindness change the AC in place each time. */
    add(c, 0x11, 0, 0, false);
    r[0x18d] = 50;
    CHECK(cok_effects_dispatch(&fx, c, 0x0c) && r[0x18d] == 0x39);
    add(c, 0x21, 0, 0, false);
    CHECK(cok_effects_dispatch(&fx, c, 0x0c) && r[0x18d] == 0x39 - 4);
    CHECK(cok_effects_dispatch(&fx, c, 0x0c) && r[0x18d] == 0x39 - 4);

    /* Attack rolls on c: invisibility (unless the attacker has 0x18),
     * 0x2f against an attacker with bit 2 of +0x13f, displacement. */
    while (c->effects != NULL) CHECK(cok_effects_remove(&fx, c, c->effects, c->effects->id));
    add(c, 0x19, 0, 0, false);
    k->attack_roll = 10;
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 6 && k->untargetable == 1);
    add(other, 0x18, 0, 0, false);
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 6);
    add(c, 0x2f, 0, 0, false);
    other->record[0x13f] = 4;
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 2);
    cok_effect *d = add(c, 0x59, 0, 0, false);
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 0xff && d->value == 0x10);
    k->attack_roll = 10;
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 6);
    /* A roll of 0 after round 0 leaves it used. As combat starts (round 0,
     * roll 0) it is ready again. */
    k->attack_roll = 4;
    k->round = 1;
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 0 && d->value == 0x10);
    k->attack_roll = 4;
    k->round = 0;
    CHECK(cok_effects_dispatch(&fx, c, 0x10) && k->attack_roll == 0 && d->value == 0);
    /* With no character selected, 0x2f reads through NULL. */
    vm.character = NULL;
    CHECK(!cok_effects_dispatch(&fx, c, 0x10) && strstr(fx.error, "none is selected") != NULL);

    /* Prayer, protection and the like are shared by the party; not in
     * combat, where only members in range count. */
    reset();
    c = member();
    other = member();
    add(other, 0x31, 0, 0, false);
    k = &fx.rolls;
    k->save_roll = 10;
    CHECK(cok_effects_dispatch(&fx, c, 0x0c) && k->save_roll == 11);
    vm.mode = 5;
    CHECK(!cok_effects_dispatch(&fx, c, 0x0c) && strstr(fx.error, "combat map") != NULL);
    /* Silence (0x15) is shared too; its handler needs combat. Others are not. */
    vm.mode = 4;
    add(other, 0x15, 0, 0, false);
    CHECK(!cok_effects_dispatch(&fx, c, 0x0f) && strstr(fx.error, "3f44:06b2") != NULL);
    add(other, 0x21, 0, 0, false);
    k->save_roll = 10;
    CHECK(cok_effects_dispatch(&fx, c, 0x0c) && k->save_roll == 11);
    /* Minor globe: a spell of a level below 4, signed, does nothing. */
    add(c, 0x3f, 0, 0, false);
    static const struct {
        uint8_t spell, amount;
    } globe[] = {{1, 0}, {36, 9}, {58, 9}, {174, 0}}; /* levels 1, 7, 4 and 0xff */
    for (size_t i = 0; i < sizeof globe / sizeof *globe; ++i) {
        k->spell = globe[i].spell;
        k->amount = 9;
        CHECK(cok_effects_dispatch(&fx, c, 9) && k->amount == globe[i].amount);
    }
    k->spell = 0;

    /* The other events: bless and curse on morale (0x11), the rate (0x12),
     * resisting what a spell adds (9), cancelling damage (5, 6). */
    reset();
    c = member();
    k = &fx.rolls;
    add(c, 0x01, 0, 0, false);
    add(c, 0x02, 0, 0, false);
    k->morale = 3;
    k->attack_roll = 7;
    CHECK(cok_effects_dispatch(&fx, c, 0x11) && k->morale == 3 && k->attack_roll == 7);
    k->morale = 2;
    CHECK(cok_effects_dispatch(&fx, c, 0x11) && k->morale == 2);
    add(c, 0x2a, 0, 0, false);
    k->rate = 12;
    CHECK(cok_effects_dispatch(&fx, c, 0x12) && k->rate == 6);
    add(c, 0x60, 0, 0, false);
    add(c, 0x61, 0, 0, false);
    k->pending = 0x34;
    k->amount = 9;
    CHECK(cok_effects_dispatch(&fx, c, 9) && k->pending == 0 && k->amount == 0);
    k->pending = 0x35;
    k->amount = 9;
    CHECK(cok_effects_dispatch(&fx, c, 9) && k->pending == 0);
    add(c, 0x3b, 0, 0, false);
    k->amount = 9;
    CHECK(cok_effects_dispatch(&fx, c, 5) && k->amount == 0);
    /* Magic resistance against a pending spell needs the caster's level,
     * from the selected character. */
    reset();
    c = member();
    c->record[0x187] = 10;
    k->pending = 0x34;
    k->amount = 0;
    vm.character = NULL;
    CHECK(!cok_effects_dispatch(&fx, c, 6) && strstr(fx.error, "6346:29fe") != NULL);
    /* At level 6 (no cleric or mage level), 10 + 5 * 5 = 35: a d100 up to
     * 35 resists; magic damage too; other damage is not checked. */
    vm.character = c->record;
    k->spell = 0x34;
    bool resisted = false, kept = false;
    for (int i = 0; i < 40; ++i) {
        uint32_t seed = vm.seed;
        uint8_t d100 = (uint8_t)(cok_tp_random(&seed, 100) + 1);
        k->pending = 0x34;
        k->amount = 0;
        CHECK(cok_effects_dispatch(&fx, c, 9));
        CHECK((k->pending == 0) == (d100 <= 35));
        if (d100 <= 35) resisted = true; else kept = true;
    }
    CHECK(resisted && kept);
    k->pending = 0x34;
    k->amount = 5;
    k->damage_type = 1;
    uint32_t before = vm.seed;
    CHECK(cok_effects_dispatch(&fx, c, 9) && k->pending == 0x34 && vm.seed == before);
    /* 0x5b and 0x52 lose only the damage; level 12 and up wraps, so that
     * a resistance of 1 to 4 always resists. */
    c->record[0x187] = 3;
    c->record[0xfe] = 12;
    k->damage_type = 8;
    k->pending = 0x5b;
    CHECK(cok_effects_dispatch(&fx, c, 9) && k->pending == 0x5b && k->amount == 0);
    c->record[0xfe] = 0;
    c->record[0x187] = 0;
    k->spell = 0;
    /* The handlers of event 0x0e are monsters' attacks in combat. */
    add(c, 0x55, 0, 0, false);
    CHECK(!cok_effects_dispatch(&fx, c, 0x0e) && strstr(fx.error, "5b04:50cf") != NULL);
}

/* What the handlers do when their effect ends, for those that look at it
 * or change the record. */
static void test_removal(void)
{
    reset();
    cok_character *c = member();
    uint8_t *r = c->record;
    /* Charm: back to the side in bit 6, and a player character again. */
    r[0xe7] = 0xb3;
    r[0x18b] = 1;
    r[0x18a] = 1;
    add(c, 0x0b, 5, 0x60, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x0b) && r[0x18a] == 1 && r[0xe7] == 0 && r[0x18b] == 0);
    /* Berserk: likewise, and to the party's side; applied, an NPC. */
    add(c, 0x4d, 0, 0, false);
    r[0xe7] = 0x90;
    CHECK(cok_effects_dispatch(&fx, c, 0x0f) && r[0x18b] == 1 && r[0xe7] == 0xb2);
    r[0xe7] = 0x10;
    CHECK(cok_effects_dispatch(&fx, c, 0x0f) && r[0xe7] == 0xb3);
    CHECK(cok_effects_dispatch(&fx, c, 0x0f) && r[0xe7] == 0xb3);
    c->effects->on_remove = true;
    CHECK(cok_effects_remove(&fx, c, NULL, 0x4d) && r[0xe7] == 0 && r[0x18b] == 0 && r[0x18a] == 0);
    /* 0x6b: the side from its value. */
    r[0xe7] = 0xb3;
    add(c, 0x6b, 5, 1, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x6b) && r[0x18a] == 1 && r[0xe7] == 0);
    /* 0x6e: +0x5f from the knight level; 0 overflows the division. */
    r[0x100] = 11;
    add(c, 0x6e, 5, 0, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x6e) && r[0x5f] == 3);
    r[0x100] = 0;
    add(c, 0x6e, 5, 0, true);
    CHECK(!cok_effects_remove(&fx, c, NULL, 0x6e) && strstr(fx.error, "200") != NULL);
    /* 0x38 and 0x6c add invisibility, for a minute or 255. */
    reset();
    c = member();
    add(c, 0x6c, 5, 0, true);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x6c) && strcmp(ids(c), "19") == 0);
    CHECK(c->effects->duration == 255 && c->effects->value == 0xff && !c->effects->on_remove);
    add(c, 0x38, 0, 0, false);
    CHECK(cok_effects_dispatch(&fx, c, 0x08) && strcmp(ids(c), "193819") == 0);
    /* 0x27, haste, doubles the rate; the first time it ages the character
     * a year and says so, which needs a way to print. */
    cok_effect *haste = add(c, 0x27, 0, 0x0f, false);
    fx.rolls.rate = 6;
    CHECK(!cok_effects_dispatch(&fx, c, 0x12) && strstr(fx.error, "3f44:0f78") != NULL);
    haste->value = 0x0f;
    c->record[0x60] = 0xff;
    c->record[0x61] = 0;
    fx.say = said;
    said_text[0] = '\0';
    CHECK(cok_effects_dispatch(&fx, c, 0x12) && fx.rolls.rate == 12 && haste->value == 0x1f);
    CHECK(c->record[0x60] == 0 && c->record[0x61] == 1 && strcmp(said_text, "ages;") == 0);
    CHECK(cok_effects_dispatch(&fx, c, 0x12) && fx.rolls.rate == 24 && c->record[0x60] == 0);
    fx.say = NULL;
    /* 0x2b, strength drain, comes back each hour; above 3 it weakens, with a
     * message; at 3 or less it brings 0x1f; while curing nothing. */
    reset();
    c = member();
    add(c, 0x2b, 60, 7, true);
    CHECK(!cok_effects_remove(&fx, c, NULL, 0x2b) && strstr(fx.error, "3f44:1469") != NULL);
    c->record[0x11] = 3;
    CHECK(cok_effects_remove(&fx, c, NULL, 0x2b) && strcmp(ids(c), "2b1f") == 0);
    CHECK(c->effects->duration == 60 && c->effects->value == 7 && c->effects->on_remove);
    CHECK(cok_effects_remove(&fx, c, NULL, 0x2b) && strcmp(ids(c), "1f2b") == 0);
    fx.rolls.curing = 1;
    CHECK(cok_effects_remove(&fx, c, NULL, 0x2b) && strcmp(ids(c), "1f") == 0);
    fx.rolls.curing = 0;
    /* 0x39: +2 to hit and AC 2 better, each time. */
    add(c, 0x39, 0, 0, false);
    c->record[0x18d] = 50;
    fx.rolls.attack_roll = 5;
    CHECK(cok_effects_dispatch(&fx, c, 0x0a) && c->record[0x18d] == 52 && fx.rolls.attack_roll == 7);
}

/* 57e4:0171: effects lose the minutes that pass, ten at a time, and end. */
static void test_timers(void)
{
    reset();
    cok_character *c = member(), *d = member();
    add(c, 0x05, 0, 0, false);
    cok_effect *e = add(c, 0x13, 25, 0, false);
    add(d, 0x05, 3, 0, false);
    CHECK(cok_effects_pass_time(&fx, 1, 2) && e->duration == 23 && d->effects->duration == 1);
    CHECK(fx.timed[0] == 1 && fx.timed[1] == 1);
    /* Unit 2 is ten minutes; d's effect ends and d has none left. */
    CHECK(cok_effects_pass_time(&fx, 2, 1) && e->duration == 13 && d->effects == NULL);
    CHECK(fx.timed[1] == 0 && strcmp(ids(c), "0513") == 0);
    /* In camp nothing is counted while no member of the party's size
     * (0x7f3e) had time left. */
    vm.mode = 2;
    vm.mem7c00[0x33e] = 2;
    fx.timed[0] = 0;
    CHECK(cok_effects_pass_time(&fx, 2, 1) && e->duration == 13);
    /* The last member counts. */
    fx.timed[1] = 1;
    CHECK(cok_effects_pass_time(&fx, 1, 1) && e->duration == 13 && fx.timed[1] == 0);
    fx.timed[0] = 1;
    CHECK(cok_effects_pass_time(&fx, 1, 1) && e->duration == 12 && fx.timed[0] == 1);
    e->duration = 13;
    /* An hour: 13 minutes runs out in the second ten. */
    vm.mode = 4;
    CHECK(cok_effects_pass_time(&fx, 3, 1) && strcmp(ids(c), "05") == 0 && fx.timed[0] == 0);
    /* Unit 0 counts as minutes. */
    e = add(c, 0x13, 25, 0, false);
    CHECK(cok_effects_pass_time(&fx, 0, 4) && e->duration == 21);
    /* When the second effect ends after a first that is counted down, the
     * walk starts again from the first, which loses the time twice. */
    reset();
    c = member();
    cok_effect *first = add(c, 0x05, 30, 0, false);
    add(c, 0x13, 5, 0, false);
    cok_effect *third = add(c, 0x18, 40, 0, false);
    CHECK(cok_effects_pass_time(&fx, 2, 1) && strcmp(ids(c), "0518") == 0);
    CHECK(first->duration == 10 && third->duration == 30);
    /* When it was the last when the walk began, the walk has stopped. */
    reset();
    c = member();
    first = add(c, 0x05, 30, 0, false);
    add(c, 0x13, 5, 0, false);
    CHECK(cok_effects_pass_time(&fx, 2, 1) && strcmp(ids(c), "05") == 0 && first->duration == 20);
    /* Effects past the last one, added on the way, are not counted down
     * but keep the member counted: 0x6c adds 0x19 for 255 minutes. */
    reset();
    c = member();
    add(c, 0x6c, 5, 0, true);
    add(c, 0x13, 5, 0, false);
    CHECK(cok_effects_pass_time(&fx, 2, 1) && strcmp(ids(c), "19") == 0);
    CHECK(c->effects->duration == 255 && fx.timed[0] == 1);
    /* An ending effect's handler runs: death throes add 0x3b and 0x3c,
     * which are not counted down until the next time. */
    reset();
    c = member();
    add(c, 0x3a, 6, 0, true);
    CHECK(cok_effects_pass_time(&fx, 2, 1) && strcmp(ids(c), "3b3c") == 0);
    CHECK(c->effects->next->duration == 3);
    /* The explosion that 0x3c sets off when it ends is not ported. */
    CHECK(!cok_effects_pass_time(&fx, 2, 1) && strstr(fx.error, "3f44:18b9") != NULL);
    /* The stats an effect changed are restored when it ends. */
    reset();
    c = member();
    c->record[0x11] = 18;
    c->record[0x1c] = 75;
    add(c, 0x0c, 60, 76, false);
    CHECK(cok_effects_pass_time(&fx, 3, 1) && c->effects == NULL && c->record[0x11] == 12);
}

static void test_rolls(void)
{
    reset();
    cok_character *c = member();
    uint8_t *r = c->record;
    bool hit, made;
    /* A d20 of 10 with bonus 46 beats +0x18d, 55; with 45 it only meets it ... */
    vm.seed = seed_for(10);
    CHECK(cok_effects_attack(&fx, c, 46, &hit) && hit && fx.rolls.attack_roll == 10);
    vm.seed = seed_for(10);
    CHECK(cok_effects_attack(&fx, c, 45, &hit) && !hit);
    /* ... but not with invisibility's -4. */
    add(c, 0x19, 0, 0, false);
    vm.character = r;
    vm.seed = seed_for(10);
    CHECK(cok_effects_attack(&fx, c, 46, &hit) && !hit && fx.rolls.attack_roll == 6);
    /* A 20 counts as 100; displacement makes it -1, a miss. */
    add(c, 0x59, 0, 0, false);
    vm.seed = seed_for(20);
    CHECK(cok_effects_attack(&fx, c, 0, &hit) && !hit && fx.rolls.attack_roll == 0xff);
    /* A 1 misses before the effects run. */
    vm.seed = seed_for(1);
    CHECK(cok_effects_attack(&fx, c, 200, &hit) && !hit && fx.rolls.attack_roll == 1);

    /* Saving throw 2 is 15: a 10 with the +5 of constitution 19 makes it. */
    reset();
    c = member();
    r = c->record;
    r[0x19] = 19;
    vm.seed = seed_for(10);
    CHECK(cok_effects_save(&fx, c, 2, 0, &made) && !made && fx.rolls.save_roll == 10);
    add(c, 0x5e, 0, 0xff, false);
    vm.seed = seed_for(10);
    CHECK(cok_effects_save(&fx, c, 2, 0, &made) && made && fx.rolls.save_roll == 15);
    CHECK(fx.rolls.save_type == 2 && fx.rolls.save_made == 1);
    /* The bonus and +0x17c are signed; a 20 is made before the effects. */
    vm.seed = seed_for(10);
    r[0x17c] = 0xff;
    CHECK(cok_effects_save(&fx, c, 2, 0xff, &made) && !made && fx.rolls.save_roll == 13);
    vm.seed = seed_for(20);
    r[0x19] = 2; /* the uninitialized local is not reached */
    CHECK(cok_effects_save(&fx, c, 2, 0, &made) && made && fx.rolls.save_roll == 20);
    vm.seed = seed_for(10);
    CHECK(!cok_effects_save(&fx, c, 2, 0, &made) && strstr(fx.error, "uninitialized") != NULL);
}

/* The effects of the characters the original saved: racial ones, all
 * permanent. */
static void test_saved(void)
{
    reset();
    static const struct {
        const char *base, *ids;
    } saved[] = {
        {"CHRDATA2", "1269"}, {"CHRDATA4", "5c5e07"}, {"CHRDATB5", "5e1a2f"}, {"CHRDATB6", "5c5e"},
    };
    char error[300];
    cok_character *chars[4];
    for (size_t i = 0; i < 4; ++i) {
        chars[i] = calloc(1, sizeof *chars[i]);
        CHECK(chars[i] != NULL);
        if (!cok_character_read(chars[i], "SAVE", saved[i].base, &types, error, sizeof error)) {
            puts("effect: SAVE/ not present; saved characters not checked");
            free(chars[i]);
            for (size_t k = 0; k < i; ++k) {
                cok_character_free(chars[k]);
                free(chars[k]);
            }
            return;
        }
        CHECK(strcmp(ids(chars[i]), saved[i].ids) == 0);
        CHECK(cok_party_add(&party, chars[i]));
    }
    cok_character *molly = chars[1], *roark = chars[2];
    for (const cok_effect *e = molly->effects; e != NULL; e = e->next)
        CHECK(e->duration == 0 && e->value == 0xff && e->on_remove == (e->id == 0x07));
    /* A day passes and nothing ends. */
    CHECK(cok_effects_pass_time(&fx, 4, 1) && strcmp(ids(molly), "5c5e07") == 0);
    CHECK(fx.timed[0] == 0 && fx.timed[1] == 0);
    /* Roark, a dwarf of constitution 19, saves at +5 against spells. */
    CHECK(roark->record[0x19] == 19);
    bool made;
    vm.seed = seed_for(7);
    CHECK(cok_effects_save(&fx, roark, 4, 0, &made));
    CHECK(fx.rolls.save_roll == (uint8_t)(7 + (int8_t)roark->record[0x17c] + 5));
    /* Molly's 0x07 runs its handler when removed, which needs combat. */
    CHECK(!cok_effects_remove(&fx, molly, NULL, 0x07) && strstr(fx.error, "3f44:0208") != NULL);
}

/* 60f4:1743 for the other abilities: dexterity from items of power 2, 8
 * and 10; constitution with the maximum hit points; intelligence and
 * wisdom are not stored. */
static void test_more_abilities(void)
{
    reset();
    cok_character *c = member();
    uint8_t *r = c->record;
    c->items = calloc(3, COK_ITEM_SIZE);
    c->item_count = 3;
    for (size_t i = 0; i < 3; ++i) c->items[i][0x34] = 1;
    /* Power 2 adds 4 below dexterity 7, 2 up to 13 and 1 above. */
    c->items[0][0x3e] = 0x82;
    uint8_t dex[] = {6, 7, 13, 14};
    uint8_t want[] = {10, 9, 15, 15};
    for (size_t i = 0; i < 4; ++i) {
        r[0x16] = dex[i];
        CHECK(cok_effects_ability(&fx, c, 3) && r[0x17] == want[i]);
    }
    /* Power 8 of kind 3 adds 1 below 18, power 10 takes 2; power 0 and
     * unreadied items count for nothing. */
    c->items[1][0x3e] = 0x88;
    c->items[1][0x3d] = 3;
    c->items[2][0x3e] = 0x8a;
    r[0x16] = 17;
    CHECK(cok_effects_ability(&fx, c, 3) && r[0x17] == 17);
    c->items[2][0x34] = 0;
    CHECK(cok_effects_ability(&fx, c, 3) && r[0x17] == 19);
    c->items[0][0x3e] = 0x80;
    c->items[1][0x3e] = 0x80;
    CHECK(cok_effects_ability(&fx, c, 3) && r[0x17] == 17);
    /* Intelligence: nothing changes. */
    r[0x13] = 7;
    c->items[0][0x3e] = 0x8c;
    CHECK(cok_effects_ability(&fx, c, 1) && r[0x13] == 7);
    memset(c->items, 0, 3 * COK_ITEM_SIZE);
    /* Constitution: a level 3 fighter of constitution 17 with 30 of 40
     * hit points and 10 for its first level gets 3 a level above level 0,
     * 10 + 9 = 19; it loses 21, as many as it lacks, so 9 are left. A
     * ranger gets one more level; a mage 2 a level above 15. */
    r[0x18] = 17;
    r[0x62] = 40;
    r[0x197] = 30;
    r[0x11b] = 10;
    r[0xfb] = 3;
    CHECK(cok_effects_ability(&fx, c, 4) && r[0x19] == 17 && r[0x62] == 19 && r[0x197] == 9);
    r[0x197] = 30;
    r[0x62] = 19;
    r[0xfb] = 0;
    r[0xfd] = 3;
    CHECK(cok_effects_ability(&fx, c, 4) && r[0x62] == 22 && r[0x197] == 33);
    /* Two classes divide; a level at its class's top (DS:3903) or above
     * counts as one less, and constitution 15 gives 1 a level: the ranger
     * 4 (3, and one more), the mage 11 (20 capped at 12). */
    r[0xfe] = 20;
    r[0x18] = 15;
    CHECK(cok_effects_ability(&fx, c, 4) && r[0x62] == 10 + (4 + 11) / 2);
    /* 20 and up brings effect 0x3e; below, it goes, with its handler, which
     * is not ported. */
    r[0x18] = 20;
    CHECK(cok_effects_ability(&fx, c, 4) && strcmp(ids(c), "3e") == 0);
    CHECK(c->effects->duration == 0x3c && c->effects->value == 0xff && c->effects->on_remove);
    CHECK(cok_effects_ability(&fx, c, 4) && strcmp(ids(c), "3e") == 0);
    r[0x18] = 19;
    CHECK(!cok_effects_ability(&fx, c, 4) && strstr(fx.error, "3f44:1acd") != NULL);
    /* With no class level the original divides by zero. */
    memset(r + 0xf9, 0, 8);
    CHECK(!cok_effects_ability(&fx, c, 4) && strstr(fx.error, "200") != NULL);
}

/* 6346:29fe: the selected character's caster level by the spell's class. */
static void test_caster_level(void)
{
    reset();
    cok_character *c = member();
    uint8_t *r = c->record;
    uint8_t level = 0;
    CHECK(!cok_effects_caster_level(&fx, 1, &level) && strstr(fx.error, "6346:29fe") != NULL);
    vm.character = r;
    /* No cleric or mage level, a knight below 9 and a ranger below 8: 6. */
    r[0x100] = 8;
    r[0xfd] = 7;
    CHECK(cok_effects_caster_level(&fx, 1, &level) && level == 6);
    /* Cleric spells: the cleric level or the knight level - 8. */
    r[0xf9] = 3;
    r[0x100] = 12;
    CHECK(cok_effects_caster_level(&fx, 1, &level) && level == 4);
    CHECK(cok_effects_caster_level(&fx, 0x65, &level) && level == 4); /* class 2 */
    /* Druid spells: the ranger level - 7, at least 0. */
    CHECK(cok_effects_caster_level(&fx, 0x4d, &level) && level == 0);
    r[0xfd] = 9;
    CHECK(cok_effects_caster_level(&fx, 0x4d, &level) && level == 2);
    /* Magic-user spells: the mage level, one less under moon phase 0 of
     * its order, one more at 2 above level 5, or the ranger level - 8. */
    r[0xfe] = 6;
    r[0x5e] = 2;
    vm.mem4b00[0x1fa] = 0;
    CHECK(cok_effects_caster_level(&fx, 0x0f, &level) && level == 5);
    vm.mem4b00[0x1fa] = 2;
    CHECK(cok_effects_caster_level(&fx, 0x0f, &level) && level == 7);
    r[0xfe] = 5;
    CHECK(cok_effects_caster_level(&fx, 0x0f, &level) && level == 5);
    vm.mem4b00[0x1fa] = 1;
    r[0xfe] = 0;
    r[0xfd] = 10;
    CHECK(cok_effects_caster_level(&fx, 0x0f, &level) && level == 2);
    /* Items' powers (class 4): 12; any other spell of an item: 6. */
    CHECK(cok_effects_caster_level(&fx, 0x39, &level) && level == 12);
    fx.rolls.item = 1;
    CHECK(cok_effects_caster_level(&fx, 0x39, &level) && level == 12);
    CHECK(cok_effects_caster_level(&fx, 1, &level) && level == 6);
    fx.rolls.item = 0;
    /* A human's former cleric level counts while it may use it. */
    r[0x5a] = 6;
    r[0xf9] = 0;
    r[0x101] = 7;
    r[0xd7] = 3;
    r[0xfb] = 4;
    r[0x100] = 0;
    CHECK(cok_effects_caster_level(&fx, 1, &level) && level == 7);
    vm.character = NULL;
}

int main(void)
{
    char error[300];
    if (!cok_item_types_read("Assets/ITEMS", &types, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    test_list();
    test_abilities();
    test_more_abilities();
    test_caster_level();
    test_hammer();
    test_weapons();
    test_dispatch();
    test_removal();
    test_timers();
    test_rolls();
    test_saved();
    reset();
    cok_party_free(&party);
    cok_effects_free(&fx);
    puts("effect tests passed");
    return 0;
}
