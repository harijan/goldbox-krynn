#define _POSIX_C_SOURCE 200809L /* mkdtemp */

/* The Dark Queen of Krynn's levels (src/queen_levels.c) and the tables of
 * its data segment they read (src/data_dark_queen.c), against the original's
 * bytes in build/DQK_FULL.EXE (make dqk-merged) and the party of its shipped
 * saved game in DQKAssets/ (the checks needing either are skipped, with a
 * line saying so, where it is not there). */
#include "adventure.h"
#include "ai.h"
#include "arena.h"
#include "cast.h"
#include "combat.h"
#include "ecl.h"
#include "dark_queen.h"
#include "queen_view.h"
#include "data_dark_queen.h"
#include "field.h"
#include "hlib.h"
#include "party.h"
#include "queen.h"
#include "queen_levels.h"
#include "round.h"
#include "rules.h"
#include "rules_dark_queen_magic.h"
#include "test_temp.h"
#include "vga.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;

/* A spell's name (magic.c's cok_spell_name, not linked here). */
static const char *spell_name(unsigned id)
{
    return id >= 1 && id < R->spells.count ? R->spells.names[id - 1] : NULL;
}

static size_t read_whole(const char *path, uint8_t *out, size_t size)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return 0;
    size_t n = fread(out, 1, size, f);
    fclose(f);
    return n;
}

/* The tables byte for byte against the executable's data segment (DS
 * 0x422e: image paragraph 0x322e). */
static void test_tables(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); its tables not checked");
        return;
    }
    CHECK(exe[0] == 'M' && exe[1] == 'Z');
    size_t header = (size_t)(exe[8] | exe[9] << 8) * 16, ds = header + (size_t)0x322e * 16;
    const cok_data_segment *D = R->data;
    CHECK(D == &cok_dark_queen_data && D->table_count > 0);
    for (size_t i = 0; i < D->table_count; ++i) {
        const cok_ds_table *t = &D->tables[i];
        CHECK(t->stride == 1 && ds + t->offset + t->size <= size);
        CHECK(memcmp(exe + ds + t->offset, t->bytes, t->size) == 0);
    }
}

/* The experience a level needs (8bcc:0e9c): the table's by level up to
 * 17, past it so much a level more, nothing past 0x28; a row for each
 * class by the deity, the order, the robes. */
static void test_experience(void)
{
    uint8_t c[COK_CHARACTER_MAX] = {0};
    char error[200];
    int32_t need;
    /* A fighter (row 2): 2,001 for the second level, 1,000,001 for the 12th. */
    CHECK(cok_queen_experience(R, c, 2, 2, &need, error, sizeof error) && need == 2001);
    CHECK(cok_queen_experience(R, c, 12, 2, &need, error, sizeof error) && need == 1000001);
    CHECK(cok_queen_experience(R, c, 17, 2, &need, error, sizeof error) && need == 2250001);
    CHECK(cok_queen_experience(R, c, 20, 2, &need, error, sizeof error) &&
          need == 2250001 + 3 * 250000);
    CHECK(cok_queen_experience(R, c, 0x29, 2, &need, error, sizeof error) && need == 0x7fffffff);
    /* A cleric of Paladine (deity 1) row 0, of Sirrion (5) row 1. */
    c[0x19b] = 1;
    CHECK(cok_queen_experience_row(R, c, 0) == 0);
    c[0x19b] = 5;
    CHECK(cok_queen_experience_row(R, c, 0) == 1);
    /* A knight's by the order; none has no row. */
    CHECK(cok_queen_experience_row(R, c, 1) == -1);
    CHECK(!cok_queen_experience(R, c, 3, 1, &need, error, sizeof error) && error[0] != '\0');
    for (int order = 1; order <= 3; ++order) {
        c[0x19a] = (uint8_t)order;
        CHECK(cok_queen_experience_row(R, c, 1) == order + 2);
    }
    CHECK(cok_queen_experience_row(R, c, 3) == 10 && cok_queen_experience_row(R, c, 4) == 6 &&
          cok_queen_experience_row(R, c, 6) == 9 && cok_queen_experience_row(R, c, 7) == -1);
    c[0x19d] = 1;
    CHECK(cok_queen_experience_row(R, c, 5) == 7);
    c[0x19d] = 2;
    CHECK(cok_queen_experience_row(R, c, 5) == 8);
}

/* The six characters of the shipped saved game, which the original had
 * recomputed (stats and levels) as it made them, recomputed again
 * (8bcc:07f1, 177e:0be2): every byte of each record the same but its
 * pointers. The hundred more cases of the original's own recompute
 * (the party of dqk_add's, a character trained) are the checks in
 * tools/dosbox/checks/. */
static void test_party_again(void)
{
    static uint8_t data[8192];
    static cok_item_types types;
    char error[300];
    if (read_whole("DQKAssets/SAVE/SAVGAMA.QSV", data, sizeof data) == 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/SAVE/SAVGAMA.QSV or ITEMS.DAT; its party's levels not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    size_t at = 0x40f, count = (size_t)(data[0x40d] | data[0x40e] << 8);
    CHECK(count == 6);
    for (size_t i = 0; i < count; ++i) {
        cok_character c;
        size_t used;
        CHECK(cok_character_from_bytes(&c, R, data + at, sizeof data - at, 0, &used, error,
                                       sizeof error));
        uint8_t before[COK_CHARACTER_MAX];
        memcpy(before, c.record, L->size);
        CHECK(cok_character_stats(&c, R, &types, error, sizeof error));
        CHECK(cok_character_levels(&c, R, &types, error, sizeof error));
        CHECK(cok_character_stats(&c, R, &types, error, sizeof error));
        for (size_t k = 0; k < L->size; ++k)
            if (!cok_field_pointer_at(L, (long)k) && before[k] != c.record[k])
                fprintf(stderr, "character %zu +0x%03zx: %02x, recomputed %02x\n", i, k,
                        before[k], c.record[k]);
        for (size_t k = 0; k < L->size; ++k)
            CHECK(cok_field_pointer_at(L, (long)k) || before[k] == c.record[k]);
        at += used;
        cok_character_free(&c);
    }
}

static bool have_assets(void)
{
    FILE *f = fopen("DQKAssets/DISK3/ECL.GLB", "rb");
    if (f == NULL) return false;
    fclose(f);
    return true;
}

static int no_keys(void *context)
{
    (void)context;
    return -1;
}

/* A Dark Queen character as its .QCH holds it (test_adventure's dqk_member). */
static cok_character *dqk_member(const cok_adventure *game, const char *base)
{
    char error[300];
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    CHECK(cok_character_read_file(c, R, "DQKAssets/SAVE", base, "QCH", &game->item_types, error,
                                  sizeof error));
    return c;
}

/* A record of the shared game as a combatant at x, y (test_round's record(), for Dark Queen). */
static void place(cok_adventure *game, cok_character *c, int8_t x, int8_t y)
{
    cok_combat *k = &game->combat;
    cok_combatant *e = &k->combatant[k->count];
    e->x = x;
    e->y = y;
    e->size = 1;
    e->character = c;
    ++k->count;
    k->combatant[k->count].size = 0;
    k->combatant[k->count].character = NULL;
    CHECK(cok_combat_occupy(k, R));
}

/* The round's start (dqk 5045:059d's loop, 52db:0004) in Dark Queen's game. CINDER (strength 12,
 * base movement 12) carries 640, over her allowance of 100 by more than 0x200: the load caps her
 * movement at 9 (177e:038c, 6346:03e6's), which her round's start doubles to 0x12. Var 0x3c (the
 * gate) takes a third of that unless effect 0x96 is on her; a cloud under her stops her items and
 * her casting (52db:0004's +1 and +2); with var 0x3c set, her ammunition is none (51c2:10cd). */
static void test_round_start_queen(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the round's start not checked");
        return;
    }
    /* Dark Queen's descriptor has no fight block yet, so its battlefield's cloud tile is 0 (the
     * probe's cloud is inert): this copy gives it Champions' 0x1e (6beb:0c9d). */
    static cok_rules rules;
    rules = *R;
    rules.fight.cloud_tile = 0x1e;
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    CHECK(cok_adventure_open(&game, &rules, "DQKAssets", &keys, &hooks));
    const cok_character_layout *L = R->character;
    cok_character *c = dqk_member(&game, "'CINDER'");
    CHECK(cok_party_add(&game.party, R, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    uint8_t *r = c->record;
    cok_field_set8_at(L, r, COK_FIELD_SCORE, COK_SCORE_STRENGTH, 12);
    cok_field_set8(L, r, COK_FIELD_STRENGTH_EXCEPTIONAL, 0);
    cok_field_set8(L, r, COK_FIELD_BASE_MOVEMENT, 12);
    cok_field_set16(L, r, COK_FIELD_WEIGHT, 640);
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_combat *k = &game.combat;
    memset(k->cells, COK_COMBAT_FLOOR, sizeof k->cells);
    k->count = 1; /* combatant 0 is no one's (test_round's reset) */
    place(&game, c, 5, 5);
    cok_ecl *vm = &game.vm;
    vm->seed = 1;
    CHECK(cok_combat_round_start(&game, c));
    CHECK(cok_field_u8(L, r, COK_FIELD_MOVEMENT) == 9 && c->combat->movement == 0x12);
    CHECK(c->combat->may_use == 1);
    /* Var 0x3c takes a third of the doubled movement (6); effect 0x96 leaves it whole (and no
     * effect is removed here: remove_effect asserts for an id that is no named effect). */
    cok_ecl_set_var(vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(cok_combat_round_start(&game, c) && c->combat->movement == 6);
    CHECK(cok_character_add_effect(c, 0x96, 0, 0x10, false) != NULL);
    CHECK(cok_combat_round_start(&game, c) && c->combat->movement == 0x12);
    /* A cloud under her: no items and no casting. */
    cok_ecl_set_var(vm, COK_VAR_RECORD_EFFECT, 0);
    k->cells[5][5] = (uint8_t)rules.fight.cloud_tile;
    CHECK(cok_combat_round_start(&game, c));
    CHECK(c->combat->may_use == 0 && c->combat->may_cast == 0);
    k->cells[5][5] = COK_COMBAT_FLOOR;
    CHECK(cok_combat_round_start(&game, c) && c->combat->may_use == 1);
    /* Ammunition: a weapon's type (the item's type at the rules' record offset) with flags 0x0a
     * or 0x0c has some; with var 0x3c set it has none. */
    CHECK(game.item_types.record_type_at == R->item_record.type);
    uint8_t item[COK_ITEM_SIZE] = {0};
    item[R->item_record.type] = 5;
    size_t at = c->item_count;
    CHECK(cok_character_insert_item(c, at, item));
    c->slots[0] = at + 1;
    size_t ammo = 9;
    bool has = true;
    game.item_types.type[5][14] = 0x0c;
    CHECK(cok_combat_ammunition(&game, c, &ammo, &has) && has && ammo == 0);
    game.item_types.type[5][14] = 0x0a;
    CHECK(cok_combat_ammunition(&game, c, &ammo, &has) && has && ammo == 0);
    game.item_types.type[5][14] = 0x04;
    CHECK(cok_combat_ammunition(&game, c, &ammo, &has) && !has && ammo == 0);
    game.item_types.type[5][14] = 0x0c;
    cok_ecl_set_var(vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(cok_combat_ammunition(&game, c, &ammo, &has) && !has && ammo == 0);
    cok_ecl_set_var(vm, COK_VAR_RECORD_EFFECT, 0);
    /* Effect 0xb2 (event 0x12's list) runs its handler at the round's start, not ported (dqk
     * 7e76:0dc6): the round's start stops there. */
    CHECK(cok_character_add_effect(c, 0xb2, 0, 0x10, false) != NULL);
    CHECK(!cok_combat_round_start(&game, c) && strstr(game.error, "0dc6") != NULL);
    cok_adventure_close(&game);
}

/* The first turn's dispatch (dqk 5045:078b) in Dark Queen's game: a party member that can act
 * (+0x18b) stops at the command menu (5045:08cf), one with a spell pending at its cast
 * (849a:145b), a computer's turn at 5bb8:0000, and a member that cannot act is ended at once
 * (08cf's 51c2:0c11: initiative, spell, guard and movement zeroed). --combat's stubs do not pass
 * over those stops. The round's end, the turn's tail and the battle's end are cited stops too. */
static void test_turn_dispatch(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the turn's dispatch not checked");
        return;
    }
    static cok_rules rules;
    rules = *R;
    rules.fight.cloud_tile = 0x1e;
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    CHECK(cok_adventure_open(&game, &rules, "DQKAssets", &keys, &hooks));
    const cok_character_layout *L = R->character;
    cok_character *c = dqk_member(&game, "'CINDER'");
    CHECK(cok_party_add(&game.party, R, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_combat *k = &game.combat;
    memset(k->cells, COK_COMBAT_FLOOR, sizeof k->cells);
    k->count = 1;
    place(&game, c, 5, 5);
    game.vm.seed = 1;
    CHECK(cok_combat_round_start(&game, c));
    c->combat->initiative = 5;
    /* A party member that can act reads the command menu (5045:08cf): with no keys the run ends
     * there, its turn not ended and its initiative kept. */
    CHECK(cok_combat_act(&game, c, false) && game.vm.status == COK_ECL_OK);
    CHECK(c->combat->initiative == 5);
    /* With a spell pending (the combat block's spell), the stop is the cast (849a:145b). */
    game.vm.status = COK_ECL_OK;
    c->combat->spell = 3;
    CHECK(!cok_combat_act(&game, c, false));
    CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "849a:145b") != NULL);
    CHECK(strstr(game.error, "5045:08cf") == NULL && c->combat->initiative == 5);
    c->combat->spell = 0;
    /* One that cannot act (+0x18b 0): ended at once, its initiative, spell, guard and movement
     * zero (51c2:0c11), with a spell and movement set beforehand. */
    game.vm.status = COK_ECL_OK;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
    c->combat->spell = 3;
    c->combat->guarding = 1;
    c->combat->movement = 2;
    CHECK(cok_combat_act(&game, c, false) && game.vm.status == COK_ECL_OK);
    CHECK(c->combat->initiative == 0 && c->combat->spell == 0);
    CHECK(c->combat->guarding == 0 && c->combat->movement == 0);
    /* A computer's turn stops at 5bb8:0000. */
    game.vm.status = COK_ECL_OK;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    c->combat->initiative = 5;
    CHECK(!cok_combat_act(&game, c, true));
    CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "5bb8:0000") != NULL);
    /* --combat's stubs do not pass over the stops: the party's menu (no keys: kept), the
     * computer's turn. */
    static const cok_combat_stub stubs[] = {COK_COMBAT_PASS, COK_COMBAT_MELEE};
    for (size_t i = 0; i < sizeof stubs / sizeof stubs[0]; ++i) {
        game.combat_stub = stubs[i];
        game.vm.status = COK_ECL_OK;
        CHECK(cok_combat_act(&game, c, false) && game.vm.status == COK_ECL_OK);
        CHECK(c->combat->initiative == 5);
        game.vm.status = COK_ECL_OK;
        CHECK(!cok_combat_act(&game, c, true));
        CHECK(game.vm.status == COK_ECL_UNPORTED && strstr(game.error, "5bb8:0000") != NULL);
    }
    game.combat_stub = COK_COMBAT_UNPORTED;
    /* The keys ran out at the menus above: the run is over, and the tail, the round's end and
     * the battle's end are checked on their own (test_round_end). */
    cok_adventure_close(&game);
}

/* The scripted keys and the log of test_round_end. */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[4096];
} end_script;
static end_script es;

static int end_read(void *context)
{
    end_script *e = context;
    if (e->at == e->length) return -1;
    return (unsigned char)e->keys[e->at++];
}

static void end_log(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    end_script *e = context;
    size_t used = strlen(e->log);
    snprintf(e->log + used, sizeof e->log - used, "%s: %s;", kind, text);
}

#define END_LOGGED(text) (strstr(es.log, text) != NULL)

/* The turn's tail (5045:059d's: the pending lists, damage type 0, the demonstration's key test,
 * the colour cycles), the round's end (5045:0f6c: the clouds, the teammate's text, the Continue
 * question) and the battle's end (5045:0001: effect 0xb0, the charm test, the icons), on the
 * first turn's one party member (CINDER). Each stop is one the port cites; the rest runs. */
/* 1396:019f's walk past a removal (dark_queen.c's pass_time): the chain is P (0x13, 5 minutes,
 * kept), E (0x72, one minute, flag 1: the handler is death throes, COK_FX_DEATH_THROES, which on
 * its removal removes the effects its names give, then adds 0x70 and 0x71), N (0x60, 5 minutes)
 * and Z (0x05, 3 minutes). The test's names make 0x72's removal take BOLTS as N's id (case
 * "next") or as P's (case "kept"). The walk must not follow N once the removal freed it; after
 * "next" it goes on from P to Z, after "kept" it stops, cited. */
static void timer_walk(bool kept_removed)
{
    static cok_rules rules;
    static uint8_t ids[COK_EFFECT_NAMES];
    static cok_rules_effect handlers[0xe6];
    rules = *R;
    memcpy(ids, cok_dark_queen_effect_ids, sizeof ids);
    ids[COK_EFFECT_BOLTS] = kept_removed ? 0x13 : 0x60;
    ids[COK_EFFECT_MAD] = 0x6e;
    ids[COK_EFFECT_IMMOLATE] = 0x6d;
    ids[COK_EFFECT_PLUS_TWO] = 0x6c;
    ids[COK_EFFECT_IMMUNE] = 0x70;
    ids[COK_EFFECT_BURST] = 0x71;
    memcpy(handlers, R->effects.handlers, sizeof handlers);
    handlers[0x72].handler = COK_FX_DEATH_THROES;
    handlers[0x72].unported = NULL;
    rules.effects.ids = ids;
    rules.effects.handlers = handlers;
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    CHECK(cok_adventure_open(&game, &rules, "DQKAssets", &keys, &hooks));
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_add(&game.party, &rules, c));
    cok_character_add_effect(c, 0x13, 5, 0, false);
    cok_character_add_effect(c, 0x72, 1, 0, true);
    cok_character_add_effect(c, 0x60, 5, 0, false);
    cok_character_add_effect(c, 0x05, 3, 0, false);
    game.vm.mode = 4;
    bool ok = cok_dark_queen_round_clock(&game);
    if (kept_removed) {
        /* The kept node is gone: the walk stops, cited (1396:019f's kept next, 031c). */
        CHECK(!ok && game.vm.status == COK_ECL_UNPORTED);
        CHECK(strstr(game.error, "dqk 1396:019f") != NULL);
    } else {
        /* N is removed by the handler; the walk goes on from P to Z (3 minutes less one) and
         * on to the 0x70 and 0x71 death throes added: 0x71 counts down too. */
        CHECK(ok && game.vm.status == COK_ECL_OK);
        const cok_effect *p = c->effects;
        CHECK(p != NULL && p->id == 0x13 && p->duration == 4);
        CHECK(p->next != NULL && p->next->id == 0x05 && p->next->duration == 2);
        CHECK(p->next->next != NULL && p->next->next->id == 0x70 && p->next->next->duration == 0);
        CHECK(p->next->next->next != NULL && p->next->next->next->id == 0x71 &&
              p->next->next->next->duration == 2);
        CHECK(p->next->next->next->next == NULL);
    }
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    cok_adventure_close(&game);
}

static void test_timer_walk(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the timers' walk not checked");
        return;
    }
    timer_walk(false);
    timer_walk(true);
}

static void test_round_end(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the round's end not checked");
        return;
    }
    static cok_rules rules;
    rules = *R;
    static cok_adventure game;
    cok_keyboard keys = {end_read, &es, NULL, NULL};
    cok_adventure_hooks hooks = {.log = end_log, .context = &es};
    CHECK(cok_adventure_open(&game, &rules, "DQKAssets", &keys, &hooks));
    const cok_character_layout *L = R->character;
    cok_character *c = dqk_member(&game, "'CINDER'");
    CHECK(cok_party_add(&game.party, R, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_combat *k = &game.combat;
    memset(k->cells, COK_COMBAT_FLOOR, sizeof k->cells);
    k->count = 1;
    place(&game, c, 5, 5);
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;

    /* The tail: a pending list stops it (cited, all three names), else the damage type is 0. */
    k->pending[1] = 1;
    game.effects.rolls.damage_type = 8;
    CHECK(!cok_combat_turn_tail(&game) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "148f:2ab8") != NULL && strstr(game.error, "2d9b") != NULL);
    k->pending[1] = 0;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false; /* a stop ends the run */
    CHECK(cok_combat_turn_tail(&game) && game.vm.status == COK_ECL_OK);
    CHECK(game.effects.rolls.damage_type == 0);
    /* The demonstration's key test is a cited stop. */
    game.demo = true;
    CHECK(!cok_combat_turn_tail(&game) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "5045:06cf-070e") != NULL);
    game.demo = false;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;

    /* The round's end: a cloud stops it (148f:2817), before the count of rounds. */
    cok_combat_cloud *cloud = calloc(1, sizeof *cloud);
    CHECK(cloud != NULL);
    k->clouds = cloud;
    bool done = false;
    CHECK(!cok_combat_end_round(&game, &done) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "148f:2817") != NULL && !done);
    k->clouds = NULL;
    free(cloud);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;

    /* The square test (148f:12ab, probe 8's footprint: cok_combat_marks): a cloud kind 0x1b or
     * 0x1c stops it, cited; 0x1d is skipped at the round's end (12f0-12fc); acid (0x46) stops
     * it, also where a later cell of the footprint is floor (the terrain pick would drop it). */
    static const struct {
        uint8_t tile;
        bool stops;
    } squares[] = {{0x1b, true}, {0x1c, true}, {0x1d, false}, {0x46, true}};
    for (size_t i = 0; i < sizeof squares / sizeof squares[0]; ++i) {
        k->cells[5][5] = squares[i].tile;
        game.error[0] = '\0';
        bool done2 = false;
        bool ok = cok_combat_end_round(&game, &done2);
        bool stopped = !ok && game.vm.status == COK_ECL_UNPORTED &&
                       strstr(game.error, "148f:12ab") != NULL;
        CHECK(stopped == squares[i].stops);
        game.vm.status = COK_ECL_OK;
        game.vm.abort = false;
    }
    k->cells[5][5] = COK_COMBAT_FLOOR;
    /* Size 2: acid in the first cell of the footprint, floor in the rest: the walk still stops. */
    k->combatant[1].size = 2;
    CHECK(cok_combat_occupy(k, R));
    int8_t dx0 = 0, dy0 = 0;
    bool first = false;
    for (unsigned j = 0; j < 8 && !first; ++j) {
        int8_t dx, dy;
        if (!cok_combat_footprint(R, 2, j, &dx, &dy)) continue;
        if (!first) {
            dx0 = dx;
            dy0 = dy;
            first = true;
        }
        k->cells[5 + dy][5 + dx] = COK_COMBAT_FLOOR;
    }
    CHECK(first);
    k->cells[5 + dy0][5 + dx0] = 0x46;
    game.error[0] = '\0';
    bool done3 = false;
    CHECK(!cok_combat_end_round(&game, &done3) && game.vm.status == COK_ECL_UNPORTED &&
          strstr(game.error, "148f:12ab") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    k->combatant[1].size = 1;
    CHECK(cok_combat_occupy(k, R));
    k->cells[5 + dy0][5 + dx0] = COK_COMBAT_FLOOR;
    k->cells[5][5] = COK_COMBAT_FLOOR;

    /* The round's end runs the centring (the arena's map), which this fixture does not build:
     * its two texts are checked through their hooks. "Your Teammate is Dying" is in the bar;
     * the Continue question keeps the battle on Yes and not on no keys (an abort). */
    const cok_drawing *D = cok_adventure_drawing(&game);
    CHECK(D != NULL && D->teammate_dying != NULL && D->continue_battle != NULL);
    D->teammate_dying(&game);
    CHECK(END_LOGGED("message: Your Teammate is Dying;"));
    es.keys = "";
    es.length = 0;
    es.at = 0;
    CHECK(!D->continue_battle(&game)); /* the keys ran out: no answer */
    game.vm.abort = game.input_ended = false;
    es.keys = "Y";
    es.length = 1;
    es.at = 0;
    CHECK(D->continue_battle(&game)); /* Yes */
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;

    /* The battle's end: effect 0xb0 stops it (148f:017d), before the charm test. */
    es.keys = "";
    es.length = 0;
    es.at = 0;
    CHECK(cok_character_add_effect(c, 0xb0, 0, 0x10, false) != NULL);
    CHECK(!cok_combat_end(&game) && game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(game.error, "148f:017d") != NULL);
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    CHECK(cok_effects_remove(&game.effects, c, NULL, 0xb0));
    /* The charm test: charmed (0x0b) with status 0 is okay with the enemy's side standing (3),
     * else 0 (the enemy's side is gone here). */
    CHECK(cok_character_add_effect(c, 0x0b, 0, 1, false) != NULL);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    k->sides[1] = 2;
    CHECK(cok_combat_end(&game) && game.vm.status == COK_ECL_OK && !game.combat_targets);
    CHECK(cok_field_u8(L, r, COK_FIELD_STATUS) == 3);
    /* Dark Queen's status is an int (cok_combat_field_word): 0x0100 is not 0, so the charm test
     * leaves it (5045:0001's cmpw $0, +0x64), and the word is read and kept. */
    CHECK(cok_character_add_effect(c, 0x0b, 0, 1, false) != NULL);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    r[cok_field_offset(L, COK_FIELD_STATUS) + 1] = 1;
    CHECK(cok_combat_field_word(R, r, COK_FIELD_STATUS) == 0x100);
    CHECK(cok_combat_end(&game) && game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_field_word(R, r, COK_FIELD_STATUS) == 0x100);
    cok_adventure_close(&game);
}

/* A copy of path as dir/name with the byte at offset made value (0 of
 * offset: unchanged). */
static void copy_patched(const char *path, const char *dir, const char *name, size_t offset,
                         uint8_t value)
{
    static uint8_t bytes[2048];
    size_t n = read_whole(path, bytes, sizeof bytes);
    CHECK(n > 0);
    if (offset != 0) bytes[offset] = value;
    char to[1300];
    snprintf(to, sizeof to, "%s/%s", dir, name);
    FILE *f = fopen(to, "wb");
    CHECK(f != NULL && fwrite(bytes, 1, n, f) == n);
    fclose(f);
}

static uint32_t hash_character(uint32_t hash, const cok_character *c)
{
    const cok_character_layout *L = R->character;
    /* But its pointers and its party slot (+0xc5: 604b:3491 sets it as it joins). */
    for (size_t k = 0; k < L->size; ++k)
        hash = (hash ^ (cok_field_pointer_at(L, (long)k) || k == 0xc5 ? 0 : c->record[k])) *
               0x01000193u;
    for (size_t n = 0; n < c->item_count; ++n)
        for (size_t k = 0; k < 17; ++k) hash = (hash ^ c->items[n][0x28 + k]) * 0x01000193u;
    for (const cok_effect *e = c->effects; e != NULL; e = e->next) {
        const uint8_t b[5] = {e->id, (uint8_t)e->duration, (uint8_t)(e->duration >> 8), e->value,
                              e->on_remove};
        for (size_t k = 0; k < 5; ++k) hash = (hash ^ b[k]) * 0x01000193u;
    }
    return hash;
}

/* A character of a roster, added: its file in from (DQKAssets/SAVE's .QCH,
 * or DKKAssets/SAVE's CHRDATA*.GAM imported), a byte of it patched as the
 * check's files were (offset 0: none). */
typedef struct {
    const char *base, *from;
    bool import;
    size_t offset;
    uint8_t value;
} added;

/* Read (or import) one, with the siblings an import reads, patched in dir
 * where it is; its stats ran as it was read, then its levels (604b:2021). */
static void add(const added *w, const char *dir, const cok_item_types *types, cok_character *c)
{
    char error[300], from[700], to[700];
    const char *extension = w->import ? "GAM" : "QCH", *where = w->from;
    if (w->offset != 0) {
        static const char *const files[] = {"GAM", "QCH", "WIZ", "JNK", "EFF"};
        for (size_t i = 0; i < sizeof files / sizeof *files; ++i) {
            snprintf(from, sizeof from, "%s/%s.%s", w->from, w->base, files[i]);
            if (read_whole(from, (uint8_t[1]){0}, 1) == 0) continue;
            snprintf(to, sizeof to, "%s.%s", w->base, files[i]);
            bool main = strcmp(files[i], extension) == 0;
            copy_patched(from, dir, to, main ? w->offset : 0, main ? w->value : 0);
        }
        where = dir;
    }
    bool ok = w->import ? cok_character_import(c, R, where, w->base, extension, types, error,
                                               sizeof error)
                        : cok_character_read_file(c, R, where, w->base, extension, types, error,
                                                  sizeof error);
    if (!ok) fprintf(stderr, "%s: %s\n", w->base, error);
    CHECK(ok);
    CHECK(cok_character_levels(c, R, types, error, sizeof error));
}

/* Characters added from the camp (604b:2021: read, stats, levels) as the
 * original holds them (DOSBox-X, 2026-10-29, tools/dosbox/checks/dqk_add.py:
 * the parties it saved), hashed as test_party.c's: Roland of Dust and
 * Walker Keys from their .QCH, then Death Knights' Celeste, Flavius,
 * Lillith and Neacail imported; and a second party with a ranger, a
 * knight of the Sword, a paladin of the twelfth level. */
static void test_added(void)
{
    static cok_item_types types;
    char error[300], dir[512];
    if (read_whole("DQKAssets/SAVE/ROLANDOF.QCH", (uint8_t[1]){0}, 1) == 0 ||
        read_whole("DKKAssets/SAVE/CHRDATA6.GAM", (uint8_t[1]){0}, 1) == 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/ and DKKAssets/ saved characters; added ones not checked");
        return;
    }
    CHECK(test_temp_dir(dir, sizeof dir, "cok_dqk_added") != NULL);
    static const char *const dqk = "DQKAssets/SAVE", *const dkk = "DKKAssets/SAVE";
    const added parties[2][6] = {
        {{"ROLANDOF", dqk, false, 0, 0}, {"WALKERKE", dqk, false, 0, 0},
         {"CHRDATA6", dkk, true, 0, 0}, {"CHRDATA4", dkk, true, 0, 0},
         {"CHRDATA2", dkk, true, 0, 0}, {"CHRDATA5", dkk, true, 0, 0}},
        {{"'CINDER'", dqk, false, 0, 0}, {"BALTHIRN", dqk, false, 0, 0},
         {"ROARK'CR", dqk, false, 0, 0}, {"ROLANDOF", dqk, false, 0x19a, 2},
         {"CHRDATA6", dkk, true, 0, 0}, {"CHRDATA2", dkk, true, 0x48, 12}},
    };
    static const uint32_t expected[2] = {0x14d4d611u, 0x3f849c41u};
    for (int run = 0; run < 2; ++run) {
        uint32_t hash = 0x811c9dc5u;
        for (size_t i = 0; i < 6; ++i) {
            cok_character c;
            add(&parties[run][i], dir, &types, &c);
            hash = hash_character(hash, &c);
            cok_character_free(&c);
        }
        if (hash != expected[run]) fprintf(stderr, "Dark Queen's added party %d: 0x%08x\n", run, hash);
        CHECK(hash == expected[run]);
    }
}

/* Hit points (86b8:380e, 3a5b) of a fighter (class 2): the better of two
 * rolls of the class's dice (a first level's count, else one) and the
 * constitution's bonus a roll, a knight-like class's more at 17 to 25; at
 * the top level a flat 3 (a cleric's 1) over the sum. */
static void test_hit_points(void)
{
    static const uint8_t extra[] = {1, 2, 3, 3, 4, 4, 4, 5, 5};
    const cok_data_segment *D = R->data;
    const unsigned fighter = 2;
    uint8_t top = cok_data_held(D, 0x640a + fighter);
    /* The knight rolls two dice a first level, the fighter one. */
    for (unsigned k = 1; k <= fighter; ++k) {
        uint8_t mask = cok_data_held(D, 0x63fa + k), sides = cok_data_held(D, 0x6418 + k);
        for (unsigned level = 1; level <= 2; ++level)
            for (unsigned con = 3; con <= 25; ++con) {
                cok_character ch;
                memset(&ch, 0, sizeof ch);
                ch.record[0xa5 + k] = (uint8_t)level;
                ch.record[0x81] = (uint8_t)con;
                uint8_t count = level > 1 ? 1 : cok_data_held(D, 0x6411 + k);
                uint32_t seed = 12345 + con, mine = seed;
                uint8_t first = cok_dice(&mine, count, sides);
                uint8_t second = cok_dice(&mine, count, sides);
                uint8_t hit = first > second ? first : second;
                int bonus = cok_data_held(D, 0x82d + con);
                if (con >= 17) bonus += extra[con - 17];
                cok_queen_hit_points(&ch, R, &seed, mask, 1, true);
                CHECK(seed == mine);
                CHECK(ch.record[0xc0] == hit);
                CHECK(ch.record[0x89] == (uint8_t)(hit + bonus * count));
                CHECK(ch.record[0x198] == ch.record[0x89]);
            }
    }
    for (unsigned deity = 0; deity < 2; ++deity) {
        cok_character ch;
        memset(&ch, 0, sizeof ch);
        uint32_t seed = 99;
        unsigned class_ = deity ? 0 : fighter;
        ch.record[0xa5 + class_] = deity ? cok_data_held(D, 0x640a) : top;
        ch.record[0x19b] = 1;
        ch.record[0x81] = 17;
        cok_queen_hit_points(&ch, R, &seed, cok_data_held(D, 0x63fa + class_), 1, true);
        CHECK(seed == 99 && ch.record[0xc0] == (deity ? 1 : 3) && ch.record[0x89] == ch.record[0xc0]);
    }
}

/* The combat tables the port reads, under the descriptor's lookups: every
 * element at the offset its lookup gives is the executable's byte there (the
 * elements' bytes as their width), and a few the code's own arithmetic
 * reads hold the values the original's routines would fetch. */
static void test_combat_tables(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); combat tables not checked");
        return;
    }
    size_t ds = (size_t)(exe[8] | exe[9] << 8) * 16 + (size_t)0x322e * 16;
    const cok_data_segment *D = R->data;
    static const cok_data tables_checked[] = {
        COK_DATA_AREA_ROWS,           COK_DATA_TURNING,        COK_DATA_MOVES,
        COK_DATA_QUADRANTS,           COK_DATA_QUADRANT_DIRECTIONS,
        COK_DATA_START_COLUMNS,       COK_DATA_START_ROWS,     COK_DATA_FORMATIONS,
        COK_DATA_TERRAIN_FLAGS,       COK_DATA_LINE_DIRECTIONS, COK_DATA_STEP_X,
        COK_DATA_STEP_Y,              COK_DATA_TERRAIN,        COK_DATA_FOOTPRINTS,
        COK_DATA_HELPLESS,            COK_DATA_BATTLE_ONLY,    COK_DATA_CLOUD_CELLS,
    };
    for (size_t n = 0; n < sizeof tables_checked / sizeof *tables_checked; ++n) {
        cok_data t = tables_checked[n];
        const cok_data_table *s = &D->table[t];
        CHECK(s->width != 0 && s->indices >= 1 && s->indices <= 3);
        long at[3] = {0, 0, 0};
        unsigned total = 1;
        for (unsigned k = 0; k < s->indices; ++k) total *= s->index[k].count;
        for (unsigned e = 0; e < total; ++e) {
            unsigned rest = e;
            for (int k = s->indices - 1; k >= 0; --k) {
                at[k] = s->index[k].first + (long)(rest % s->index[k].count);
                rest /= s->index[k].count;
            }
            long offset = cok_data_offset_(D, t, s->indices, at[0], at[1], at[2]);
            CHECK(cok_data_holds(D, t, offset) && cok_data_holds(D, t, offset + s->width - 1));
            for (unsigned b = 0; b < s->width; ++b)
                CHECK(exe[ds + (size_t)offset + b] == cok_data_held(D, offset + (long)b));
        }
    }
    /* The original's own reads, by its code's arithmetic: the moves of a
     * quadrant's square 1 (59c7:1553), the turning of kind 1 at level 0 and
     * 1 (5045:3b6d), kind 13 at level 10, and the area of row -3 at radius
     * 1, a word (5045:74aa). */
    CHECK(cok_data_u8_at2(D, COK_DATA_MOVES, 1, 2) == 4);
    CHECK(cok_data_u8_at2(D, COK_DATA_MOVES, 1, 3) == 0);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 1, 0) == 0x63);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 1, 1) == 0x0a);
    CHECK(cok_data_u8_at2(D, COK_DATA_TURNING, 13, 10) == 0x0d);
    long area = cok_data_offset2(D, COK_DATA_AREA_ROWS, -3, 1);
    CHECK(cok_data_held(D, area) == 0xfd && cok_data_held(D, area + 1) == 0xff);
    /* The shapes the code gives: 77 terrains, up to the footprints at DS:62cb
     * (the terrain reads are unbounded; the table's end is the data's);
     * 5 formations; 9 cloud cells from 1 (5837:13c4, 148f:27a7); 6 footprint
     * cells (5837:01b9); 4 helpless effects from 0 (177e:0a19); 16 battle-only
     * effects from 1 (148f:1b66). */
    CHECK(cok_data_count(D, COK_DATA_TERRAIN, 0) == 77);
    CHECK(D->table[COK_DATA_TERRAIN].from + D->table[COK_DATA_TERRAIN].size ==
          D->table[COK_DATA_FOOTPRINTS].from);
    CHECK(D->table[COK_DATA_TERRAIN].from + D->table[COK_DATA_TERRAIN].size == 0x62cb);
    CHECK(cok_data_count(D, COK_DATA_FORMATIONS, 0) == 5);
    CHECK(cok_data_count(D, COK_DATA_CLOUD_CELLS, 0) == 9);
    CHECK(cok_data_first(D, COK_DATA_CLOUD_CELLS, 0) == 1);
    CHECK(cok_data_count(D, COK_DATA_FOOTPRINTS, 1) == 6);
    CHECK(cok_data_count(D, COK_DATA_HELPLESS, 0) == 4);
    CHECK(cok_data_first(D, COK_DATA_BATTLE_ONLY, 0) == 0); /* DS:00a2 + i: 1b66's k - 1 */
    CHECK(cok_data_count(D, COK_DATA_BATTLE_ONLY, 0) == 16);
    CHECK(cok_data_u8_at(D, COK_DATA_HELPLESS, 0) == 0x1f);
    CHECK(cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, 0) == 0x03);
    CHECK(cok_data_u8_at(D, COK_DATA_BATTLE_ONLY, 15) == 0x76);
    CHECK(cok_data_u8_at(D, COK_DATA_CLOUD_CELLS, 9) == 0x01);
}

/* The spells' and effects' handler tables against the stores their fill
 * routines make in build/DQK_FULL.EXE, read again here: `movw $imm, disp16`
 * (c7 06) is an offset (or a segment given explicitly), `mov %cs, disp16`
 * (8c 0e) the routine's own segment. The routines are 7fd8:46bb-4892 and
 * 4892-4c2c for the spells (DS:af45 + 4 * id), 78e4:4fb2-549e and 549e-5a00
 * for the effects (DS:bee8 + 4 * id). Each id a store fills has the handler the
 * store gives, cited as its routine ("dqk seg:off", past an overlay stub), or
 * COK_FX_NOTHING where the routine is a bare retf; each id no store fills is
 * empty (a spell's stop cites its 0:0 bytes). */
struct fill_routine {
    uint16_t seg, start, end;
};
struct fill_cell {
    int stores;
    bool cs;
    uint16_t imm;
};

static void fill_check(const uint8_t *exe, size_t header, const struct fill_routine *routines,
                       size_t routine_count, uint16_t base, unsigned count, struct fill_cell *cells)
{
    size_t bytes = 4 * (size_t)count + 2;
    for (size_t r = 0; r < routine_count; ++r) {
        size_t at = header + (size_t)(routines[r].seg - 0x1000) * 16;
        for (size_t i = routines[r].start; i + 6 <= routines[r].end; ++i) {
            const uint8_t *p = exe + at + i;
            if (p[0] == 0xc7 && p[1] == 0x06) {
                uint16_t disp = (uint16_t)(p[2] | p[3] << 8);
                if (disp < base || (size_t)(disp - base) >= bytes) continue;
                struct fill_cell *c = &cells[disp - base];
                c->stores++;
                c->cs = false;
                c->imm = (uint16_t)(p[4] | p[5] << 8);
            } else if (p[0] == 0x8c && p[1] == 0x0e) {
                uint16_t disp = (uint16_t)(p[2] | p[3] << 8);
                if (disp < base || (size_t)(disp - base) >= bytes) continue;
                struct fill_cell *c = &cells[disp - base];
                c->stores++;
                c->cs = true;
                c->imm = routines[r].seg;
            }
        }
    }
}

/* The cell's routine: its own segment for mov %cs, else the explicit one
 * (Ghidra's numbering: the stored segment + 0x1000). */
static void fill_expect(const struct fill_cell *seg, uint16_t *out)
{
    *out = seg->cs ? seg->imm : (uint16_t)(seg->imm + 0x1000);
}

/* The routine a stored far pointer reaches: an overlay stub is an ljmp (0xea,
 * seg:off, the target in the map's numbering: +0x1000 here) that the merge
 * leaves at each stub's address (docs/disassembly.md). */
static void routine_of(const uint8_t *exe, size_t header, uint16_t *seg, uint16_t *off)
{
    for (int hop = 0; hop < 4; ++hop) {
        const uint8_t *p = exe + header + (size_t)(*seg - 0x1000) * 16 + *off;
        if (p[0] != 0xea) return;
        *off = (uint16_t)(p[1] | p[2] << 8);
        *seg = (uint16_t)((p[3] | p[4] << 8) + 0x1000);
    }
}

static void test_magic_ids(void)
{
    static uint8_t exe[1 << 20];
    size_t size = read_whole("build/DQK_FULL.EXE", exe, sizeof exe);
    if (size == 0) {
        puts("dark queen: build/DQK_FULL.EXE not built (make dqk-merged); spell and effect tables not checked");
        return;
    }
    size_t header = (size_t)(exe[8] | exe[9] << 8) * 16, ds = header + (size_t)0x322e * 16;
    CHECK(R->spells.count == 0x8d && R->effects.count == 0xe6);

    /* The names: the far pointers at DS:25c7 + 4 * id (the pushes at 8438:0009). */
    CHECK(R->spells.names_at == 0x25c7 && R->spells.name_size == 4);
    for (unsigned id = 1; id < R->spells.count; ++id) {
        uint16_t off = (uint16_t)(exe[ds + 0x25c7 + 4 * id] | exe[ds + 0x25c8 + 4 * id] << 8);
        uint16_t seg = (uint16_t)(exe[ds + 0x25c9 + 4 * id] | exe[ds + 0x25ca + 4 * id] << 8);
        const char *name = spell_name((uint8_t)id);
        if (off == 0) {
            CHECK(id >= 0x8a && name != NULL && name[0] == '\0'); /* 0000:0000: "" */
            continue;
        }
        CHECK(seg == 0x322e && name != NULL);
        CHECK(strcmp(name, (const char *)exe + ds + off) == 0);
    }
    CHECK(spell_name(0) == NULL && spell_name(0x8d) == NULL);

    /* The headings: 41 bytes a level, 'Special' for 0 (DS:8b2e). */
    CHECK(R->spells.level_count == 10 && R->spells.levels_at == 0x8b2e && R->spells.level_size == 41);
    for (unsigned level = 0; level < R->spells.level_count; ++level)
        CHECK(strcmp(R->spells.levels[level], (const char *)exe + ds + 0x8b2e + 41 * level) == 0);

    /* The spells' handlers. */
    static const struct fill_routine spell_routines[] = {{0x7fd8, 0x46bb, 0x4892}, {0x7fd8, 0x4892, 0x4c2c}};
    static struct fill_cell spell_cells[4 * 0x8d + 2];
    fill_check(exe, header, spell_routines, 2, 0xaf45, 0x8d, spell_cells);
    for (unsigned id = 0; id < R->spells.count; ++id) {
        const struct fill_cell *off = &spell_cells[4 * id], *seg = &spell_cells[4 * id + 2];
        const cok_rules_cast *h = &R->spells.cast[id];
        bool filled = off->stores != 0 || seg->stores != 0;
        CHECK(filled == (id != 0 && id != 0x41 && id != 0x5f && id != 0x61 && id != 0x63));
        if (!filled) {
            /* Never stored: DS:af45 + 4 * id holds 0:0 in the image (and the fills
             * above store nothing there), so the stop cites that. */
            const uint8_t *far = exe + ds + 0xaf45 + 4 * id;
            CHECK(far[0] == 0 && far[1] == 0 && far[2] == 0 && far[3] == 0);
            CHECK(h->address == 0 && h->handler == COK_CAST_NONE);
            if (id == 0) {
                CHECK(h->unported == NULL);
            } else {
                char text[64];
                snprintf(text, sizeof text, "DS:af45 + 0x%x is 0:0, never stored (7fd8:46bb, 7fd8:4892)",
                         4 * id);
                CHECK(h->unported != NULL && strcmp(h->unported, text) == 0);
            }
            continue;
        }
        CHECK(off->stores == 1 && seg->stores == 1 && !off->cs);
        uint16_t sg = 0, so = off->imm;
        fill_expect(seg, &sg);
        routine_of(exe, header, &sg, &so);
        char cite[32];
        snprintf(cite, sizeof cite, "dqk %04x:%04x", sg, so);
        CHECK(h->address == off->imm && h->handler == COK_CAST_NONE && h->unported != NULL);
        CHECK(strcmp(h->unported, cite) == 0);
    }

    /* The effects' handlers: a routine that is a bare retf (push bp; mov bp,sp;
     * pop bp; retf) does nothing, COK_FX_NOTHING; the rest are cited. */
    static const struct fill_routine effect_routines[] = {{0x78e4, 0x4fb2, 0x549e}, {0x78e4, 0x549e, 0x5a00}};
    static struct fill_cell effect_cells[4 * 0xe6 + 2];
    fill_check(exe, header, effect_routines, 2, 0xbee8, 0xe6, effect_cells);
    static const uint8_t retf_only[5] = {0x55, 0x8b, 0xec, 0x5d, 0xcb};
    for (unsigned id = 0; id < R->effects.count; ++id) {
        const struct fill_cell *off = &effect_cells[4 * id], *seg = &effect_cells[4 * id + 2];
        const cok_rules_effect *h = &R->effects.handlers[id];
        bool filled = off->stores != 0 || seg->stores != 0;
        if (id == 0x80 || id == 0x90 || id == 0x9d) CHECK(!filled);
        else CHECK(filled);
        if (!filled) {
            CHECK(h->address == 0 && h->handler == COK_FX_MISSING && h->unported == NULL);
            continue;
        }
        CHECK(off->stores == 1 && seg->stores == 1 && !off->cs);
        uint16_t sg = 0, so = off->imm;
        fill_expect(seg, &sg);
        CHECK(h->address == off->imm);
        routine_of(exe, header, &sg, &so);
        if (id == 0x75) CHECK(sg == 0x52db && so == 0x4fd1 && off->imm == 0x10a3); /* 2d99:10a3, a stub */
        if (memcmp(exe + header + (size_t)(sg - 0x1000) * 16 + so, retf_only, sizeof retf_only) == 0) {
            CHECK(h->handler == COK_FX_NOTHING && h->unported == NULL);
        } else {
            char cite[32];
            snprintf(cite, sizeof cite, "dqk %04x:%04x", sg, so);
            if (id == 0x64 || id == 0xc3) /* ported: 78e4:14d1 and 4949 */
                CHECK(h->handler != COK_FX_MISSING && h->unported == NULL &&
                      sg == 0x78e4 && so == (id == 0x64 ? 0x14d1 : 0x4949));
            else
                CHECK(h->handler == COK_FX_MISSING && h->unported != NULL && strcmp(h->unported, cite) == 0);
        }
    }
    /* The effect names the code uses (rules_dark_queen_magic.c): CHARMED, INVISIBLE, BERSERK,
     * and 017d's FRIENDLY, ENLARGED and STRENGTH have their ids, and the yell's YELLED_AT; every
     * other name is 0 (none). */
    CHECK(R->effects.ids == cok_dark_queen_effect_ids);
    CHECK(R->effects.ids[COK_EFFECT_YELLED_AT] == 0x8c);
    CHECK(R->effects.ids[COK_EFFECT_CHARMED] == 0x0b && R->effects.ids[COK_EFFECT_BERSERK] == 0x4d);
    CHECK(R->effects.ids[COK_EFFECT_INVISIBLE] == 0x19);
    CHECK(R->effects.ids[COK_EFFECT_FRIENDLY] == 0x0e && R->effects.ids[COK_EFFECT_ENLARGED] == 0x0c);
    CHECK(R->effects.ids[COK_EFFECT_STRENGTH] == 0x26);
    for (unsigned n = 0; n < COK_EFFECT_NAMES; ++n)
        if (n != COK_EFFECT_CHARMED && n != COK_EFFECT_BERSERK && n != COK_EFFECT_INVISIBLE &&
            n != COK_EFFECT_FRIENDLY && n != COK_EFFECT_ENLARGED && n != COK_EFFECT_STRENGTH &&
            n != COK_EFFECT_YELLED_AT)
            CHECK(R->effects.ids[n] == 0);

    /* The named spell ids: the Dark Queen names of the Champions spells the
     * shared code asks for; the names it has none of are 0. */
    static const struct {
        cok_named_spell spell;
        uint8_t id;
        const char *name;
    } named[] = {
        {COK_SPELL_BLESS, 0x01, "Bless"},
        {COK_SPELL_CURE_LIGHT_WOUNDS, 0x03, "Cure Light Wounds"},
        {COK_SPELL_MAGIC_MISSILE, 0x0f, "Magic Missile"},
        {COK_SPELL_HOLD_PERSON, 0x17, "Hold Person"},
        {COK_SPELL_KNOCK, 0x1f, "Knock"},
        {COK_SPELL_REMOVE_CURSE, 0x2b, "Remove Curse"},
        {COK_SPELL_FIREBALL, 0x2f, "Fireball"},
        {COK_SPELL_HOLD_PERSON_MAGE, 0x31, "Hold Person"},
        {COK_SPELL_LIGHTNING_BOLT, 0x33, "Lightning Bolt"},
        {COK_SPELL_CURE_SERIOUS_WOUNDS, 0x3a, "Cure Serious Wounds"},
        {COK_SPELL_CURE_CRITICAL_WOUNDS, 0x47, "Cure Critical Wounds"},
        {COK_SPELL_ENTANGLE, 0x4e, "Entangle"},
        {COK_SPELL_CHARM_MONSTERS, 0x51, "Charm Monsters"},
        {COK_SPELL_CONFUSION, 0x52, "Confusion"},
        {COK_SPELL_DIMENSION_DOOR, 0x53, "Dimension Door"},
        {COK_SPELL_FEAR, 0x54, "Fear"},
        {COK_SPELL_FIRE_SHIELD, 0x55, "Fire Shield"},
        {COK_SPELL_FUMBLE, 0x56, "Fumble"},
        {COK_SPELL_FIREBALL_ITEM, 0x85, "Necklace Of Missiles"},
        {COK_SPELL_BLESS_POWER, 0x3e, "Bless"},
    };
    for (size_t n = 0; n < sizeof named / sizeof *named; ++n) {
        CHECK(R->spells.ids[named[n].spell] == named[n].id);
        CHECK(strcmp(spell_name(named[n].id), named[n].name) == 0);
        CHECK(cok_spell_id(R, named[n].spell) == named[n].id);
    }
    /* The roles, from the executable. The item's Fireball: 7fd8:1545 is
     * `cmpb $id, DS:b2ea` (the spell cast) and takes the item's dice when it is
     * the Necklace's id. The granted Bless: of the spell records, the only one
     * that is kind 3 at level 0 and named Bless (kind word +0, level +2). */
    const uint8_t *cmp = exe + header + (size_t)(0x7fd8 - 0x1000) * 16 + 0x1545;
    CHECK(cmp[0] == 0x80 && cmp[1] == 0x3e && cmp[2] == 0xea && cmp[3] == 0xb2);
    CHECK(cmp[4] == R->spells.ids[COK_SPELL_FIREBALL_ITEM]);
    unsigned granted = 0, granted_id = 0;
    for (unsigned id = 1; id < R->spells.count; ++id) {
        const uint8_t *rec = exe + ds + 0x27e9 + 22 * id;
        if (rec[0] == 3 && rec[1] == 0 && rec[2] == 0 && strcmp(spell_name(id), "Bless") == 0) {
            granted++;
            granted_id = id;
        }
    }
    CHECK(granted == 1 && granted_id == R->spells.ids[COK_SPELL_BLESS_POWER]);
    for (unsigned name = 0; name < COK_SPELL_NAMES; ++name) {
        bool listed = false;
        for (size_t n = 0; n < sizeof named / sizeof *named; ++n) listed |= named[n].spell == name;
        CHECK(listed || R->spells.ids[name] == 0);
    }
}

/* The cells' hash (test_combat.c's). */
static uint32_t cells_hash(const cok_combat *c)
{
    uint32_t h = 0x811c9dc5u;
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) h = (h ^ c->cells[y][x]) * 0x01000193u;
    return h;
}

/* The draws from seed that reach target, one step each (every roll of the builders is above 1,
 * so each steps the seed once); ~0 if it is not reached. */
static uint32_t draws_to(uint32_t seed, uint32_t target)
{
    for (uint32_t n = 0; n < 100000; ++n) {
        if (seed == target) return n;
        cok_tp_random(&seed, 100);
    }
    return ~0u;
}

/* Dark Queen's open ground (dqk 59c7:0e3b): Death Knights' floor, river, trees and scatter, the
 * scatter's cell as 59c7:0ce3's, the flags of DS:5d7b's entry (flags 0x71: scatter's density 110,
 * tree chance 7, and flags 0x06: density 20, tree chance 1). The builders roll with Dark Queen's
 * generator (2102:0234, the descriptor's random), from seed 12345. The pinned hashes, seeds and
 * draw counts are the port's; the wild flow of tools/dosbox/checks/dqk_fight.py checked them
 * against the original's RandSeed at 59c7:0f7b when its grounds were these flags (terrain 12 and
 * 14, then 2629 and 2327 draws); the wild flow's grounds are now the shipped var 0x1a2 values 1 and 2
 * (flags 0x18 and 0x11), built by the same routines. */
static void test_wilderness(void)
{
    static cok_combat c;
    cok_random_select(R->random);
    uint8_t flags = cok_data_u8_at(R->data, COK_DATA_TERRAIN_FLAGS, 12);
    CHECK(flags == 0x71 && cok_data_u8_at(R->data, COK_DATA_TERRAIN_FLAGS, 14) == 0x06);
    uint32_t seed = 12345;
    CHECK(cok_ecl_var_address(R, COK_VAR_RIVER, 0) == 0x33 &&
          cok_ecl_var_address(R, COK_VAR_ABYSS, 0) == 0x1a2);
    cok_combat_wilderness(&c, R, flags, false, &seed);
    CHECK(cells_hash(&c) == 0x97ec18aau && seed == 2803431546u && draws_to(12345, seed) == 2629u);
    for (int y = 0; y < COK_COMBAT_HEIGHT; ++y)
        for (int x = 0; x < COK_COMBAT_WIDTH; ++x) CHECK(c.cells[y][x] != 0x34);
    seed = 12345;
    cok_combat_wilderness(&c, R, 0x06, true, &seed);
    CHECK(cells_hash(&c) == 0x4bcdffe2u && seed == 3122112960u && draws_to(12345, seed) == 2327u);
    /* The river: from a column 34 less 5d4, moved left to a multiple of 7 less 2 (on row 0), its
     * bank 0x34 and water or a ford 0x34-0x3d, the far bank 0x34 five on, a column right a row. */
    int x = -1;
    for (int k = 0; k < COK_COMBAT_WIDTH && x < 0; ++k)
        if (c.cells[0][k] == 0x34) x = k;
    CHECK(x >= 0 && (x + 2) % 7 == 0 && x >= 0x22 - 20 && x <= 0x22 - 5);
    for (int y = 0; y < COK_COMBAT_HEIGHT && x + 5 + y < COK_COMBAT_WIDTH - 1; ++y) {
        CHECK(c.cells[y][x + y] == 0x34 && c.cells[y][x + y + 5] == 0x34);
        for (int k = 1; k <= 4; ++k)
            CHECK(c.cells[y][x + y + k] >= 0x34 && c.cells[y][x + y + k] <= 0x3d);
    }
    cok_random_select(COK_RANDOM_PASCAL);
}

/* The placement's distance (dqk 59c7:16b3's flag): var 0x37, else 1 for a facing that is not
 * the party's, the same for the demonstration as none (its demonstration distance is 0). The
 * placement itself (59c7:1864) is not ported, so this checks the helper the layout shares. */
static void test_layout_distance(void)
{
    const cok_rules_combat *C = &R->combat;
    CHECK(cok_combat_layout_distance(C, 0, 2, 0, false) == 1);
    CHECK(cok_combat_layout_distance(C, 0, 0, 0, false) == 0);
    CHECK(cok_combat_layout_distance(C, 3, 2, 0, false) == 3);
    CHECK(cok_combat_layout_distance(C, 3, 0, 0, false) == 3);
    CHECK(cok_combat_layout_distance(C, 0, 2, 0, true) == 1);
    CHECK(cok_combat_layout_distance(C, 0, 0, 0, true) == 0);
}

/* A record of Dark Queen's layout: active, one cell, on side (0 the party, 1 against it), its
 * combat block allocated as the battle's records are. */
static cok_character *placed(cok_adventure *game, uint8_t side)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    cok_field_set8(L, c->record, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, c->record, COK_FIELD_HOSTILE, side);
    cok_field_set8(L, c->record, COK_FIELD_SIZE, 1);
    CHECK(cok_party_append(&game->party, c));
    return c;
}

/* The placement's relayout (dqk 59c7:16b3's loop is a local, so a record laid out again keeps its
 * side; dkk 410b:18c2 leaves DS:86e8 1, so the descriptor's layout_sets_side is set for Death
 * Knights only). The battle stops at 59c7:1864, so this calls the placement itself, on a copy of
 * Dark Queen's descriptor with an ambush (its own has none, so no relayout is reached): an enemy
 * at facing 2 is laid out first, then a party record at the party's facing 0, laid out again. The
 * party record is placed on its side's formation, as Dark Queen's descriptor has it; with the flag
 * it takes the enemy's, and its cell differs. Skipped without DQKAssets/. */
static void test_relayout_side(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2", &st) != 0) {
        puts("dark queen: no DQKAssets/; the placement's relayout side not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    int x[2], y[2];
    for (int flag = 0; flag < 2; ++flag) {
        cok_rules rules = *R;
        rules.combat.ambush = true;
        rules.combat.layout_sets_side = flag;
        CHECK(cok_adventure_open(&game, &rules, "DQKAssets", &keys, NULL));
        game.vm.direction = 0;
        game.vm.map_x = game.vm.map_y = 7;
        game.vm.mode = 5;
        cok_ecl_set_var(&game.vm, COK_VAR_DISTANCE, 0);
        game.ambush = 0x2;
        placed(&game, 1);
        placed(&game, 0);
        memset(game.combat.cells, COK_COMBAT_FLOOR, sizeof game.combat.cells);
        CHECK(cok_combat_place_all(&game));
        cok_combatant *party = &game.combat.combatant[2];
        CHECK(party->character == game.party.members[1] && party->size == 1);
        x[flag] = party->x;
        y[flag] = party->y;
        cok_adventure_close(&game);
    }
    /* Dark Queen's (flag 0): its own side's formation, 5 on and 3 down from the party's square
     * (22, 10); with the flag, the enemy's, one row up. */
    CHECK(x[0] == 27 && y[0] == 13 && x[1] == 27 && y[1] == 12);
}

/* Whether queen_icon's slot and image hold record id of the library, byte for byte. */
static bool icon_is(const cok_adventure *game, unsigned slot, unsigned image,
                    const hlib_library *lib, unsigned id)
{
    const uint8_t *want = NULL, *got = NULL;
    size_t want_size = 0, got_size = 0;
    return hlib_record(lib, id, &want, &want_size) == HLIB_OK &&
           queen_icon(game, slot, image, &got, &got_size) && got_size == want_size &&
           memcmp(got, want, want_size) == 0;
}

/* A loaded save's combat icons (dark_queen.c's loaded, member_icon, 604b:05d7): each member's
 * slot (+0xc5) holds CBODY +0xc4 and +0xc4 + 0x80. Skipped without DQKAssets/. */
static void test_loaded_icons(void)
{
    struct stat st;
    if (stat("DQKAssets/SAVE/SAVGAMA.QSV", &st) != 0 ||
        stat("DQKAssets/DISK3/CBODY.TLB", &st) != 0) {
        puts("dark queen: no DQKAssets/ saved game; its members' icons not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    const cok_drawing *d = cok_adventure_drawing(&game);
    CHECK(d != NULL && d->load != NULL && d->load(&game, "DQKAssets/SAVE/SAVGAMA.QSV"));
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library cbody;
    CHECK(hlib_read_file("DQKAssets/DISK3/CBODY.TLB", &bytes, &n) == HLIB_OK);
    CHECK(hlib_parse(bytes, n, &cbody) == HLIB_OK);
    const cok_character_layout *L = R->character;
    CHECK(game.party.count == 6);
    for (size_t k = 0; k < game.party.count; ++k) {
        const uint8_t *r = game.party.members[k]->record;
        unsigned slot = cok_field_u8(L, r, COK_FIELD_ICON);
        unsigned body = cok_field_u8(L, r, COK_FIELD_ICON_BODY);
        CHECK(slot < 8 && body < 0x80);
        CHECK(icon_is(&game, slot, 0, &cbody, body) &&
              icon_is(&game, slot, 1, &cbody, body + 0x80));
    }
    free(bytes);
    cok_adventure_close(&game);
}

/* Whether queen_icon's slot and image are empty. */
static bool icon_empty(const cok_adventure *game, unsigned slot, unsigned image)
{
    const uint8_t *got = NULL;
    size_t got_size = 1;
    return queen_icon(game, slot, image, &got, &got_size) && got == NULL && got_size == 0;
}

/* 5045:0001's icons (204c:004d, 00e0-00f9): the battle's end empties the slots 0x0f-0x25, the
 * cursor's 0x1e among them, and not a slot past them. Two members' CBODY go into 0x17 and 0x25
 * (queen_added_icons), then the battle's end's icons (cok_dark_queen_battle_icons). Skipped
 * without DQKAssets/. */
static void test_battle_icons(void)
{
    struct stat st;
    static cok_item_types types;
    char error[300];
    if (stat("DQKAssets/DISK3/CBODY.TLB", &st) != 0 ||
        stat("DQKAssets/SAVE/ROLANDOF.QCH", &st) != 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/ characters; the battle's icons not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library cbody;
    CHECK(hlib_read_file("DQKAssets/DISK3/CBODY.TLB", &bytes, &n) == HLIB_OK);
    CHECK(hlib_parse(bytes, n, &cbody) == HLIB_OK);
    const cok_character_layout *L = R->character;
    static const unsigned slots[] = {0x17, 0x25};
    for (unsigned k = 0; k < 2; ++k) {
        cok_character *c = malloc(sizeof *c);
        CHECK(c != NULL && cok_character_read_file(c, R, "DQKAssets/SAVE", "ROLANDOF", "QCH",
                                                   &types, error, sizeof error));
        CHECK(cok_party_add(&game.party, R, c)); /* takes the lowest slot: set after */
        cok_field_set8(L, c->record, COK_FIELD_ICON, (uint8_t)slots[k]);
        CHECK(queen_added_icons(&game, c->record));
        unsigned body = cok_field_u8(L, c->record, COK_FIELD_ICON_BODY);
        CHECK(icon_is(&game, slots[k], 0, &cbody, body) &&
              icon_is(&game, slots[k], 1, &cbody, body + 0x80));
    }
    CHECK(!icon_empty(&game, 0x17, 0) && !icon_empty(&game, 0x25, 1));
    cok_dark_queen_battle_icons(&game);
    CHECK(icon_empty(&game, 0x0f, 0) && icon_empty(&game, 0x17, 0) && icon_empty(&game, 0x17, 1));
    CHECK(icon_empty(&game, 0x1e, 0) && icon_empty(&game, 0x1e, 1));
    CHECK(icon_empty(&game, 0x25, 0) && icon_empty(&game, 0x25, 1));
    free(bytes);
    cok_adventure_close(&game);
}

/* Add Character's joins and icons (cok_party_add's slot, 604b:3491; queen_added_icons, 7368:0af4):
 * each joined member takes the lowest of 0-7 no other member holds (the .QCH file's 0x0d is
 * replaced), and its CBODY goes into it; an NPC too. Skipped without DQKAssets/. */
static void test_join_icons(void)
{
    struct stat st;
    static cok_item_types types;
    char error[300];
    if (stat("DQKAssets/DISK3/CBODY.TLB", &st) != 0 ||
        stat("DQKAssets/SAVE/ROLANDOF.QCH", &st) != 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/ characters; the joins' icons not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library cbody;
    CHECK(hlib_read_file("DQKAssets/DISK3/CBODY.TLB", &bytes, &n) == HLIB_OK);
    CHECK(hlib_parse(bytes, n, &cbody) == HLIB_OK);
    const cok_character_layout *L = R->character;
    static const char *const names[] = {"ROLANDOF", "WALKERKE", "BALTHIRN"};
    for (unsigned k = 0; k < 3; ++k) {
        cok_character *c = malloc(sizeof *c);
        CHECK(c != NULL && cok_character_read_file(c, R, "DQKAssets/SAVE", names[k], "QCH",
                                                   &types, error, sizeof error));
        if (k == 2) /* BALTHIRN as an NPC (+0x9b bit 7) */
            cok_field_set8(L, c->record, COK_FIELD_NPC,
                           cok_field_u8(L, c->record, COK_FIELD_NPC) | 0x80);
        CHECK(cok_party_add(&game.party, R, c));
        CHECK(queen_added_icons(&game, c->record));
        unsigned slot = cok_field_u8(L, c->record, COK_FIELD_ICON);
        unsigned body = cok_field_u8(L, c->record, COK_FIELD_ICON_BODY);
        CHECK(slot == k && body < 0x80);
        CHECK(icon_is(&game, slot, 0, &cbody, body) && icon_is(&game, slot, 1, &cbody, body + 0x80));
    }
    free(bytes);
    cok_adventure_close(&game);
}

/* A saved game whose members' icon slots are 0x0d, as the .QCH files hold them: loaded, the
 * members take 0-5 in list order (the restore's joins, 604b:28f8 with cok_party_add), each CBODY
 * into its slot (2ab4's loop). Then a member removed (177e:1e31) empties its slot and no other.
 * The copy is patched in a temporary folder. Skipped without DQKAssets/. */
static void test_loaded_slots(void)
{
    struct stat st;
    static uint8_t data[8192];
    static cok_item_types types;
    char error[300], dir[512], path[600];
    size_t n = read_whole("DQKAssets/SAVE/SAVGAMA.QSV", data, sizeof data);
    if (n == 0 || stat("DQKAssets/DISK3/CBODY.TLB", &st) != 0 ||
        !cok_item_types_read_at("DQKAssets/DISK1/ITEMS.DAT", 0, &types, error, sizeof error)) {
        puts("dark queen: no DQKAssets/ saved game; the loaded slots not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    size_t at = 0x40f, count = (size_t)(data[0x40d] | data[0x40e] << 8);
    CHECK(count == 6);
    for (size_t i = 0; i < count; ++i) {
        cok_character c;
        size_t used;
        CHECK(cok_character_from_bytes(&c, R, data + at, sizeof data - at, 0, &used, error,
                                       sizeof error));
        CHECK(data[at + 0xc5] == cok_field_u8(L, c.record, COK_FIELD_ICON));
        data[at + 0xc5] = 0x0d;
        at += used;
        cok_character_free(&c);
    }
    CHECK(test_temp_dir(dir, sizeof dir, "cok_dqk_slots") != NULL);
    snprintf(path, sizeof path, "%s/SAVGAMA.QSV", dir);
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL && fwrite(data, 1, n, f) == n);
    fclose(f);

    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    const cok_drawing *d = cok_adventure_drawing(&game);
    CHECK(d != NULL && d->load != NULL && d->load(&game, path));
    uint8_t *bytes = NULL;
    size_t cbody_size = 0;
    hlib_library cbody;
    CHECK(hlib_read_file("DQKAssets/DISK3/CBODY.TLB", &bytes, &cbody_size) == HLIB_OK);
    CHECK(hlib_parse(bytes, cbody_size, &cbody) == HLIB_OK);
    CHECK(game.party.count == 6);
    for (unsigned k = 0; k < 6; ++k) {
        const uint8_t *r = game.party.members[k]->record;
        unsigned body = cok_field_u8(L, r, COK_FIELD_ICON_BODY);
        CHECK(cok_field_u8(L, r, COK_FIELD_ICON) == k && body < 0x80);
        CHECK(icon_is(&game, k, 0, &cbody, body) && icon_is(&game, k, 1, &cbody, body + 0x80));
    }
    queen_remove_icons(&game, game.party.members[2]->record);
    CHECK(icon_empty(&game, 2, 0) && icon_empty(&game, 2, 1));
    CHECK(icon_is(&game, 3, 0, &cbody, cok_field_u8(L, game.party.members[3]->record,
                                                    COK_FIELD_ICON_BODY)));
    free(bytes);
    remove(path);
    cok_adventure_close(&game);
}

/* The combat screen (dark_queen.c's combat_screen, 20b2:02fa): the screen cleared, then the
 * panel's cells 23-38 by 1-21 to the window's grey (0x17), FRAME 1, 2, 3 and 5, FRAME 4 within
 * rows 186-199, FRAME 6, each drawn as cok_vga_draw_record draws it (the same records, the
 * same clip, the same order), and nothing else: the adventure's frame (compass, lamps, FRAME 7)
 * is not drawn. Skipped without DQKAssets/. */
static void test_combat_frame(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK3/FRAME.TLB", &st) != 0) {
        puts("dark queen: no DQKAssets/ FRAME.TLB; the combat frame not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {NULL, NULL, NULL, NULL};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, NULL));
    uint8_t *bytes = NULL;
    size_t n = 0;
    hlib_library frame;
    CHECK(hlib_read_file("DQKAssets/DISK3/FRAME.TLB", &bytes, &n) == HLIB_OK);
    CHECK(hlib_parse(bytes, n, &frame) == HLIB_OK);
    cok_picture want;
    CHECK(cok_picture_create_vga(&want, game.screen.units, 200, 1, NULL) == COK_PICTURE_OK);
    cok_vga_fill(&game.screen, 0, 0, 320, 200, 0x00);
    cok_adventure_screen(&game, COK_SCREEN_COMBAT);
    cok_vga_fill(&want, 0, 0, 320, 200, 0x00);
    cok_vga_fill(&want, 8 * 23, 8, 8 * 16, 8 * 21, 0x17);
    for (unsigned id = 1; id <= 3; ++id)
        CHECK(cok_vga_draw_record(&want, &frame, id, 0, 0, NULL, NULL));
    CHECK(cok_vga_draw_record(&want, &frame, 5, 0, 0, NULL, NULL));
    const cok_vga_clip bottom = {186, 0, 200, 320};
    CHECK(cok_vga_draw_record(&want, &frame, 4, 0, 0, &bottom, NULL));
    CHECK(cok_vga_draw_record(&want, &frame, 6, 0, 0, NULL, NULL));
    CHECK(memcmp(want.pixels, game.screen.pixels, 320 * 200) == 0);
    cok_picture_free(&want);
    free(bytes);
    cok_adventure_close(&game);
}

/* Whether any pixel of cells x1-x2 in row (8 by 8 pixels a cell) is colour. */
static bool row_has(const cok_adventure *game, int x1, int x2, int row, uint8_t colour)
{
    for (int y = 8 * row; y < 8 * row + 8; ++y)
        for (int x = 8 * x1; x < 8 * (x2 + 1); ++x)
            if (game->screen.pixels[y * 320 + x] == colour) return true;
    return false;
}

/* Whether any pixel of cells 23-38 in row differs from the window's grey. */
static bool ink_row(const cok_adventure *game, int row)
{
    for (int y = 8 * row; y < 8 * row + 8; ++y)
        for (int x = 8 * 23; x < 8 * 39; ++x)
            if (game->screen.pixels[y * 320 + x] != 0x17) return true;
    return false;
}

/* The time the text's delay waited (hooks.delay): the weapon's name is printed without it. */
static unsigned waited_ms;

static void count_delay(cok_adventure *game, unsigned ms, void *context)
{
    (void)game;
    (void)context;
    waited_ms += ms;
}

/* 1a62:0311's character delay (1df9:052a: random(1000) through pass, 3 ms times the speed) while
 * DS:ae65 (the text's delay state) is set: the start sets it, and the battle's setup clears it
 * (59c7:1de2, arena_tiles). PRINT and JOURNAL set and clear it around their text (6c70:1d02-1d84,
 * 32fc-3323): dqk_ecl's speed blocks check that against the original. Skipped without DQKAssets/. */
static void test_text_delay_state(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the text's delay state not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    hooks.delay = count_delay;
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 4); /* 12 ms a character */
    game.vm.seed = 12345;
    uint32_t want = 12345;
    for (int i = 0; i < 5; ++i) cok_tp_random(&want, 1000);
    waited_ms = 0;
    CHECK(queen_print_window(&game, "HELLO", 1, 0x11, 0x26, 0x16, 7, true));
    CHECK(game.vm.seed == want && waited_ms == 5 * 12);
    /* The battle's setup clears the state: no delay, no draw. */
    CHECK(cok_adventure_drawing(&game)->arena_tiles(&game, false, 4));
    want = game.vm.seed;
    waited_ms = 0;
    CHECK(queen_print_window(&game, "HELLO", 1, 0x11, 0x26, 0x16, 7, true));
    CHECK(game.vm.seed == want && waited_ms == 0);
    cok_adventure_close(&game);
}

/* The side panel (dark_queen.c's combat_panel, cok_arena_panel's hook, 177e:085f): cells 23-38 by
 * 1-21 cleared to the window's grey, the name on row 1, "Hitpoints" and "AC" on rows 3 and 5,
 * the hit points in light cyan below the most (colour 0x0b), the armour class "-2" at 62, the
 * readied weapon's name on row 7 (at once, no delay), the casting line two rows below its last
 * row (row 9 for CINDER's one-line name, row 7 with none), and nothing else in the panel.
 * Skipped without DQKAssets/. */
static void test_combat_panel(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the side panel not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    hooks.delay = count_delay;
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    cok_ecl_set_var(&game.vm, COK_VAR_SPEED, 2); /* the text's delay, 6 ms a character if slow */
    /* The battle's setup clears the text's delay state (59c7:1de2, arena_tiles): the weapon's
     * name is then printed at once. */
    CHECK(cok_adventure_drawing(&game)->arena_tiles(&game, false, 4));
    const cok_character_layout *L = R->character;
    cok_character *c = dqk_member(&game, "'CINDER'");
    CHECK(cok_party_add(&game.party, R, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_field_set8(L, r, COK_FIELD_MAX_HIT_POINTS, 10);
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 7);
    cok_field_set8(L, r, COK_FIELD_AC, 62);
    game.vm.mode = 5;
    memset(game.screen.pixels, 0x00, 320 * 200);
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, c) && !game.combat.panel);
    /* CINDER's readied weapon is on row 7 (its name, grey), the status line is not due. */
    CHECK(ink_row(&game, 1) && ink_row(&game, 3) && ink_row(&game, 5) && ink_row(&game, 7));
    CHECK(!ink_row(&game, 2) && !ink_row(&game, 4) && !ink_row(&game, 6));
    CHECK(!ink_row(&game, 8) && !ink_row(&game, 21));
    CHECK(row_has(&game, 23, 31, 3, 0x07) && !row_has(&game, 23, 38, 3, 0x0a));
    CHECK(row_has(&game, 33, 35, 3, 0x0b));
    CHECK(row_has(&game, 23, 24, 5, 0x07) && row_has(&game, 26, 27, 5, 0x07));
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 10);
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, c));
    CHECK(!row_has(&game, 33, 35, 3, 0x0b) && row_has(&game, 33, 34, 3, 0x07));
    /* A combatant who casts: "(Casting)" on row 9, two below CINDER's one-line weapon name's
     * row 7, in light cyan. */
    c->combat->spell = 3;
    cok_field_set8(L, r, COK_FIELD_HIT_POINTS, 7);
    memset(game.screen.pixels, 0x00, 320 * 200);
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, c) && row_has(&game, 23, 38, 9, 0x0b));
    CHECK(!row_has(&game, 23, 38, 7, 0x0b) && !ink_row(&game, 8));
    /* With no readied weapon (slot 0 of the battle's items cleared) the line is on row 7. */
    size_t slot = c->slots[0];
    c->slots[0] = 0;
    memset(game.screen.pixels, 0x00, 320 * 200);
    game.combat.panel = true;
    CHECK(cok_arena_panel(&game, c) && row_has(&game, 23, 38, 7, 0x0b));
    CHECK(!ink_row(&game, 9) && !ink_row(&game, 8));
    c->slots[0] = slot;
    c->combat->spell = 0;
    /* A readied weapon's name (CINDER's, slot 0) is drawn on row 7 from column 23 in grey
     * (177e:0311), at once (no delay), and with no status nothing else is drawn on row 7. */
    CHECK(c->slots[0] != 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    memset(game.screen.pixels, 0x00, 320 * 200);
    game.combat.panel = true;
    waited_ms = 0;
    CHECK(cok_arena_panel(&game, c) && ink_row(&game, 7) && row_has(&game, 23, 38, 7, 0x07));
    CHECK(waited_ms == 0);
    cok_adventure_close(&game);
}

/* The command menu (dqk 5045:08cf's 0cd7 and 1595) and its first routines, through
 * queen_commands with a keyboard queue (script). CINDER is the one combatant. */
typedef struct {
    const char *keys;
    size_t at;
    int menus;          /* the "menu" lines logged (one per build) */
    char menu[64];      /* the last one */
    char all[512];      /* every "menu" line, each ended with ';' */
    char messages[256]; /* the messages logged, each ended with ';' */
    char prints[128];   /* the texts said (the "print" lines), each ended with ';' */
} menu_script;

static menu_script script;
static cok_adventure menu_game;
static bool menu_is_open;
static cok_character *menu_c;
static cok_character dummy_turn; /* a turn list entry that is not CINDER */

static void menu_close(void)
{
    if (menu_is_open) cok_adventure_close(&menu_game);
    menu_is_open = false;
}

/* The pixel a '\x01' key clicks at (the mouse's pointer, read with the click). */
static int16_t click_x, click_y;

/* The keys: each character is a key, '~' the extended key's 0 (its scan code follows), '\x01' a
 * click at click_x, click_y. A key read clears the pointer's click, as adventure.c's read does. */
static int menu_keys(void *context)
{
    menu_script *s = context;
    if (s->keys[s->at] == '\0') return -1;
    char c = s->keys[s->at++];
    menu_game.pointer.clicked = c == '\x01';
    if (c == '\x01') {
        menu_game.pointer.x = click_x;
        menu_game.pointer.y = click_y;
        return 0;
    }
    return c == '~' ? 0 : (unsigned char)c;
}

static void menu_log(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    menu_script *s = context;
    if (strcmp(kind, "menu") == 0) {
        ++s->menus;
        snprintf(s->menu, sizeof s->menu, "%s", text);
        size_t used = strlen(s->all);
        snprintf(s->all + used, sizeof s->all - used, "%s;", text);
    } else if (strcmp(kind, "message") == 0) {
        size_t used = strlen(s->messages);
        snprintf(s->messages + used, sizeof s->messages - used, "%s;", text);
    } else if (strcmp(kind, "print") == 0) {
        size_t used = strlen(s->prints);
        snprintf(s->prints + used, sizeof s->prints - used, "%s;", text);
    }
}

/* Opens Dark Queen's game with CINDER at (5, 5) as combatant 1 (the list's count is 1, as the round
 * tests set it), her turn to act with initiative 5, no items, no spells, no class levels, no weapon,
 * not dying: the menu's defaults (Aim, Guard, Quick, Delay, View, Speed, End). The keys are set by
 * menu_keys(script.keys). */
static void menu_open(void)
{
    static cok_rules rules;
    rules = *R;
    rules.fight.cloud_tile = 0x1e;
    menu_close();
    cok_keyboard keys = {menu_keys, &script, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    hooks.log = menu_log;
    hooks.context = &script;
    CHECK(cok_adventure_open(&menu_game, &rules, "DQKAssets", &keys, &hooks));
    menu_is_open = true;
    const cok_character_layout *L = R->character;
    menu_c = dqk_member(&menu_game, "'CINDER'");
    CHECK(cok_party_add(&menu_game.party, R, menu_c));
    menu_c->combat = calloc(1, sizeof *menu_c->combat);
    CHECK(menu_c->combat != NULL);
    uint8_t *r = menu_c->record;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    cok_field_set8(L, r, COK_FIELD_ITEM_COUNT, 0);
    cok_field_set8(L, r, COK_FIELD_RACE, 0);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 0, 0);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 3, 0);
    size_t at = cok_field_offset(L, COK_FIELD_MEMORIZED);
    memset(r + at, 0, 0x89);
    menu_c->slots[0] = 0;
    cok_combat *k = &menu_game.combat;
    memset(k->cells, COK_COMBAT_FLOOR, sizeof k->cells);
    k->count = 1; /* combatant 0 is no one's (cok_combat_index counts from 1) */
    place(&menu_game, menu_c, 5, 5);
    menu_game.vm.seed = 1;
    CHECK(cok_combat_round_start(&menu_game, menu_c));
    menu_c->combat->initiative = 5;
    menu_game.undead = 1;
    menu_c->combat->turns = 0;
    menu_c->combat->may_cast = 0;
    menu_c->combat->forced = 0;
    menu_c->combat->guarding = 0;
    script.menus = 0;
    script.menu[0] = '\0';
    script.all[0] = '\0';
    script.messages[0] = '\0';
    script.prints[0] = '\0';
}

/* Runs queen_commands on CINDER with keys, from a fresh menu's state (the keys' queue reset). */
static bool menu_run(const char *keys)
{
    script.keys = keys;
    script.at = 0;
    script.menus = 0;
    script.menu[0] = '\0';
    script.all[0] = '\0';
    script.messages[0] = '\0';
    script.prints[0] = '\0';
    menu_game.vm.status = COK_ECL_OK;
    menu_game.vm.abort = false;
    return queen_commands(&menu_game, menu_c);
}

/* The menu's text when CINDER is as the test set her: queen_commands with no keys (the menu
 * built, shown, and the keys run out at once). */
static void menu_expect(const char *text)
{
    CHECK(menu_run(""));
    if (strcmp(script.menu, text) != 0) {
        fprintf(stderr, "menu \"%s\", expected \"%s\"\n", script.menu, text);
        exit(1);
    }
    CHECK(menu_c->combat->initiative == 5 && menu_game.vm.status == COK_ECL_OK);
}

/* The menu's 13 conditions (0cd7): each item shown or hidden by the record's fields. */
static void test_command_menu_items(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the command menu's items not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    menu_open();
    uint8_t *r = menu_c->record;
    /* The defaults: Guard (no weapon), Quick and Delay, Aim, View, Speed and End. */
    menu_expect("Aim Guard Quick@Delay View Speed End");
    /* Use while the record has items (+0xc9). */
    cok_field_set8(L, r, COK_FIELD_ITEM_COUNT, 1);
    menu_expect("Aim Use Guard Quick@Delay View Speed End");
    cok_field_set8(L, r, COK_FIELD_ITEM_COUNT, 0);
    /* Cast: a memorised byte past the old count (+0x150, below +0xce + 0x4a), with var 2 (the
     * area's magic: COK_VAR_NO_MAGIC) 0 and the round's may-cast set; any of the three not, hides
     * it. */
    size_t at = cok_field_offset(L, COK_FIELD_MEMORIZED);
    r[0x150] = 0x03;
    menu_c->combat->may_cast = 1;
    menu_expect("Aim Cast Guard Quick@Delay View Speed End");
    cok_ecl_set_var(&menu_game.vm, COK_VAR_NO_MAGIC, 1);
    menu_expect("Aim Guard Quick@Delay View Speed End");
    cok_ecl_set_var(&menu_game.vm, COK_VAR_NO_MAGIC, 0);
    menu_c->combat->may_cast = 0;
    menu_expect("Aim Guard Quick@Delay View Speed End");
    menu_c->combat->may_cast = 1;
    r[0x150] = 0x80; /* the low 7 bits only count: this is no spell */
    menu_expect("Aim Guard Quick@Delay View Speed End");
    r[0x150] = 0;
    memset(r + at, 0, 0x89);
    /* Turn: a class's level above 0 (class 0 here, class 3 next), with fewer turns than undead. */
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 0, 1);
    menu_expect("Aim Turn Guard Quick@Delay View Speed End");
    menu_c->combat->turns = 1; /* as many as undead: none */
    menu_expect("Aim Guard Quick@Delay View Speed End");
    menu_c->combat->turns = 0;
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 0, 0xff); /* 177e:2234 reads it unsigned */
    menu_expect("Aim Turn Guard Quick@Delay View Speed End");
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 0, 0);
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 3, 3);
    menu_expect("Aim Turn Guard Quick@Delay View Speed End");
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 3, 2); /* class 3 needs above 2 */
    menu_expect("Aim Guard Quick@Delay View Speed End");
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 3, 0);
    /* Yell: a kender (+0x58 5, and +0x59 0 for the word) while no one has yelled. */
    cok_field_set8(L, r, COK_FIELD_RACE, 5);
    menu_expect("Aim Guard Quick@Delay Yell View Speed End");
    r[0x59] = 1;
    menu_expect("Aim Guard Quick@Delay View Speed End");
    r[0x59] = 0;
    uint8_t yelled = 0;
    menu_game.combat.yelled = &yelled;
    menu_expect("Aim Guard Quick@Delay View Speed End");
    menu_game.combat.yelled = NULL;
    cok_field_set8(L, r, COK_FIELD_RACE, 0);
    /* Bandage: a party member dying (status 5, not hostile). */
    cok_field_set8(L, r, COK_FIELD_STATUS, 5);
    menu_expect("Aim Guard Quick@Delay Bandage View Speed End");
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    /* Guard: a bow (missile, not thrown) gives none; a thrown weapon, a sword, one. The type is
     * the readied item's at +0x28 (Dark Queen's item_record.type), not Champions' +0x2e. */
    cok_item_types *types = &menu_game.item_types;
    free(menu_c->items); /* the saved record's items: this one replaces them */
    menu_c->items = calloc(1, COK_ITEM_SIZE);
    CHECK(menu_c->items != NULL);
    menu_c->item_count = 1;
    menu_c->slots[0] = 1;
    uint8_t bow = 5;
    menu_c->items[0][0x28] = bow;
    types->type[bow][12] = 2; /* range above 1: a missile weapon */
    types->type[bow][14] = 0; /* not thrown */
    menu_expect("Aim Quick@Delay View Speed End");
    types->type[bow][14] = 0x14;
    menu_expect("Aim Guard Quick@Delay View Speed End");
    types->type[bow][14] = 0;
    menu_c->items[0][0x2e] = 0x43; /* Champions' field, a hoopak: not read here */
    menu_expect("Aim Quick@Delay View Speed End");
    types->type[bow][12] = 0; /* a sword: not a missile weapon */
    menu_expect("Aim Guard Quick@Delay View Speed End");
    menu_c->items[0][0x2e] = 0;
    menu_close();
}

/* The built text with every item shown, and the split at Quick (the '@'). */
static void test_command_menu_text(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the command menu's text not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    menu_open();
    uint8_t *r = menu_c->record;
    cok_field_set8(L, r, COK_FIELD_ITEM_COUNT, 1);
    menu_c->combat->may_cast = 1;
    r[0x150] = 0x03;
    cok_field_set8_at(L, r, COK_FIELD_CLASS_LEVEL, 0, 1);
    cok_field_set8(L, r, COK_FIELD_RACE, 5);
    cok_field_set8(L, r, COK_FIELD_STATUS, 5);
    menu_expect("Aim Use Cast Turn Guard Quick@Delay Bandage Yell View Speed End");
    menu_close();
}

/* End, Guard, Delay and the keys: Escape does nothing, the keys running out end the run with the
 * turn kept. */
static void test_command_menu_keys(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the command menu's keys not checked");
        return;
    }
    menu_open();
    cok_combat_record *cr = menu_c->combat;
    /* Use's stop (the item's routine, 86b8:0f41) with the item shown. */
    cok_field_set8(R->character, menu_c->record, COK_FIELD_ITEM_COUNT, 1);
    CHECK(!menu_run("U") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "86b8:0f41") != NULL);
    cok_field_set8(R->character, menu_c->record, COK_FIELD_ITEM_COUNT, 0);
    /* Escape gives nothing: the menu shown again, then End. */
    menu_game.vm.status = COK_ECL_OK;
    CHECK(menu_run("\x1b" "E") && menu_game.vm.status == COK_ECL_OK);
    CHECK(script.menus == 1 && script.at == 2);
    CHECK(cr->initiative == 0 && cr->movement == 0 && cr->guarding == 0);
    /* Guard: guarding, the message, and one draw (the message's wait). */
    menu_open();
    cr = menu_c->combat;
    uint32_t before = menu_game.vm.seed, drawn = before;
    cok_tp_random(&drawn, 1000);
    CHECK(menu_run("G") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cr->initiative == 0 && cr->guarding == 1);
    CHECK(strcmp(script.messages, "Guarding;") == 0);
    CHECK(menu_game.vm.seed == drawn);
    /* Guard while forced: no guard, no message, and no draw. */
    menu_open();
    cr = menu_c->combat;
    cr->forced = 1;
    before = menu_game.vm.seed;
    CHECK(menu_run("G") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cr->initiative == 0 && cr->guarding == 0 && script.messages[0] == '\0');
    CHECK(menu_game.vm.seed == before);
    /* Delay: initiative 1, the turn list shifted (c to the end), the index put back one. */
    menu_open();
    cr = menu_c->combat;
    before = menu_game.vm.seed;
    menu_game.combat.turn_index = 1;
    menu_game.combat.turn[1] = menu_c;
    menu_game.combat.turn[2] = &dummy_turn;
    menu_game.combat.turn[3] = NULL;
    CHECK(menu_run("D") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cr->initiative == 1 && menu_game.combat.turn[1] == &dummy_turn);
    CHECK(menu_game.combat.turn[2] == menu_c && menu_game.combat.turn_index == 0);
    CHECK(menu_game.vm.seed == before);
    /* No keys: the run ends with the turn kept (initiative 5). */
    menu_open();
    CHECK(menu_run("") && menu_game.vm.status == COK_ECL_OK && menu_c->combat->initiative == 5);
    menu_close();
}

/* queen_combat_menu's mouse areas (dqk 1bdb:1595 15de-1828, scope section 5): with the mouse on, a
 * click in each of the eight areas round the cell at 3, 4 (and 0, 0) gives its code (0x81-0x88), the
 * cell's edges as the rects have them (the cell's own columns above and below it are in no area: the
 * menu waits on, -1); with the mouse off there are none. A first-row button over area 3 gives the
 * button. Space is 0x20 with the mouse on and off: the bar's filter (space = true) stands for the
 * type-7 control at 1810, which 1595 adds to a fresh list with or without the mouse. */
static int combat_bar(bool mouse, int p, int q, size_t count, int split, int16_t x, int16_t y,
                      const char *keys)
{
    static const char *const bar[] = {"Quick", "Speed", "Delay", "Move"};
    menu_open();
    menu_game.mouse.on = mouse;
    click_x = x;
    click_y = y;
    script.keys = keys;
    script.at = 0;
    menu_game.vm.abort = false;
    return queen_combat_menu(&menu_game, bar, count, split, p, q);
}

static void test_combat_menu_areas(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the command menu's mouse areas not checked");
        return;
    }
    static const struct { int16_t x, y; int code; } at_3_4[] = {
        {50, 90, 0x86}, {110, 30, 0x88}, {150, 90, 0x82}, {110, 130, 0x84},
        {50, 40, 0x87}, {150, 40, 0x81}, {150, 150, 0x83}, {50, 150, 0x85},
        {126, 90, 0x82}, /* the cell's last four columns: area 2 */
        {101, 112, 0x84}, /* over area 7's columns: area 3 is first */
    };
    static const struct { int16_t x, y; int code; } at_0_0[] = {
        {4, 20, 0x86}, {20, 2, 0x88}, {100, 20, 0x82}, {10, 100, 0x84},
        {2, 2, 0x87}, {100, 2, 0x81}, {100, 100, 0x83}, {2, 100, 0x85},
    };
    for (size_t i = 0; i < sizeof at_3_4 / sizeof at_3_4[0]; ++i)
        CHECK(combat_bar(true, 3, 4, 0, -1, at_3_4[i].x, at_3_4[i].y, "\x01") == at_3_4[i].code);
    for (size_t i = 0; i < sizeof at_0_0 / sizeof at_0_0[0]; ++i)
        CHECK(combat_bar(true, 0, 0, 0, -1, at_0_0[i].x, at_0_0[i].y, "\x01") == at_0_0[i].code);
    /* No area: the cell (110, 90), the cell's columns above it (110, 78) and below it (110, 105),
     * and the cell at 0, 0 (20, 20): the keys run out, the menu waits on. */
    CHECK(combat_bar(true, 3, 4, 0, -1, 110, 78, "\x01") == -1);
    CHECK(combat_bar(true, 3, 4, 0, -1, 110, 90, "\x01") == -1);
    CHECK(combat_bar(true, 3, 4, 0, -1, 110, 105, "\x01") == -1);
    CHECK(combat_bar(true, 0, 0, 0, -1, 20, 20, "\x01") == -1);
    /* The mouse off: the same clicks give nothing. */
    CHECK(combat_bar(false, 3, 4, 0, -1, 50, 90, "\x01") == -1);
    CHECK(combat_bar(false, 3, 4, 0, -1, 101, 112, "\x01") == -1);
    /* The first row's button "Delay" (cols 100-148, rows 176-188) over area 3 (rows 108-188). */
    CHECK(combat_bar(true, 3, 4, 4, 3, 110, 180, "\x01") == 2);
    CHECK(combat_bar(false, 3, 4, 4, 3, 110, 180, "\x01") == 2);
    /* Space is 0x20 with the mouse on and off (the filter; the control, see combat_bar). */
    CHECK(combat_bar(true, 3, 4, 4, 3, 0, 0, " ") == 0x20);
    CHECK(combat_bar(false, 3, 4, 4, 3, 0, 0, " ") == 0x20);
    /* queen_commands passes the combatant's cell: CINDER at 5, 5 (her cell 3, 3 on the view, the view
     * at 2, 2), so a click at 90, 20 is area 1 (code 0x88: Move, not yet ported) with the mouse on.
     * Her cell at 0, 0 would have no area there, and the run would wait on (the mouse off gives
     * nothing). */
    menu_open();
    menu_game.mouse.on = true;
    cok_combat *k = &menu_game.combat;
    k->view_x = 2;
    k->view_y = 2;
    click_x = 90;
    click_y = 20;
    CHECK(!menu_run("\x01") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "Move with a direction key") != NULL);
    /* 20, 90 is area 0 (code 0x86) for the cell at 3, 3 and no area for a cell at 3, 0 or 0, 3. */
    click_x = 20;
    click_y = 90;
    CHECK(!menu_run("\x01") && menu_game.vm.status == COK_ECL_UNPORTED);
    menu_open();
    menu_game.mouse.on = false;
    CHECK(menu_run("\x01") && menu_game.vm.status == COK_ECL_OK);
    menu_close();
}

/* Quick, Alt-Q and Alt-M, Speed (dqk 5045:0a2c, 0c0f, 0bd7, 166c): the state each leaves, the
 * wait's one draw, and the keys (Alt-Q is a 0 and its scan code 0x10, Alt-M 0x32). */
static void test_command_menu_quick_speed(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Quick, Alt-M and Speed not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    /* Quick: the computer's, the target on her own side dropped, one draw, then the computer's
     * turn's stop (5bb8:0000). */
    menu_open();
    cok_combat_record *cr = menu_c->combat;
    cr->target = menu_c->record;
    uint32_t drawn = menu_game.vm.seed;
    cok_tp_random(&drawn, 1000);
    CHECK(!menu_run("Q") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "5bb8:0000") != NULL);
    CHECK(cok_field_u8(L, menu_c->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cr->target == NULL && cr->initiative == 5);
    CHECK(menu_game.vm.seed == drawn);
    /* Alt-Q: the turn index less one, initiative 0x14, every record of the combat list taken by
     * the computer (a second party member and a monster, appended as the battle's are), and the
     * turn ends (one draw). */
    menu_open();
    cr = menu_c->combat;
    cok_character *ally = placed(&menu_game, 0);
    cok_character *monster = placed(&menu_game, 1);
    CHECK(cok_field_u8(L, ally->record, COK_FIELD_COMPUTER) == 0);
    CHECK(cok_field_u8(L, monster->record, COK_FIELD_COMPUTER) == 0);
    menu_game.combat.turn_index = 1;
    menu_game.vm.status = COK_ECL_OK;
    drawn = menu_game.vm.seed;
    cok_tp_random(&drawn, 1000);
    CHECK(menu_run("~\x10") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_game.combat.turn_index == 0 && cr->initiative == 0x14);
    CHECK(cok_field_u8(L, menu_c->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cok_field_u8(L, ally->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cok_field_u8(L, monster->record, COK_FIELD_COMPUTER) == 1);
    CHECK(menu_game.vm.seed == drawn);
    /* Alt-M: the toggle and its message ("magic on"), then the turn goes on to the arena (c8c's
     * 5837:12f5, which needs the battle's map: not in this test, so its stop is the check). */
    menu_open();
    CHECK(!menu_game.combat.magic_on);
    CHECK(!menu_run("~\x32") && strstr(menu_game.error, "6beb:12ef") != NULL);
    CHECK(menu_game.combat.magic_on && strcmp(script.messages, "magic on;") == 0);
    CHECK(script.menus == 1 && menu_c->combat->initiative == 5);
    menu_game.vm.status = COK_ECL_OK;
    CHECK(!menu_run("~\x32") && !menu_game.combat.magic_on);
    CHECK(strcmp(script.messages, "magic off;") == 0);
    /* Speed: Slower then Exit; the variable 0x12 up by one, the menu shown again (the arena), then
     * the keys run out. Faster and Escape (1b) leave it as they are. */
    menu_open();
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 5);
    CHECK(!menu_run("SSE") && strstr(menu_game.error, "6beb:12ef") != NULL);
    CHECK(cok_ecl_var(&menu_game.vm, COK_VAR_SPEED) == 6);
    menu_open();
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 5);
    CHECK(!menu_run("SFE") && cok_ecl_var(&menu_game.vm, COK_VAR_SPEED) == 4);
    menu_open();
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 5);
    CHECK(!menu_run("S\x1b") && cok_ecl_var(&menu_game.vm, COK_VAR_SPEED) == 5);
    menu_close();
}

/* The battle's ground for a menu's arena stops (cok_arena_turn needs the map: the tiles and
 * the battle's combat.active, as the arena's tests set them). */
static void battle_map(void)
{
    CHECK(cok_adventure_drawing(&menu_game)->arena_tiles(&menu_game, false, 4));
    menu_game.combat.active = true;
}

/* The battle's say (dqk 177e:156e in mode 5, cok_drawing.arena_say through cok_arena_say): c's
 * name on row 10, the text under it in grey on row 11, and with wait the speed's time (one draw)
 * then the rows 10-21 cleared; without wait the text stays and nothing is drawn. The texts are
 * logged as the say's are: the name, then the text. Skipped without DQKAssets/. */
static void test_arena_say(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's say not checked");
        return;
    }
    menu_open();
    menu_game.vm.mode = 5;
    memset(menu_game.screen.pixels, 0x00, 320 * 200);
    char name[17];
    cok_field_name(R->character, menu_c->record, name);
    uint32_t want = menu_game.vm.seed;
    CHECK(cok_arena_say(&menu_game, menu_c, "HELLO", 10, false));
    CHECK(menu_game.vm.seed == want);
    CHECK(ink_row(&menu_game, 10) && row_has(&menu_game, 23, 38, 11, 0x07));
    CHECK(!ink_row(&menu_game, 12) && !ink_row(&menu_game, 21));
    char logged[160];
    snprintf(logged, sizeof logged, "%s;HELLO;", name);
    CHECK(strcmp(script.prints, logged) == 0);
    /* A second say without wait starts at cell 0x17 of row 11 (1a62:0311's clear homes the
     * cursor), not where the first one ended (cell 28). */
    CHECK(cok_arena_say(&menu_game, menu_c, "HI", 10, false));
    CHECK(row_has(&menu_game, 23, 24, 11, 0x07) && !row_has(&menu_game, 25, 38, 11, 0x07));
    want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    memset(menu_game.screen.pixels, 0x00, 320 * 200);
    script.prints[0] = '\0';
    CHECK(cok_arena_say(&menu_game, menu_c, "HELLO", 10, true));
    CHECK(menu_game.vm.seed == want);
    for (int row = 10; row <= 21; ++row) CHECK(!ink_row(&menu_game, row));
    CHECK(strcmp(script.prints, logged) == 0);
    menu_close();
}

/* The camp's say of a member (member_say: 177e:156e's camp shape, the window cleared and the text
 * from row 19 by 1a62:0311 with its bp+0x12 1): the text starts at cell 1 of row 19 though the
 * cursor was left inside the window. Skipped without DQKAssets/. */
static void test_member_say(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the camp's say not checked");
        return;
    }
    menu_open();
    memset(menu_game.screen.pixels, 0x00, 320 * 200);
    menu_game.vm.cursor = (cok_text_cursor){10, 0x14};
    cok_dark_queen_member_say_for_test(&menu_game, menu_c->record, "HI");
    CHECK(row_has(&menu_game, 1, 2, 19, 0x07) && !row_has(&menu_game, 10, 11, 20, 0x07));
    CHECK(menu_game.vm.cursor.x == 3 && menu_game.vm.cursor.y == 19);
    menu_close();
}

/* Bandage (dqk 5045:0adb: 51c2:0d63(1), then 0b29 End): a dying member who is not hostile is
 * bandaged by the menu's Bandage: its status word 4 and its dying 0, "is bandaged" said (its name
 * and the text logged), one draw, and CINDER's turn ended. Skipped without DQKAssets/. */
static void test_bandage(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Bandage not checked");
        return;
    }
    menu_open();
    menu_game.vm.mode = 5;
    cok_character *ally = placed(&menu_game, 0);
    uint8_t *r = ally->record;
    cok_field_set8(R->character, r, COK_FIELD_STATUS, 5);
    ally->combat->dying = 3;
    menu_expect("Aim Guard Quick@Delay Bandage View Speed End");
    char name[17];
    cok_field_name(R->character, r, name);
    uint32_t drawn = menu_game.vm.seed;
    cok_tp_random(&drawn, 1000);
    CHECK(menu_run("B") && menu_game.vm.status == COK_ECL_OK);
    CHECK(r[0x64] == 4 && r[0x65] == 0 && ally->combat->dying == 0);
    CHECK(menu_game.vm.seed == drawn);
    char logged[160];
    snprintf(logged, sizeof logged, "%s;is bandaged;", name);
    CHECK(strcmp(script.prints, logged) == 0);
    CHECK(menu_c->combat->initiative == 0 && script.menus == 1);
    menu_close();
}

/* Alt-X (dqk 5045:0c5d, 52db:3feb). Without the Helm's password (game->helm false) "That doesn't
 * work" on the bar (one draw), the turn not ended: the menu built again. With the Helm the gods
 * (cok_combat_gods): the hostile monster (word +0x66 1) leaves the map (+0x18b 0, status word 6,
 * size 0) and every record's turn ends, "The Gods intervene!" on the bar, and the menu is not
 * built again. Skipped without DQKAssets/. */
static void test_alt_x(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Alt-X not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    menu_open();
    battle_map();
    uint32_t drawn = menu_game.vm.seed;
    cok_tp_random(&drawn, 1000);
    menu_game.helm = false;
    CHECK(menu_run("~\x2d") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "That doesn't work;") == 0);
    CHECK(script.menus == 2 && menu_game.vm.seed == drawn && menu_c->combat->initiative == 5);
    menu_close();

    menu_open();
    battle_map();
    cok_character *monster = placed(&menu_game, 1);
    place(&menu_game, monster, 9, 9);
    uint8_t *m = monster->record;
    uint8_t n = cok_combat_index(&menu_game.combat, m);
    CHECK(n == 2); /* CINDER is combatant 1 (menu_open) */
    m[0x64] = 1;   /* a status word of 1, not 6 */
    m[0x65] = 1;   /* its high byte, which the gods' word write clears */
    m[0x66] = 1;   /* the hostile word 1 */
    menu_game.helm = true;
    CHECK(menu_run("~\x2d") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8(L, m, COK_FIELD_ACTIVE) == 0 && m[0x18b] == 0);
    CHECK(m[0x64] == 6 && m[0x65] == 0);
    CHECK(menu_game.combat.combatant[n].size == 0);
    CHECK(menu_c->combat->initiative == 0 && monster->combat->initiative == 0);
    CHECK(strcmp(script.messages, "The Gods intervene!;") == 0 && script.menus == 1);
    menu_close();
}

/* Yell (dqk 5045:0ae9, 148f:0005): CINDER as a kender (race 5, 0x59 0) with her menu's Yell. Each
 * hostile member of the other side (word +0x66 1) with status word 0, intelligence above 3 and not
 * helpless makes a save of kind 4 (its saving throw byte 4, 0x8f: one roll of 1d20, a roll of 1
 * fails); one that fails takes 0x8c (0 minutes, value 0xff) and its flash stops (dqk 51c2:0731).
 * The yelled record is CINDER, the damage type is cleared and her turn ends. The seed's first roll
 * is not 1 (checked), so a byte of 1 saves. Skipped without DQKAssets/. */
enum { YELL_SEED = 0x2468ACE1 };

static void yell_menu(void)
{
    menu_open();
    cok_field_set8(R->character, menu_c->record, COK_FIELD_RACE, 5);
    menu_c->record[0x59] = 0;
    menu_c->record[0x67] = 0; /* CINDER's side word 0 */
    menu_game.vm.seed = YELL_SEED;
}

/* A hostile member of the other side (word +0x66 1) with intelligence and saving throw 4. */
static cok_character *yelled_monster(uint8_t intelligence, uint8_t save)
{
    cok_character *m = placed(&menu_game, 1);
    cok_field_set8_at(R->character, m->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE,
                      intelligence);
    cok_field_set8_at(R->character, m->record, COK_FIELD_SAVING_THROW, 4, save);
    return m;
}

static void test_yell(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Yell not checked");
        return;
    }
    /* A monster that saves: one roll, nothing added or said, the damage type cleared, CINDER's turn
     * ended. */
    yell_menu();
    cok_character *m = yelled_monster(10, 1);
    uint32_t after = YELL_SEED;
    CHECK(cok_dice(&after, 1, 20) != 1);
    menu_game.effects.rolls.damage_type = 8;
    CHECK(menu_run("Y") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_game.combat.yelled == menu_c->record);
    CHECK(menu_game.vm.seed == after);
    CHECK(cok_character_find_named(m, R, COK_EFFECT_YELLED_AT) == NULL);
    CHECK(menu_game.effects.rolls.damage_type == 0);
    CHECK(menu_c->combat->initiative == 0 && script.menus == 1 && script.prints[0] == '\0');
    menu_close();

    /* Not tested: intelligence 3; status word 0x100 (its high byte 1, a byte test would roll). No
     * draw either way. */
    yell_menu();
    cok_character *weak = yelled_monster(3, 1);
    CHECK(menu_run("Y") && menu_game.vm.seed == YELL_SEED);
    CHECK(cok_character_find_named(weak, R, COK_EFFECT_YELLED_AT) == NULL);
    menu_close();

    yell_menu();
    cok_character *status = yelled_monster(10, 1);
    status->record[0x65] = 1;
    CHECK(menu_run("Y") && menu_game.vm.seed == YELL_SEED);
    menu_close();

    /* Tested: side word 0x100 against CINDER's 0 (its byte is 0, a byte test would skip it). One
     * draw. */
    yell_menu();
    cok_character *other = yelled_monster(10, 1);
    other->record[0x66] = 0;
    other->record[0x67] = 1;
    uint32_t tested = YELL_SEED;
    cok_dice(&tested, 1, 20);
    CHECK(menu_run("Y") && menu_game.vm.seed == tested);
    menu_close();

    /* A monster that fails (saving throw 0x7f): the add of 0x8c, then the flash, a stop (dqk
     * 51c2:0731) before any text is said; one roll. */
    yell_menu();
    menu_game.vm.mode = 5;
    cok_character *failing = yelled_monster(10, 0x7f);
    uint32_t drawn = YELL_SEED;
    cok_dice(&drawn, 1, 20);
    CHECK(!menu_run("Y"));
    CHECK(menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "an effect's flash") != NULL &&
          strstr(menu_game.error, "dqk 51c2:0731") != NULL);
    CHECK(menu_game.vm.seed == drawn && script.prints[0] == '\0');
    cok_effect *e = cok_character_find_named(failing, R, COK_EFFECT_YELLED_AT);
    CHECK(e != NULL && e->id == 0x8c && e->duration == 0 && e->value == 0xff);
    menu_close();

    /* "is Unaffected" said through the battle's say (dqk 177e:156e): the branch of an effect that
     * a save of kind 1 saved against (cok_cast_add_effect's, as the yell's is kind 0; event 9's
     * cancel has no ported handler here). Nothing added, the name and the text logged, one draw. */
    yell_menu();
    menu_game.vm.mode = 5;
    memset(menu_game.screen.pixels, 0x00, 320 * 200);
    cok_character *saved = yelled_monster(10, 1);
    uint32_t said = YELL_SEED;
    cok_tp_random(&said, 1000);
    CHECK(cok_cast_add_effect(&menu_game, saved, 0x8c, 0, 0xff, false, 1, true, ""));
    CHECK(menu_game.vm.seed == said);
    CHECK(cok_character_find_named(saved, R, COK_EFFECT_YELLED_AT) == NULL);
    char name[17], logged[64];
    cok_field_name(R->character, saved->record, name);
    snprintf(logged, sizeof logged, "%s;is Unaffected;", name);
    CHECK(strcmp(script.prints, logged) == 0);
    menu_close();
}

/* View in a battle (dqk 5045:0af8, 86b8:0b0c, the mode 5 tail 86b8:0d8b-0d97): the sheet's bar is
 * "Items Drop Exit" for a member with items and coins (no Trade in combat); Escape leaves it, then
 * the weapon attacks (52db:0edf), the arena again (51c2:0c94, nothing drawn), the command menu built
 * again, the turn kept, and game->redraw set (177e:1ad1). The keys running out in the sheet end
 * the run with the tail not run. Skipped without DQKAssets/. */
static void view_member(void)
{
    menu_open();
    battle_map();
    menu_game.vm.mode = 5;
    free(menu_c->items); /* one item and coins: Items and Drop on the sheet, Use on the menu */
    menu_c->items = calloc(1, COK_ITEM_SIZE);
    CHECK(menu_c->items != NULL);
    menu_c->item_count = 1;
    cok_field_set8(R->character, menu_c->record, COK_FIELD_ITEM_COUNT, 1);
    menu_c->record[0x4c] = 5;
    menu_game.vm.character = menu_c->record; /* the turn's (round.c's cok_combat_turn) */
    menu_game.redraw = false;
}

static void test_view_combat(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; View in a battle not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    static const char *const sheet = "Aim Use Guard Quick@Delay View Speed End;Items Drop Exit;";
    char again[160];
    snprintf(again, sizeof again, "%sAim Use Guard Quick@Delay View Speed End;", sheet);
    view_member();
    uint8_t *r = menu_c->record;
    uint32_t before = menu_game.vm.seed;
    CHECK(menu_run("V\x1b") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.all, again) == 0);
    CHECK(menu_c->combat->initiative == 5 && menu_game.vm.seed == before);
    CHECK(menu_game.redraw);
    menu_close();

    /* The weapon attacks run after the sheet (52db:0edf): ATTACKS_LEFT index 1 (+0xb3) was 1 and
     * BASE_ATTACKS index 1 is 4, so it is 4 >> 1 (attacked 0) after. */
    view_member();
    r = menu_c->record; /* the last game's record: the previous one is closed */
    cok_field_set8_at(L, r, COK_FIELD_BASE_ATTACKS, 1, 4);
    cok_field_set8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1, 1);
    CHECK(menu_run("V\x1b") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1) == 2);
    menu_close();

    /* The keys run out at the sheet's bar: the run ends with the turn kept, no arena, no redraw. */
    view_member();
    before = menu_game.vm.seed;
    CHECK(menu_run("V") && menu_game.vm.status == COK_ECL_OK && menu_game.vm.abort);
    CHECK(strcmp(script.all, sheet) == 0);
    CHECK(menu_c->combat->initiative == 5 && menu_game.vm.seed == before);
    CHECK(!menu_game.redraw);
    menu_close();

    /* A former paladin (+0xaf) in a battle: View does not stop at the former paladin's rule
     * (177e:21bd), since 314e and 31af give Lay and Cure 0 in mode 5 whatever it says. */
    view_member();
    menu_c->record[0xaf] = 1;
    CHECK(menu_run("V\x1b") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.all, again) == 0);
    CHECK(menu_c->combat->initiative == 5);
    menu_close();

    /* The sheet's Spells is a stop (dqk 849a:2080, view_character's routine), with a memorised
     * spell (+0xce). */
    menu_open();
    menu_game.vm.character = menu_c->record;
    menu_c->record[0xce] = 1;
    CHECK(!menu_run("VS") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "849a:2080") != NULL);
    menu_close();
}

/* View's Items in a battle (dqk 86b8:16bc): a readied item of type 0x8c (+0x37, +0x32 set) ends the
 * Items menu after any choice, Exit too, with done set (86b8:16d1): the turn is over, so the sheet
 * is not drawn again and the command menu is not built again; the weapon attacks still run
 * (5045:0af8's 52db:0edf). Not readied, or another type, Exit gives the sheet again, and Escape the
 * turn's tail with the menu built again. Skipped without DQKAssets/. */
static void test_view_items_combat(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; View's Items in a battle not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    static const char *const sheet = "Aim Use Guard Quick@Delay View Speed End;Items Drop Exit;";
    static const char *const items = "Rdy Use Drop Halve Join Exit;"; /* 86b8:0f41's mode 5 menu */
    static const char *const again = "Aim Use Guard Quick@Delay View Speed End;";
    char want[512];

    /* A readied 0x8c ends the turn: VIE (View, Items, Exit). The keys are all used, the turn kept
     * (initiative 5, no End), the weapon attacks run (ATTACKS_LEFT index 1, 1 to 2). */
    view_member();
    menu_c->items[0][0x37] = 0x8c;
    menu_c->items[0][0x32] = 1;
    uint8_t *r = menu_c->record;
    cok_field_set8_at(L, r, COK_FIELD_BASE_ATTACKS, 1, 4);
    cok_field_set8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1, 1);
    CHECK(menu_run("VIE") && menu_game.vm.status == COK_ECL_OK && !menu_game.vm.abort);
    snprintf(want, sizeof want, "%s%s", sheet, items);
    CHECK(strcmp(script.all, want) == 0);
    CHECK(menu_c->combat->initiative == 5);
    CHECK(cok_field_u8_at(L, r, COK_FIELD_ATTACKS_LEFT, 1) == 2);
    menu_close();

    /* Without it (0x8c not readied, then another readied type): Exit gives the sheet again, Escape
     * the tail and the command menu built again, where the keys run out. */
    for (int k = 0; k < 2; ++k) {
        view_member();
        menu_c->items[0][0x37] = k == 0 ? 0x8c : 0x10;
        menu_c->items[0][0x32] = k == 0 ? 0 : 1;
        CHECK(menu_run("VIE\x1b") && menu_game.vm.status == COK_ECL_OK);
        snprintf(want, sizeof want, "%s%sItems Drop Exit;%s", sheet, items, again);
        CHECK(strcmp(script.all, want) == 0);
        CHECK(menu_c->combat->initiative == 5);
        menu_close();
    }
}

/* The bar message as its wait shows it (hooks.delay): 177e:153c's 1a62:0046 at x 0 boxes it from
 * cell 1, the black edges at pixels 7 and 8 + 8 * 8 (the text's 8 characters), the text from 8
 * in white on grey; the bar's colours left of 7. dqk_bar checks the original's (the screen while
 * 177e:1560 spins). */
static uint8_t bar_shot[320 * 200];
static bool bar_shot_taken;

static void shoot_bar(cok_adventure *game, unsigned ms, void *context)
{
    (void)ms;
    (void)context;
    if (bar_shot_taken) return;
    memcpy(bar_shot, game->screen.pixels, sizeof bar_shot);
    bar_shot_taken = true;
}

static void test_bar_message_place(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the bar message's place not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    hooks.delay = shoot_bar;
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    bar_shot_taken = false;
    queen_bar_message(&game, "Guarding");
    CHECK(bar_shot_taken);
    for (int y = 187; y < 198; ++y) {
        CHECK(bar_shot[y * 320 + 7] == 0 && bar_shot[y * 320 + 72] == 0);
        for (int x = 0; x < 7; ++x) CHECK(bar_shot[y * 320 + x] != 0);
    }
    for (int x = 7; x <= 72; ++x)
        CHECK(bar_shot[187 * 320 + x] == 0 && bar_shot[197 * 320 + x] == 0);
    CHECK(bar_shot[192 * 320 + 64] != 0);
    cok_adventure_close(&game);
}

/* The bar message's wait draws once (1df9:052a's random(1000) at 058a). */
static void test_pass_draw(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the wait's draw not checked");
        return;
    }
    menu_open();
    uint32_t want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    queen_bar_message(&menu_game, "Guarding");
    CHECK(menu_game.vm.seed == want);
    /* ADD EP's wait (1a62:0949 -> 1df9:052a): one draw for a time, none for none. */
    want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    cok_dark_queen_wait(&menu_game, 100);
    CHECK(menu_game.vm.seed == want);
    want = menu_game.vm.seed;
    cok_dark_queen_wait(&menu_game, 0);
    CHECK(menu_game.vm.seed == want);
    menu_close();
}

/* The count of pixels of colour in x1 <= x < x2, y1 <= y < y2. */
static int count_px(const cok_adventure *game, int x1, int y1, int x2, int y2, uint8_t colour)
{
    int n = 0;
    for (int y = y1; y < y2; ++y)
        for (int x = x1; x < x2; ++x)
            if (game->screen.pixels[y * 320 + x] == colour) ++n;
    return n;
}

/* The computer's row 24 line (ai.h's cok_ai_moves_left_text): the trailing space is left off from
 * ten up for Dark Queen's (5bb8:06b0's 5e0a), kept for Champions' (3afb:095a's "%u "). */
static void test_moves_left_text(void)
{
    char text[48];
    cok_ai_moves_left_text(&R->fight, 18, text, sizeof text);
    CHECK(strcmp(text, "Move/Attack, Move Left = 9 ") == 0);
    cok_ai_moves_left_text(&R->fight, 20, text, sizeof text);
    CHECK(strcmp(text, "Move/Attack, Move Left = 10") == 0);
    cok_ai_moves_left_text(&cok_rules_champions.fight, 20, text, sizeof text);
    CHECK(strcmp(text, "Move/Attack, Move Left = 10 ") == 0);
}

/* The battle's clear (dqk 177e:1665, cok_drawing.arena_clear_text): cells 0x17-0x26 by 0x0a-0x15
 * in the window's grey, the rest of the screen kept. Without the hook the shared clear would
 * black them. Skipped without DQKAssets/. */
static void test_battle_clear(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's clear not checked");
        return;
    }
    menu_open();
    menu_game.vm.mode = 5;
    memset(menu_game.screen.pixels, 0x07, 320 * 200);
    cok_arena_clear_text(&menu_game);
    int bad = 0;
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 320; ++x) {
            bool inside = x >= 8 * 0x17 && x < 8 * 0x27 && y >= 8 * 0x0a && y < 8 * 0x16;
            if (menu_game.screen.pixels[y * 320 + x] != (inside ? 0x17 : 0x07)) ++bad;
        }
    CHECK(bad == 0);
    menu_close();
}

/* The battle's text (dqk 1a62:0046, cok_drawing.arena_text): on row 24 boxed in black from cell 1
 * (prompt_box), and a fg of 7 with a background of 0 on the bar is white (15) on the grey; above
 * it the text in its colours on the grey. Skipped without DQKAssets/. */
static void test_battle_text(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's text not checked");
        return;
    }
    menu_open();
    menu_game.vm.mode = 5;
    memset(menu_game.screen.pixels, 0x05, 320 * 200);
    CHECK(cok_arena_text(&menu_game, 0, 0x18, "MOVE", 7, 0));
    const uint8_t *px = menu_game.screen.pixels;
    CHECK(px[187 * 320 + 7] == 0 && px[187 * 320 + 40] == 0 && px[187 * 320 + 41] == 0x05);
    CHECK(px[188 * 320 + 8] == 0x17 && px[188 * 320 + 39] == 0x17);
    CHECK(count_px(&menu_game, 8, 189, 40, 200, 0x0f) > 0);
    CHECK(count_px(&menu_game, 0, 187, 320, 200, 0x07) == 0);
    /* Above the bar, "HI" in 7 on the grey in cells 23 and 24 of row 12 (1a62:0046's attribute). */
    memset(menu_game.screen.pixels, 0x05, 320 * 200);
    CHECK(cok_arena_text(&menu_game, 0x17, 12, "HI", 7, 0));
    CHECK(count_px(&menu_game, 184, 96, 200, 104, 0x07) > 0);
    CHECK(count_px(&menu_game, 184, 96, 200, 104, 0x17) > 0);
    CHECK(count_px(&menu_game, 184, 96, 200, 104, 0x0f) == 0);
    CHECK(count_px(&menu_game, 184, 96, 200, 104, 0x05) == 0);
    menu_close();
}

/* The battle's bottom bar blanked at the computer's turn's start (dqk 1bdb:036c,
 * cok_drawing.clear_bar): rows 186-199 as the bar is drawn. Skipped without DQKAssets/. */
static void test_battle_bar(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's bar not checked");
        return;
    }
    menu_open();
    static uint8_t drawn[14 * 320];
    memset(menu_game.screen.pixels, 0x05, 320 * 200);
    queen_bottom_bar(&menu_game);
    memcpy(drawn, menu_game.screen.pixels + 186 * 320, sizeof drawn);
    memset(menu_game.screen.pixels, 0x05, 320 * 200);
    cok_arena_clear_bar(&menu_game);
    CHECK(memcmp(drawn, menu_game.screen.pixels + 186 * 320, sizeof drawn) == 0);
    menu_close();
}

/* The battle's notice and waits (dqk 177e:153c, 1df9:052a, 1a62:0949): the notice draws once for
 * the speed's time; a wait draws once for a time and not for none; the pause draws once for
 * variable 0x12 times 100 ms and not for 0. Skipped without DQKAssets/. */
static void test_battle_waits(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's waits not checked");
        return;
    }
    menu_open();
    menu_game.vm.mode = 5;
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 4);
    CHECK(cok_ecl_load_var(&menu_game.vm, 0x12) == 4);
    script.messages[0] = '\0';
    uint32_t want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    cok_arena_notice(&menu_game, "Guarding");
    CHECK(menu_game.vm.seed == want && strcmp(script.messages, "Guarding;") == 0);
    want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    cok_arena_wait(&menu_game, 100);
    CHECK(menu_game.vm.seed == want);
    want = menu_game.vm.seed;
    cok_arena_wait(&menu_game, 0);
    CHECK(menu_game.vm.seed == want);
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 0);
    want = menu_game.vm.seed;
    cok_arena_pause(&menu_game);
    CHECK(menu_game.vm.seed == want);
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 4);
    want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    cok_arena_pause(&menu_game);
    CHECK(menu_game.vm.seed == want);
    menu_close();
}

int main(void)
{
    test_wilderness();
    test_layout_distance();
    test_relayout_side();
    test_tables();
    test_combat_tables();
    test_magic_ids();
    test_hit_points();
    test_experience();
    test_party_again();
    test_added();
    test_loaded_icons();
    test_join_icons();
    test_battle_icons();
    test_loaded_slots();
    test_combat_frame();
    test_combat_panel();
    test_round_start_queen();
    test_turn_dispatch();
    test_round_end();
    test_timer_walk();
    test_command_menu_items();
    test_command_menu_text();
    test_command_menu_keys();
    test_combat_menu_areas();
    test_command_menu_quick_speed();
    test_arena_say();
    test_member_say();
    test_bandage();
    test_alt_x();
    test_yell();
    test_view_combat();
    test_view_items_combat();
    test_pass_draw();
    test_bar_message_place();
    test_text_delay_state();
    test_moves_left_text();
    test_battle_clear();
    test_battle_text();
    test_battle_bar();
    test_battle_waits();
    puts("dark queen tests passed");
    return 0;
}
