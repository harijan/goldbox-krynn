/* The songs' driver (src/songs.c, Death Knights' MUSIC.DAX): its port
 * writes against the original's, recorded in an 8086 emulator
 * (tests/test_songs_data.h), on every song and three made-up ones, on the
 * AdLib, the Tandy and the speaker; the songs' parts and header as read;
 * the game's calls (dkk 15dc, 16ab: a sound stopping the song, a song
 * unhooking the effects, Ctrl-U, Ctrl-S, quitting, ECL's MUSIC through
 * the descriptor); the timer's rate; and the AdLib's and the Tandy's
 * title rendered. Skipped, saying so, without DKKAssets/. */

#include "sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    char op;
    uint16_t a;
} songs_step;

typedef struct {
    const char *name;
    uint8_t device;
    const songs_step *steps;
    size_t count;
    uint32_t digest;
    unsigned writes;
} songs_scenario;

#include "test_songs_data.h"

static const char *const assets = "DKKAssets";
static cok_sound s;

typedef struct {
    uint32_t digest;
    unsigned writes;
    uint16_t first[200][2];
} trace;

static void feed(trace *t, uint8_t b)
{
    t->digest = (t->digest ^ b) * 0x01000193u;
}

static void traced(void *context, uint16_t port, uint8_t value)
{
    trace *t = context;
    feed(t, (uint8_t)port);
    feed(t, (uint8_t)(port >> 8));
    feed(t, value);
    if (t->writes < 200) {
        t->first[t->writes][0] = port;
        t->first[t->writes][1] = value;
    }
    ++t->writes;
}

/* A machine whose detection finds the device: 2 an AdLib, 1 the Tandy, 0
 * the speaker alone. */
static void setup(unsigned device, trace *t)
{
    static const cok_sound_device machine[] = {COK_SOUND_SPEAKER, COK_SOUND_TANDY,
                                               COK_SOUND_ADLIB};
    cok_sound_free(&s);
    cok_sound_init(&s, machine[device]);
    CHECK(cok_sound_load(&s, &cok_rules_death_knights, assets));
    if (t != NULL) {
        *t = (trace){0x811c9dc5u, 0, {{0}}};
        s.trace = traced;
        s.trace_context = t;
    }
}

static void load(const uint8_t *data, size_t size)
{
    memset(s.songs.buffer, 0, sizeof s.songs.buffer);
    memcpy(s.songs.buffer, data, size);
}

static void run_step(const songs_step *step, trace *t)
{
    static const uint8_t *const made[] = {made_up_0, made_up_1, made_up_2};
    static const size_t made_size[] = {sizeof made_up_0, sizeof made_up_1, sizeof made_up_2};
    switch (step->op) {
    case 'I':
        load(s.songs.song[step->a], s.songs.song_size[step->a]);
        cok_songs_call(&s, 0, 0);
        break;
    case 'F':
        load(made[step->a], made_size[step->a]);
        cok_songs_call(&s, 0, 0);
        break;
    case 'P':
        cok_songs_call(&s, 1, 0);
        /* The original waits in 076b for the next interrupt. */
        if (s.songs.waiting) cok_songs_interrupt(&s);
        break;
    case 'Q': cok_songs_call(&s, 2, 0); break;
    case 'X': cok_songs_call(&s, 3, 0); break;
    case 'T':
        for (unsigned i = 0; i < step->a; ++i) {
            cok_songs_interrupt(&s);
            feed(t, 0xee);
        }
        return;
    default: CHECK(!"unknown step");
    }
    feed(t, 0xee);
}

static void test_original(void)
{
    const uint16_t(*const first[3])[2] = {first_writes_0, first_writes_1, first_writes_2};
    bool seen[3] = {false, false, false};
    for (size_t i = 0; i < sizeof scenarios / sizeof *scenarios; ++i) {
        const songs_scenario *sc = &scenarios[i];
        trace t;
        setup(sc->device, &t);
        for (size_t k = 0; k < sc->count; ++k) run_step(&sc->steps[k], &t);
        if (t.digest != sc->digest || t.writes != sc->writes) {
            fprintf(stderr, "%s: digest %08x, %u writes; the original's %08x, %u\n", sc->name,
                    (unsigned)t.digest, t.writes, (unsigned)sc->digest, sc->writes);
            CHECK(!"a scenario's writes differ from the original's");
        }
        if (!seen[sc->device]) {
            /* The first scenario of each device, write for write. */
            seen[sc->device] = true;
            for (unsigned k = 0; k < 200; ++k)
                if (t.first[k][0] != first[sc->device][k][0] ||
                    t.first[k][1] != first[sc->device][k][1]) {
                    fprintf(stderr, "%s: write %u %x:%02x, the original's %x:%02x\n", sc->name,
                            k, t.first[k][0], t.first[k][1], first[sc->device][k][0],
                            first[sc->device][k][1]);
                    CHECK(!"a write differs from the original's");
                }
        }
    }
}

static uint16_t be(const uint8_t *p)
{
    return (uint16_t)(p[0] << 8 | p[1]);
}

/* MUSIC.DAX as read; a song's parts (AdLib instruments 0-7, 12 bytes from
 * 0x4c) and header (0x1bc) as the driver takes them. */
static void test_parts(void)
{
    static const size_t sizes[8] = {12528, 5098, 1268, 1086, 712, 982, 858, 5116};
    setup(2, NULL);
    CHECK(s.songs.driver_size == sizes[0]);
    for (unsigned n = 1; n < 8; ++n) {
        CHECK(s.songs.song_size[n] == sizes[n]);
        CHECK(memcmp(s.songs.song[n], "GLBR", 4) == 0);
    }
    CHECK(s.songs.song[0] == NULL && s.songs.song[8] == NULL);
    /* The data segment as loaded: DS:0114, the F-numbers of C to B. */
    CHECK(s.songs.ds[0x114] == 0x57 && s.songs.ds[0x115] == 0x01);
    CHECK(s.songs.ds[0x12a] == 0x8a && s.songs.ds[0x12b] == 0x02);
    for (unsigned n = 1; n < 8; ++n) {
        const uint8_t *song = s.songs.song[n];
        load(song, s.songs.song_size[n]);
        cok_songs_call(&s, 0, 0);
        CHECK(cok_songs_device(&s) == 2);
        CHECK(memcmp(&s.songs.ds[0x358], song + 0x4c, 96) == 0);
        CHECK(memcmp(&s.songs.ds[0x322], song + 0x1bc, 10) == 0);
        CHECK(s.songs.ds[0x32a] == 1);
        /* The offsets, little-endian now; the song past them. */
        CHECK(s.songs.ds[0x2f2] == 0 && s.songs.ds[0x2f3] == 0);
        CHECK((s.songs.ds[0x2f4] | s.songs.ds[0x2f5] << 8) == be(song + 0x1c8));
        CHECK((s.songs.ds[0x32c] | s.songs.ds[0x32d] << 8) == 0x1ca);
        cok_songs_call(&s, 1, 0);
        CHECK(s.songs.waiting);
        cok_songs_interrupt(&s);
        CHECK(!s.songs.waiting && s.songs.count == 1);
        /* Voices 0-2 play tracks 1-3, from the time 1, the tempo / 84. */
        const uint8_t *body = song + 0x1ca;
        for (unsigned v = 0; v < 3; ++v) {
            const uint8_t *voice = &s.songs.ds[0x29e + 14 * v];
            CHECK(voice[0] == 1 && voice[1] == 0 && voice[2] == 0 && voice[3] == 0);
            CHECK((voice[4] | voice[5] << 8) == 0x1ca + 0x0e + be(body + 6 + 2 * v));
            CHECK((voice[8] | voice[9] << 8) == be(body) / 84);
            CHECK(voice[12] == body[2] && voice[13] == 3);
        }
        CHECK(cok_songs_call(&s, 2, 0) == 1);
        cok_songs_call(&s, 3, 0);
        CHECK(cok_songs_call(&s, 2, 0) == 0 && !s.songs.hooked);
    }
    /* The speaker plays track 0 alone. */
    setup(0, NULL);
    load(s.songs.song[1], s.songs.song_size[1]);
    cok_songs_call(&s, 0, 0);
    cok_songs_call(&s, 1, 0);
    cok_songs_interrupt(&s);
    CHECK(cok_songs_device(&s) == 0 && s.songs.ds[0x260] == 1);
    CHECK((s.songs.ds[0x2a2] | s.songs.ds[0x2a3] << 8) == 0x1ca + 0x0e);
    CHECK(s.songs.ds[0x29e + 14] == 0);
}

/* The game's calls (dkk 15dc, 15fe, 16ab, 3632:2878). */
static void test_game(void)
{
    trace t;
    setup(1, &t);
    /* The title's song: the record loaded, the driver started and hooked,
     * PIT channel 0 at 0x4000. */
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 1);
    CHECK(s.songs.playing && s.songs.current == 1 && s.songs.hooked && s.pit0 == 0x4000);
    CHECK(!s.installed);
    /* Every interrupt reaches the BIOS: the DOS clock four times as fast. */
    CHECK(cok_sound_clock_speed(&s) == 4);
    for (int i = 0; i < 40; ++i) cok_sound_tick(&s);
    CHECK(s.songs.count == 40);
    /* A record read and an overlay leave it playing. */
    unsigned before = t.writes;
    cok_sound_call_at(&s, COK_SOUND_RECORD, 0);
    cok_sound_call_at(&s, COK_SOUND_OVERLAY, 0);
    cok_sound_call_at(&s, COK_SOUND_OVERLAY_END, 0);
    CHECK(t.writes == before && s.songs.playing && !s.installed);
    /* The same song again: played from its start, not loaded. */
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 1);
    CHECK(s.songs.playing && s.songs.count == 40);
    /* A sound: the song stopped (PIT channel 0 to 0, the voices silenced),
     * the effects driver hooked. */
    cok_sound_call_at(&s, COK_SOUND_COMMAND, 9);
    CHECK(!s.songs.playing && !s.songs.hooked && s.installed && s.isr == COK_SOUND_FX);
    CHECK(cok_sound_clock_speed(&s) == 1);
    /* Another song: the effects driver unhooked first. */
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 3);
    CHECK(s.songs.playing && s.songs.current == 3 && !s.installed && s.isr == COK_SOUND_BIOS);
    /* Ctrl-S: the sound off, the song playing on; no new song meanwhile. */
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    CHECK(s.device == COK_SOUND_NONE && s.songs.playing);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 4);
    CHECK(s.songs.current == 3);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    CHECK(s.device == COK_SOUND_TANDY);
    /* Ctrl-U: off stops the song, on starts nothing; the next song plays. */
    cok_sound_call_at(&s, COK_SOUND_CTRL_U, 0x15);
    CHECK(!s.songs.on && !s.songs.playing);
    cok_sound_call_at(&s, COK_SOUND_CTRL_U, 0x15);
    CHECK(s.songs.on && !s.songs.playing);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 4);
    CHECK(s.songs.playing && s.songs.current == 4);
    /* The main loop's stop, and quitting. */
    cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
    CHECK(!s.songs.playing);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 2);
    cok_sound_call_at(&s, COK_SOUND_QUIT, 0);
    CHECK(!s.songs.playing && !s.songs.hooked);
    /* A song the archive lacks: the buffer empty, nothing played. */
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 0x50);
    for (int i = 0; i < 10; ++i) cok_sound_tick(&s);
    CHECK(cok_songs_call(&s, 2, 0) == 0);
    /* The sound off: no song. */
    setup(1, &t);
    s.device = COK_SOUND_NONE;
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 1);
    CHECK(!s.songs.playing && t.writes == 0);
    /* ECL's MUSIC 1-6: the sounds at DS:0002-0007. */
    static const uint8_t sounds[7] = {0, 0x0c, 0x09, 0x04, 0x07, 0x0a, 0x0b};
    CHECK(memcmp(cok_rules_death_knights.ecl.music_sounds, sounds, sizeof sounds) == 0);
    CHECK(cok_rules_death_knights.start.title_song == 1 &&
          cok_rules_death_knights.start.music_past_title);
    CHECK(!cok_rules_champions.start.music_past_title &&
          cok_rules_champions.archives.music_songs == NULL);
    /* A CMS: its songs not ported, said at each song. */
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_CMS);
    CHECK(!cok_sound_load(&s, &cok_rules_death_knights, assets));
    CHECK(strstr(s.music_error, "C/MS") != NULL);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 1);
    char text[100];
    CHECK(cok_sound_music_unported(&s, &cok_rules_death_knights, text, sizeof text));
    CHECK(cok_songs_device(&s) == 3);
}

/* INT 1Ch at 72.8 interrupts a second (PIT channel 0 at 0x4000), a tick
 * every second one; the title's song rendered on the AdLib and the Tandy:
 * sound, never clipped. */
static void test_render(void)
{
    static int16_t buffer[44100];
    for (unsigned device = 1; device <= 2; ++device) {
        setup(device, NULL);
        cok_sound_call_at(&s, COK_SOUND_MUSIC, 1);
        uint32_t start = s.songs.count;
        double sum = 0;
        int peak = 0;
        for (int second = 0; second < 10; ++second) {
            cok_sound_render(&s, buffer, 44100);
            for (size_t i = 0; i < 44100; ++i) {
                sum += (double)buffer[i] * buffer[i];
                if (abs(buffer[i]) > peak) peak = abs(buffer[i]);
            }
        }
        uint32_t n = s.songs.count - start;
        CHECK(n >= 727 && n <= 729); /* 1,193,181.8 / 16,384 = 72.83 a second */
        CHECK(sum / 441000 > 500.0 * 500.0 && peak < 32767);
    }
}

int main(void)
{
    struct stat st;
    if (stat("DKKAssets/MUSIC.DAX", &st) != 0) {
        puts("songs: DKKAssets/MUSIC.DAX not found; the songs' checks skipped");
        return 0;
    }
    test_parts();
    test_original();
    test_game();
    test_render();
    cok_sound_free(&s);
    puts("songs: ok");
    return 0;
}
