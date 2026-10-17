/* The sound (src/sound.c): the effects driver's commands, the music
 * drivers and Ctrl-S against the port writes of the original's code
 * recorded in an 8086 emulator (tests/test_sound_data.h), the driver's
 * data against build/START_FULL.EXE, and the synthesis on known inputs:
 * the chip's square waves and noise, its volumes, the speaker's square
 * wave, and rendering split anyhow giving the same samples. */

#include "sound.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

typedef struct {
    char op;
    uint16_t a, b;
} sound_step;

typedef struct {
    const char *name;
    uint8_t device;
    const sound_step *steps;
    size_t count;
    const uint8_t *score;
    size_t score_size;
    uint32_t digest;
    unsigned writes;
    enum { END_NONE, END_DIVIDE, END_BOMB } end; /* how the original's run ended */
} sound_scenario;

#include "test_sound_data.h"

static cok_sound s;

/* The writes traced: an FNV-1a digest and a count, or a copy. */
typedef struct {
    uint32_t digest;
    unsigned writes;
    uint8_t copy[4096];
    size_t copied;
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
    ++t->writes;
    if (t->copied + 2 <= sizeof t->copy) {
        t->copy[t->copied++] = (uint8_t)port;
        t->copy[t->copied++] = value;
    }
}

static const char *assets = "COKAssets";

static void setup(cok_sound_device device, trace *t)
{
    cok_sound_free(&s);
    cok_sound_init(&s, device == COK_SOUND_SPEAKER ? COK_SOUND_SPEAKER : COK_SOUND_TANDY);
    s.device = (uint8_t)device; /* 0-2 too: effects on the speaker */
    CHECK(cok_sound_load(&s, assets) || device < COK_SOUND_TANDY);
    s.score_segment = 0xc000; /* where the emulator put the score */
    *t = (trace){0x811c9dc5u, 0, {0}, 0};
    s.trace = traced;
    s.trace_context = t;
}

/* Returns false once the run has ended: the music driver's divide
 * overflow, or the copy protection's runtime error. */
static bool run_step(const sound_step *step, trace *t)
{
    unsigned reps = step->op == 't' ? (step->a > 0 ? step->a : 1) : 1;
    for (unsigned r = 0; r < reps; ++r) {
        switch (step->op) {
        case 'p': s.fx[step->a] = (uint8_t)step->b; continue;
        case 'P': s.music_seg[step->a] = (uint8_t)step->b; continue;
        case 'c': cok_sound_call_at(&s, COK_SOUND_COMMAND, step->a); break;
        case 'i': cok_sound_call_at(&s, COK_SOUND_INSTALL, 0); break;
        case 'o': cok_sound_call_at(&s, COK_SOUND_OVERLAY, 0); break;
        case 'O': cok_sound_call_at(&s, COK_SOUND_OVERLAY_END, 0); break;
        case 'r': cok_sound_call_at(&s, COK_SOUND_RECORD, 0); break;
        case 'k':
            if (step->a == 0x13) cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
            break;
        case 'm': cok_sound_call_at(&s, COK_SOUND_MUSIC, 0); break;
        case 'M': cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0); break;
        case 'q': cok_sound_call_at(&s, COK_SOUND_QUIT, 0); break;
        case 't': cok_sound_tick(&s); break;
        default: CHECK(!"unknown step");
        }
        feed(t, 0xee);
        if (s.divide_error || s.bomb) return false;
    }
    return true;
}

/* Every scenario's writes, as the original made them. */
static void test_scenarios(void)
{
    size_t n = sizeof scenarios / sizeof *scenarios;
    for (size_t i = 0; i < n; ++i) {
        const sound_scenario *sc = &scenarios[i];
        trace t;
        setup((cok_sound_device)sc->device, &t);
        if (sc->score != NULL) {
            free(s.score);
            s.score = malloc(sc->score_size);
            CHECK(s.score != NULL);
            memcpy(s.score, sc->score, sc->score_size);
            s.score_size = sc->score_size;
        }
        for (size_t k = 0; k < sc->count && run_step(&sc->steps[k], &t); ++k) continue;
        if (t.digest != sc->digest || t.writes != sc->writes) {
            fprintf(stderr, "%s: digest %08x, %u writes; the original's %08x, %u\n", sc->name,
                    (unsigned)t.digest, t.writes, (unsigned)sc->digest, sc->writes);
            CHECK(!"a scenario's writes differ from the original's");
        }
        CHECK(s.divide_error == (sc->end == END_DIVIDE) && s.bomb == (sc->end == END_BOMB));
    }
}

static void check_ticks(cok_sound_device device, const sound_step *steps, size_t count,
                        const uint8_t *expected, size_t size)
{
    trace t;
    setup(device, &t);
    for (size_t k = 0; k < count; ++k) run_step(&steps[k], &t);
    size_t at = 0;
    for (int tick = 0; tick < 4; ++tick) {
        t.copied = 0;
        cok_sound_tick(&s);
        CHECK(at + t.copied + 2 <= size);
        CHECK(memcmp(expected + at, t.copy, t.copied) == 0);
        at += t.copied;
        CHECK(expected[at] == 0 && expected[at + 1] == 0);
        at += 2;
    }
    CHECK(at == size);
}

/* The first ticks written out: sound 10 on the Tandy (its noise voice at
 * tone 2's rate, every voice written each tick), sound 6 on the speaker,
 * the title's music on both. */
static void test_first_ticks(void)
{
    const sound_step tandy[] = {{'i', 0, 0}, {'c', 11, 0}}, speaker[] = {{'i', 0, 0}, {'c', 7, 0}};
    const sound_step music[] = {{'m', 0, 0}};
    check_ticks(COK_SOUND_TANDY, tandy, 2, sound_10_tandy, sizeof sound_10_tandy);
    check_ticks(COK_SOUND_SPEAKER, speaker, 2, sound_6_speaker, sizeof sound_6_speaker);
    check_ticks(COK_SOUND_TANDY, music, 1, music_tandy, sizeof music_tandy);
    check_ticks(COK_SOUND_SPEAKER, music, 1, music_speaker, sizeof music_speaker);
}

/* The driver's data as START.EXE has it, the commands' set and Ctrl-S's. */
static void test_image(void)
{
    FILE *f = fopen("build/START_FULL.EXE", "rb");
    if (f == NULL) {
        puts("sound: build/START_FULL.EXE not built; the driver's data not checked");
        return;
    }
    static uint8_t exe[1 << 20];
    size_t size = fread(exe, 1, sizeof exe, f);
    fclose(f);
    CHECK(size > 0x20 && exe[0] == 'M' && exe[1] == 'Z');
    size_t header = (size_t)(exe[8] | exe[9] << 8) * 16;
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_TANDY);
    for (size_t k = 0; k < COK_SOUND_FX_IMAGE; ++k)
        CHECK(s.fx[k] == exe[header + 0x7e8 * 16 + k]);
    /* 17e8:0000, the commands taken: 0-15 and 0xff. */
    CHECK(s.fx[0] == 0xff && s.fx[1] == 0xff && s.fx[0x1f] == 0x80);
    /* 1614:022c, the devices Ctrl-S turns back on: 3 and 4. */
    const uint8_t *set = exe + header + 0x614 * 16 + 0x22c;
    CHECK(set[0] == 0x18);
    for (int k = 1; k < 32; ++k) CHECK(set[k] == 0);
}

/* Ctrl-S, device by device: off, then on again only for the Tandy and the
 * speaker; ignored during the title's music; after an install while off
 * (the title after a demonstration) never on again. */
static void test_ctrl_s(void)
{
    trace t;
    for (int device = 0; device <= 5; ++device) {
        setup((cok_sound_device)device, &t);
        cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
        cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
        CHECK(s.device == COK_SOUND_NONE && s.saved == device);
        cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
        CHECK(s.device == (device == 3 || device == 4 ? device : COK_SOUND_NONE));
    }
    /* Off, then an overlay's pair: 0xff does nothing with the device off,
     * so DS:577c stays set and nothing installs again; Ctrl-S turns it
     * back on, still hooked (2fd3:1968-1984's tests of 577c). */
    setup(COK_SOUND_TANDY, &t);
    cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    cok_sound_call_at(&s, COK_SOUND_OVERLAY, 0);
    cok_sound_call_at(&s, COK_SOUND_OVERLAY_END, 0);
    CHECK(s.installed && s.saved == COK_SOUND_TANDY && s.isr == COK_SOUND_FX);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    CHECK(s.device == COK_SOUND_TANDY && s.isr == COK_SOUND_FX);
    setup(COK_SOUND_TANDY, &t);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    CHECK(s.device == COK_SOUND_TANDY && s.music && s.isr == COK_SOUND_TITLE);
    cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
    CHECK(!s.music && s.isr == COK_SOUND_BIOS);
    cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
    CHECK(s.isr == COK_SOUND_FX && s.installed);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    cok_sound_call_at(&s, COK_SOUND_COMMAND, 0xff); /* off: does nothing */
    CHECK(s.isr == COK_SOUND_FX && s.installed);
    cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
    CHECK(s.saved == COK_SOUND_NONE);
    cok_sound_call_at(&s, COK_SOUND_CTRL_S, 0x13);
    CHECK(s.device == COK_SOUND_NONE);
}

/* The timer: 0x13b1 for the effects (236.7 Hz), 0x5555 for the music
 * (54.6 Hz), counted in clocks of the 14,318,181.8 Hz crystal (12 a PIT
 * clock). */
static void test_timer(void)
{
    trace t;
    setup(COK_SOUND_TANDY, &t);
    CHECK(s.isr == COK_SOUND_BIOS && s.next_tick == 65536ull * 12);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
    CHECK(s.isr == COK_SOUND_TITLE && s.pit0 == 0x5555 && s.next_tick == 0x5555ull * 12);
    cok_sound_tick(&s);
    CHECK(s.next_tick == 2 * 0x5555ull * 12);
    cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
    CHECK(s.pit0 == 0xffff && s.isr == COK_SOUND_BIOS);
    uint64_t pending = s.next_tick;
    cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
    CHECK(s.isr == COK_SOUND_FX && s.pit0 == 0x13b1);
    /* Written without channel 0's control word (0xb6 is channel 2's), the
     * count takes over after the interrupt pending. */
    CHECK(s.next_tick == pending);
    uint64_t at = s.next_tick;
    cok_sound_tick(&s);
    CHECK(s.now == at && s.next_tick == at + 0x13b1ull * 12);
    /* A second of samples runs 236 or 237 of its interrupts. */
    static int16_t samples[COK_SOUND_RATE];
    uint64_t from = s.next_tick;
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    uint64_t ticks = (s.next_tick - from) / (0x13b1ull * 12);
    CHECK(ticks == 236 || ticks == 237);
    /* A second of samples is 157,500,000 / 11 crystal clocks, to the clock:
     * 44,100 of 324 + 327,600 / 485,100 each. */
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_TANDY);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    CHECK(s.now == 14318181);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    CHECK(s.now == 28636363);
}

/* The chip: tone N toggles its output every N steps of 3,579,545 / 16
 * Hz; 0 counts as 0x400. */
static void test_chip_tone(void)
{
    cok_sound_chip chip;
    cok_sound_chip_init(&chip);
    cok_sound_chip_write(&chip, 0x80 | 0x0e); /* tone 0 = 0x0fe (254) */
    cok_sound_chip_write(&chip, 0x0f);
    CHECK(chip.reg[0] == 0xfe && chip.period[0] == 254);
    cok_sound_chip_write(&chip, 0x90); /* volume 0, full */
    unsigned toggles = 0;
    uint8_t last = chip.output[0];
    for (int i = 0; i < 223722; ++i) { /* a second */
        cok_sound_chip_step(&chip);
        if (chip.output[0] != last) ++toggles;
        last = chip.output[0];
    }
    /* 3,579,545 / (32 * 254) = 440.4 Hz: 881 toggles. */
    CHECK(toggles == 880 || toggles == 881);
    cok_sound_chip_write(&chip, 0xa0); /* tone 1 = 0 */
    cok_sound_chip_write(&chip, 0x00);
    CHECK(chip.period[1] == 0x400);
    /* A data byte goes to the register latched last (tone 1's high bits). */
    cok_sound_chip_write(&chip, 0x3f);
    CHECK(chip.reg[2] == 0x3f0 && chip.period[1] == 0x3f0);
    /* Tone 2's change moves the noise when it runs at tone 2's rate. */
    cok_sound_chip_write(&chip, 0xe7);
    CHECK(chip.period[3] == 2 * chip.period[2]);
    cok_sound_chip_write(&chip, 0xc5);
    cok_sound_chip_write(&chip, 0x01);
    CHECK(chip.period[2] == 0x15 && chip.period[3] == 0x2a);
}

/* The noise: a 16-bit register fed back at bit 15 from bits 1 and 5
 * (XNOR for white noise, bit 1 alone for periodic), shifted every 32, 64
 * or 128 steps; reset to 0x8000 only when the white/periodic bit changes,
 * so that the effects driver's rewriting it each tick does not. */
static void test_chip_noise(void)
{
    cok_sound_chip chip;
    cok_sound_chip_init(&chip);
    CHECK(chip.rng == 0x8000);
    cok_sound_chip_write(&chip, 0xe4); /* white, N/512 */
    CHECK(chip.period[3] == 32 && chip.rng == 0x8000);
    uint16_t rng = 0x8000;
    for (int shift = 0; shift < 200; ++shift) {
        for (int k = 0; k < 32; ++k) cok_sound_chip_step(&chip);
        bool tap1 = rng & 2, tap2 = rng & 0x20;
        rng = (uint16_t)(rng >> 1 | (tap1 == tap2 ? 0x8000 : 0));
        CHECK(chip.rng == rng && chip.output[3] == (rng & 1));
    }
    cok_sound_chip_write(&chip, 0xe4); /* the same mode: no reset */
    CHECK(chip.rng == rng);
    cok_sound_chip_write(&chip, 0xe1); /* periodic, N/1024: reset */
    CHECK(chip.rng == 0x8000 && chip.period[3] == 64);
    rng = 0x8000;
    unsigned pulses = 0;
    for (int shift = 0; shift < 150; ++shift) {
        for (int k = 0; k < 64; ++k) cok_sound_chip_step(&chip);
        rng = (uint16_t)(rng >> 1 | (rng & 2 ? 0x8000 : 0));
        CHECK(chip.rng == rng && chip.output[3] == (rng & 1));
        pulses += rng & 1;
    }
    /* Periodic: the bit fed back from bit 1 comes round every 15 shifts. */
    CHECK(pulses == 10);
    cok_sound_chip_write(&chip, 0xe2);
    CHECK(chip.period[3] == 128);
}

/* Attenuation in 2 dB steps from 8191, 15 silent. */
static void test_volumes(void)
{
    for (unsigned k = 0; k < 15; ++k) {
        double expected = 8191.0 * pow(10.0, -(double)k / 10.0);
        CHECK(fabs(cok_sound_volume(k) - expected) <= 0.5);
    }
    CHECK(cok_sound_volume(15) == 0);
    cok_sound_chip chip;
    cok_sound_chip_init(&chip);
    CHECK(cok_sound_chip_level(&chip) == 0); /* every voice off at first */
    cok_sound_chip_write(&chip, 0x93);
    chip.output[0] = 1;
    CHECK(cok_sound_chip_level(&chip) == cok_sound_volume(3));
}

/* Rising crossings of zero in a run of samples. */
static unsigned crossings(const int16_t *samples, size_t n)
{
    unsigned count = 0;
    for (size_t i = 1; i < n; ++i)
        if (samples[i - 1] < 0 && samples[i] >= 0) ++count;
    return count;
}

/* Rendered: a Tandy voice at 440 Hz, the speaker at 440 Hz (PIT channel 2
 * at 2712), silence with the voices off and the speaker's bit clear. */
static void test_render(void)
{
    static int16_t samples[COK_SOUND_RATE];
    trace t;
    setup(COK_SOUND_TANDY, &t);
    cok_sound_out(&s, 0xc0, 0x8e);
    cok_sound_out(&s, 0xc0, 0x0f);
    cok_sound_out(&s, 0xc0, 0x90);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    unsigned n = crossings(samples + 4410, COK_SOUND_RATE - 4410);
    CHECK(n >= 394 && n <= 398); /* 440.4 Hz over 0.9 s */
    int16_t peak = 0;
    for (size_t i = 4410; i < COK_SOUND_RATE; ++i) if (samples[i] > peak) peak = samples[i];
    CHECK(peak > 3000 && peak < 5000); /* half of 8191 each side once the DC is gone */
    cok_sound_out(&s, 0xc0, 0x9f);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    for (size_t i = 22050; i < COK_SOUND_RATE; ++i) CHECK(samples[i] == 0);

    setup(COK_SOUND_SPEAKER, &t);
    cok_sound_out(&s, 0x43, 0xb6);
    cok_sound_out(&s, 0x42, 2712 & 0xff);
    cok_sound_out(&s, 0x42, 2712 >> 8);
    cok_sound_out(&s, 0x61, 3);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    n = crossings(samples + 4410, COK_SOUND_RATE - 4410);
    CHECK(n >= 394 && n <= 398); /* 1,193,182 / 2712 = 440 Hz */
    /* A control word for channel 2 holds its output high until a count
     * comes, the old one forgotten. */
    cok_sound_out(&s, 0x43, 0xb6);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    CHECK(crossings(samples + 4410, COK_SOUND_RATE - 4410) == 0);
    /* An even count: as many PIT clocks high as low (1,193,182 / 400). */
    cok_sound_out(&s, 0x42, 400 & 0xff);
    cok_sound_out(&s, 0x42, 400 >> 8);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    n = crossings(samples + 4410, COK_SOUND_RATE - 4410);
    CHECK(n >= 2683 && n <= 2686); /* 2,983 Hz over 0.9 s */
    cok_sound_out(&s, 0x42, 2712 & 0xff);
    cok_sound_out(&s, 0x42, 2712 >> 8);
    /* The gate on, the speaker's bit off: silence. */
    cok_sound_out(&s, 0x61, 1);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    for (size_t i = 22050; i < COK_SOUND_RATE; ++i) CHECK(samples[i] == 0);
    cok_sound_out(&s, 0x61, 3);
    /* The speaker's sound only reaches a speaker; the chip's writes do not. */
    cok_sound_out(&s, 0x61, 0);
    cok_sound_out(&s, 0xc0, 0x8e);
    cok_sound_out(&s, 0xc0, 0x0f);
    cok_sound_out(&s, 0xc0, 0x90);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    for (size_t i = 22050; i < COK_SOUND_RATE; ++i) CHECK(samples[i] == 0);
    /* Gate low: the output held high, no wave. */
    cok_sound_out(&s, 0x61, 2);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    CHECK(crossings(samples + 4410, COK_SOUND_RATE - 4410) == 0);
    /* The volume scales the samples. */
    setup(COK_SOUND_TANDY, &t);
    s.volume = 128;
    cok_sound_out(&s, 0xc0, 0x8e);
    cok_sound_out(&s, 0xc0, 0x0f);
    cok_sound_out(&s, 0xc0, 0x90);
    cok_sound_render(&s, samples, COK_SOUND_RATE);
    int16_t half = 0;
    for (size_t i = 4410; i < COK_SOUND_RATE; ++i) if (samples[i] > half) half = samples[i];
    CHECK(abs(half - peak / 2) <= 2);
}

/* The title's music and a sound or two, rendered in one go or in pieces of
 * any size, give the same samples. */
static void test_split(void)
{
    enum { N = 3 * COK_SOUND_RATE };
    static int16_t whole[N], pieces[N];
    for (int pass = 0; pass < 2; ++pass) {
        trace t;
        setup(pass == 0 ? COK_SOUND_TANDY : COK_SOUND_SPEAKER, &t);
        cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
        cok_sound_render(&s, whole, N / 2);
        cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
        cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
        cok_sound_call_at(&s, COK_SOUND_COMMAND, 6);
        cok_sound_render(&s, whole + N / 2, N - N / 2);
        bool sounding = false;
        for (size_t i = 0; i < N; ++i) sounding |= whole[i] != 0;
        CHECK(sounding);

        setup(pass == 0 ? COK_SOUND_TANDY : COK_SOUND_SPEAKER, &t);
        cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
        size_t at = 0, size = 1;
        while (at < N / 2) {
            size_t n = size < N / 2 - at ? size : N / 2 - at;
            cok_sound_render(&s, pieces + at, n);
            at += n;
            size = size * 7 % 1013 + 1;
        }
        cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
        cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
        cok_sound_call_at(&s, COK_SOUND_COMMAND, 6);
        while (at < N) {
            size_t n = size < N - at ? size : N - at;
            cok_sound_render(&s, pieces + at, n);
            at += n;
            size = size * 5 % 997 + 1;
        }
        CHECK(memcmp(whole, pieces, sizeof whole) == 0);
    }
}

/* Without MIDI.DAX or SOUND.DAX: no music, the effects all the same. */
static void test_missing_files(void)
{
    cok_sound_free(&s);
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_TANDY);
    CHECK(!cok_sound_load(&s, "no-such-directory"));
    CHECK(strstr(s.music_error, "MIDI.DAX") != NULL);
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
    CHECK(s.music && s.isr == COK_SOUND_BIOS);
    cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
    cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
    cok_sound_call_at(&s, COK_SOUND_COMMAND, 6);
    static int16_t samples[COK_SOUND_RATE / 2];
    cok_sound_render(&s, samples, COK_SOUND_RATE / 2);
    bool sounding = false;
    for (size_t i = 0; i < COK_SOUND_RATE / 2; ++i) sounding |= samples[i] != 0;
    CHECK(sounding);
    cok_sound_free(&s);
}

/* The devices KRYNN.CFG names that are not ported (tests/test_config.c
 * reads the file): CMS, MT-32 and AdLib. */
static void test_devices(void)
{
    /* CMS, MT-32 and AdLib: the effects on the speaker, no music. */
    for (int d = COK_SOUND_CMS; d <= COK_SOUND_ADLIB; ++d) {
        cok_sound_free(&s);
        cok_sound_init(&s, (cok_sound_device)d);
        CHECK(s.hardware == COK_SOUND_SPEAKER && s.device == d);
        CHECK(!cok_sound_load(&s, assets) && strstr(s.music_error, "not ported") != NULL);
        char text[100];
        CHECK(!cok_sound_music_unported(&s, text, sizeof text));
        cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
        CHECK(cok_sound_music_unported(&s, text, sizeof text));
        CHECK(strstr(text, cok_sound_device_name((cok_sound_device)d)) != NULL);
        CHECK(s.isr == COK_SOUND_BIOS);
        cok_sound_call_at(&s, COK_SOUND_MUSIC_END, 0);
        cok_sound_call_at(&s, COK_SOUND_INSTALL, 0);
        cok_sound_call_at(&s, COK_SOUND_COMMAND, 7);
        static int16_t samples[COK_SOUND_RATE / 2];
        cok_sound_render(&s, samples, COK_SOUND_RATE / 2);
        bool sounding = false;
        for (size_t i = 0; i < COK_SOUND_RATE / 2; ++i) sounding |= samples[i] != 0;
        CHECK(sounding); /* the speaker's sound 6 */
    }
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_TANDY);
    CHECK(cok_sound_load(&s, assets));
    cok_sound_call_at(&s, COK_SOUND_MUSIC, 0);
    char text[100];
    CHECK(!cok_sound_music_unported(&s, text, sizeof text));
    cok_sound_free(&s);
    cok_sound_init(&s, COK_SOUND_NONE);
    CHECK(s.hardware == COK_SOUND_TANDY);
    CHECK(strcmp(cok_sound_device_name(COK_SOUND_SPEAKER), "PC") == 0);
}

int main(void)
{
    test_devices();
    test_image();
    test_scenarios();
    test_first_ticks();
    test_ctrl_s();
    test_timer();
    test_chip_tone();
    test_chip_noise();
    test_volumes();
    test_render();
    test_split();
    test_missing_files();
    cok_sound_free(&s);
    puts("sound: ok");
    return 0;
}
