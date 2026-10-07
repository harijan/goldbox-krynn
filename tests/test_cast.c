#include "camp.h"
#include "cast.h"
#include "magic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[16384];
} script;

static int scripted(void *context)
{
    script *s = context;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    if (strcmp(kind, "item") == 0 || strcmp(kind, "heading") == 0) return;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static script s;
static cok_adventure game;

static void keys(const char *k)
{
    s.keys = k;
    s.at = 0;
    s.length = strlen(k);
    s.log[0] = '\0';
    game.input_ended = false;
    game.vm.abort = false;
    game.vm.status = COK_ECL_OK;
}

static void empty_party(void)
{
    cok_party_free(&game.party);
    game.vm.character = NULL;
    game.spell_target = NULL;
    game.vm.mem7c00[0x33e] = 0;
}

/* A member who can act with hit points hp of max and a level in class. */
static cok_character *add(const char *name, uint8_t hp, uint8_t max, size_t class, uint8_t level)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    for (size_t i = 0; i < 6; ++i) c->record[0x10 + 2 * i] = c->record[0x11 + 2 * i] = 12;
    c->record[0x197] = hp;
    c->record[0x62] = max;
    c->record[0x189] = 1;
    c->record[0x5a] = 6;
    c->record[0xf9 + class] = level;
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    if (game.vm.character == NULL) game.vm.character = c->record;
    return c;
}

static uint8_t *give(cok_character *c, uint8_t type)
{
    c->items = realloc(c->items, (c->item_count + 1) * sizeof *c->items);
    CHECK(c->items != NULL);
    uint8_t *item = c->items[c->item_count++];
    memset(item, 0, COK_ITEM_SIZE);
    item[0x2e] = type;
    return item;
}

static size_t count_effects(const cok_character *c, uint8_t id)
{
    size_t n = 0;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next) n += e->id == id;
    return n;
}

/* The next d-sided roll the game would make, without making it. */
static unsigned peek(unsigned skip, uint16_t sides)
{
    uint32_t seed = game.vm.seed;
    for (unsigned i = 0; i < skip; ++i) cok_tp_random(&seed, 1);
    return cok_tp_random(&seed, sides) + 1u;
}

static void remove_effects(cok_character *c, uint8_t id)
{
    for (cok_effect **p = &c->effects; *p != NULL;) {
        if ((*p)->id != id) {
            p = &(*p)->next;
            continue;
        }
        cok_effect *e = *p;
        *p = e->next;
        free(e);
    }
}

static void cast_from(uint8_t spell, bool announce, uint8_t frame, bool *done)
{
    *done = false;
    cok_cast_spell(&game, spell, announce, frame, done);
}

static void cast(uint8_t spell, bool announce, bool *done)
{
    cast_from(spell, announce, COK_CAST_CAMP, done);
}

/* Cast from the camp's Magic menu: the list in memory, the targets, and
 * what is used up. */
static void test_magic_cast(void)
{
    empty_party();
    game.vm.mode = 2;
    cok_character *kal = add("KAL", 5, 9, 0, 3);
    kal->record[0x15] = 16;
    cok_character *sir = add("SIRRION", 2, 9, 2, 3);
    kal->record[0x1e] = 0x01; /* Bless */
    kal->record[0x1f] = 0x03; /* Cure Light Wounds */
    kal->record[0x20] = 0x83; /* marked, not memorized */
    /* The list starts on its last row, Cure Light Wounds; Enter casts it,
     * on whom down picks: Sirrion, who gains 1d8. */
    game.vm.seed = 7;
    unsigned d8 = peek(0, 8);
    keys("c\r" "\x01P" "S" "\x1b" "e");
    bool interrupted = false;
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "list: Spells in Memory;choice: 3;print: KAL;print: casts;"
                        "print: Cure Light Wounds;menu: Select Exit;menu: Select Exit;"
                        "print: SIRRION;") != NULL);
    CHECK(sir->record[0x197] == (d8 + 2 > 9 ? 9 : d8 + 2));
    CHECK(strstr(s.log, sir->record[0x197] == 9 ? "print: is fully healed;"
                                                : "print: is partially healed;") != NULL);
    /* The spell is used up; the marked one stays; the target is kept. */
    CHECK(kal->record[0x1f] == 0 && kal->record[0x20] == 0x83);
    CHECK(game.spell_target == sir->record);
    /* Bless, on the whole party on the caster's side. */
    sir->record[0x18a] = 1;
    keys("c\r\x1b" "e");
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "print: KAL;print: is Blessed;") != NULL);
    CHECK(strstr(s.log, "print: SIRRION;print: is Blessed") == NULL);
    CHECK(count_effects(kal, 1) == 1 && count_effects(sir, 1) == 0);
    CHECK(kal->effects->duration == 6 && kal->effects->value == 3);
    /* None left: "has no spells memorized", and Cast starts its targets
     * from the caster again (DS:710b). */
    keys("ce");
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "print: KAL;print: has no spells memorized;") != NULL);
    CHECK(game.spell_target == NULL);
    /* In no condition to: status 1. Nor where the area forbids it. */
    kal->record[0x1e] = 0x01;
    kal->record[0x188] = 1;
    keys("ce");
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "print: is in no condition to cast any spells;") != NULL);
    kal->record[0x188] = 0;
    game.vm.mem4b00[0xe5] = 1;
    keys("ce");
    cok_magic(&game, &interrupted);
    CHECK(strstr(s.log, "print: cannot cast spells in this area;") != NULL);
    game.vm.mem4b00[0xe5] = 0;
    /* A knight's Strength from camp's Magic adds the low byte of
     * 5b04:1415's BP there, 0x9a: the effect's value is 0x9a + 100. */
    empty_party();
    cok_character *knight = add("SIR", 9, 9, 7, 9);
    knight->record[0x10] = knight->record[0x11] = 3;
    knight->record[0x1e] = 0x23;
    keys("c\rS\x1b" "e");
    cok_magic(&game, &interrupted);
    CHECK(game.vm.status == COK_ECL_OK && strstr(s.log, "print: Strength;") != NULL);
    CHECK(count_effects(knight, 0x26) == 1 && cok_character_find_effect(knight, 0x26)->value == 0xfe);
}

/* 5b04:1415 outside combat: spells for combat only, miscasting, and the
 * caster's invisibility. */
static void test_cast_spell(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *kal = add("KAL", 5, 9, 5, 3);
    kal->record[0x13] = 16;
    kal->record[0x1e] = 0x0f; /* Magic Missile, combat only */
    bool done;
    keys("n");
    cast(0x0f, true, &done);
    CHECK(strcmp(s.log, "print: Magic Missile;print: can't be cast here...;menu: Lose it? ;"
                        "choice: N;") == 0);
    CHECK(!done && kal->record[0x1e] == 0x0f);
    keys("y");
    cast(0x0f, true, &done);
    CHECK(!done && kal->record[0x1e] == 0);
    /* Used from an item: "Use it?", Yes uses the item up for nothing. */
    game.effects.rolls.item = 1;
    keys("y");
    cast(0x0f, false, &done);
    CHECK(strcmp(s.log, "print: That Item;print: is a combat-only item...;menu: Use it? ;"
                        "choice: Y;") == 0);
    CHECK(done);
    game.effects.rolls.item = 0;
    /* Effect 0x4a miscasts on a d2 of 1, the spell kept; the d2 is also
     * rolled for a spell refused. */
    cok_character_add_effect(kal, 0x4a, 0, 0, false);
    kal->record[0x1e] = 0x13; /* Shield */
    for (int tries = 0; tries < 8; ++tries) {
        unsigned d2 = peek(0, 2);
        keys("");
        cast(0x13, true, &done);
        if (d2 == 1) {
            CHECK(strcmp(s.log, "print: KAL;print: miscasts;print: Shield;") == 0 && !done);
            CHECK(kal->record[0x1e] == 0x13);
        } else {
            CHECK(done && kal->record[0x1e] == 0 && count_effects(kal, 0x11) > 0);
            kal->record[0x1e] = 0x13;
        }
    }
    /* The d2 is rolled for a spell refused here too: after "Lose it?",
     * it can say it "miscasts". */
    kal->record[0x1e] = 0x0f;
    for (int tries = 0; tries < 8; ++tries) {
        unsigned d2 = peek(0, 2);
        uint32_t before = game.vm.seed;
        keys("n");
        cast(0x0f, true, &done);
        CHECK(game.vm.seed != before && kal->record[0x1e] == 0x0f);
        CHECK((strstr(s.log, "choice: N;print: KAL;print: miscasts;print: Magic Missile;") != NULL)
              == (d2 == 1));
    }
    cok_effects_remove(&game.effects, kal, NULL, 0x4a);
    /* Casting ends every invisibility of the caster; Invisibility's own is
     * permanent, so it stacks on others. */
    cok_character *sir = add("SIRRION", 9, 9, 2, 3);
    cok_character_add_effect(kal, 0x19, 0, 0, false);
    cok_character_add_effect(kal, 0x19, 0, 0, false);
    game.spell_target = sir->record;
    keys("S");
    cast(0x1e, false, &done);
    keys("S");
    cast(0x1e, false, &done);
    CHECK(count_effects(kal, 0x19) == 0 && count_effects(sir, 0x19) == 2);
    CHECK(sir->effects->duration == 0 && sir->effects->value == 3);
    /* Exit picks no one: nothing is used. */
    kal->record[0x1e] = 0x1e;
    keys("E");
    cast(0x1e, true, &done);
    CHECK(!done && kal->record[0x1e] == 0x1e && game.spell_target == NULL);
    /* Past the handler table. */
    keys("");
    cast(0x6c, true, &done);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "DS:6e3a") != NULL);
}

/* The handlers' quirks. */
static void test_handlers(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *kal = add("KAL", 5, 9, 0, 3);
    cok_character *sir = add("SIRRION", 0, 9, 2, 3);
    bool done;
    game.spell_target = sir->record;
    /* Slow Poison: a poisoned target, even dead, is okay with a hit point,
     * and gets 0x16 for 3780 minutes and 0x0f for 10. */
    sir->record[0x188] = 6;
    sir->record[0x189] = 0;
    cok_character_add_effect(sir, 0x37, 0, 0xff, false);
    keys("S");
    cast(0x1a, false, &done);
    CHECK(sir->record[0x188] == 0 && sir->record[0x189] == 1 && sir->record[0x197] == 1);
    CHECK(count_effects(sir, 0x16) == 1 && count_effects(sir, 0x0f) == 1);
    CHECK(sir->effects->next->duration == 0xec4 && sir->effects->next->next->duration == 10);
    /* Neutralize Poison would remove 0x16, whose handler (it kills, with
     * text) is not ported. */
    keys("S");
    cast(0x43, false, &done);
    CHECK(game.vm.status == COK_ECL_EFFECT_FAILED && strstr(game.error, "3f44:0725") != NULL);
    /* Without those, poison goes and the target is okay; without poison,
     * it "is unaffected", lower case. */
    while (sir->effects != NULL) {
        cok_effect *e = sir->effects;
        sir->effects = e->next;
        free(e);
    }
    cok_character_add_effect(sir, 0x37, 0, 0xff, false);
    sir->record[0x188] = 6;
    sir->record[0x197] = 0;
    keys("S");
    cast(0x43, false, &done);
    CHECK(sir->record[0x188] == 0 && sir->record[0x197] == 1 && sir->effects == NULL);
    CHECK(strstr(s.log, "print: is unpoisoned;") != NULL);
    keys("S");
    cast(0x43, false, &done);
    CHECK(strstr(s.log, "print: is unaffected;") != NULL);
    /* Status 1 drops the target, silently. */
    sir->record[0x188] = 1;
    cok_character_add_effect(sir, 0x37, 0, 0xff, false);
    keys("S");
    cast(0x43, false, &done);
    CHECK(done && strstr(s.log, "unpoisoned") == NULL && count_effects(sir, 0x37) == 1);
    sir->record[0x188] = 0;

    /* Strength: the roll for a mage, cleric or thief, or fighter; another
     * class (a knight) adds an uninitialized byte, the low byte of
     * 5b04:1415's BP: 0x74 from the commands, 0x9a from camp's Magic, and
     * 0 from an item. A knight of 3 with exceptional strength 7 gets past
     * 18 either way; one of base 18(50) and 0 from an item stays as it
     * is. */
    sir->record[0xfb] = 0;
    sir->record[0x100] = 7;
    sir->record[0x10] = sir->record[0x11] = 3;
    sir->record[0x1c] = sir->record[0x1d] = 7;
    keys("S");
    cast_from(0x23, false, COK_CAST_COMMANDS, &done);
    CHECK(game.vm.status == COK_ECL_OK && count_effects(sir, 0x26) == 1);
    CHECK(cok_character_find_effect(sir, 0x26)->value == 0xd8);
    remove_effects(sir, 0x26);
    keys("S");
    cast_from(0x23, false, COK_CAST_CAMP, &done);
    CHECK(game.vm.status == COK_ECL_OK && count_effects(sir, 0x26) == 1);
    CHECK(cok_character_find_effect(sir, 0x26)->value == 0xfe);
    remove_effects(sir, 0x26);
    /* From an item, 0: the current strength itself, better only than a
     * lower base. */
    sir->record[0x10] = 12;
    sir->record[0x11] = 12;
    sir->record[0x1c] = sir->record[0x1d] = 0;
    game.effects.rolls.item = 1;
    keys("S");
    cast_from(0x23, false, COK_CAST_COMMANDS, &done);
    CHECK(game.vm.status == COK_ECL_OK && count_effects(sir, 0x26) == 0);
    sir->record[0x10] = 11;
    sir->record[0x11] = 12;
    keys("S");
    cast_from(0x23, false, COK_CAST_COMMANDS, &done);
    CHECK(count_effects(sir, 0x26) == 1 && cok_character_find_effect(sir, 0x26)->value == 100);
    remove_effects(sir, 0x26);
    game.effects.rolls.item = 0;
    /* A fighter of 18: past 18 into exceptional strength, which must beat
     * the base's. */
    sir->record[0x100] = 0;
    sir->record[0xfb] = 3;
    sir->record[0x10] = sir->record[0x11] = 18;
    sir->record[0x1c] = sir->record[0x1d] = 0;
    game.vm.seed = 3;
    unsigned d8 = peek(0, 8);
    keys("S");
    cast(0x23, false, &done);
    CHECK(count_effects(sir, 0x26) == 1);
    CHECK(cok_character_find_effect(sir, 0x26)->value == (uint8_t)(d8 + 100));
    CHECK(sir->record[0x11] == 18 && sir->record[0x1c] == (d8 * 10 > 100 ? 100 : d8 * 10));
    /* 0x3b, an item's: strength 21, against the base, so 21 itself
     * leaves its value uninitialized. */
    sir->record[0x10] = 20;
    game.spell_target = kal->record;
    game.vm.character = sir->record;
    keys("");
    cast(0x3b, false, &done);
    CHECK(strstr(s.log, "print: is stronger;") != NULL && count_effects(sir, 0x71) == 1);
    CHECK(sir->record[0x11] == 21);
    /* A base of 21 still gets 0x71, silently: from an item with the
     * value 0x16, the high byte of 1415's return address. */
    sir->record[0x10] = 21;
    game.effects.rolls.item = 1;
    size_t printed = strlen(s.log);
    keys("");
    cast(0x3b, false, &done);
    CHECK(game.vm.status == COK_ECL_OK && count_effects(sir, 0x71) == 2);
    CHECK(strstr(s.log + printed, "is stronger") == NULL);
    CHECK(sir->effects != NULL);
    bool found = false;
    for (cok_effect *e = sir->effects; e != NULL; e = e->next)
        if (e->id == 0x71 && e->value == 0x16) found = true;
    CHECK(found);
    game.effects.rolls.item = 0;
    /* From memory, the overlay's segment: the port stops. */
    keys("");
    cast(0x3b, false, &done);
    CHECK(game.vm.status == COK_ECL_UNDEFINED && strstr(game.error, "5b04:3eb8") != NULL);
    game.vm.character = kal->record;

    /* Raise Dead does nothing, though the spell is used up. */
    sir->record[0x188] = 6;
    kal->record[0x1e] = 0x4b;
    game.spell_target = sir->record;
    keys("S");
    cast(0x4b, true, &done);
    CHECK(done && kal->record[0x1e] == 0 && sir->record[0x188] == 6);
    sir->record[0x188] = 0;

    /* Remove Curse: the first cursed item is unreadied but stays cursed;
     * with a power, its effect goes and the abilities are recomputed. */
    uint8_t *ring = give(sir, 0x09);
    ring[0x34] = 1;
    ring[0x36] = 1;
    ring[0x3d] = 0x5c;
    ring[0x3e] = 0x80;
    cok_character_add_effect(sir, 0x5c, 0, 0xff, false);
    sir->record[0x18] = 12;
    sir->record[0x11b] = 9;
    keys("S");
    cast(0x2b, false, &done);
    CHECK(ring[0x34] == 0 && ring[0x36] == 1 && count_effects(sir, 0x5c) == 0);
    CHECK(strstr(s.log, "print: has an item un-cursed;") != NULL && sir->record[0x19] == 12);

    /* Fire Shield: Hot is protected and says so, Cold says nothing; each
     * adds 0x70 too, for the mage level + 2 minutes. Escape asks to abort. */
    kal->record[0xfe] = 4;
    kal->record[0xf9] = 0;
    keys("c");
    cast(0x55, false, &done);
    CHECK(strcmp(s.log, "menu: Hot Cold;") == 0);
    CHECK(count_effects(kal, 0x36) == 1 && count_effects(kal, 0x70) == 1);
    keys("\x1byh");
    cast(0x55, false, &done);
    CHECK(strcmp(s.log, "menu: Hot Cold;menu: Yes No;") == 0 && count_effects(kal, 0x32) == 0);
    keys("\x1bnh");
    cast(0x55, false, &done);
    CHECK(strstr(s.log, "print: KAL;print: is protected;") != NULL && count_effects(kal, 0x32) == 1);
    for (cok_effect *e = kal->effects; e != NULL; e = e->next)
        if (e->id == 0x70) CHECK(e->duration == 6);

    /* Haste: the first time it runs, its effect ages each target a year. */
    kal->record[0x60] = 30;
    kal->record[0x61] = 0;
    keys("");
    cast(0x30, false, &done);
    CHECK(strstr(s.log, "print: KAL;print: is Hasted;print: SIRRION;print: is Hasted;"
                        "print: KAL;print: ages;print: SIRRION;print: ages;") != NULL);
    CHECK(kal->record[0x60] == 31);

    /* Mirror Image: 1d4 images in the high nibble, the level below. */
    game.vm.seed = 11;
    unsigned d4 = peek(0, 4);
    keys("");
    cast(0x20, false, &done);
    for (cok_effect *e = kal->effects; e != NULL; e = e->next)
        if (e->id == 0x1c) CHECK(e->value == (uint8_t)(d4 << 4 | 4) && e->duration == 8);

    /* Spiritual Hammer: a hammer at the end, not readied. */
    size_t before = kal->item_count;
    keys("");
    cast(0x1c, false, &done);
    CHECK(kal->item_count == before + 1 && kal->items[before][0x31] == 0x79);
    CHECK(kal->items[before][0x34] == 0 && strstr(s.log, "print: Gains an item;") != NULL);

    /* Burning Hands from an item burns its user for 6, from fire. Damage
     * runs event 6, where Mirror Image's effect (3f44:0a2f) takes the
     * damage on a d(images + 1) above 1, losing an image; and event 0x14,
     * where Fire Shield's hot flame (0x32) doubles fire damage while the
     * last saving throw (DS:6b44) was not made. */
    game.effects.rolls.item = 1;
    uint8_t hp = kal->record[0x197];
    keys("");
    cast(0x6b, false, &done);
    CHECK(game.vm.status == COK_ECL_OK && strcmp(s.log, "print: KAL;print: lost an image;") == 0);
    const cok_effect *images = cok_character_find_effect(kal, 0x1c);
    CHECK(images != NULL && (images->value & 0x0f) == 3 && kal->record[0x197] == hp);
    CHECK(cok_effects_remove(&game.effects, kal, NULL, 0x1c));
    kal->record[0x197] = 30;
    kal->record[0x62] = 30;
    keys("");
    cast(0x6b, false, &done);
    CHECK(strstr(s.log, "print: KAL;print: takes 12 points of damage from Fire;") != NULL);
    cok_character *tas = add("TAS", 9, 9, 6, 1);
    game.vm.character = tas->record;
    keys("");
    cast(0x6b, false, &done);
    CHECK(strcmp(s.log, "print: TAS;print: takes 6 points of damage from Fire;") == 0);
    CHECK(tas->record[0x197] == 3);
    /* Damage that drops it: "Goes Down", and dying 3 past its hit points. */
    keys("");
    cast(0x6b, false, &done);
    CHECK(strstr(s.log, "print: TAS;print: Goes Down, and is Dying;") != NULL);
    CHECK(tas->record[0x188] == 5);
    game.effects.rolls.item = 0;
    game.vm.character = kal->record;
}

/* Durations (5b04:0f78) and magic resistance. */
static void test_durations(void)
{
    empty_party();
    game.vm.mode = 4;
    cok_character *kal = add("KAL", 9, 9, 0, 5);
    uint16_t minutes;
    CHECK(cok_cast_duration(&game, 0x06, &minutes) && minutes == 3 * 5);
    CHECK(cok_cast_duration(&game, 0x1a, &minutes) && minutes == 0xec4);
    CHECK(cok_cast_duration(&game, 0x43, &minutes) && minutes == 0x5a0);
    game.vm.seed = 5;
    unsigned d10 = peek(0, 10);
    CHECK(cok_cast_duration(&game, 0x3f, &minutes) && minutes == (d10 + 10) * 10);
    /* Magic resistance: Bless on a member of resistance 1 at caster level
     * 12 (an item's power class 4 would be; here 0x69 by a cleric of 12)
     * wraps to 254, always resisted. */
    kal->record[0xf9] = 12;
    kal->record[0x187] = 1;
    game.effects.rolls.amount = 0;
    keys("");
    bool done = false;
    cok_cast_spell(&game, 0x69, false, COK_CAST_CAMP, &done);
    CHECK(strstr(s.log, "print: KAL;print: is Unaffected;") != NULL && kal->effects == NULL);
    /* Damage left over from before (DS:6b30) skips the check, as nothing
     * a blessing does clears it. */
    game.effects.rolls.amount = 6;
    keys("");
    cok_cast_spell(&game, 0x69, false, COK_CAST_CAMP, &done);
    CHECK(strstr(s.log, "print: KAL;print: is Blessed;") != NULL && count_effects(kal, 1) == 1);
}

int main(void)
{
    cok_keyboard k = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &k, &hooks));
    game.vm.mem4b00[0xe6] = 1;
    test_magic_cast();
    test_cast_spell();
    test_handlers();
    test_durations();
    cok_adventure_close(&game);
    puts("cast tests passed");
    return 0;
}
