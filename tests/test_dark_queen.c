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
 * (+0x18b) stops at the command menu (5045:08cf), a computer's turn at 5bb8:0000, and a member
 * that cannot act is ended at once (08cf's 51c2:0c11: initiative, spell, guard and movement
 * zeroed). --combat's stubs do not pass over those stops. The round's end, the turn's tail and the
 * battle's end are cited stops too. */
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
    /* A spell pending is cast first (5045:08e3, no stop): see test_pending_act. */
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
    /* A computer's turn is 5bb8:0000 (queen_computer): with no enemy the pick finds none, and the
     * turn is guarded and ends (5bb8:0209's guard, 0fec). */
    game.vm.status = COK_ECL_OK;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 1);
    c->combat->initiative = 5;
    game.vm.character = r; /* round.c's cok_combat_turn (933): the View's record for the weapon */
    CHECK(cok_combat_act(&game, c, true) && game.vm.status == COK_ECL_OK);
    CHECK(c->combat->initiative == 0);
    c->combat->initiative = 5;
    /* --combat's stubs do not pass over the stops: the party's menu (no keys: kept), the
     * computer's turn. */
    static const cok_combat_stub stubs[] = {COK_COMBAT_PASS, COK_COMBAT_MELEE};
    for (size_t i = 0; i < sizeof stubs / sizeof stubs[0]; ++i) {
        game.combat_stub = stubs[i];
        game.vm.status = COK_ECL_OK;
        CHECK(cok_combat_act(&game, c, false) && game.vm.status == COK_ECL_OK);
        CHECK(c->combat->initiative == 5);
        game.vm.status = COK_ECL_OK;
        CHECK(cok_combat_act(&game, c, true) && game.vm.status == COK_ECL_OK);
        c->combat->initiative = 5;
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

/* The "combat: seed N" line that round.c's rounds logs after its loop for --combat auto (eclplay
 * prints it as "combat: seed N"). CINDER is not active, so no side stands and the battle is over
 * before any round: rounds goes to its tail (the line) and the battle's end. */
static char seed_line[32];
static uint32_t seed_seen;

static void seed_log(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)context;
    if (strcmp(kind, "combat") != 0 || strncmp(text, "seed ", 5) != 0) return;
    snprintf(seed_line, sizeof seed_line, "%s", text);
    seed_seen = game->vm.seed;
}

static void test_auto_seed_line(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the battle's seed line not checked");
        return;
    }
    static cok_adventure game;
    cok_keyboard keys = {no_keys, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {0};
    hooks.log = seed_log;
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    const cok_character_layout *L = R->character;
    cok_character *c = dqk_member(&game, "'CINDER'");
    CHECK(cok_party_add(&game.party, R, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    uint8_t *r = c->record;
    cok_field_set8(L, r, COK_FIELD_ACTIVE, 0);
    cok_field_set8(L, r, COK_FIELD_HOSTILE, 0);
    cok_field_set8(L, r, COK_FIELD_COMPUTER, 0);
    cok_field_set8(L, r, COK_FIELD_STATUS, 0);
    game.combat_stub = COK_COMBAT_AUTO;
    game.wall_ids[0] = 4; /* the wall set the battle's tiles load (as the arena tests) */
    game.vm.seed = 1;
    game.vm.status = COK_ECL_OK;
    game.vm.abort = false;
    game.input_ended = false;
    seed_line[0] = '\0';
    CHECK(cok_combat_battle(&game) && game.vm.status == COK_ECL_OK);
    /* The line, as the RandSeed at the tail: the setup's draws (the battle's tiles and placement)
     * take the seed from 1 to this. */
    CHECK(strcmp(seed_line, "seed 1204427034") == 0 && seed_seen == 1204427034u);
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
        COK_DATA_WAYS,
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
    /* The spell engine's and the fight's words (queen_cast.c), each the C string at its DS offset: the
     * damage's "takes %d%s" with " points of damage " (the amount as the word), "takes 1 point of
     * damage " for 1, the source suffixes "%sfrom Fire" (Cold, Electricity, Acid), "%sfrom Magic",
     * "Goes Down", "%s and is Dying", "is killed", "lost a spell", the heal's two texts and the
     * flash's "is Unaffected" (dqk 148f:2214, 177e:187a, 51c2:0731). */
    static const struct { unsigned off; const char *text; } words[] = {
        {0x0c7, "lost a spell"},   {0x0fc, "is killed"},          {0x120, "takes %d%s"},
        {0x12b, " points of damage "}, {0x13e, "takes 1 point of damage "},
        {0x157, "%sfrom Fire"},    {0x163, "%sfrom Cold"},        {0x16f, "%sfrom Electricity"},
        {0x182, "%sfrom Acid"},    {0x18e, "%sfrom Magic"},       {0x19b, "Goes Down"},
        {0x1a5, "%s and is Dying"}, {0x1b5, "is Unaffected"},     {0x263, "is fully healed"},
        {0x273, "is partially healed"},
    };
    for (size_t i = 0; i < sizeof words / sizeof *words; ++i)
        CHECK(strcmp((const char *)exe + ds + words[i].off, words[i].text) == 0);
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
            if (id == 0x64 || id == 0xc3 || id == 0x69) /* ported: 78e4:14d1, 4949 and 1aa2 */
                CHECK(h->handler != COK_FX_MISSING && h->unported == NULL &&
                      sg == 0x78e4 && so == (id == 0x64 ? 0x14d1 : id == 0xc3 ? 0x4949 : 0x1aa2));
            else
                CHECK(h->handler == COK_FX_MISSING && h->unported != NULL && strcmp(h->unported, cite) == 0);
        }
    }
    /* The effect names the code uses (rules_dark_queen_magic.c): CHARMED, INVISIBLE, BERSERK,
     * and 017d's FRIENDLY, ENLARGED and STRENGTH have their ids, the yell's YELLED_AT, and the
     * use-up's HAMMER (52db:1b26); every other name is 0 (none). */
    CHECK(R->effects.ids == cok_dark_queen_effect_ids);
    CHECK(R->effects.ids[COK_EFFECT_YELLED_AT] == 0x8c &&
          R->effects.ids[COK_EFFECT_HAMMER] == 0x17);
    CHECK(R->effects.ids[COK_EFFECT_CHARMED] == 0x0b && R->effects.ids[COK_EFFECT_BERSERK] == 0x4d);
    CHECK(R->effects.ids[COK_EFFECT_INVISIBLE] == 0x19);
    CHECK(R->effects.ids[COK_EFFECT_FRIENDLY] == 0x0e && R->effects.ids[COK_EFFECT_ENLARGED] == 0x0c);
    CHECK(R->effects.ids[COK_EFFECT_STRENGTH] == 0x26);
    for (unsigned n = 0; n < COK_EFFECT_NAMES; ++n)
        if (n != COK_EFFECT_CHARMED && n != COK_EFFECT_BERSERK && n != COK_EFFECT_INVISIBLE &&
            n != COK_EFFECT_FRIENDLY && n != COK_EFFECT_ENLARGED && n != COK_EFFECT_STRENGTH &&
            n != COK_EFFECT_YELLED_AT && n != COK_EFFECT_HAMMER)
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
    char codes[64];     /* the codes logged ("code: 0x87" lines), each ended with ';' */
    char rows[256];     /* the list's rows logged ("item", "heading"), each ended with ';' */
    char prints[128];   /* the texts said (the "print" lines), each ended with ';' */
    char sounds[64];    /* the sounds (the "sound" lines), each ended with ';' */
    bool pending;       /* a key waits while keys are left (the computer's keys' test only) */
    bool once;          /* a key read clears pending: the next waits only once the test sets it */
    int flushes;        /* the keyboard's flushes */
    int delays;         /* the delays waited (hooks.delay) */
    unsigned delay_ms;  /* the first one's ms */
    uint32_t delay_seed; /* RandSeed when the first one began */
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
    if (s->once) s->pending = false;
    if (c == '\x01') {
        menu_game.pointer.x = click_x;
        menu_game.pointer.y = click_y;
        return 0;
    }
    return c == '~' ? 0 : (unsigned char)c;
}

static bool menu_pending(cok_adventure *game, void *context)
{
    (void)game;
    menu_script *s = context;
    return s->pending && s->keys[s->at] != '\0';
}

static void menu_flush(cok_adventure *game, void *context)
{
    (void)game;
    ((menu_script *)context)->flushes++;
}

static void menu_delay(cok_adventure *game, unsigned ms, void *context)
{
    menu_script *s = context;
    if (s->delays++ == 0) {
        s->delay_ms = ms;
        s->delay_seed = game->vm.seed;
    }
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
    } else if (strcmp(kind, "sound") == 0) {
        size_t used = strlen(s->sounds);
        snprintf(s->sounds + used, sizeof s->sounds - used, "%s;", text);
    } else if (strcmp(kind, "code") == 0) {
        size_t used = strlen(s->codes);
        snprintf(s->codes + used, sizeof s->codes - used, "%s;", text);
    } else if (strcmp(kind, "item") == 0 || strcmp(kind, "heading") == 0) {
        size_t used = strlen(s->rows);
        snprintf(s->rows + used, sizeof s->rows - used, "%s;", text);
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
    hooks.key_pending = menu_pending;
    hooks.flush = menu_flush;
    hooks.delay = menu_delay;
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
    script.codes[0] = '\0';
    script.delays = 0;
}

/* Runs queen_commands on CINDER with keys and pressed (command.c's: a scan code the first read
 * gives, 0 for none), from a fresh menu's state (the keys' queue reset). */
static bool menu_run_pressed(const char *keys, uint8_t pressed)
{
    script.keys = keys;
    script.at = 0;
    script.menus = 0;
    script.menu[0] = '\0';
    script.all[0] = '\0';
    script.messages[0] = '\0';
    script.prints[0] = '\0';
    script.codes[0] = '\0';
    script.delays = 0;
    menu_game.vm.status = COK_ECL_OK;
    menu_game.vm.abort = false;
    return queen_commands(&menu_game, menu_c, pressed);
}

static bool menu_run(const char *keys)
{
    return menu_run_pressed(keys, 0);
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

/* Turn (dqk 08cf's 0x9c7, 52db:1117, 0b29): a cleric with fewer turns than undead turns, the
 * search finds none here (CINDER alone), so "Nothing Happens..." and the turn is over (done, the
 * turn counted, the attack-over flag set); with as many turns as undead the item is not built and
 * T does nothing (the menu again, the turn kept). */
static void test_command_turn(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the command Turn not checked");
        return;
    }
    const cok_character_layout *L = R->character;
    menu_open();
    cok_field_set8_at(L, menu_c->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    menu_game.undead = 1;
    CHECK(menu_run("T") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->turns == 1 && menu_c->combat->initiative == 0 && menu_game.combat.attack_over);
    CHECK(strstr(script.prints, "turns undead...") != NULL);
    CHECK(strcmp(script.messages, "Nothing Happens...;") == 0);
    menu_open();
    cok_field_set8_at(L, menu_c->record, COK_FIELD_CLASS_LEVEL, 0, 3);
    menu_game.undead = 1;
    menu_c->combat->turns = 1; /* as many as undead: Turn is not built */
    CHECK(menu_run("T") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->turns == 1 && menu_c->combat->initiative == 5 && script.prints[0] == '\0');
    CHECK(strcmp(script.menu, "Aim Guard Quick@Delay View Speed End") == 0);
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
    return queen_combat_menu(&menu_game, "", bar, count, split, p, q, 0);
}

static void battle_map(void);

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
     * at 2, 2), so a click at 90, 20 is area 1 (code 0x88: Move, up) with the mouse on. She steps to
     * 5, 4 and the Move menu waits on (the keys run out: the turn kept). Her cell at 0, 0 would have
     * no area there, and the run would wait on (the mouse off gives nothing). */
    menu_open();
    battle_map();
    menu_game.mouse.on = true;
    cok_combat *k = &menu_game.combat;
    k->view_x = 2;
    k->view_y = 2;
    click_x = 90;
    click_y = 20;
    CHECK(menu_run("\x01") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.codes, "0x88;") == 0);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cok_combat_y(k, menu_c->record) == 4);
    CHECK(menu_c->combat->initiative == 5);
    /* 20, 90 is area 0 (code 0x86, left) for the cell at 3, 3: from 5, 5 she steps to 4, 5. */
    menu_open();
    battle_map();
    menu_game.mouse.on = true;
    k->view_x = 2;
    k->view_y = 2;
    click_x = 20;
    click_y = 90;
    CHECK(menu_run("\x01") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.codes, "0x86;") == 0);
    CHECK(cok_combat_x(k, menu_c->record) == 4 && cok_combat_y(k, menu_c->record) == 5);
    menu_open();
    menu_game.mouse.on = false;
    CHECK(menu_run("\x01") && menu_game.vm.status == COK_ECL_OK);
    menu_close();
}

/* Move (dqk 5045:1099, 15a3, 2aef) from the command menu, through queen_commands: "~M" (Right,
 * 0x82) takes one step; the Move menu's Undo ("U") and Escape put her back, its Done ("D") keeps the
 * step and shows the command menu again. A cost above the movement says "can't go there", and Done
 * under 2 movement leaves 0. A combatant in the cell is melee by the step: the ally question first
 * for one on her side (No: no attack; Yes: var 0x36 set, the attack), a foe attacked; a missile
 * weapon not thrown is "Not with that weapon". A wall in the cell asks "flee ? ". An enemy beside
 * the square left takes its attack of opportunity before the step; a step onto a cloud's square
 * (0x1d) stops at the unported square test; a helpless mover's turn ends after its step. The keys
 * running out at the Move menu end the run with the turn kept. The Move menu's prompt is drawn by
 * queen_combat_menu (dark_queen.c), not checked here: no test reads its text, and the DOSBox move
 * flow compares no Move-menu screen (only the mouse flow's "MOVE LEFT = 10" is compared). */
/* The foes the move test makes (WALKERKE's record, a foe or an ally by its +0x66): kept to be freed
 * after the test's last menu. */
static cok_character *foes[8];
static size_t foe_count;

static cok_character *foe_of(void)
{
    assert(foe_count < sizeof foes / sizeof foes[0]);
    foes[foe_count] = dqk_member(&menu_game, "WALKERKE");
    return foes[foe_count++];
}

static void test_command_move(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Move not checked");
        return;
    }
    cok_combat *k = &menu_game.combat;
    cok_combat_record *cr;
    const uint8_t floor_cost = cok_combat_terrain(R, COK_COMBAT_FLOOR).cost;
    /* Right and Done: one square right (floor, right is even: twice its cost), the command menu
     * again (two builds). */
    menu_open();
    battle_map();
    cr = menu_c->combat;
    uint8_t m0 = cr->movement, f0 = cr->facing;
    CHECK(menu_run("~MD") && menu_game.vm.status == COK_ECL_OK);
    CHECK(script.menus == 2);
    CHECK(cok_combat_x(k, menu_c->record) == 6 && cok_combat_y(k, menu_c->record) == 5);
    CHECK(cr->movement == (uint8_t)(m0 - 2 * floor_cost) && cr->facing == 2);
    /* Undo puts her back: square, movement and facing as they were. Escape does the same. */
    menu_open();
    battle_map();
    cr = menu_c->combat;
    CHECK(menu_run("~MU") && menu_game.vm.status == COK_ECL_OK && script.menus == 2);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cok_combat_y(k, menu_c->record) == 5);
    CHECK(cr->movement == m0 && cr->facing == f0);
    menu_open();
    battle_map();
    cr = menu_c->combat;
    CHECK(menu_run("~M\x1b") && menu_game.vm.status == COK_ECL_OK && script.menus == 2);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cok_combat_y(k, menu_c->record) == 5);
    CHECK(cr->movement == m0 && cr->facing == f0);
    menu_open();
    battle_map();
    cr = menu_c->combat;
    CHECK(menu_run("~ID") && menu_game.vm.status == COK_ECL_OK && script.menus == 2);
    CHECK(cok_combat_x(k, menu_c->record) == 6 && cok_combat_y(k, menu_c->record) == 4);
    CHECK(cr->movement == (uint8_t)(m0 - 3 * floor_cost) && cr->facing == 1);
    /* The gate: with two floor squares of movement, a diagonal (three) says "can't go there" and
     * a straight step (two) is taken, leaving none. */
    menu_open();
    battle_map();
    menu_c->combat->movement = (uint8_t)(2 * floor_cost);
    CHECK(menu_run("~ID") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "can't go there;") == 0 && cok_combat_x(k, menu_c->record) == 5);
    menu_open();
    battle_map();
    menu_c->combat->movement = (uint8_t)(2 * floor_cost);
    CHECK(menu_run("~MD") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_x(k, menu_c->record) == 6 && menu_c->combat->movement == 0);
    menu_open();
    battle_map();
    cr = menu_c->combat;
    CHECK(cok_character_add_effect(menu_c, 0xd5, 0, 0, false) != NULL);
    CHECK(menu_run("~MU") && menu_game.vm.status == COK_ECL_OK && script.menus == 2);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cr->facing == 2);
    /* A cost above the movement: "can't go there" (one message, one wait), no step, and Done with
     * under 2 movement leaves 0. */
    menu_open();
    battle_map();
    cr = menu_c->combat;
    cr->movement = 1;
    CHECK(menu_run("~MD") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "can't go there;") == 0 && script.delays == 1);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cr->movement == 0);
    /* A wall (terrain 0) to her right: "flee ? ", No keeps the turn (the Move menu again), Yes
     * flees (the turn ends: no more menu). */
    menu_open();
    battle_map();
    k->cells[5][6] = 0;
    CHECK(menu_run("~MN") && menu_game.vm.status == COK_ECL_OK && script.menus == 1);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && menu_c->combat->initiative == 5);
    menu_open();
    battle_map();
    k->cells[5][6] = 0;
    CHECK(menu_run("~MY") && menu_game.vm.status == COK_ECL_OK && script.menus == 1);
    /* She fled (52db:0e26: no enemy, so away) and her turn ended: not the Move menu's keys out. */
    CHECK(cok_field_u8(R->character, menu_c->record, COK_FIELD_ACTIVE) == 0 &&
          menu_c->combat->initiative == 0 && !menu_game.vm.abort);
    /* A foe (+0x66 1) in the square to her right: melee by the step. The attack runs (the seed
     * moves), the target kept, and the turn goes on with the Move menu. */
    menu_open();
    battle_map();
    cok_character *foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    CHECK(foe->combat != NULL);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 1);
    place(&menu_game, foe, 6, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    uint32_t seed = menu_game.vm.seed;
    CHECK(menu_run("~M") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->target == foe->record && menu_game.vm.seed != seed);
    CHECK(cok_combat_x(k, menu_c->record) == 5 && cok_combat_y(k, menu_c->record) == 5);
    /* An ally (+0x66 0) on her side: "attack ally? ", No attacks nobody and asks nothing more;
     * Yes sets var 0x36 and attacks. */
    menu_open();
    battle_map();
    foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 0);
    place(&menu_game, foe, 6, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    seed = menu_game.vm.seed;
    CHECK(menu_run("~MN") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->target == NULL && menu_game.vm.seed == seed);
    CHECK(cok_ecl_var(&menu_game.vm, COK_VAR_FLED) == 0);
    menu_open();
    battle_map();
    foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 0);
    place(&menu_game, foe, 6, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    k->sides[0] = k->sides[1] = 0xee;
    CHECK(menu_run("~MY") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_ecl_var(&menu_game.vm, COK_VAR_FLED) == 1 && menu_c->combat->target == foe->record);
    CHECK(k->sides[0] != 0xee && k->sides[1] != 0xee); /* 51c2:0920: the sides counted */
    /* queen_attack_ally itself (no keys: a question asked would be a no): the computer's record
     * goes without asking; and the sides are words (51c2:08fd): one with +0x67 set is no ally. */
    script.keys = "";
    script.at = 0;
    menu_game.vm.abort = false;
    bool go = false;
    cok_field_set8(R->character, menu_c->record, COK_FIELD_COMPUTER, 1);
    CHECK(queen_attack_ally(&menu_game, menu_c, foe, &go) && go);
    cok_field_set8(R->character, menu_c->record, COK_FIELD_COMPUTER, 0);
    menu_game.vm.abort = false;
    CHECK(queen_attack_ally(&menu_game, menu_c, foe, &go) && !go);
    menu_game.vm.abort = false;
    foe->record[cok_field_offset(R->character, COK_FIELD_HOSTILE) + 1] = 1;
    CHECK(queen_attack_ally(&menu_game, menu_c, foe, &go) && go);
    foe->record[cok_field_offset(R->character, COK_FIELD_HOSTILE) + 1] = 0;
    /* A missile weapon not thrown (a bow, as the Guard test's): "Not with that weapon" and no
     * attack; with var 0x3c (underwater) set, "Can't use that underwater" first. */
    menu_open();
    battle_map();
    cok_item_types *types = &menu_game.item_types;
    free(menu_c->items);
    menu_c->items = calloc(1, COK_ITEM_SIZE);
    CHECK(menu_c->items != NULL);
    menu_c->item_count = 1;
    menu_c->slots[0] = 1;
    uint8_t bow = 5;
    menu_c->items[0][0x28] = bow;
    types->type[bow][12] = 2;
    types->type[bow][14] = 0;
    foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 1);
    place(&menu_game, foe, 6, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    CHECK(menu_run("~M") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "Not with that weapon;") == 0 && menu_c->combat->target == NULL);
    menu_open();
    battle_map();
    types = &menu_game.item_types;
    free(menu_c->items);
    menu_c->items = calloc(1, COK_ITEM_SIZE);
    menu_c->item_count = 1;
    menu_c->slots[0] = 1;
    menu_c->items[0][0x28] = bow;
    types->type[bow][12] = 2;
    types->type[bow][14] = 0;
    foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 1);
    place(&menu_game, foe, 6, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(menu_run("~M") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "Can't use that underwater;Not with that weapon;") == 0);
    /* An enemy beside the square she leaves (4, 5) and none beside the square she steps to (6, 5):
     * its attack of opportunity (attack_slot set) runs before the step, which is then taken. */
    menu_open();
    battle_map();
    foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    CHECK(foe->combat != NULL);
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, 1);
    place(&menu_game, foe, 4, 5);
    CHECK(queen_added_icons(&menu_game, foe->record)); /* the foe's body icon (its slot) */
    CHECK(menu_run("~M") && menu_game.vm.status == COK_ECL_OK);
    CHECK(foe->combat->attack_slot != 0 && cok_combat_x(k, menu_c->record) == 6);
    /* A step onto a cloud's square (terrain 0x1d, which a step stops at: 148f:12ab, not ported):
     * the step itself is taken, then the stop is the run's unported. */
    menu_open();
    battle_map();
    k->cells[5][6] = 0x1d;
    CHECK(!menu_run("~MD") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(cok_combat_x(k, menu_c->record) == 6);
    /* A helpless mover (its effect, which ends the turn when it is put on, so the movement and
     * initiative are set back after it): the step is taken, then the turn ends (one menu, the Move
     * menu not shown again; initiative 0). */
    menu_open();
    battle_map();
    cr = menu_c->combat;
    uint8_t movement_before = cr->movement;
    CHECK(cok_character_add_effect(menu_c, cok_data_u8_at(R->data, COK_DATA_HELPLESS, 0), 0, 0, false) !=
          NULL);
    CHECK(cok_combat_helpless(menu_c, R));
    cr->movement = movement_before;
    cr->initiative = 5;
    CHECK(menu_run("~MD") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_combat_x(k, menu_c->record) == 6 && cr->initiative == 0 && script.menus == 1);
    /* No movement: the cost check refuses the step (every terrain's cost is 2 or more), so no step. */
    menu_open();
    battle_map();
    menu_c->combat->movement = 0;
    CHECK(menu_run("~MD") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.messages, "can't go there;") == 0 && cok_combat_x(k, menu_c->record) == 5);
    menu_close();
    for (size_t i = 0; i < foe_count; ++i) {
        cok_character_free(foes[i]);
        free(foes[i]);
    }
    foe_count = 0;
}

/* Aim (dqk 52db:37dd, 36c6, 3465, 2b60; queen_aim.c) from the command menu, through queen_commands:
 * the targets are self, then the enemies she can attack within sight (a party member is not listed);
 * Next and Prev step through them, a step passing the party's side while an enemy is listed, and
 * wrap; the target she kept (combat record +0x0b) is the one shown first. Each menu shows "Next Prev
 * Manual Target Center Exit" with Target only where the range reaches the one shown (the readied
 * weapon's range, 1 for none) and it is not self; a missile weapon needs ammunition (a thrown one, or
 * none beside her). Target when attacking is the strike (52db:2d46); Manual is test_command_aim_manual's;
 * Escape and Exit leave the menu
 * (the command menu again); Center centres and shows the menu again. The range line is drawn on row
 * 23 (1a62:01d0). Skipped without DQKAssets/. */

/* Whether the view's corner is the one Center gives for the cell x, y (cok_arena_centre, margin 0). */
static bool centred_on(int x, int y)
{
    cok_combat *k = &menu_game.combat;
    return k->view_x == x - 3 && k->view_y == y - 3;
}

/* Whether a pixel of colour lies in columns x0 to x1 and rows y0 to y1 (ends excluded). */
static bool pixel_in(int x0, int y0, int x1, int y1, uint8_t colour)
{
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            if (menu_game.screen.pixels[y * 320 + x] == colour) return true;
    return false;
}

/* A foe (+0x66 1) or an ally (0) at x, y, in the party's records after the members (monsters are
 * there, as member_of finds them); its record is kept in foes until aim_open. */
static cok_character *aim_foe(int8_t x, int8_t y, uint8_t hostile)
{
    cok_character *foe = foe_of();
    foe->combat = calloc(1, sizeof *foe->combat);
    CHECK(foe->combat != NULL);
    CHECK(cok_party_add(&menu_game.party, R, foe));
    cok_field_set8(R->character, foe->record, COK_FIELD_HOSTILE, hostile);
    place(&menu_game, foe, x, y);
    CHECK(queen_added_icons(&menu_game, foe->record));
    return foe;
}

/* The menu's battle, fresh: the last one closed (its party's records, the foes among them, freed by
 * the close), CINDER at 5, 5 in the battle. */
static void aim_open(void)
{
    menu_close();
    foe_count = 0;
    menu_open();
    battle_map();
    menu_game.combat.show_actions = true; /* the arena's turns centre the view (cok_arena_turn) */
}

/* CINDER's readied weapon: an item whose type 0 has range and flags (its type's bytes 12 and 14),
 * held in her slot 0. */
static void aim_weapon(uint8_t range, uint8_t flags)
{
    cok_item_types *types = &menu_game.item_types;
    types->type[0][12] = range;
    types->type[0][14] = flags;
    free(menu_c->items);
    menu_c->items = calloc(1, sizeof *menu_c->items);
    CHECK(menu_c->items != NULL);
    menu_c->items[0][types->record_type_at != 0 ? types->record_type_at : 0x2e] = 0;
    menu_c->item_count = 1;
    menu_c->slots[0] = 1;
}

/* queen_aim with keys (the script's), from CINDER with the arguments given. */
static bool aim_direct(const char *keys, uint8_t range, uint8_t attack, uint8_t empty,
                       uint8_t creatures, bool *done, cok_combat_aimed *res)
{
    script.keys = keys;
    script.at = 0;
    menu_game.vm.status = COK_ECL_OK;
    menu_game.vm.abort = false;
    return queen_aim(&menu_game, menu_c, range, attack, empty, creatures, done, res);
}

static void test_command_aim(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Aim not checked");
        return;
    }
    cok_combat *k = &menu_game.combat;
    /* Self only: "AE" shows the menu without Target, Exit leaves it (the command menu again: three
     * menus) and the view stays. "A" alone leaves the menu waiting, with its range line on row 23:
     * "Aim: range = 0 " from cell 1 at pixel row 178 (1a62:01d0), its grey line on row 177. */
    aim_open();
    int vx = k->view_x, vy = k->view_y;
    CHECK(menu_run("AE") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strstr(script.all, "Next Prev Manual Center Exit;") != NULL);
    CHECK(strstr(script.all, "Target") == NULL && script.menus == 3);
    CHECK(strncmp(script.menu, "Aim ", 4) == 0); /* the command menu again, not Aim's */
    CHECK(k->view_x == vx && k->view_y == vy);
    aim_open();
    CHECK(menu_run("A") && menu_game.vm.status == COK_ECL_OK && menu_c->combat->initiative == 5);
    CHECK(pixel_in(8, 178, 128, 186, 15) && pixel_in(8, 177, 128, 178, 0x17));
    /* Two enemies, south (5, 20) and east (20, 5), and an ally to the west (2, 5), not listed.
     * Each step shows the next target (the wrap never shows self; Prev from self goes to the last),
     * which Center then centres: the view's corner is the cell less 3. The runs share one battle:
     * each starts from the command menu and sets no target. */
    aim_open();
    aim_foe(5, 20, 1);
    aim_foe(20, 5, 1);
    aim_foe(2, 5, 0);
    CHECK(menu_run("ANC") && menu_game.vm.status == COK_ECL_OK);
    CHECK(centred_on(20, 5) != centred_on(5, 20));
    bool first_east = centred_on(20, 5);
    CHECK(first_east || centred_on(5, 20));
    CHECK(menu_run("ANNC") && centred_on(first_east ? 5 : 20, first_east ? 20 : 5));
    CHECK(menu_run("ANNNC") && centred_on(first_east ? 20 : 5, first_east ? 5 : 20));
    CHECK(menu_run("ANNPC") && centred_on(first_east ? 20 : 5, first_east ? 5 : 20));
    CHECK(menu_run("APC") && centred_on(first_east ? 5 : 20, first_east ? 20 : 5));
    /* The target she kept (+0x0b) is the one shown at the start: east, centred with C. */
    aim_open();
    cok_character *east = aim_foe(20, 5, 1);
    aim_foe(5, 20, 1);
    menu_c->combat->target = east->record;
    CHECK(menu_run("AC") && centred_on(20, 5));
    /* The keypad's 5 (0x80) is Center too: Aim's menu again, the view on her. */
    aim_open();
    east = aim_foe(20, 5, 1);
    menu_c->combat->target = east->record;
    CHECK(menu_run("A~L") && centred_on(20, 5) && script.menus == 3);
    CHECK(strcmp(script.menu, "Next Prev Manual Center Exit") == 0);
    /* Target: a foe beside her (range 1, distance 1) is listed with it, one three squares off is
     * not, self never is. Target when attacking is the strike (52db:2d46, checked below). */
    aim_open();
    aim_foe(6, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Next Prev Manual Target Center Exit;") != NULL);
    /* Target at a foe beside her, bare-handed (range 1, no missile): the strike (52db:2d46) runs. The
     * book (the foe's hits) and the attack (the seed moves), and the turn ended: Target leaves with
     * no command menu after it. The strike's centre (5837:0846 at the view's own centre, margin 3)
     * never scrolls, so the view is the one Aim's menu left (compared with the same centring made
     * directly: it guards against a centre on another cell); it only redraws that cell. */
    aim_open();
    aim_foe(6, 5, 1);
    CHECK(menu_run("AN") && menu_game.vm.status == COK_ECL_OK);
    CHECK(cok_arena_centre(&menu_game, (int8_t)(k->view_x + 3), (int8_t)(k->view_y + 3), 3, 8));
    int8_t cx = k->view_x, cy = k->view_y;
    aim_open();
    cok_character *beside = aim_foe(6, 5, 1);
    uint32_t seed = menu_game.vm.seed;
    CHECK(menu_run("ANT") && menu_game.vm.status == COK_ECL_OK && script.menus == 3);
    CHECK(beside->combat->hits == 1 && menu_game.vm.seed != seed && menu_c->combat->initiative == 0);
    CHECK(k->view_x == cx && k->view_y == cy && k->cursor == 0);
    /* The target down with attacks left: the attack (52db:18d6) leaves done clear, so Target's
     * not-done path (37dd 3b5e) runs: the cell centred at margin 0 (the view's corner the cell less
     * 3; Aim's turn at margin 3 had not moved the view to it), Target leaves, and the command menu
     * comes again for the attacks left (four menus), her turn not ended. A second foe keeps the
     * battle going; the foe beside her has 1 hit point and armour class 0. */
    aim_open();
    beside = aim_foe(6, 5, 1);
    aim_foe(20, 20, 1);
    cok_field_set8(R->character, beside->record, COK_FIELD_HIT_POINTS, 1);
    cok_field_set8(R->character, beside->record, COK_FIELD_AC, 0);
    cok_field_set8_at(R->character, menu_c->record, COK_FIELD_ATTACKS_LEFT, 1, 10);
    CHECK(menu_run("AN") && !centred_on(6, 5));
    CHECK(menu_run("ANT") && menu_game.vm.status == COK_ECL_OK && script.menus == 4);
    CHECK(strncmp(script.menu, "Aim ", 4) == 0 && beside->combat->hits == 1);
    CHECK(cok_field_u8(R->character, beside->record, COK_FIELD_ACTIVE) == 0);
    uint8_t left = cok_field_u8_at(R->character, menu_c->record, COK_FIELD_ATTACKS_LEFT, 1);
    CHECK(left > 0 && left < 10 && menu_c->combat->initiative != 0);
    CHECK(centred_on(6, 5) && k->cursor == 0);
    aim_open();
    aim_foe(8, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Next Prev Manual Center Exit;") != NULL);
    CHECK(strstr(script.all, "Target") == NULL);
    /* A weapon of range 0 reaches as 1 (37dd's a938: 0 or 0xff is 1). */
    aim_open();
    aim_weapon(0, 0x00);
    aim_foe(6, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Next Prev Manual Target Center Exit;") != NULL);
    aim_open();
    CHECK(menu_run("AT") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strstr(script.all, "Target") == NULL);
    /* A readied missile weapon of range 3: one that is its own ammunition (flags 0x10, not thrown)
     * reaches the foe three squares off with no foe beside her (Target listed, then its strike),
     * one with no ammunition does not. */
    aim_open();
    aim_weapon(3, 0x10);
    aim_foe(8, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Next Prev Manual Target Center Exit;") != NULL);
    /* The strike's item (52db:2d46): a missile weapon with its own ammunition, not thrown, at three squares
     * is the attack's item, so the weapon is used (its count one less); a thrown one at one square has
     * no item (the attack is bare, the count kept), at three it is the item. */
    aim_open();
    aim_weapon(3, 0x10);
    menu_c->items[0][R->item_record.count] = 3;
    cok_character *three = aim_foe(8, 5, 1);
    CHECK(menu_run("ANT") && menu_game.vm.status == COK_ECL_OK && script.menus == 3);
    CHECK(three->combat->hits == 1 && menu_c->items[0][R->item_record.count] == 2);
    aim_open();
    aim_weapon(3, 0x14);
    menu_c->items[0][R->item_record.count] = 3;
    cok_character *beside_thrown = aim_foe(6, 5, 1);
    CHECK(menu_run("ANT") && beside_thrown->combat->hits == 1 && menu_c->items[0][R->item_record.count] == 3);
    aim_open();
    aim_weapon(3, 0x14);
    menu_c->items[0][R->item_record.count] = 3;
    cok_character *thrown_three = aim_foe(8, 5, 1);
    CHECK(menu_run("ANT") && thrown_three->combat->hits == 1 && menu_c->items[0][R->item_record.count] == 2);
    aim_open();
    aim_weapon(3, 0x00);
    aim_foe(8, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Target") == NULL);
    /* With a foe beside her: a missile weapon with its ammunition (flags 0x0a) but not thrown has no
     * Target (2b60's 51c2:0980), a thrown one (0x14: itself the ammunition) has. */
    aim_open();
    aim_weapon(3, 0x0a);
    aim_foe(6, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Target") == NULL);
    aim_open();
    aim_weapon(3, 0x14);
    aim_foe(6, 5, 1);
    CHECK(menu_run("AN") && strstr(script.all, "Next Prev Manual Target Center Exit;") != NULL);
    /* The party's side is not listed (52db:1030 refuses the attacker's side): an ally kept as the
     * target is not shown first, she is. */
    aim_open();
    cok_character *ally = aim_foe(2, 5, 0);
    aim_foe(20, 5, 1);
    menu_c->combat->target = ally->record;
    CHECK(menu_run("AC") && centred_on(5, 5));
    aim_open();
    CHECK(menu_run("A\x1b") && menu_game.vm.status == COK_ECL_OK && script.menus == 3);
    CHECK(menu_c->combat->initiative == 5 && strncmp(script.menu, "Aim ", 4) == 0);
    aim_open();
    CHECK(menu_run("AC") && menu_game.vm.status == COK_ECL_OK && script.menus == 3);
    CHECK(k->cursor == 0 && strcmp(script.menu, "Next Prev Manual Center Exit") == 0);
    /* queen_aim with a spell's attack (0): with creatures, the foe three squares off is the
     * Target: done, its record and cell, no strike and no stop. */
    aim_open();
    cok_character *foe = aim_foe(8, 5, 1);
    bool done = false;
    cok_combat_aimed res = {NULL, 0, 0};
    CHECK(aim_direct("NT", 4, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == foe->record && res.x == 8 && res.y == 5);
    CHECK(script.menus == 2); /* Target leaves: no menu after it */
    /* Exit when attacking clears the range line (1bdb:03a4): rows 176-185 are FRAME 6's again. */
    aim_open();
    CHECK(aim_direct("E", 0xff, 1, 0, 1, &done, &res) && !done);
    static uint8_t line[10 * 320];
    memcpy(line, menu_game.screen.pixels + 176 * 320, sizeof line);
    queen_range_line_clear(&menu_game);
    CHECK(memcmp(line, menu_game.screen.pixels + 176 * 320, sizeof line) == 0);
    /* The keys running out at the menu: true, nothing picked. */
    aim_open();
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("", 1, 1, 0, 1, &done, &res) && !done && res.target == NULL);
    CHECK(menu_game.vm.status == COK_ECL_OK);
    /* The battle's row 23 text (1a62:01d0) on a screen of 0xfe: the grey line on row 177 and the
     * 15 cells from column 8 to 127 on rows 178-185, nothing else. Then 1bdb:03a4 redraws rows
     * 176-185 from FRAME 6, rows 175 and 186 kept. */
    aim_open();
    uint8_t *px = menu_game.screen.pixels;
    memset(px, 0xfe, 320 * 200);
    CHECK(cok_arena_text(&menu_game, 0, 0x17, "Aim: range = 0 ", 7, 0));
    CHECK(menu_game.vm.status == COK_ECL_OK && pixel_in(8, 178, 128, 186, 15));
    for (int y = 175; y < 188; ++y)
        for (int x = 0; x < 320; ++x) {
            bool cell = x >= 8 && x < 128 && y >= 177 && y < 186;
            if ((px[y * 320 + x] != 0xfe) != cell || (y == 177 && cell && px[y * 320 + x] != 0x17)) {
                fprintf(stderr, "row 23's text: pixel %d, %d is %#x\n", x, y, px[y * 320 + x]);
                exit(1);
            }
        }
    queen_range_line_clear(&menu_game);
    for (int x = 0; x < 320; ++x)
        CHECK(px[175 * 320 + x] == 0xfe && px[186 * 320 + x] == 0xfe);
    CHECK(!pixel_in(0, 176, 320, 186, 0xfe));
    aim_open();
    menu_close();
}

/* Aim's Manual (dqk 52db:2e9c, queen_aim.c's manual; part 25): the cursor from the target's cell, stepped by
 * the direction keys (keys only: the mouse's map areas are the stop "dqk 52db:40d6"), Center centres it at
 * margin 0, Target picks (the strike, as Aim's), Exit and Escape leave to Aim. The party's side is picked
 * here (Aim lists none): Target asks "attack ally? " (queen_attack_ally): No leaves the turn kept, Yes attacks.
 * Skipped without DQKAssets/. */
static void test_command_aim_manual(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Aim's Manual not checked");
        return;
    }
    cok_combat *k = &menu_game.combat;
    /* From self: the menu is "Center Exit" (self is invalid with attack 1). Exit leaves to Aim, Exit again
     * to the command menu; the cursor is off and no draw is made (the RandSeed holds). */
    aim_open();
    uint32_t seed = menu_game.vm.seed;
    CHECK(menu_run("AMEE") && menu_game.vm.status == COK_ECL_OK);
    CHECK(script.menus == 5 && strstr(script.all, "Next Prev Manual Center Exit;Center Exit;Next Prev Manual "
                                                  "Center Exit;") != NULL);
    CHECK(strncmp(script.menu, "Aim ", 4) == 0 && k->cursor == 0 && menu_game.vm.seed == seed);
    /* Escape is Exit too: Aim again, then Exit. */
    aim_open();
    CHECK(menu_run("AM\x1b" "E") && menu_game.vm.status == COK_ECL_OK && script.menus == 5);
    CHECK(strncmp(script.menu, "Aim ", 4) == 0);
    /* Right from Manual's menu: the next pass steps the cursor one cell east (6, 5), its menu again, and a
     * second Right to (7, 5). Center there, and the cursor stays on: the keys run out at Manual's menu. */
    aim_open();
    CHECK(menu_run("AM~M~MC") && menu_game.vm.status == COK_ECL_OK && script.menus == 6);
    CHECK(centred_on(7, 5) && k->cursor == 1 && k->cursor_size == 1 && strncmp(script.menu, "Center Exit", 11) == 0);
    /* Each pass centres at margin 3 (5837:0846(x, y, 3, dir)): two Rights scroll the view by four only. */
    aim_open();
    CHECK(menu_run("AM~M~M") && k->view_x == 4 && k->view_y == 0);
    /* The keypad's 5 (0x80) centres there at margin 0 and shows the menu again, the cursor on (3360). */
    aim_open();
    CHECK(menu_run("AM~M~M~L") && centred_on(7, 5) && k->cursor == 1 && strcmp(script.menu, "Center Exit") == 0);
    /* Aim's direction key (Right at the Aim menu): Manual starts with that step (its first pass has none), so
     * the cursor is on (6, 5) at its menu; Center centres there. */
    aim_open();
    CHECK(menu_run("A~MC") && menu_game.vm.status == COK_ECL_OK && script.menus == 4);
    CHECK(centred_on(6, 5) && k->cursor == 1 && strcmp(script.menu, "Center Exit") == 0);
    aim_open();
    CHECK(menu_run("A~MCEE") && menu_game.vm.status == COK_ECL_OK && script.menus == 6);
    CHECK(strncmp(script.menu, "Aim ", 4) == 0 && k->cursor == 0);
    /* A foe beside her (range 1, distance 1) is "Target Center Exit" from its cell; Target strikes it as Aim's
     * does (the turn ended: no command menu after it). Aim's N reaches the foe, then Manual on it. */
    aim_open();
    cok_character *beside = aim_foe(6, 5, 1);
    uint32_t before = menu_game.vm.seed;
    CHECK(menu_run("ANMT") && menu_game.vm.status == COK_ECL_OK && script.menus == 4);
    CHECK(strstr(script.all, "Next Prev Manual Target Center Exit;Target Center Exit;") != NULL);
    CHECK(beside->combat->hits == 1 && menu_game.vm.seed != before && menu_c->combat->initiative == 0);
    /* A thrown missile weapon (flags 0x14) at one square is pickable from Manual with no ammunition held (2e9c's
     * thrown test at one square comes before the ammunition's; Aim's Target offer needs the ammunition, so none
     * is listed there). */
    aim_open();
    aim_weapon(3, 0x1d); /* thrown (0x14); its ammunition is the slot 11 item (0x08, 0x01), none held (round.c) */
    menu_c->slots[11] = 0;
    aim_foe(6, 5, 1);
    CHECK(menu_run("ANM") && menu_game.vm.status == COK_ECL_OK && script.menus == 4);
    CHECK(strstr(script.all, "Target Center Exit;") != NULL && strstr(script.all, "Next Prev Manual Target") == NULL);
    /* Out of range: a foe three squares off has no Target from Manual either. */
    aim_open();
    aim_foe(8, 5, 1);
    CHECK(menu_run("ANM") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strstr(script.all, "Next Prev Manual Center Exit;Next Prev Manual Center Exit;Center Exit;") != NULL);
    CHECK(strstr(script.all, "Target") == NULL);
    /* A body on a cell (terrain 0x1f, no occupant) is not pickable with attack 1: the cell east of her holds a
     * body of the foe, its occupant gone. Manual's menu there is "Center Exit". */
    aim_open();
    beside = aim_foe(6, 5, 1);
    k->occupant[5][6] = 0;
    k->cells[5][6] = COK_COMBAT_BODY;
    k->body[0] = (cok_combat_body){beside, 6, 5, COK_COMBAT_FLOOR};
    k->bodies = 1;
    CHECK(menu_run("AM~M") && menu_game.vm.status == COK_ECL_OK);
    CHECK(strstr(script.all, "Target") == NULL && strstr(script.all, "Center Exit;Center Exit;") != NULL);
    k->bodies = 0;
    k->cells[5][6] = COK_COMBAT_FLOOR;
    /* The mouse on: Manual stops before its first menu (1bdb:1862's map areas: "dqk 52db:40d6"). */
    aim_open();
    menu_game.mouse.on = true;
    CHECK(!menu_run("AM") && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "dqk 52db:40d6") != NULL);
    menu_game.mouse.on = false;
    /* The carry-over (dqk-aim-b's F3): Manual's pick of an ally, Target, asks "attack ally? ". Manual's
     * cursor is on (6, 5) at its menu with "Target Center Exit". No: not done, the turn kept, the command
     * menu again, no attack (the cursor off, the cell centred at margin 0). */
    aim_open();
    cok_character *ally = aim_foe(6, 5, 0);
    CHECK(menu_run("AM~MTN") && menu_game.vm.status == COK_ECL_OK && script.menus == 5);
    CHECK(strstr(script.all, "Target Center Exit;") != NULL && strncmp(script.menu, "Aim ", 4) == 0);
    CHECK(ally->combat->hits == 0 && cok_ecl_var(&menu_game.vm, COK_VAR_FLED) == 0);
    CHECK(k->cursor == 0 && centred_on(6, 5) && menu_c->combat->initiative == 5);
    /* The same by queen_aim: No clears res (NULL, 0, 0) and done. */
    aim_open();
    ally = aim_foe(6, 5, 0);
    bool done = true;
    cok_combat_aimed res = {NULL, 3, 3};
    CHECK(aim_direct("M~MTN", 1, 1, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(!done && res.target == NULL && res.x == 0 && res.y == 0);
    CHECK(ally->combat->hits == 0);
    /* Yes: var 0x36 (FLED) set, the book and the attack run on the ally (hits), and the turn ends. The same
     * by queen_aim: done, res the ally's record and cell. */
    aim_open();
    ally = aim_foe(6, 5, 0);
    CHECK(menu_run("AM~MTY") && menu_game.vm.status == COK_ECL_OK && script.menus == 4);
    CHECK(cok_ecl_var(&menu_game.vm, COK_VAR_FLED) == 1 && ally->combat->hits == 1);
    CHECK(menu_c->combat->initiative == 0);
    aim_open();
    ally = aim_foe(6, 5, 0);
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("M~MTY", 1, 1, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == ally->record && res.x == 6 && res.y == 5);
    /* A spell's attack (0, creatures): Manual's Target on the foe east of her picks it with no strike, the
     * cursor off (2e9c's Target), done set and the record and cell returned. */
    aim_open();
    cok_character *foe = aim_foe(6, 5, 1);
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("M~MT", 4, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == foe->record && res.x == 6 && res.y == 5 && k->cursor == 0);
    /* The map's edges (2e9c 2f41-2f69: 0..0x31, 0..0x18). A spell's Target picks the cursor's cell, so res is
     * where it stopped: from her own cell at the corner 0x31, 0x18, Right, PgDn and Down stay on it; from 0, 0
     * Home, Up and Left stay there (empty 0: a cell off hers has no Target, the keys run out, done clear). */
    aim_open();
    k->combatant[1].x = 0x31;
    k->combatant[1].y = 0x18;
    CHECK(cok_combat_occupy(k, R));
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("M~M~Q~PT", 0xfe, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == menu_c->record && res.x == 0x31 && res.y == 0x18);
    aim_open();
    k->combatant[1].x = 0;
    k->combatant[1].y = 0;
    CHECK(cok_combat_occupy(k, R));
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("M~G~H~KT", 0xfe, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == menu_c->record && res.x == 0 && res.y == 0);
    /* Up is 0x88, direction 0 (y - 1); Center leaves no step pending (3360 -> 3444: direction 8): Up, Center, Up
     * reach the foe two cells north. */
    aim_open();
    foe = aim_foe(5, 3, 1);
    done = false;
    res = (cok_combat_aimed){NULL, 0, 0};
    CHECK(aim_direct("M~HC~HT", 0xfe, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == foe->record && res.x == 5 && res.y == 3);
    /* A cell not seen may not be picked, and its occupant is not looked up (2fe8, 3049: valid only from the
     * sight): a foe behind a wall (terrain 1: cost 0xff, block 2) has no Target and no panel (the cells cleared:
     * no "Hitpoints" in grey on row 3); seen, its panel shows. A cell of cost 0xff may not be picked though seen
     * (3126: terrain 0x37, cost 0xff, block 0), with empty set. */
    aim_open();
    k->cells[5][6] = 1;
    aim_foe(7, 5, 1);
    CHECK(aim_direct("M~M~M", 0xfe, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.menu, "Center Exit") == 0 && !pixel_in(184, 24, 312, 32, 7));
    k->cells[5][6] = COK_COMBAT_FLOOR;
    CHECK(aim_direct("M~M~M", 0xfe, 0, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.menu, "Target Center Exit") == 0 && pixel_in(184, 24, 312, 32, 7));
    aim_open();
    k->cells[5][6] = 0x37;
    CHECK(aim_direct("M~M", 0xfe, 0, 1, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.menu, "Center Exit") == 0);
    k->cells[5][6] = COK_COMBAT_FLOOR;
    CHECK(aim_direct("M~M", 0xfe, 0, 1, 1, &done, &res) && strcmp(script.menu, "Target Center Exit") == 0);
    /* A creature with creatures 0 may not be picked (3169), empty or not. */
    aim_open();
    aim_foe(6, 5, 1);
    CHECK(aim_direct("M~M", 0xfe, 0, 1, 0, &done, &res) && strcmp(script.menu, "Center Exit") == 0);
    /* A missile weapon not thrown (31c9-3209): with no ammunition no Target; with it (flags 0x0a) Target, unless
     * an enemy is beside her. */
    aim_open();
    aim_weapon(3, 0x09); /* its ammunition is the slot 11 item, none held */
    menu_c->slots[11] = 0;
    aim_foe(7, 5, 1);
    CHECK(menu_run("AM~M~M") && strcmp(script.menu, "Center Exit") == 0);
    aim_open();
    aim_weapon(3, 0x0a);
    aim_foe(7, 5, 1);
    CHECK(menu_run("AM~M~M") && strcmp(script.menu, "Target Center Exit") == 0);
    aim_open();
    aim_weapon(3, 0x0a);
    aim_foe(7, 5, 1);
    aim_foe(4, 5, 1);
    CHECK(menu_run("AM~M~M") && strcmp(script.menu, "Center Exit") == 0);
    /* A stone body (terrain 0x1e, no occupant) is a body cell as 0x1f is (3073-307d), but only 0x1f's is barred
     * with attack 1 (3198): Target is listed on it. */
    aim_open();
    beside = aim_foe(6, 5, 1);
    k->occupant[5][6] = 0;
    k->cells[5][6] = 0x1e;
    k->body[0] = (cok_combat_body){beside, 6, 5, COK_COMBAT_FLOOR};
    k->bodies = 1;
    CHECK(menu_run("AM~M") && strcmp(script.menu, "Target Center Exit") == 0);
    k->bodies = 0;
    k->cells[5][6] = COK_COMBAT_FLOOR;
    /* After a direction key's Manual, its Exit shows no panel of hers (37dd 3c6c), after the menu's it does
     * (3ad7). Her panel would stop here: inactive, with a status past the panel's table (177e:085f). Aim's
     * target is the foe she kept (+0x0b), whose panel the Aim menu shows. */
    aim_open();
    foe = aim_foe(7, 5, 1);
    menu_c->combat->target = foe->record;
    cok_field_set8(R->character, menu_c->record, COK_FIELD_ACTIVE, 0);
    cok_field_set8(R->character, menu_c->record, COK_FIELD_STATUS, 10);
    CHECK(aim_direct("~ME", 1, 1, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(strcmp(script.all, "Next Prev Manual Center Exit;Center Exit;Next Prev Manual Center Exit;") == 0);
    script.all[0] = '\0';
    CHECK(!aim_direct("ME", 1, 1, 0, 1, &done, &res) && menu_game.vm.status == COK_ECL_UNPORTED);
    CHECK(strstr(menu_game.error, "dqk 177e:085f") != NULL);
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
    /* Quick: the computer's, the target on her own side dropped, the wait's draw, then the
     * computer's turn for her (5bb8:0000) at once, no menu again: its draws go on, and its first
     * stop, the weapon choice (5bb8:1396, this test's state), is the one reached. */
    menu_open();
    cok_combat_record *cr = menu_c->combat;
    cr->target = menu_c->record;
    uint32_t drawn = menu_game.vm.seed;
    cok_tp_random(&drawn, 1000);
    CHECK(!menu_run("Q") && menu_game.vm.status == COK_ECL_UNDEFINED);
    CHECK(strstr(menu_game.error, "5bb8:1396") != NULL);
    CHECK(script.menus == 1);
    /* The wait's one draw came before its 200 ms (the first delay), and nothing else drew
     * before it. */
    CHECK(script.delays >= 1 && script.delay_ms == 200 && script.delay_seed == drawn);
    CHECK(cok_field_u8(L, menu_c->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cr->target == NULL && cr->initiative == 5);
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

/* The state an Alt-Q turn leaves: the turn index, CINDER's initiative, and the COMPUTER flag of
 * CINDER, an ally and a monster. */
static void alt_q_state(const cok_character *ally, const cok_character *monster, int out[6])
{
    const cok_character_layout *L = R->character;
    out[0] = menu_game.combat.turn_index;
    out[1] = menu_c->combat->initiative;
    out[2] = cok_field_u8(L, menu_c->record, COK_FIELD_COMPUTER);
    out[3] = cok_field_u8(L, ally->record, COK_FIELD_COMPUTER);
    out[4] = cok_field_u8(L, monster->record, COK_FIELD_COMPUTER);
    out[5] = script.menus;
}

/* --combat auto's and --combat gods' first party turn (dqk 5045:08cf with the stub's key, round.c's
 * menu_first): the pressed scan code 0x10 (Alt-Q) is taken after the menu is drawn (1bdb:0d26 draws
 * before it reads), so the turn gives back at once: every record on Auto, the turn index less one,
 * then the same combatant's turn comes again as the computer's (cok_combat_act, 5bb8:1396's stop
 * here). The typed Alt-Q (~ then 0x10) leaves the same state, and so does the stub's call. Pressed
 * 0x2d (Alt-X) without the Helm says "That doesn't work" and shows the menu again (the arena's
 * stop, 6beb:12ef, in this test). Skipped without DQKAssets/. */
static void test_command_menu_auto(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; --combat auto's first turn not checked");
        return;
    }
    int typed[6], pressed[6], stub[6];
    menu_open();
    cok_character *ally = placed(&menu_game, 0), *monster = placed(&menu_game, 1);
    menu_game.combat.turn_index = 1;
    CHECK(menu_run("~\x10") && menu_game.vm.status == COK_ECL_OK);
    alt_q_state(ally, monster, typed);
    menu_open();
    ally = placed(&menu_game, 0), monster = placed(&menu_game, 1);
    menu_game.combat.turn_index = 1;
    CHECK(menu_run_pressed("", 0x10) && menu_game.vm.status == COK_ECL_OK);
    alt_q_state(ally, monster, pressed);
    CHECK(memcmp(typed, pressed, sizeof typed) == 0);
    CHECK(pressed[0] == 0 && pressed[1] == 0x14 && pressed[2] == 1 && pressed[3] == 1 &&
          pressed[4] == 1);
    /* The stub's call: cok_combat_act with --combat auto (round.c's menu_first branch) gives the
     * same state, and the same combatant's turn after it is the computer's (CINDER on Auto). */
    menu_open();
    ally = placed(&menu_game, 0), monster = placed(&menu_game, 1);
    menu_game.combat.turn_index = 1;
    menu_game.combat_stub = COK_COMBAT_AUTO;
    script.keys = "";
    script.at = 0;
    script.menus = 0;
    CHECK(cok_combat_act(&menu_game, menu_c, false) && menu_game.vm.status == COK_ECL_OK);
    alt_q_state(ally, monster, stub);
    CHECK(memcmp(pressed, stub, sizeof pressed) == 0);
    CHECK(!cok_combat_act(&menu_game, menu_c, true) && strstr(menu_game.error, "5bb8:1396") != NULL);
    /* Pressed Alt-X without the Helm: "That doesn't work" on the bar, the menu again (script.menus
     * the arena's turn stops at 6beb:12ef, as Alt-M's does). */
    menu_open();
    CHECK(!menu_run_pressed("", 0x2d) && strstr(menu_game.error, "6beb:12ef") != NULL);
    CHECK(strcmp(script.messages, "That doesn't work;") == 0 && script.menus == 1);
    menu_close();
}

/* The battle's ground for a menu's arena stops (cok_arena_turn needs the map: the tiles and
 * the battle's combat.active, as the arena's tests set them). */
static void battle_map(void)
{
    CHECK(cok_adventure_drawing(&menu_game)->arena_tiles(&menu_game, false, 4));
    menu_game.combat.active = true;
    menu_game.vm.mode = 5; /* the battle's mode: cok_combat_place puts a combatant only in it */
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
 * fails); one that fails takes 0x8c (0 minutes, value 0xff) and its flash runs (dqk 51c2:0731).
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

    /* A monster that fails (saving throw 0x7f): the add of 0x8c, then the flash (dqk 51c2:0731,
     * kind 1: four frames for each of var 0x12 + 1 passes, one random draw per frame's wait, and
     * no pause), and the turn ends. */
    yell_menu();
    menu_game.vm.mode = 5;
    /* The flash's frames (icon slots 0x17 and 0x19) are COMSPR 8 and 10, loaded at the battle's
     * start (arena_tiles, 59c7:0e80-0f06), which this fixture skips; the test loads CPIC 64 into each. */
    CHECK(cok_adventure_drawing(&menu_game)->arena_load_icon(&menu_game, "CPIC.TLB", 64, 0x17));
    CHECK(cok_adventure_drawing(&menu_game)->arena_load_icon(&menu_game, "CPIC.TLB", 64, 0x19));
    cok_character *failing = yelled_monster(10, 0x7f);
    place(&menu_game, failing, 2, 2); /* a combatant shown whole: the flash does not centre */
    uint32_t drawn = YELL_SEED;
    cok_dice(&drawn, 1, 20);
    unsigned passes = cok_ecl_var(&menu_game.vm, COK_VAR_SPEED);
    for (unsigned k = 0; k < 4 * (passes + 1); ++k) cok_tp_random(&drawn, 1000);
    CHECK(menu_run("Y") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_game.vm.seed == drawn && menu_c->combat->initiative == 0);
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

/* A party record of its own (not a monster, not an NPC) for the computer's keys' test: appended to
 * the party with a combat record, its computer flag and status set. */
static cok_character *keys_member(uint8_t npc, uint8_t status, uint8_t computer)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_append(&menu_game.party, c));
    c->combat = calloc(1, sizeof *c->combat);
    CHECK(c->combat != NULL);
    cok_field_set8(L, c->record, COK_FIELD_NPC, npc);
    cok_field_set8(L, c->record, COK_FIELD_STATUS, status);
    cok_field_set8(L, c->record, COK_FIELD_COMPUTER, computer);
    return c;
}

/* The computer's keys (dqk 5bb8:1050, cok_ai_keys through the Dark Queen's reader,
 * queen_read_code): nothing is read with no key waiting, and a mouse button down is not a key (the
 * keyboard alone, 1df9:045b); Space gives the turn back (initiative 0x14) and clears the computer
 * flag of the party's records that are not NPCs and not status 1 (the status a word: a record
 * with +0x65 set and its low byte 1 is cleared); with Alt-M (scan 0x32, "~2") and Space both
 * waiting, the reader reads every key waiting and keeps the last, so Space alone acts: Magic is
 * not toggled, no notice, no draw; with the Space waiting only after Alt-M, Alt-M toggles Magic On
 * with its notice, after one draw for its wait, and nothing is flushed; Alt-X (scan 0x2d, "~-")
 * gives the gods only with the Helm (52db:3feb tests it itself). Skipped without DQKAssets/. */
static void test_computer_keys(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the computer's keys not checked");
        return;
    }
    menu_open();
    const cok_character_layout *L = R->character;
    cok_ecl_set_var(&menu_game.vm, COK_VAR_SPEED, 4);
    cok_character *plain = keys_member(0, 0, 1), *npc = keys_member(0x80, 0, 1);
    cok_character *fled = keys_member(0, 1, 1);
    cok_character *word1 = keys_member(0, 0, 1); /* status word 0x0101: its low byte 1, not 1 */
    cok_combat_set_field_word(R, word1->record, COK_FIELD_STATUS, 0x101);
    bool back = true;
    /* No key waiting: nothing read, nothing flushed. */
    script.pending = false;
    script.flushes = 0;
    script.keys = "";
    script.at = 0;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && !back && script.at == 0 && script.flushes == 0);
    /* A mouse button down and no key: nothing is read, the button stays down (the keyboard alone
     * is tested). The Space in the queue is not waiting. */
    menu_game.mouse.on = true;
    menu_game.mouse.driver.buttons = 1;
    script.keys = " ";
    script.at = 0;
    menu_c->combat->initiative = 5;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && !back && script.at == 0);
    CHECK(menu_c->combat->initiative == 5 && menu_game.mouse.driver.buttons == 1);
    menu_game.mouse.driver.buttons = 0;
    menu_game.mouse.on = false;
    /* Space: the party's computer flags cleared (not the NPC's nor the status 1 one's; the word-1
     * one's status word is 0x101, not 1, so it is cleared), and CINDER's turn given back. */
    script.keys = " ";
    script.at = 0;
    script.pending = true;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && back && menu_c->combat->initiative == 0x14);
    CHECK(script.at == 1 && script.flushes == 0);
    CHECK(cok_field_u8(L, plain->record, COK_FIELD_COMPUTER) == 0);
    CHECK(cok_field_u8(L, npc->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cok_field_u8(L, fled->record, COK_FIELD_COMPUTER) == 1);
    CHECK(cok_field_u8(L, word1->record, COK_FIELD_COMPUTER) == 0);
    /* Alt-M and Space both waiting: the reader reads both and keeps Space, so the turn is given
     * back, and Magic is not toggled (no notice, no draw, nothing left waiting). */
    menu_game.combat.magic_on = false;
    script.keys = "~2 ";
    script.at = 0;
    script.pending = true;
    script.messages[0] = '\0';
    uint32_t seed = menu_game.vm.seed;
    back = false;
    menu_c->combat->initiative = 5;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && back && menu_c->combat->initiative == 0x14);
    CHECK(script.at == 3 && !menu_game.combat.magic_on && menu_game.vm.seed == seed);
    CHECK(script.messages[0] == '\0' && script.flushes == 0);
    /* Alt-M with the Space not waiting during the call (once: a read clears the wait, so the
     * Space starts waiting only after it): Magic On and its notice, one draw for the notice's wait,
     * and nothing is flushed. */
    menu_game.combat.magic_on = false;
    script.keys = "~2 ";
    script.at = 0;
    script.pending = true;
    script.once = true;
    script.messages[0] = '\0';
    uint32_t want = menu_game.vm.seed;
    cok_tp_random(&want, 1000);
    back = true;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && !back && menu_game.combat.magic_on);
    CHECK(strcmp(script.messages, "Magic On;") == 0 && menu_game.vm.seed == want);
    CHECK(script.at == 2 && script.flushes == 0);
    /* The Space waits now: the next poll reads it and gives the turn back, Magic still on. */
    script.once = false;
    script.pending = true;
    back = false;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && back && menu_c->combat->initiative == 0x14);
    CHECK(script.at == 3 && menu_game.combat.magic_on && script.flushes == 0);
    /* Alt-X without the Helm: nothing. With it: the gods (the notice, then the battle's end of the
     * turns; no value back). */
    script.keys = "~-";
    script.at = 0;
    script.messages[0] = '\0';
    menu_game.helm = false;
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && !back && script.at == 2);
    CHECK(strstr(script.messages, "Gods") == NULL && script.flushes == 0);
    script.at = 0;
    menu_game.helm = true;
    battle_map(); /* the battle's ground: the gods centre the arena (6beb:096b) */
    CHECK(cok_ai_keys(&menu_game, menu_c, &back) && !back);
    CHECK(strstr(script.messages, "The Gods intervene!") != NULL && script.flushes == 0);
    menu_game.combat.active = false;
    menu_game.helm = false;
    script.pending = false;
    menu_close();
}

/* The spell picker (src/queen_spells.c; 849a:2080 for Cast): the spells the class may cast by its
 * levels and abilities (849a:0031), the list of the memorised ones with headings and counts
 * (849a:0b39, 05cc), its keys and its screen. Skipped without DQKAssets/ (menu_open opens the
 * game). */

/* CINDER's intelligence, wisdom, race (a word) and class levels for the spell tests. */
static void spell_stats(uint8_t intelligence, uint8_t wisdom, uint16_t race,
                        const uint8_t levels[7])
{
    uint8_t *r = menu_c->record;
    r[0x7b] = intelligence;
    r[0x7d] = wisdom;
    r[0x58] = (uint8_t)race;
    r[0x59] = (uint8_t)(race >> 8);
    memcpy(r + 0xa5, levels, 7);
}

static bool spell_usable(uint8_t spell, bool scroll, uint8_t mode)
{
    menu_game.vm.mode = mode;
    return queen_spell_usable(&menu_game, menu_c, spell, scroll);
}

static void test_spell_usable(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the spells a class may cast not checked");
        return;
    }
    menu_open();
    uint8_t lv[7] = {0};
    /* Kind 0 (Bless 0x01, Cure Light Wounds 0x03): wisdom above 8, and class 0 above 0, class 1
     * above 5 or class 3 above 8. */
    lv[0] = 1;
    spell_stats(0, 8, 0, lv);
    CHECK(!spell_usable(0x03, false, 0));
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x03, false, 0) && spell_usable(0x01, false, 0));
    CHECK(spell_usable(0x83, false, 0)); /* bit 7 is masked */
    memset(lv, 0, 7);
    lv[1] = 5;
    spell_stats(0, 9, 0, lv);
    CHECK(!spell_usable(0x03, false, 0));
    lv[1] = 6;
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x03, false, 0));
    lv[1] = 0x80; /* a level byte from 0x80 is large (unsigned) */
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x03, false, 0));
    lv[1] = 0;
    lv[3] = 8;
    spell_stats(0, 9, 0, lv);
    CHECK(!spell_usable(0x03, false, 0));
    lv[3] = 9;
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x03, false, 0));
    /* Kind 1 (0x4d): wisdom above 8 and class 4 above 7. */
    memset(lv, 0, 7);
    lv[4] = 7;
    spell_stats(0, 9, 0, lv);
    CHECK(!spell_usable(0x4d, false, 0));
    lv[4] = 8;
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x4d, false, 0));
    /* Kind 2 (Magic Missile 0x0f, Fireball 0x2f): intelligence above 8, then class 5 above 0
     * (not a human with armour in the battle at class 4 8 or less), or class 4 above 8; with the
     * scroll, class 6 above 10. */
    memset(lv, 0, 7);
    lv[4] = 9;
    spell_stats(8, 0, 0, lv);
    CHECK(!spell_usable(0x0f, false, 0));
    spell_stats(9, 0, 0, lv);
    CHECK(spell_usable(0x0f, false, 0) && spell_usable(0x2f, false, 0));
    lv[4] = 8;
    spell_stats(9, 0, 0, lv);
    CHECK(!spell_usable(0x0f, false, 0));
    lv[5] = 1;
    lv[4] = 0;
    spell_stats(9, 0, 0, lv);
    CHECK(spell_usable(0x0f, false, 0));
    lv[4] = 8;
    lv[5] = 1;
    spell_stats(9, 0, 6, lv);
    menu_c->slots[2] = 1; /* armour as readied (+0x14) */
    CHECK(!spell_usable(0x0f, false, 5)); /* the human's exception, in the battle */
    CHECK(spell_usable(0x0f, false, 0));
    menu_c->slots[2] = 0;
    CHECK(spell_usable(0x0f, false, 5)); /* no armour */
    menu_c->slots[2] = 1;
    spell_stats(9, 0, 3, lv);
    CHECK(spell_usable(0x0f, false, 5)); /* not a human */
    lv[4] = 9;
    spell_stats(9, 0, 6, lv);
    CHECK(spell_usable(0x0f, false, 5)); /* class 4 above 8 */
    menu_c->slots[2] = 0;
    memset(lv, 0, 7);
    lv[4] = 1;
    lv[6] = 10;
    spell_stats(9, 0, 0, lv);
    CHECK(!spell_usable(0x2f, false, 0) && !spell_usable(0x2f, true, 0));
    lv[6] = 11;
    spell_stats(9, 0, 0, lv);
    CHECK(!spell_usable(0x2f, false, 0) && spell_usable(0x2f, true, 0));
    /* Kind 3 (0x39) as kind 0; kind 4 (0x41) and above never. */
    memset(lv, 0, 7);
    lv[3] = 9;
    spell_stats(0, 9, 0, lv);
    CHECK(spell_usable(0x39, false, 0));
    memset(lv, 9, 7);
    spell_stats(9, 9, 0, lv);
    CHECK(!spell_usable(0x41, false, 0) && !spell_usable(0x02, false, 0));
    menu_close();
}

/* CINDER's spells in memory for the list: a level-1 cleric and a level-3 mage's, Magic Missile
 * twice (+0xce 0x0f, 0x0f) with a bit 7 byte that is no spell in the list (0x83). */
static const uint8_t listed[5] = {0x0f, 0x03, 0x2f, 0x0f, 0x83};

static void spell_list_set(void)
{
    static const uint8_t lv[7] = {1, 0, 0, 0, 9, 0, 0};
    spell_stats(9, 9, 0, lv);
    memcpy(menu_c->record + 0xce, listed, sizeof listed);
}

/* queen_spell_pick with keys (script.keys): the spell or the result, the rows logged. */
static int spell_pick_keys(const char *keys)
{
    script.keys = keys;
    script.at = 0;
    script.menus = 0;
    script.rows[0] = '\0';
    menu_game.vm.status = COK_ECL_OK;
    menu_game.vm.abort = false;
    return queen_spell_pick(&menu_game, menu_c, 1);
}

static void test_spell_list(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Cast's spell list not checked");
        return;
    }
    static uint8_t before[320 * 200], after[320 * 200];
    menu_open();
    menu_game.vm.mode = 0;
    spell_list_set();
    /* Headings are rows of their own: the first is a heading, so the first spell (Magic Missile)
     * is the current one; Cure Light Wounds is the second spell, Fireball the third. */
    CHECK(spell_pick_keys("C") == 0x0f);
    CHECK(strcmp(script.rows, "1st Level;\tMagic Missile (2);\tCure Light Wounds;3rd Level;"
                              "\tFireball;") == 0);
    CHECK(spell_pick_keys("\r") == 0x0f);
    CHECK(spell_pick_keys("~P" "C") == 0x03);     /* Down: Cure Light Wounds */
    CHECK(spell_pick_keys("~P~P\r") == 0x2f);     /* Down skips the heading to Fireball */
    CHECK(spell_pick_keys("~P~P~H\r") == 0x03);   /* Up skips it back */
    CHECK(spell_pick_keys("~H\r") == 0x2f);       /* Up wraps past the top heading */
    CHECK(spell_pick_keys("~Q\r") == 0x2f);       /* PgDn to the last spell */
    CHECK(spell_pick_keys("~Q~I\r") == 0x0f);     /* PgUp back to the first */
    CHECK(spell_pick_keys("E") == 0);             /* Exit */
    CHECK(spell_pick_keys("~P~P\x1b") == 0);      /* Escape */
    CHECK(spell_pick_keys("") == -1);             /* the keys run out */
    /* A click on a heading is none: Down to Cure Light Wounds, the click at the first row (the
     * heading "1st Level" at row 5), then Cast gives Cure Light Wounds still. */
    click_x = 16;
    click_y = 8 * 5 + 3;
    CHECK(spell_pick_keys("~P\x01" "C") == 0x03);

    /* The screen: the rows in their colours (the heading light red, the current row light cyan,
     * the others grey), and the title "NAME's" with "Spells in Memory" at strlen(name) + 4 on row
     * 1, in grey. */
    CHECK(spell_pick_keys("") == -1);
    CHECK(row_has(&menu_game, 1, 9, 5, 0x0c) && !row_has(&menu_game, 1, 9, 5, 0x0b));
    CHECK(row_has(&menu_game, 1, 12, 6, 0x0b) && !row_has(&menu_game, 1, 12, 6, 0x0c));
    CHECK(row_has(&menu_game, 1, 12, 7, 0x07) && !row_has(&menu_game, 1, 12, 7, 0x0b));
    CHECK(row_has(&menu_game, 1, 9, 8, 0x0c));
    char name[17];
    cok_field_name(R->character, menu_c->record, name);
    size_t len = strlen(name);
    CHECK(row_has(&menu_game, (int)len + 4, (int)len + 18, 1, 0x07));
    CHECK(!row_has(&menu_game, (int)len + 3, (int)len + 3, 1, 0x07)); /* the blank cell */
    CHECK(row_has(&menu_game, (int)len + 4, (int)len + 4, 1, 0x07)); /* the 'S' at len + 4 */
    CHECK(row_has(&menu_game, (int)len + 1, (int)len + 2, 1, 0x07)); /* the name's "'s" */

    /* Mode 5 (the battle) draws the full window; the title window (the other modes) has the frame
     * under row 2 as well: the screens differ there only. */
    menu_game.vm.mode = 5;
    CHECK(spell_pick_keys("") == -1);
    memcpy(before, menu_game.screen.pixels, sizeof before);
    menu_game.vm.mode = 0;
    CHECK(spell_pick_keys("") == -1);
    memcpy(after, menu_game.screen.pixels, sizeof after);
    CHECK(memcmp(before + 8 * 320, after + 8 * 320, 8 * 320) == 0);
    CHECK(memcmp(before + 16 * 320, after + 16 * 320, 8 * 320) != 0);
    /* Which is which: row 2 as queen_full_window and queen_title_window draw it. */
    static uint8_t full[8 * 320], title[8 * 320];
    queen_full_window(&menu_game);
    memcpy(full, menu_game.screen.pixels + 16 * 320, sizeof full);
    queen_title_window(&menu_game);
    memcpy(title, menu_game.screen.pixels + 16 * 320, sizeof title);
    CHECK(memcmp(before + 16 * 320, full, sizeof full) == 0);   /* mode 5 */
    CHECK(memcmp(after + 16 * 320, title, sizeof title) == 0);  /* mode 0 */

    /* The last memorised byte (+0x156, the scan's 0x88) is listed too: Bless, level 1. */
    memset(menu_c->record + 0xce, 0, 0x89);
    menu_c->record[0xce + 0x88] = 0x01;
    CHECK(spell_pick_keys("C") == 0x01 && strcmp(script.rows, "1st Level;\tBless;") == 0);

    /* Nothing memorised: 0, nothing drawn (no menu, no row, the screen as it was). */
    memset(menu_c->record + 0xce, 0, 0x89);
    menu_game.vm.mode = 0;
    CHECK(spell_pick_keys("C") == 0 && script.rows[0] == '\0');
    memcpy(before, menu_game.screen.pixels, sizeof before);
    CHECK(spell_pick_keys("C") == 0);
    memcpy(after, menu_game.screen.pixels, sizeof after);
    CHECK(memcmp(before, after, sizeof before) == 0);

    /* The other modes stop (849a:2080 as View's Spells). */
    CHECK(queen_spell_pick(&menu_game, menu_c, 0) == -1);
    CHECK(menu_game.vm.status == COK_ECL_UNPORTED && strstr(menu_game.error, "849a:2080") != NULL);
    menu_close();
}

/* A list longer than the 17 rows shown: "Special" (0x39, level 0), "1st Level" with 13 spells
 * (rows 3-15), "2nd Level" at row 16 (the last row shown), 7 spells (rows 17-23). PgDn and PgUp
 * landing on a heading at the bottom or top row shown go one row past it, the list scrolling
 * (1b05:0ca2 through 079b and 0768). */
static void test_spell_list_long(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Cast's long spell list not checked");
        return;
    }
    static const uint8_t bytes[] = {0x39, 0x01, 0x03, 0x05, 0x06, 0x08, 0x09, 0x0a, 0x0b, 0x0c,
                                    0x0d, 0x0e, 0x0f, 0x10, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b,
                                    0x1c};
    static const uint8_t lv[7] = {1, 0, 0, 0, 9, 0, 0};
    menu_open();
    menu_game.vm.mode = 0;
    spell_stats(9, 9, 0, lv);
    memset(menu_c->record + 0xce, 0, 0x89);
    memcpy(menu_c->record + 0xce, bytes, sizeof bytes);
    CHECK(spell_pick_keys("C") == 0x39);
    CHECK(strncmp(script.rows, "Special;\tProt. From Evil, 10' Radius;1st Level;\tBless;", 54) == 0);
    /* PgDn from the first spell: the bottom row shown (16) is "2nd Level": row 17. */
    CHECK(spell_pick_keys("~Q\r") == 0x16);
    /* Down to row 18 (the top row shown is then "1st Level", row 2); PgUp: row 1, scrolling. */
    CHECK(spell_pick_keys("~P~P~P~P~P~P~P~P~P~P~P~P~P~P~P\r") == 0x17);
    CHECK(spell_pick_keys("~P~P~P~P~P~P~P~P~P~P~P~P~P~P~P~I\r") == 0x39);
    menu_close();
}

/* Cast (dqk 52db:2543, 849a:145b, 52db:1f78, 52db:1e78, 849a:21b5; queen_cast.c): the command, the
 * cast at once and by its time, the targets by kind, the abort, the dispatch's stop. Each runs on
 * CINDER in the battle (aim_open) with the keys set by cast_keys; the caster is the selected member. */
static void cast_keys(const char *keys)
{
    script.keys = keys;
    script.at = 0;
    script.menus = 0;
    script.messages[0] = '\0';
    script.all[0] = '\0';
    script.sounds[0] = '\0';
    menu_game.vm.status = COK_ECL_OK;
    menu_game.vm.abort = false;
    menu_game.vm.character = menu_c->record;
}

static bool cast_said(const char *text)
{
    return strstr(script.messages, text) != NULL;
}

/* A battle for Cast: CINDER with her memorised spells (the list's: MM 0x0f, CLW 0x03, 0x2f, 0x83) and
 * a readied cast, a foe beside her (6, 5) unless no_foe. */
static cok_character *cast_open(bool foe)
{
    aim_open();
    spell_list_set();
    menu_c->combat->may_cast = 1;
    cok_character *beside = foe ? aim_foe(6, 5, 1) : NULL;
    cast_keys("");
    return beside;
}

/* CLW's Cast (time 1) from the battle with a foe of initiative init at the turn after the caster's
 * (turn 4): whether the caster moved past it, and the seed after. */
static uint32_t cast_clw_order(uint8_t init, bool *moved)
{
    cok_combat *k = &menu_game.combat;
    cast_open(false);
    cok_character *foe = aim_foe(7, 5, 1);
    foe->combat->initiative = (int8_t)init;
    k->turn_index = 3;
    k->turn[3] = menu_c;
    k->turn[4] = foe;
    k->turn[5] = NULL;
    cast_keys("");
    bool done = false;
    CHECK(queen_cast_command(&menu_game, menu_c, 3, false, &done) && done);
    CHECK(menu_c->combat->spell == 3 && menu_c->combat->initiative == 4);
    *moved = k->turn[3] == foe;
    return menu_game.vm.seed;
}

static void test_cast_command(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the Cast command not checked");
        return;
    }
    cok_combat *k = &menu_game.combat;
    bool done = true;
    /* C, then Escape at the list: the arena and the command menu again (no draw, the turn kept). */
    cast_open(false);
    uint32_t seed = menu_game.vm.seed;
    CHECK(menu_run("C\x1b") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->initiative == 5 && menu_game.vm.seed == seed && script.menus >= 2);
    CHECK(strstr(script.all, "Cast") != NULL);
    /* A camp-only spell (Read Magic 0x12, the word at +0x0f 0): "Camp Only Spell" on the bar, no turn
     * ended. */
    cast_open(false);
    cast_keys("");
    CHECK(queen_cast_command(&menu_game, menu_c, 0x12, true, &done) && !done);
    CHECK(cast_said("Camp Only Spell") && menu_c->combat->initiative == 5);
    /* Underwater (var 0x3c) with 0x2f: "That has no effect underwater!". */
    cast_keys("");
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(queen_cast_command(&menu_game, menu_c, 0x2f, true, &done) && !done);
    CHECK(cast_said("That has no effect underwater!"));
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 0);
    /* CLW (time 5 / 3 = 1): "Begins Casting", the spell kept, the initiative less 1, the caster moved
     * past a higher initiative (the list's next), the turn ended for the cast, turn_index less one. */
    cast_open(false);
    cok_character *foe = aim_foe(7, 5, 1);
    foe->combat->initiative = 6;
    k->turn_index = 3;
    k->turn[3] = menu_c;
    k->turn[4] = foe;
    k->turn[5] = NULL;
    CHECK(queen_cast_command(&menu_game, menu_c, 3, false, &done) && done);
    CHECK(menu_c->combat->spell == 3 && menu_c->combat->initiative == 4);
    CHECK(k->turn[3] == foe && k->turn[4] == menu_c && k->turn_index == 2);
    /* Floor 1: an initiative of 1 stays 1. */
    menu_c->combat->initiative = 1;
    k->turn_index = 3;
    k->turn[3] = menu_c;
    k->turn[4] = NULL;
    CHECK(queen_cast_command(&menu_game, menu_c, 3, false, &done) && done);
    CHECK(menu_c->combat->initiative == 1 && k->turn[3] == menu_c && k->turn_index == 2);
    /* A tie with the foe's initiative is the 1d2's (60f4:1216): the die is the cast's last draw, so
     * a foe of 4 (a tie) leaves the seed one die further on than a foe of 6 (no die), and a foe of 3
     * (below) leaves it where the foe of 6 does; the die decides the order. */
    bool moved6, moved4, moved3;
    uint32_t s6 = cast_clw_order(6, &moved6);
    uint32_t s4 = cast_clw_order(4, &moved4);
    uint32_t s3 = cast_clw_order(3, &moved3);
    CHECK(moved6 && !moved3 && s3 == s6);
    uint32_t die_seed = s6;
    uint8_t die = cok_dice(&die_seed, 1, 2);
    CHECK(s4 == die_seed && moved4 == (die == 1));
    /* MM at once (time 0): the Aim gives the foe (N, then T), the cast runs, the turn ends, the spell is
     * forgotten (its byte marked 0x80), the foe is hurt. */
    foe = cast_open(true);
    cast_keys("NT");
    uint8_t hp_before = cok_field_u8(R->character, foe->record, COK_FIELD_HIT_POINTS);
    CHECK(queen_cast_command(&menu_game, menu_c, 0x0f, false, &done) && done);
    CHECK(menu_c->combat->initiative == 0 && (menu_c->record[0xce] & 0x80) != 0); /* MM is first in the list */
    CHECK(cok_field_u8(R->character, foe->record, COK_FIELD_HIT_POINTS) < hp_before);
    menu_close();
}

/* The targets (52db:1f78) by kind, the abort (52db:2543's Abort Spell) and the dispatch (849a:21b5). */
static void test_cast_targets(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the cast's targets not checked");
        return;
    }
    cok_combat *k = &menu_game.combat;
    bool done = false;
    cok_character *beside = cast_open(true);
    /* Kind 0 (Shield 0x13: the caster alone, no Aim): entry 1 is herself, one target. */
    cast_keys("");
    CHECK(queen_targets(&menu_game, menu_c, 0x13, false, &done) && done);
    CHECK(menu_game.spell_targets[1] == menu_c && menu_game.spell_target_count == 1);
    /* Kind 4 (CLW): Target on herself, one pick; Exit with no pick: done clear, the cell unchanged. */
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 3, false, &done) && done);
    CHECK(menu_game.spell_targets[1] == menu_c && menu_game.spell_target_count == 1);
    cast_keys("E");
    CHECK(queen_targets(&menu_game, menu_c, 3, false, &done) && !done);
    /* The cell is the caster's (set at the start, as 1f78 does; picks 0 leaves it). */
    CHECK(menu_game.spell_target_count == 0 && menu_game.spell_x == 5 && menu_game.spell_y == 5);
    /* Entry 0 is cleared, a stale entry past the count is kept. */
    CHECK(menu_game.spell_targets[0] == NULL);
    /* Kind 6 (0x17, three picks): herself refused ("Cannot target yourself"), then the foe, then the
     * foe again ("Already been targeted"), then Exit: one target, the foe, the cell her foe's. */
    cast_keys("TNTTE");
    done = true;
    CHECK(queen_targets(&menu_game, menu_c, 0x17, false, &done) && done);
    CHECK(cast_said("Cannot target yourself") && cast_said("Already been targeted"));
    CHECK(menu_game.spell_target_count == 1 && menu_game.spell_targets[1] == beside);
    CHECK(menu_game.spell_x == 6 && menu_game.spell_y == 5 && menu_game.effects.rolls.images == 0);
    /* An area (0x2f with var 0x22 0: radius 2) around herself, Target: both sides listed, images 1. */
    cast_keys("T");
    done = true;
    aim_foe(4, 5, 0);
    CHECK(queen_targets(&menu_game, menu_c, 0x2f, false, &done) && done);
    CHECK(menu_game.effects.rolls.images == 1 && menu_game.spell_target_count >= 3);
    bool foe_listed = false, ally_listed = false;
    for (unsigned i = 1; i <= menu_game.spell_target_count; ++i) {
        if (menu_game.spell_targets[i] == beside) foe_listed = true;
        if (menu_game.spell_targets[i] != NULL &&
            cok_field_u8(R->character, menu_game.spell_targets[i]->record, COK_FIELD_HOSTILE) == 0)
            ally_listed = true;
    }
    CHECK(foe_listed && ally_listed && k->listed_count >= 2);
    /* Only entry 0 is cleared: a one-target cast after the area keeps the area's stale entry 2. */
    cok_character *stale = menu_game.spell_targets[2];
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 3, false, &done) && done && menu_game.spell_target_count == 1);
    CHECK(stale != NULL && menu_game.spell_targets[2] == stale);
    /* Abort: Exit at the Aim, then Yes: "Spell Aborted", the spell marked, not done, the turn kept. */
    cast_open(false);
    cast_keys("EY");
    done = true;
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && !done);
    CHECK(cast_said("Spell Aborted") && menu_c->record[0xce + 1] == 0x83);
    /* No: the Aim again, then Exit and Yes: the keys all read. */
    cast_open(false);
    cast_keys("ENEY");
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && !done && script.at == 4);
    CHECK(cast_said("Spell Aborted"));
    /* The dispatch: an unported spell (0x13 Shield, kind 0, no Aim) runs its flight and forgets it,
     * then stops at its cited routine (7fd8:056a). The byte is marked (0x93). */
    cast_open(false);
    menu_c->record[0xce] = 0x13;
    cast_keys("");
    CHECK(!queen_cast(&menu_game, 0x13, false, true, &done));
    CHECK(menu_game.vm.status == COK_ECL_UNPORTED && strstr(menu_game.error, "7fd8:056a") != NULL);
    CHECK(menu_c->record[0xce] == 0x93);
    /* A ported handler runs: CLW on herself (the Aim's T), the spell marked, the cast over. */
    cast_open(false);
    cast_keys("T");
    done = true;
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && done && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_game.effects.rolls.spell == 0 && menu_game.effects.rolls.images == 0);
    menu_close();
}

/* The forget's place (dqk 849a:145b): a spell is forgotten after its flight, not before it (a flight that
 * stops, the Fireball family's picture, leaves the byte), and an item's cast forgets nothing and says no
 * "Casts a Spell" (the announce is the spell's own, 7fd8:45d7). */
static void test_cast_forget(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the cast's forget and announce not checked");
        return;
    }
    bool done = false;
    /* A Fireball (0x2f) whose flight stops at 51c2:0fc8: the stop comes before the forget, so the byte
     * stays 0x2f (unmarked). */
    cast_open(true);
    menu_c->record[0xce] = 0x2f;
    cast_keys("T");
    CHECK(!queen_cast(&menu_game, 0x2f, false, true, &done));
    CHECK(menu_game.vm.status == COK_ECL_UNPORTED && strstr(menu_game.error, "51c2:0fc8") != NULL);
    CHECK(menu_c->record[0xce] == 0x2f);
    /* A ported spell (CLW 0x03) cast from the list: the announce, then the byte marked. */
    cast_open(true);
    menu_c->record[0xce] = 0x03;
    cast_keys("T");
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && menu_game.vm.status == COK_ECL_OK);
    CHECK(strstr(script.prints, "Casts a Spell") != NULL && menu_c->record[0xce] == 0x83);
    /* An item's cast (rolls.item set): no announce, the byte kept. */
    cast_open(true);
    menu_c->record[0xce] = 0x03;
    cast_keys("T");
    menu_game.effects.rolls.item = 1;
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && menu_game.vm.status == COK_ECL_OK);
    menu_game.effects.rolls.item = 0;
    CHECK(strstr(script.prints, "Casts a Spell") == NULL && menu_c->record[0xce] == 0x03);
    menu_close();
}

/* queen_commands' pending cast (5045:08e3): a spell pending is cast first (its Aim's T), the spell
 * cleared, the turn ended, and no menu is built (no "menu" log). Called directly. */
static void test_pending_cast(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the pending cast not checked");
        return;
    }
    cast_open(false);
    menu_c->combat->spell = 3;
    menu_c->combat->may_cast = 1;
    CHECK(menu_run("T") && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->spell == 0 && menu_c->combat->initiative == 0);
    CHECK(strstr(script.all, "Next Prev Manual") != NULL && strstr(script.all, "Guard") == NULL);
    menu_close();
}

/* The battle's dispatch of a pending spell (cok_combat_act, 5045:078b): no stop (the descriptor's
 * spell_unported is NULL), the cast's Aim reads "T", the spell cleared and the turn ended. */
static void test_pending_act(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the pending spell's dispatch not checked");
        return;
    }
    cast_open(false);
    menu_c->combat->spell = 3;
    cast_keys("T");
    CHECK(cok_combat_act(&menu_game, menu_c, false) && menu_game.vm.status == COK_ECL_OK);
    CHECK(menu_c->combat->spell == 0 && menu_c->combat->initiative == 0);
    CHECK(strstr(script.all, "Guard") == NULL);
    menu_close();
}

/* 1f78's kind 5 (Faerie Fire 0x4f, Charm Monsters 0x51) with two foes (6, 5) and (7, 5) of the
 * levels (+0x91) or sizes (+0x8a) given: queen_targets with keys, the keys read and the count. */
static unsigned cast_several(uint8_t spell, uint8_t field_at, uint8_t a, uint8_t b, const char *keys,
                             size_t *read, bool *done)
{
    cast_open(false);
    cok_character *f1 = aim_foe(6, 5, 1), *f2 = aim_foe(7, 5, 1);
    f1->record[field_at] = a;
    f2->record[field_at] = b;
    cast_keys(keys);
    *done = false;
    CHECK(queen_targets(&menu_game, menu_c, spell, false, done) && menu_game.vm.status == COK_ECL_OK);
    *read = script.at;
    return menu_game.spell_target_count;
}

/* The kinds the cast tests above leave out (dqk 52db:1f78, 1e78; 849a:145b's flight sound; 2543's
 * underwater 0x7c and the time-0 abort; the pending cast's clear before the cast). */
static void test_cast_kinds(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; the cast's kinds not checked");
        return;
    }
    bool done = false;
    size_t read = 0;
    /* Charm Monsters (0x51): 2d4 allowance, the weight by the highest level (+0x91). Two foes of level
     * 4 weigh 8 each: the second pick passes any allowance and ends the picks (the Exit unread). */
    CHECK(cast_several(0x51, 0x91, 4, 4, "NTNTE", &read, &done) == 2 && done && read == 4);
    /* Level 1 weighs 1: two picks never pass the 2d4 (at least 2), the Exit ends them. */
    CHECK(cast_several(0x51, 0x91, 1, 1, "NTNTE", &read, &done) == 2 && done && read == 5);
    /* One pick never stops by weight (picks > 1): level 4 then the Exit. */
    CHECK(cast_several(0x51, 0x91, 4, 4, "NTE", &read, &done) == 1 && done && read == 3);
    /* The same foe again: "Already been targeted", one target. Exit at once: done stays set (1f78's
     * kind 5 never clears it), no target. */
    CHECK(cast_several(0x51, 0x91, 1, 1, "NTTE", &read, &done) == 1 && done && read == 4);
    CHECK(cast_said("Already been targeted"));
    CHECK(cast_several(0x51, 0x91, 1, 1, "E", &read, &done) == 0 && done && read == 1);
    /* Faerie Fire (0x4f): the allowance is the caster level (177e:1900 kind 1: ranger 9 - 7 = 2, no
     * die), the weight by the size less one (+0x8a 3: 2). Two of size 3 weigh 4 > 2: the picks end. */
    CHECK(cast_several(0x4f, 0x8a, 3, 3, "NTNTE", &read, &done) == 2 && read == 4);
    /* Size 0 (less one: 0xff) weighs nothing, size 1 weighs 1 (total 1 <= 2): the Exit ends them. */
    CHECK(cast_several(0x4f, 0x8a, 0, 1, "NTNTE", &read, &done) == 2 && read == 5);
    /* The areas (kinds 8-0x0e, picked as cells too): 0x57 is radius 3 underwater, else +7 & 7 (2);
     * a foe 3 east of CINDER is listed only underwater. 0x33 is radius 2 underwater, else 0 (only the
     * picked cell). */
    cast_open(false);
    cok_character *near = aim_foe(6, 5, 1), *far = aim_foe(8, 5, 1);
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 1);
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 0x57, false, &done) && done);
    bool far_listed = false, near_listed = false;
    for (unsigned i = 1; i <= menu_game.spell_target_count; ++i) {
        far_listed |= menu_game.spell_targets[i] == far;
        near_listed |= menu_game.spell_targets[i] == near;
    }
    CHECK(far_listed && near_listed && menu_game.effects.rolls.images == 1);
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 0x33, false, &done) && done);
    near_listed = false;
    for (unsigned i = 1; i <= menu_game.spell_target_count; ++i)
        near_listed |= menu_game.spell_targets[i] == near;
    CHECK(near_listed);
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 0);
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 0x57, false, &done) && done);
    far_listed = near_listed = false;
    for (unsigned i = 1; i <= menu_game.spell_target_count; ++i) {
        far_listed |= menu_game.spell_targets[i] == far;
        near_listed |= menu_game.spell_targets[i] == near;
    }
    CHECK(!far_listed && near_listed);
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 0x33, false, &done) && done);
    CHECK(menu_game.spell_target_count == 1 && menu_game.spell_targets[1] == menu_c);
    /* Silence (0x19, kind 0x0f) on a foe: that one alone, no image (1f78's start clears the area's). */
    cast_keys("NT");
    CHECK(queen_targets(&menu_game, menu_c, 0x19, false, &done) && done);
    CHECK(menu_game.spell_target_count == 1 && menu_game.spell_targets[1] == near);
    CHECK(menu_game.effects.rolls.images == 0);
    /* An area left with Exit: not done, the count 0 (1f78's start), the caster's target cleared (1e78
     * writes Aim's, NULL, always). */
    cast_keys("T");
    CHECK(queen_targets(&menu_game, menu_c, 0x57, false, &done) && done);
    menu_c->combat->target = near->record;
    cast_keys("E");
    CHECK(queen_targets(&menu_game, menu_c, 0x57, false, &done) && !done);
    CHECK(menu_game.spell_target_count == 0 && menu_c->combat->target == NULL);
    /* Herself twice with 0x6c (kind 7, four picks; not 0x17 or 0x31): the second is refused with
     * "Cannot target yourself" (the text is by the record, whatever the spell). */
    cast_keys("TTE");
    CHECK(queen_targets(&menu_game, menu_c, 0x6c, false, &done) && done);
    CHECK(cast_said("Cannot target yourself") && !cast_said("Already been targeted"));
    CHECK(menu_game.spell_target_count == 1);
    /* 0x6c with two foes: the near one, the far one, the near one again (refused: 1e78 set the cell
     * to its), then Exit: the cell is the last target's again, the far one's. */
    cast_open(false);
    near = aim_foe(6, 5, 1);
    far = aim_foe(8, 5, 1);
    cast_keys("NTNTNTE");
    CHECK(queen_targets(&menu_game, menu_c, 0x6c, false, &done) && done);
    CHECK(menu_game.spell_target_count == 2 && menu_game.spell_targets[1] == near);
    CHECK(menu_game.spell_targets[2] == far && cast_said("Already been targeted"));
    CHECK(menu_game.spell_x == 8 && menu_game.spell_y == 5);
    /* Underwater, 0x7c is refused too (the sixth id). */
    cast_keys("");
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 1);
    CHECK(queen_cast_command(&menu_game, menu_c, 0x7c, true, &done) && !done);
    CHECK(cast_said("That has no effect underwater!"));
    cok_ecl_set_var(&menu_game.vm, COK_VAR_RECORD_EFFECT, 0);
    /* MM at once, aborted (Exit, Yes): the turn still ends (2543 ends it whatever 145b gave), the
     * spell is marked. */
    cast_open(true);
    cast_keys("EY");
    CHECK(queen_cast_command(&menu_game, menu_c, 0x0f, false, &done) && done);
    CHECK(cast_said("Spell Aborted") && menu_c->combat->initiative == 0 && menu_c->record[0xce] == 0x8f);
    /* MM's flight sounds 3 (2023:0006), before the damage's flash (4). */
    cast_open(true);
    cast_keys("NT");
    CHECK(queen_cast_command(&menu_game, menu_c, 0x0f, false, &done) && done);
    CHECK(strncmp(script.sounds, "3;", 2) == 0);
    /* CLW on herself: the flight's arc search from facing 0 finds 0 (her own cell), and the pose
     * (5837:09ab) writes it, whatever she faced. */
    cast_open(false);
    menu_c->combat->facing = 2;
    cast_keys("T");
    CHECK(queen_cast(&menu_game, 3, false, true, &done) && done && menu_c->combat->facing == 0);
    /* A pending spell is cleared before its cast: one that stops (Shield's handler, 7fd8:056a) leaves
     * none pending. */
    cast_open(false);
    menu_c->record[0xce] = 0x13;
    menu_c->combat->spell = 0x13;
    CHECK(!menu_run("") && strstr(menu_game.error, "7fd8:056a") != NULL);
    CHECK(menu_c->combat->spell == 0);
    menu_close();
}

/* Aim's Manual on an empty cell with empty set (C1): Target picks the cell, res is (NULL, cell); the
 * tail redraws the cell queen_cell_of gives (no record is read). */
static void test_command_aim_empty(void)
{
    if (!have_assets()) {
        puts("dark queen: DQKAssets not found; Aim's Manual on an empty cell not checked");
        return;
    }
    aim_open();
    bool done = true;
    cok_combat_aimed res;
    CHECK(aim_direct("M~MT", 0xfe, 0, 1, 0, &done, &res) && menu_game.vm.status == COK_ECL_OK);
    CHECK(done && res.target == NULL && res.x == 6 && res.y == 5);
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
    test_auto_seed_line();
    test_timer_walk();
    test_command_menu_items();
    test_command_turn();
    test_command_menu_text();
    test_command_menu_keys();
    test_combat_menu_areas();
    test_command_move();
    test_command_aim();
    test_command_aim_manual();
    test_command_menu_quick_speed();
    test_command_menu_auto();
    test_arena_say();
    test_member_say();
    test_bandage();
    test_alt_x();
    test_yell();
    test_view_combat();
    test_view_items_combat();
    test_spell_usable();
    test_spell_list();
    test_spell_list_long();
    test_cast_command();
    test_cast_targets();
    test_command_aim_empty();
    test_cast_forget();
    test_pending_cast();
    test_pending_act();
    test_cast_kinds();
    test_pass_draw();
    test_bar_message_place();
    test_text_delay_state();
    test_moves_left_text();
    test_battle_clear();
    test_battle_text();
    test_battle_bar();
    test_battle_waits();
    test_computer_keys();
    puts("dark queen tests passed");
    return 0;
}
