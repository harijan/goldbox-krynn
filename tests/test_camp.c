#define _POSIX_C_SOURCE 200809L /* mkdtemp */

#include "camp.h"
#include "magic.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

/* Keys from a string; \x01 stands for the 0 that starts an extended key,
 * and \x02 makes the next key wait while the party rests (KeyPressed). */
typedef struct {
    const char *keys;
    size_t at, length;
    char log[8192];
} script;

static int scripted(void *context)
{
    script *s = context;
    while (s->at < s->length && s->keys[s->at] == 2) ++s->at;
    if (s->at == s->length) return -1;
    char c = s->keys[s->at++];
    return c == 1 ? 0 : (unsigned char)c;
}

static bool pending(cok_adventure *game, void *context)
{
    (void)game;
    script *s = context;
    if (s->at == s->length || s->keys[s->at] != 2) return false;
    ++s->at;
    return true;
}

static void log_line(cok_adventure *game, const char *kind, const char *text, void *context)
{
    (void)game;
    script *s = context;
    size_t used = strlen(s->log);
    snprintf(s->log + used, sizeof s->log - used, "%s: %s;", kind, text);
}

static script s;
static cok_adventure game;

static void open_game(void)
{
    cok_keyboard keys = {scripted, &s};
    cok_adventure_hooks hooks = {.log = log_line, .key_pending = pending, .context = &s};
    CHECK(cok_adventure_open(&game, "Assets", &keys, &hooks));
    game.vm.mode = 4;
    game.vm.mem4b00[0xe6] = 1;
}

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

/* A character who can act, with hit points hp of max, and a level in
 * class. */
static cok_character *member(const char *name, uint8_t hp, uint8_t max, size_t class)
{
    cok_character *c = calloc(1, sizeof *c);
    CHECK(c != NULL);
    c->record[0] = (uint8_t)strlen(name);
    memcpy(c->record + 1, name, strlen(name));
    c->record[0x197] = hp;
    c->record[0x62] = max;
    c->record[0x189] = 1;
    c->record[0xf9 + class] = 1;
    return c;
}

static cok_character *add(cok_character *c)
{
    CHECK(cok_party_add(&game.party, c));
    ++game.vm.mem7c00[0x33e];
    if (game.vm.character == NULL) game.vm.character = c->record;
    return c;
}

static void empty_party(void)
{
    cok_party_free(&game.party);
    game.vm.character = NULL;
    game.vm.mem7c00[0x33e] = 0;
}

/* Give c a scroll (type 0x27) of the order of magic 1 with spells a, b, c
 * and a count of uses. */
static uint8_t *scroll(cok_character *c, uint8_t a, uint8_t b, uint8_t d, uint8_t uses)
{
    c->items = realloc(c->items, (c->item_count + 1) * sizeof *c->items);
    CHECK(c->items != NULL);
    uint8_t *item = c->items[c->item_count++];
    memset(item, 0, COK_ITEM_SIZE);
    item[0x2e] = 0x27;
    item[0x2f] = uses;
    item[0x35] = 0x20;
    item[0x3c] = a;
    item[0x3d] = b;
    item[0x3e] = d;
    return item;
}

static uint16_t *clock_words(void)
{
    return &game.vm.mem4b00[0xc6];
}

static void set_rest(unsigned days, unsigned hours, unsigned tens, unsigned minutes)
{
    memset(game.rest, 0, sizeof game.rest);
    game.rest[4] = (uint16_t)days;
    game.rest[3] = (uint16_t)hours;
    game.rest[2] = (uint16_t)tens;
    game.rest[1] = (uint16_t)minutes;
}

static void test_rest_time(void)
{
    /* Borrowing: an hour less five minutes is 0:55. */
    set_rest(0, 1, 0, 0);
    cok_camp_subtract(&game, 5, 1);
    CHECK(game.rest[3] == 0 && game.rest[2] == 5 && game.rest[1] == 5);
    /* With nothing to borrow from up to the days, it is all gone. */
    set_rest(0, 0, 0, 3);
    cok_camp_subtract(&game, 5, 1);
    for (size_t i = 0; i < 7; ++i) CHECK(game.rest[i] == 0);
    set_rest(0, 2, 0, 0);
    cok_camp_subtract(&game, 1, 4);
    CHECK(game.rest[3] == 0);
    /* A day of hours carries into a day, which moves the moons a day as
     * the clock does; months fold into days, at most 99. */
    uint16_t *moon_days = &game.vm.mem4b00[0x1fc];
    moon_days[0] = moon_days[1] = moon_days[2] = 0;
    set_rest(0, 30, 0, 0);
    cok_camp_normalize(&game);
    CHECK(game.rest[4] == 1 && game.rest[3] == 6 && moon_days[0] == 1);
    set_rest(5, 0, 0, 0);
    game.rest[5] = 4;
    cok_camp_normalize(&game);
    CHECK(game.rest[4] == 99 && game.rest[5] == 0);
}

static void test_preparation(void)
{
    cok_character *c = member("A", 1, 1, 5);
    /* Levels 1 and 2 to memorize: 4 hours and 15 minutes a level. */
    c->record[0x1e] = 0x80 | 0x12;   /* Read Magic, level 1 */
    c->record[0x1f] = 0x80 | 0x20;   /* Mirror Image, level 2 */
    CHECK(cok_camp_preparation(c) == 4 * 60 + 3 * 15 && c->record[0x58] == 4);
    /* A level-0 power counts as level 1 to memorize, but as 0 on a scroll. */
    memset(c->record + 0x1e, 0, 0x3a);
    c->record[0x1e] = 0x80 | 101;
    CHECK(cok_camp_preparation(c) == 4 * 60 + 15);
    memset(c->record + 0x1e, 0, 0x3a);
    scroll(c, 0x80 | 101, 0, 0, 100);
    CHECK(cok_camp_preparation(c) == 0 && c->record[0x58] == 0);
    /* Above level 2, 6 hours; the levels add as a byte. */
    for (size_t i = 0; i < 0x3a; ++i) c->record[0x1e + i] = 0x80 | 0x5c; /* Cone of Cold, 5 */
    CHECK(cok_camp_preparation(c) == 6 * 60 + (uint8_t)(58 * 5) * 15 && c->record[0x58] == 6);
    cok_camp_forget(c);
    CHECK(c->record[0x1e] == 0 && c->record[0x58] == 0 && c->items[0][0x3c] == 101);
    cok_character_free(c);
    free(c);
}

static void test_heal(void)
{
    uint8_t r[COK_CHARACTER_SIZE] = {0};
    r[0x62] = 255;
    r[0x197] = 255;
    /* Hit points add as a byte: 255 and 1 make 0. */
    CHECK(cok_character_heal(r, 1, false, 2) && r[0x197] == 0);
    CHECK(!cok_character_heal(r, 1, true, 2) || r[0x197] == 1);
    /* An unconscious or dying character recovers outside combat. */
    r[0x62] = 10;
    r[0x197] = 0;
    r[0x188] = 5;
    r[0x189] = 0;
    CHECK(cok_character_heal(r, 3, false, 2) && r[0x188] == 0 && r[0x189] == 1 && r[0x197] == 3);
    r[0x188] = 5;
    r[0x189] = 0;
    CHECK(cok_character_heal(r, 30, false, 5) && r[0x188] == 4 && r[0x197] == 10);
    r[0x188] = 6;
    CHECK(!cok_character_heal(r, 1, false, 2));
}

static void test_rest(void)
{
    empty_party();
    cok_character *a = add(member("ARN", 5, 9, 0));
    cok_character *b = add(member("BEA", 9, 9, 5));
    game.vm.mode = 2;
    uint16_t *clock = clock_words();
    memset(clock, 0, 7 * sizeof *clock);
    game.vm.mem7c00[0x2d2] = 0;
    /* Twelve minutes take three ticks of five. */
    keys("");
    set_rest(0, 0, 1, 2);
    game.heal_ticks = 0x11e;
    CHECK(!cok_camp_rest(&game, false));
    CHECK(clock[1] == 5 && clock[2] == 1);
    /* The 288th tick, counted across rests, heals each member a point. */
    CHECK(strcmp(s.log, "print: The Whole Party Is Healed;") == 0);
    CHECK(a->record[0x197] == 6 && b->record[0x197] == 9 && game.heal_ticks == 1);

    /* A spell to memorize after no hours of preparation is learned on the
     * first tick; the next waits 3 ticks a level. */
    a->record[0x1e] = 0x80 | 0x03;   /* Cure Light Wounds, level 1 */
    a->record[0x1f] = 0x80 | 0x3a;   /* Cure Serious Wounds, level 4 */
    keys("");
    set_rest(0, 0, 0, 5);
    CHECK(!cok_camp_rest(&game, false));
    CHECK(strcmp(s.log, "print: ARN;print: has memorized;print: Cure Light Wounds;") == 0);
    CHECK(a->record[0x1e] == 0x03 && a->record[0x1f] == 0xba && game.learn_ticks[0] == 12);
    a->record[0x1f] = 0;
    /* Hours of preparation count down each twelfth tick, after the tick's
     * learning; then the first spell waits 3 ticks a level. */
    a->record[0x58] = 1;
    a->record[0x1f] = 0x80 | 0x03;
    keys("");
    set_rest(0, 1, 0, 0);
    CHECK(!cok_camp_rest(&game, false));
    CHECK(a->record[0x58] == 0 && a->record[0x1f] == 0x83 && game.learn_ticks[0] == 3);
    /* Each rest starts the timers over, so another rest learns it on its
     * first tick. */
    keys("");
    set_rest(0, 0, 0, 5);
    CHECK(!cok_camp_rest(&game, false));
    CHECK(a->record[0x1f] == 0x03);

    /* Scrolls first: only spells above 0x80 are scribed, though 0x80
     * counts toward the time. Used below 100, the scroll is gone, and the
     * walk ends with it. */
    scroll(b, 0x80 | 0x12, 0x80, 0, 100);
    scroll(b, 0, 0, 0, 150);
    scroll(b, 0x80 | 0x13, 0, 0, 150);
    keys("");
    set_rest(0, 0, 0, 5);
    CHECK(!cok_camp_rest(&game, false));
    CHECK(b->item_count == 2 && b->items[1][0x3c] == (0x80 | 0x13) && b->record[0x62 + 0x12] == 1);
    CHECK(game.learn_ticks[1] == 0);
    keys("");
    set_rest(0, 0, 0, 5);
    CHECK(!cok_camp_rest(&game, false));
    CHECK(b->item_count == 2 && b->items[1][0x3c] == 0 && b->items[1][0x2f] == 149);
    CHECK(strstr(s.log, "print: BEA;print: has scribed;print: Shield;") != NULL);
    /* 0x80 alone (spell 0 marked) is not scribed. */
    scroll(b, 0x80, 0x80 | 0x13, 0, 150);
    keys("");
    set_rest(0, 0, 0, 5);
    CHECK(!cok_camp_rest(&game, false) && game.vm.status == COK_ECL_OK);
    CHECK(b->items[2][0x3c] == 0x80 && b->items[2][0x3d] == 0 && b->items[2][0x2f] == 149);

    /* An encounter: every second tick, a roll of 1-100 up to 0x7ed3. The
     * count (DS:4b53) carries over between rests. */
    game.vm.mem7c00[0x2d2] = 2;
    game.vm.mem7c00[0x2d3] = 100;
    game.rest_ticks = 1;
    memset(clock, 0, 7 * sizeof *clock);
    keys("");
    set_rest(0, 1, 0, 0);
    CHECK(cok_camp_rest(&game, false));
    CHECK(clock[1] == 5 && clock[2] == 0 && game.rest_ticks == 0);
    CHECK(strstr(s.log, "print: Your repose is suddenly interrupted!;") != NULL);
    game.vm.mem7c00[0x2d3] = 0;
    memset(clock, 0, 7 * sizeof *clock);
    keys("");
    set_rest(0, 0, 3, 0);
    CHECK(!cok_camp_rest(&game, false) && clock[2] == 3 && game.rest_ticks == 0);
    game.vm.mem7c00[0x2d2] = 0;

    /* The rest menu: Hours, Add twice, Subtract, Rest: an hour. A key
     * pressed while resting asks whether to stop. */
    memset(clock, 0, 7 * sizeof *clock);
    keys("haasr\x02y");
    CHECK(!cok_camp_rest(&game, true));
    CHECK(clock[1] == 0 && clock[2] == 0 && clock[3] == 0);
    CHECK(strstr(s.log, "menu: Stop Resting? ;choice: Y;") != NULL);
    /* Stopped, the time left stays for the next rest; Rest clears it. */
    CHECK(game.rest[3] == 1);
    /* No is selected once: Yes chosen with the left arrow stays chosen
     * through Escape, and Enter stops. */
    set_rest(0, 0, 0, 0);
    keys("har\x02\x01K\x1b\r");
    CHECK(!cok_camp_rest(&game, true) && clock[3] == 0);
    CHECK(strstr(s.log, "menu: Stop Resting? ;choice: Y;") != NULL);
    set_rest(0, 0, 0, 0);
    memset(clock, 0, 7 * sizeof *clock);
    keys("haasr");
    CHECK(!cok_camp_rest(&game, true) && clock[3] == 1 && clock[2] == 0);
    /* Down and up (2 and 8) are Subtract and Add; Add on minutes is five. */
    memset(clock, 0, 7 * sizeof *clock);
    set_rest(0, 0, 0, 0);
    keys("8\x01H2r");
    CHECK(!cok_camp_rest(&game, true) && clock[1] == 5 && clock[2] == 0);
    /* Exit, or Escape, rests not at all. */
    keys("ha\x1b");
    CHECK(!cok_camp_rest(&game, true) && clock[1] == 5);
}

static void test_fix(void)
{
    empty_party();
    game.vm.mode = 2;
    game.vm.mem7c00[0x2d2] = 0;
    uint16_t *clock = clock_words();
    /* A cleric with one spell of level 1 a day and Cure Light Wounds
     * memorized, and a fighter 2 points down. The estimate of points
     * starts at 14, what the stack holds (4888:28f9): with 27 for the
     * cleric's level 1 spells, 41 / 2 makes the 270 minutes 13. */
    cok_character *cleric = add(member("CLERIC", 8, 8, 0));
    cok_character *fighter = add(member("FIGHTER", 6, 8, 2));
    cleric->record[0x11c] = 2;
    cleric->record[0x1e] = 0x03;
    memset(clock, 0, 7 * sizeof *clock);
    memset(game.rest, 0, sizeof game.rest);
    keys("");
    CHECK(!cok_camp_fix(&game));
    CHECK(clock[1] == 5 && clock[2] == 1);
    CHECK(fighter->record[0x197] == 8 && cleric->record[0x1e] == 0x03);
    for (size_t i = 0; i < 7; ++i) CHECK(game.rest[i] == 0);
    /* With no one hurt, nothing happens. */
    memset(clock, 0, 7 * sizeof *clock);
    CHECK(!cok_camp_fix(&game) && clock[1] == 0);
    /* A member who is not okay after the cleric counts the cleric's spells
     * again: 14 + 27 + 27 = 68, / 2 = 34, and 270 / 34 = 7 minutes. */
    empty_party();
    cleric = add(member("CLERIC", 8, 8, 0));
    cleric->record[0x11c] = 2;
    cok_character *out = add(member("OUT", 0, 2, 2));
    out->record[0x188] = 4;
    out->record[0x189] = 0;
    memset(clock, 0, 7 * sizeof *clock);
    keys("");
    CHECK(!cok_camp_fix(&game));
    CHECK(clock[1] == 0 && clock[2] == 1);
    /* Unconscious, it wakes when healed. */
    CHECK(out->record[0x188] == 0 && out->record[0x189] == 1 && out->record[0x197] == 2);

    /* An estimate 256 times the points lacking is a ratio of 0 as a byte:
     * the original divides by zero. 14 + 6 (27 + 34) + 27 + (27 + 78) =
     * 512, 2 points lacking. */
    empty_party();
    for (size_t i = 0; i < 8; ++i) {
        cok_character *c = add(member("C", 9, 9, 0));
        c->record[0x11c] = 1;
        if (i < 6) c->record[0x11f] = 1;
        if (i == 7) c->record[0x120] = 1;
    }
    game.party.members[0]->record[0x197] = 7;
    keys("");
    CHECK(!cok_camp_fix(&game) && game.vm.status == COK_ECL_DIVIDE_BY_ZERO);

    /* An encounter interrupts the rest: no one is healed. */
    empty_party();
    cleric = add(member("CLERIC", 8, 8, 0));
    fighter = add(member("FIGHTER", 1, 8, 2));
    cleric->record[0x11c] = 2;
    game.vm.mem7c00[0x2d2] = 1;
    game.vm.mem7c00[0x2d3] = 100;
    game.rest_ticks = 0;
    set_rest(0, 0, 0, 7);
    keys("");
    CHECK(cok_camp_fix(&game) && fighter->record[0x197] == 1);
    /* The rest time it set (41 / 7 makes 270 minutes 54), less a tick,
     * stays; it is put back only after a rest that ends. */
    CHECK(game.rest[3] == 0 && game.rest[2] == 4 && game.rest[1] == 9);
    game.vm.mem7c00[0x2d2] = 0;
}

static void test_spells(void)
{
    CHECK(strcmp(cok_spell_name(1), "Bless") == 0 && strcmp(cok_spell_name(0x6b), "Burning Hands") == 0);
    CHECK(cok_spell_name(0) == NULL && cok_spell_name(0x6c) == NULL);
    CHECK(cok_spell_level(0x5c) == 5 && cok_spell_class(0x5c) == 3 && cok_spell_class(101) == 2);
    /* The sort puts empty bytes first. */
    uint8_t r[COK_CHARACTER_SIZE] = {0};
    r[0x1e] = 9;
    r[0x1f] = 0x80 | 3;
    r[0x20] = 3;
    cok_magic_sort(r);
    CHECK(r[0x1e] == 0 && r[0x55] == 3 && r[0x56] == (0x80 | 3) && r[0x57] == 9);
    /* Slots: a day's spells less those memorized and marked. */
    r[0x11c] = 3;
    uint8_t left, count;
    CHECK(cok_magic_slots(r, 1, 0, -1, &left, &count) && left == 1 && count == 2);
    /* Powers granted by a deity: 1, 3 for deity 4, 0 for deity 6, for a
     * cleric; without a cleric level the original reads an uninitialized
     * byte. */
    CHECK(!cok_magic_slots(r, 0, 2, -1, &left, &count));
    /* Memorize's call finds the low byte of its 34ec call's return, 0x71. */
    r[0x1e] = 101;
    CHECK(cok_magic_slots(r, 0, 2, 0x71, &left, &count) && left == 0x70);
    r[0x1e] = 9;
    r[0xf9] = 1;
    r[0x5d] = 4;
    r[0x1e] = 101;
    CHECK(cok_magic_slots(r, 0, 2, -1, &left, &count) && left == 2);
}

/* Game words, files and keys for the camp. */

static void camp_party(void)
{
    empty_party();
    game.vm.mode = game.vm.last_mode = 4;
    cok_character *k = add(member("KAL", 5, 9, 0));
    k->record[0x15] = 16;
    k->record[0x11c] = 2;
    k->record[0x62 + 1] = k->record[0x62 + 3] = 1; /* Bless, Cure Light Wounds */
    add(member("SIRRION", 9, 9, 2));
    add(member("MOLLY", 9, 9, 6));
}

static void test_camp(void)
{
    char dir[] = "/tmp/cok_camp_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    camp_party();
    /* Escape leaves camp; the mode and status line come back. */
    keys("\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strcmp(s.log, "print: The party makes camp...;menu: Save View Magic Rest Alter Fix Exit;") == 0);
    CHECK(game.vm.mode == 4 && game.picture_id == 0x3b);
    /* The menu keeps the selection the last menu left: after Encamp, the
     * fifth item of the commands, Enter picks Alter. */
    game.selected = 5;
    keys("\r\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "menu: Order Drop Speed Icon Pics Level Exit;") != NULL);
    /* View and Cast are not ported. */
    keys("vmce\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "unported: View (546c:0d74);") != NULL);
    CHECK(strstr(s.log, "unported: Cast (4888:0a0d);") != NULL);

    /* Save: Escape cancels, but the camp still asks whether to quit; then
     * game A, and not quitting. The camp's mode is saved, and the game
     * counts as saved (DS:5885). */
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    game.vm.mem4b00[0x13c] = 0;
    /* An item file left from before goes, as the first has no items. */
    char stale[600];
    snprintf(stale, sizeof stale, "%s/CHRDATA1.STF", dir);
    FILE *f = fopen(stale, "wb");
    CHECK(f != NULL && fputc(1, f) == 1 && fclose(f) == 0);
    keys("s\x1bnsan\x1b");
    CHECK(!cok_camp(&game) && !game.quit);
    CHECK(strstr(s.log, "menu: A B C D E F G H I J;menu: Quit TO DOS ;choice: N;menu: Save") != NULL);
    CHECK(game.vm.mem4b00[0x13c] == 2 && game.effects.rolls.saved == 1);
    char path[600];
    snprintf(path, sizeof path, "%s/SAVGAMA.DAT", dir);
    static cok_saved_game saved;
    char error[300];
    CHECK(cok_saved_game_read(path, &saved, error, sizeof error));
    CHECK(saved.mode == 2 && saved.last_mode == 4 && saved.count == 3);
    CHECK(strcmp(saved.names[2], "CHRDATA3") == 0 && saved.mem4b00[0x13c] == 2);
    /* Wall set 1 is record 0 at slot 1 from startup; pictures and
     * animation are bits 1 and 0 of 0x4bff, the speed 0x4bfc. */
    CHECK(saved.wall_ids[0] == 0 && saved.wall_slots[0] == 1 && saved.wall_ids[1] == -1 &&
          saved.wall_slots[2] == -1);
    CHECK(saved.mem4b00[0xff] == 3 && saved.mem4b00[0xfc] == 4 && saved.mem7c00[0x312] == 1);
    snprintf(path, sizeof path, "%s/CHRDATA1.SAV", dir);
    CHECK(access(path, F_OK) == 0);
    snprintf(path, sizeof path, "%s/CHRDATA1.STF", dir);
    CHECK(access(path, F_OK) != 0);
    /* F7 is scan code 0x41, A; the tenth save asks the rule book's word,
     * which is not ported. */
    game.vm.mem4b00[0x13c] = 9;
    keys("s\x01\x41n\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "unported: copy protection (4888:0376);menu: A B C D E F G H I J;choice: A;") != NULL);
    CHECK(game.vm.mem4b00[0x13c] == 0);
    /* Yes quits to DOS, also chosen with the left arrow before an Escape. */
    keys("sby");
    CHECK(!cok_camp(&game) && game.quit && game.vm.abort);
    game.quit = false;
    keys("sb\x01K\x1b\r");
    CHECK(!cok_camp(&game) && game.quit);
    game.quit = false;
    snprintf(path, sizeof path, "%s/SAVGAMB.DAT", dir);
    CHECK(access(path, F_OK) == 0);
    game.save_dir[0] = '\0';
    keys("sce\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "error: no directory to save games in;") != NULL);

    /* Rest from the camp menu: Mins Add, Rest. */
    uint16_t *clock = clock_words();
    memset(clock, 0, 7 * sizeof *clock);
    keys("rmar\x1b");
    CHECK(!cok_camp(&game) && clock[1] == 5 && clock[2] == 0);

    /* Alter. Order: select the first and move it up, to the end. */
    uint8_t *kal = game.party.members[0]->record;
    game.vm.character = game.party.members[0]->record;
    keys("aos\x01H\x1b\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(game.party.members[2]->record == kal && game.vm.character == kal);
    CHECK(strstr(s.log, "print: KAL;print: has been selected;") != NULL);
    /* Speed: Faster from 4, then the up arrow twice. */
    game.speed = 4;
    keys("asf\x01H\x01H\x1b\x1b\x1b");
    CHECK(!cok_camp(&game) && game.speed == 5);
    /* Pics: the down arrow is P; A turns animation off. */
    game.pictures = 1;
    game.animate = true;
    keys("ap\x01Pp\x01\x41\x1b\x1b\x1b");
    CHECK(!cok_camp(&game) && game.pictures == 1 && !game.animate);
    /* Level: Veteran is 3. */
    keys("alv\x1b\x1b");
    CHECK(!cok_camp(&game) && game.vm.mem4b00[0x1f4] == 3);
    /* Drop: No keeps; Yes drops the selected and selects the one before. */
    game.vm.character = game.party.members[1]->record;
    keys("adn\x1b\x1b");
    CHECK(!cok_camp(&game) && game.party.count == 3);
    CHECK(strstr(s.log, "print: Breathes A sigh of relief;") != NULL);
    game.vm.mem7c00[0x33e] = 3;
    keys("ad\x01K\x1b\r\x1b\x1b");
    CHECK(!cok_camp(&game) && game.party.count == 2 && game.vm.mem7c00[0x33e] == 2);
    CHECK(game.vm.character == game.party.members[0]->record);
    CHECK(strstr(s.log, "print: bids you farewell;") != NULL);

    /* The party's last asks to quit instead; Yes drops it and quits. */
    cok_party_remove(&game.party, 1);
    game.vm.character = game.party.members[0]->record;
    keys("ad\x01K\x1b\r");
    CHECK(!cok_camp(&game) && game.quit && game.party.count == 0);
    CHECK(strstr(s.log, "menu: quit TO DOS: ;choice: Y;") != NULL);
    game.quit = false;
    camp_party();
    kal = game.party.members[0]->record;
    /* Fix from the camp menu heals KAL with the spells he could memorize. */
    game.vm.character = kal;
    keys("f\x1b");
    CHECK(!cok_camp(&game) && kal[0x197] == 9);
    remove(path);
    snprintf(path, sizeof path, "%s/SAVGAMA.DAT", dir);
    remove(path);
    for (unsigned n = 1; n <= 3; ++n) {
        for (char g = 'A'; g <= 'B'; ++g) {
            snprintf(path, sizeof path, "%s/CHRDAT%c%u.SAV", dir, g, n);
            remove(path);
        }
    }
    rmdir(dir);
}

static void test_magic(void)
{
    camp_party();
    cok_character *k = game.party.members[0];
    game.vm.character = k->record;
    /* Memorize: nothing marked, so the table and the grimoire; its list
     * starts with a heading, so the pick starts on the last row shown:
     * Cure Light Wounds. Then Exit, see the spells marked, keep them, and
     * leave. */
    keys("mmm\x1b" "eye\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "list: Spells in Grimoire;heading: 1st Level;item:   Bless;"
                        "item:   Cure Light Wounds;") != NULL);
    CHECK(strstr(s.log, "choice: 3;") != NULL);
    CHECK(strstr(s.log, "list: Spells to Memorize;heading: 1st Level;item:  *Cure Light Wounds;")
          != NULL);
    /* Leaving camp unmarks it. */
    CHECK(k->record[0x57] == 0);
    /* Marked twice, then Magic's Rest, for 4 hours and 30 minutes, learns
     * them. */
    keys("mmmm\x1b" "eyrr\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(k->record[0x56] == 3 && k->record[0x57] == 3);
    CHECK(strstr(s.log, "item:  *Cure Light Wounds (2);") != NULL);
    /* With spells marked, Memorize shows them first; Yes keeps them and
     * leaves without opening the grimoire. */
    k->record[0x11c] = 3;
    keys("mmm\x1b" "eym\x1bye\x1b");
    CHECK(!cok_camp(&game));
    k->record[0x11c] = 2;
    const char *again = strstr(s.log, "choice: Y;menu: Cast Memorize Scribe Display Rest Exit;");
    CHECK(again != NULL);
    CHECK(strstr(again, "list: Spells to Memorize;heading: 1st Level;item:  *Cure Light Wounds;"
                        "menu: Memorize These Spells? ;choice: Y;menu: Cast") != NULL);
    CHECK(strstr(again, "Grimoire") == NULL);
    /* With none left today, a pick marks nothing; with no spells a day,
     * the character cannot memorize. */
    keys("mmm\x1b\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(k->record[0x55] == 0 && k->record[0x56] == 3 && k->record[0x57] == 3);
    game.vm.character = game.party.members[1]->record;
    keys("mm\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "print: SIRRION;print: cannot memorize any spells;") != NULL);
    game.vm.character = k->record;
    /* Not while unable to act. */
    k->record[0x189] = 0;
    keys("mm\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "print: is in no condition to memorize spells;") != NULL);
    k->record[0x189] = 1;

    /* A magic-user of an order gets bonus spells from its moon, 0x4cf8 +
     * the order: tested unsigned, so an order of 0x80 reads 0x4d78. */
    cok_character *mage = game.party.members[2];
    game.vm.character = mage->record;
    mage->record[0xfe] = 1;
    mage->record[0x5e] = 0x80;
    mage->record[0x13] = 12;
    game.vm.mem4b00[0x1f8 + 0x80] = 1;
    game.bonus_spells = 0;
    keys("mm\x1b\x1b");
    CHECK(!cok_camp(&game) && game.bonus_spells == 1);
    mage->record[0xfe] = 0;
    game.vm.mem4b00[0x1f8 + 0x80] = 0;

    /* A knight above level 5 with no cleric level may memorize a granted
     * power: 4888:0700 counts it against the 0x71 its stack holds. */
    cok_character *knight = game.party.members[1];
    game.vm.character = knight->record;
    knight->record[0x15] = 12;
    knight->record[0x100] = 7;
    knight->record[0x11c] = 1;
    knight->record[0x62 + 101] = 1;
    keys("mmm\x1b" "e\x1b\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "list: Spells in Grimoire;heading: Special;"
                        "item:   Protection From Evil 10' Radius;choice: 101;") != NULL);
    CHECK(strstr(s.log, "list: Spells to Memorize;heading: Special;"
                        "item:  *Protection From Evil 10' Radius;") != NULL);
    knight->record[0x11c] = 0;

    /* Scribe: a magic-user of order 1 with a scroll of Shield marks the
     * first byte equal to it, here on a sword before the scroll. */
    cok_character *m = game.party.members[2];
    game.vm.character = m->record;
    m->record[0x5e] = 1;
    m->record[0x12b] = 1;
    m->record[0x13] = 12;
    m->items = realloc(m->items, sizeof *m->items);
    m->item_count = 1;
    memset(m->items[0], 0, COK_ITEM_SIZE);
    m->items[0][0x2e] = 1;
    m->items[0][0x3d] = 0x13;
    scroll(m, 0x13, 0, 0, 100);
    keys("ms\r\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(m->items[0][0x3d] == (0x80 | 0x13) && m->items[1][0x3c] == 0x13);
    CHECK(strstr(s.log, "list: Spells on Scrolls;heading: 1st Level;item:   Shield;") != NULL);
    /* Known already. */
    m->record[0x62 + 0x13] = 1;
    keys("ms\r\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "print: You already know that spell;") != NULL);

    /* Display: each member's effects, named after the first spell that
     * adds them (byte 10 of the spell table): 0x13 is Find Traps', 0x11
     * Shield's; 0x37 has its own text, and 0x5c none. */
    CHECK(cok_character_add_effect(k, 0x37, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(k, 0x13, 10, 0, false) != NULL);
    CHECK(cok_character_add_effect(k, 0x5c, 0, 0, false) != NULL);
    CHECK(cok_character_add_effect(k, 0x11, 0, 0, false) != NULL);
    keys("md\x1b\x1b\x1b");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "item:  ;heading: KAL;item:  Poisoned;item:  Find Traps;item:  Shield;"
                        "item:  ;heading: SIRRION;item:  <No Spell Effects>;") != NULL);
}

/* Save goes on past a file it cannot write, as the original checks none,
 * and with no party warns before writing the count and the names. */
static void test_save_errors(void)
{
    char dir[] = "/tmp/cok_errors_XXXXXX", path[600];
    CHECK(mkdtemp(dir) != NULL);
    camp_party();
    snprintf(game.save_dir, sizeof game.save_dir, "%s", dir);
    snprintf(path, sizeof path, "%s/CHRDATC1.SAV", dir);
    CHECK(mkdir(path, 0700) == 0);
    game.effects.rolls.saved = 0;
    keys("sc");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "error: ") != NULL && strstr(s.log, "CHRDATC1.SAV: cannot write;") != NULL);
    CHECK(game.effects.rolls.saved == 1);
    char done[600];
    snprintf(done, sizeof done, "%s/CHRDATC3.SAV", dir);
    CHECK(access(done, F_OK) == 0);
    rmdir(path);
    /* The warning waits for a key before anything is written. */
    empty_party();
    keys("sd");
    CHECK(!cok_camp(&game));
    CHECK(strstr(s.log, "print: WARNING: Problem Saving Characters;") != NULL);
    snprintf(path, sizeof path, "%s/SAVGAMD.DAT", dir);
    CHECK(access(path, F_OK) != 0);
    keys("sd\rn\x1b");
    CHECK(!cok_camp(&game));
    static cok_saved_game saved;
    char error[300];
    CHECK(cok_saved_game_read(path, &saved, error, sizeof error) && saved.count == 0);
    remove(path);
    for (unsigned n = 2; n <= 3; ++n) {
        snprintf(path, sizeof path, "%s/CHRDATC%u.SAV", dir, n);
        remove(path);
    }
    snprintf(path, sizeof path, "%s/SAVGAMC.DAT", dir);
    remove(path);
    CHECK(rmdir(dir) == 0);
    game.save_dir[0] = '\0';
}

/* Load each saved game in SAVE/, save it again in a new directory and
 * compare the files: they match but for the far pointers the original
 * writes as they were in memory, the bytes after each name in the .DAT
 * and its unused slots, which held what was on its stack, and spell 8 of
 * game A's clerics (see Derived stats). */
static void test_round_trip(void)
{
    FILE *probe = fopen("SAVE/SAVGAMA.DAT", "rb");
    if (probe == NULL) {
        puts("camp: SAVE/SAVGAMA.DAT not found; save round trip not checked");
        return;
    }
    fclose(probe);
    char dir[] = "/tmp/cok_save_XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    for (char letter = 'A'; letter <= 'B'; ++letter) {
        static cok_adventure copy;
        CHECK(cok_adventure_open(&copy, "Assets", NULL, NULL));
        char path[600];
        snprintf(path, sizeof path, "SAVE/SAVGAM%c.DAT", letter);
        if (access(path, F_OK) != 0) {
            cok_adventure_close(&copy);
            continue;
        }
        CHECK(cok_adventure_restore(&copy, path));
        snprintf(copy.save_dir, sizeof copy.save_dir, "%s", dir);
        CHECK(cok_camp_save_game(&copy, letter));
        size_t members = copy.party.count;
        cok_adventure_close(&copy);
        static const char *const kinds[] = {"SAV", "STF", "SFX"};
        for (size_t n = 0; n <= members; ++n) {
            for (size_t kind = 0; kind < (n == 0 ? 1 : 3); ++kind) {
                char name[32], mine[700], theirs[64];
                if (n == 0)
                    snprintf(name, sizeof name, "SAVGAM%c.DAT", letter);
                else
                    snprintf(name, sizeof name, "CHRDAT%c%zu.%s", letter, n, kinds[kind]);
                snprintf(mine, sizeof mine, "%s/%s", dir, name);
                snprintf(theirs, sizeof theirs, "SAVE/%s", name);
                FILE *a = fopen(mine, "rb"), *b = fopen(theirs, "rb");
                CHECK((a == NULL) == (b == NULL));
                if (a == NULL) continue;
                static uint8_t x[8192], y[8192];
                size_t nx = fread(x, 1, sizeof x, a), ny = fread(y, 1, sizeof y, b);
                fclose(a);
                fclose(b);
                remove(mine);
                CHECK(nx == ny);
                for (size_t i = 0; i < nx; ++i) {
                    if (x[i] == y[i]) continue;
                    bool pointer;
                    if (n == 0) {
                        size_t slot = (i - 0x1415) / 41, at = (i - 0x1415) % 41;
                        pointer = i >= 0x1415 && (slot >= y[0x1414] || at > y[0x1415 + 41 * slot]);
                    } else if (kind == 0) {
                        pointer = (i >= 0xe3 && i < 0xe7) || (i >= 0x143 && i < 0x17b) ||
                                  (i >= 0x17f && i < 0x187) || (letter == 'A' && i == 0x6a);
                    } else if (kind == 1) {
                        pointer = i % COK_ITEM_SIZE >= 0x2a && i % COK_ITEM_SIZE < 0x2e;
                    } else {
                        pointer = i % COK_EFFECT_SIZE >= 5;
                    }
                    if (!pointer) fprintf(stderr, "%s byte %zu: %u, not %u\n", name, i, x[i], y[i]);
                    CHECK(pointer);
                }
            }
        }
    }
    rmdir(dir);
}

int main(void)
{
    open_game();
    test_rest_time();
    test_preparation();
    test_heal();
    test_rest();
    test_fix();
    test_spells();
    test_camp();
    test_magic();
    test_save_errors();
    cok_adventure_close(&game);
    test_round_trip();
    puts("camp tests passed");
    return 0;
}
