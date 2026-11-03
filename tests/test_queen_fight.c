/* The Dark Queen of Krynn's battle where it differs from Champions' and Death
 * Knights' (dqk 52db:4dd2, 4d50, the computer flags and leadership before 59c7:1dd7;
 * its var 0x36, FLED): on records made by hand in its layout, the game opened on
 * DQKAssets/ (skipped without). */
#include "adventure.h"
#include "ecl.h"
#include "party.h"
#include "round.h"
#include "rules.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static const cok_rules *const R = &cok_rules_dark_queen;
static cok_adventure game;
static char log_text[1 << 14];

static void log_line(cok_adventure *g, const char *kind, const char *text, void *context)
{
    (void)g, (void)context;
    size_t used = strlen(log_text);
    snprintf(log_text + used, sizeof log_text - used, "%s: %s;", kind, text);
}

static int no_key(void *context)
{
    (void)context;
    return -1;
}

/* A seed whose first d(sides) rolls are in their bounds (the triples: sides, low, high). */
static uint32_t seed_from(unsigned n, const unsigned *triples)
{
    for (uint32_t start = 1;; ++start) {
        uint32_t seed = start;
        bool ok = true;
        for (unsigned i = 0; i < n && ok; ++i) {
            unsigned d = cok_dice(&seed, 1, (uint8_t)triples[3 * i]);
            ok = d >= triples[3 * i + 1] && d <= triples[3 * i + 2];
        }
        if (ok) return start;
    }
}

/* A party record in Dark Queen's layout, named by a letter: fighting level 3 (its class
 * level 1), hostile or not, an NPC (the flag 0x80 at +0x9b) or not, intelligence 10. */
static cok_character *record(char name, uint8_t hostile, int npc)
{
    const cok_character_layout *L = R->character;
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL && cok_party_append(&game.party, c));
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 0, 1);
    cok_field_set8_at(L, c->record, COK_FIELD_NAME, 1, (uint8_t)name);
    cok_field_set8(L, c->record, COK_FIELD_HOSTILE, hostile);
    cok_field_set8(L, c->record, COK_FIELD_NPC, npc ? 0x80 : 0);
    cok_field_set8_at(L, c->record, COK_FIELD_SCORE, COK_SCORE_INTELLIGENCE, 10);
    cok_field_set8_at(L, c->record, COK_FIELD_CLASS_LEVEL, 1, 3);
    cok_field_set8(L, c->record, COK_FIELD_HIT_POINTS, 10);
    return c;
}

static uint8_t computer(cok_character *c)
{
    return cok_field_u8(R->character, c->record, COK_FIELD_COMPUTER);
}

/* Leadership (dqk 52db:4dd2): the computer flags (+0x18c: 1 for a hostile, 0x80 for an NPC,
 * 0 else) before the leaders; a leader of order 1 (chance 20) leads the NPC on a roll below
 * 20 (no announcement: nothing logged), and the NPC's flag then clears; a roll of 60 leads
 * nothing and the flag holds. Dark Queen's 4d50 keeps no chance from one knight to the next
 * (its own frame): a knight of no order stops the run, where dkk's would use the last chance. */
static void test_leadership(void)
{
    const cok_character_layout *L = R->character;
    for (int lead = 0; lead < 2; ++lead) {
        cok_party_free(&game.party);
        log_text[0] = '\0';
        game.vm.status = COK_ECL_OK;
        cok_character *a = record('A', 0, 0), *n = record('N', 0, 1), *m = record('M', 1, 0);
        cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 1);
        const unsigned lead_dice[] = {100, 1, 19};
        const unsigned hold_dice[] = {100, 60, 100};
        game.vm.seed = seed_from(1, lead ? lead_dice : hold_dice);
        CHECK(cok_combat_leadership(&game) == true);
        CHECK(game.vm.status == COK_ECL_OK && strstr(log_text, "print:") == NULL);
        CHECK(computer(m) == 1);
        CHECK(computer(n) == (lead ? 0 : 0x80));
        CHECK(computer(a) == 0);
    }
    /* A leader of order 1 (its chance 20) does not lead the NPC on 100; then a leader of no
     * order: Dark Queen's chance is not kept from one call to the next, so it stops. */
    cok_party_free(&game.party);
    game.vm.status = COK_ECL_OK;
    cok_character *a = record('A', 0, 0), *b = record('B', 0, 0), *n = record('N', 0, 1);
    cok_field_set8(L, a->record, COK_FIELD_KNIGHT_ORDER, 1);
    cok_field_set8(L, b->record, COK_FIELD_KNIGHT_ORDER, 0);
    const unsigned dice[] = {100, 60, 100};
    game.vm.seed = seed_from(1, dice);
    CHECK(!cok_combat_leadership(&game) && game.vm.status == COK_ECL_UNDEFINED &&
          strstr(game.error, "52db:4d50") != NULL && computer(n) == 0x80);
    game.vm.status = COK_ECL_OK;
    cok_party_free(&game.party);
}

/* Dark Queen's var 0x36 is FLED (59c7:1e3e clears it, 52db:2aef sets it). */
static void test_fled(void)
{
    CHECK(cok_ecl_has_var(R, COK_VAR_FLED) && R->ecl.vars[COK_VAR_FLED].address == 0x36);
    cok_ecl_set_var(&game.vm, COK_VAR_FLED, 1);
    CHECK(cok_ecl_var(&game.vm, COK_VAR_FLED) == 1);
    cok_ecl_set_var(&game.vm, COK_VAR_FLED, 0);
    CHECK(cok_ecl_var(&game.vm, COK_VAR_FLED) == 0);
}

int main(void)
{
    struct stat st;
    if (stat("DQKAssets/DISK2/DUNGCOM.TLB", &st) != 0) {
        puts("queen fight: no DQKAssets/; the battle's leadership not checked");
        return 0;
    }
    cok_keyboard keys = {no_key, NULL, NULL, NULL};
    cok_adventure_hooks hooks = {.log = log_line};
    CHECK(cok_adventure_open(&game, R, "DQKAssets", &keys, &hooks));
    test_leadership();
    test_fled();
    cok_party_free(&game.party);
    puts("queen fight tests passed");
    return 0;
}
