/* The AdLib's chip (src/opl2.c) against the YM3812's documented behaviour:
 * the ROMs from Gambrell and Niemitalo's formulas, the phase generator's
 * frequencies, the four waveforms, total level and KSL (the datasheet's
 * Table 3-4), the envelope's times (Table 3-6), KSR, EG-TYP, feedback and
 * the two connections, tremolo's and vibrato's depths and rates, the
 * timers and the status register, CSM, and writes to addresses the chip
 * lacks. */

#include "opl2.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

enum { RATE = 49716 };

static const double PI = 3.14159265358979323846;

/* The operator's output from the ROMs as the formulas give them. */
static int expected_wave(unsigned phase, unsigned attenuation)
{
    unsigned x = phase & 0xff;
    if (phase & 0x100) x ^= 0xff;
    long logsin = lround(-log(sin((x + 0.5) * PI / 256 / 2)) / log(2.0) * 256);
    unsigned level = (unsigned)logsin + (attenuation << 3);
    unsigned f = (level & 0xff) ^ 0xff;
    long e = lround((pow(2.0, f / 256.0) - 1) * 1024);
    int v = (int)(((unsigned)e + 1024) << 1 >> (level >> 8));
    if ((level >> 8) >= 13) v = 0;
    return phase & 0x200 ? -v : v;
}

static void test_roms(void)
{
    for (unsigned att = 0; att < 512; att += 37)
        for (unsigned p = 0; p < 1024; ++p) CHECK(cok_opl2_wave(p, 0, att) == expected_wave(p, att));
    /* Full level: a sine of amplitude 4084 (12 bits and the sign). */
    int peak = 0;
    for (unsigned p = 0; p < 1024; ++p) {
        int v = cok_opl2_wave(p, 0, 0);
        double s = 4084 * sin(2 * PI * (p + 0.5) / 1024);
        CHECK(fabs(v - s) <= 4084 * 0.01 + 2);
        if (v > peak) peak = v;
    }
    CHECK(peak == 4084);
    CHECK(cok_opl2_wave(0x100, 0, 511) == 0);
}

/* A chip with channel 0 set up as one carrier (CNT 1, the modulator
 * silent) at fnum/block/mult, full level, attack instant, sustained. */
static void one_voice(cok_opl2 *chip, unsigned fnum, unsigned block, unsigned mult)
{
    cok_opl2_init(chip);
    cok_opl2_write(chip, 0x20, 0x20);        /* modulator: EG-TYP */
    cok_opl2_write(chip, 0x40, 0x3f);        /* silent */
    cok_opl2_write(chip, 0x23, (uint8_t)(0x20 | mult));
    cok_opl2_write(chip, 0x43, 0x00);
    cok_opl2_write(chip, 0x63, 0xf0);        /* AR 15 */
    cok_opl2_write(chip, 0x83, 0x00);        /* SL 0 */
    cok_opl2_write(chip, 0xc0, 0x01);        /* additive */
    cok_opl2_write(chip, 0xa0, (uint8_t)fnum);
    cok_opl2_write(chip, 0xb0, (uint8_t)(0x20 | block << 2 | fnum >> 8));
}

static unsigned crossings(cok_opl2 *chip, unsigned samples)
{
    unsigned n = 0;
    int32_t last = cok_opl2_sample(chip);
    for (unsigned i = 1; i < samples; ++i) {
        int32_t v = cok_opl2_sample(chip);
        if (last < 0 && v >= 0) ++n;
        last = v;
    }
    return n;
}

/* The frequency: fnum * 49,716 * 2^(block - 20) Hz times the multiple. */
static void test_phase(void)
{
    static const double multiple[16] = {0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};
    for (unsigned mult = 0; mult < 16; ++mult) {
        double hz = 0x244 * pow(2.0, 4 - 20.0) * multiple[mult] * RATE;
        CHECK(fabs(cok_opl2_increment(0x244, 4, mult) * (double)RATE / (1 << 19) - hz) < 0.1);
    }
    CHECK(cok_opl2_increment(0x244, 4, 0) * 2 == cok_opl2_increment(0x244, 4, 1));
    CHECK(cok_opl2_increment(0x244, 4, 11) == cok_opl2_increment(0x244, 4, 10));
    CHECK(cok_opl2_increment(0x244, 4, 13) == cok_opl2_increment(0x244, 4, 12));
    CHECK(cok_opl2_increment(0x244, 4, 15) == 15 * cok_opl2_increment(0x244, 4, 1));
    /* Played: A at 440 Hz (the AdLib programming guide's 0x244, block 4), a
     * second of it; an octave up with block 5; MULT 0 an octave down. */
    cok_opl2 chip;
    one_voice(&chip, 0x244, 4, 1);
    unsigned n = crossings(&chip, RATE);
    CHECK(n >= 439 && n <= 441);
    one_voice(&chip, 0x244, 5, 1);
    n = crossings(&chip, RATE);
    CHECK(n >= 879 && n <= 881);
    one_voice(&chip, 0x244, 4, 0);
    n = crossings(&chip, RATE);
    CHECK(n >= 219 && n <= 221);
    one_voice(&chip, 0x244, 4, 15);
    n = crossings(&chip, RATE);
    CHECK(n >= 6599 && n <= 6601);
    /* Key-on restarts the phase. */
    one_voice(&chip, 0x244, 4, 1);
    for (int i = 0; i < 100; ++i) cok_opl2_sample(&chip);
    CHECK(chip.op[3].phase != 0);
    cok_opl2_write(&chip, 0xb0, 0x10);
    cok_opl2_write(&chip, 0xb0, 0x32);
    CHECK(chip.op[3].phase == 0 && chip.op[3].state == COK_OPL2_ATTACK);
}

/* The waveforms, only with the test register's bit 5. */
static void test_waveforms(void)
{
    for (unsigned p = 0; p < 1024; ++p) {
        int sine = cok_opl2_wave(p, 0, 0);
        CHECK(cok_opl2_wave(p, 1, 0) == (p < 512 ? sine : 0));
        CHECK(cok_opl2_wave(p, 2, 0) == abs(sine));
        CHECK(cok_opl2_wave(p, 3, 0) == ((p & 256) ? 0 : abs(sine)));
    }
    cok_opl2 chip;
    one_voice(&chip, 0x244, 4, 1);
    cok_opl2_write(&chip, 0xe3, 0x02);   /* |sine|, but not enabled */
    bool negative = false;
    for (int i = 0; i < 2000; ++i) negative |= cok_opl2_sample(&chip) < 0;
    CHECK(negative);
    cok_opl2_write(&chip, 0x01, 0x20);
    negative = false;
    for (int i = 0; i < 2000; ++i) negative |= cok_opl2_sample(&chip) < 0;
    CHECK(!negative);
}

static int32_t peak_of(cok_opl2 *chip, unsigned samples)
{
    int32_t peak = 0;
    for (unsigned i = 0; i < samples; ++i) {
        int32_t v = cok_opl2_sample(chip);
        if (v > peak) peak = v;
    }
    return peak;
}

/* Total level: 0.75 dB steps; KSL: the datasheet's Table 3-4 at 3 dB an
 * octave (KSL 1), half at KSL 2, twice at 3. */
static void test_levels(void)
{
    cok_opl2 chip;
    one_voice(&chip, 0x244, 4, 1);
    int32_t full = peak_of(&chip, 2000);
    CHECK(full >= 4000);
    for (unsigned tl = 4; tl < 64; tl += 12) {
        one_voice(&chip, 0x244, 4, 1);
        cok_opl2_write(&chip, 0x43, (uint8_t)tl);
        double db = 20 * log10((double)full / peak_of(&chip, 2000));
        CHECK(fabs(db - 0.75 * tl) < 0.3);
    }
    /* Table 3-4 (dB at 3 dB an octave), block by row, the F-number's top
     * four bits by column. */
    static const struct { unsigned block, top; double db; } table[] = {
        {7, 15, 21.000}, {7, 1, 9.000}, {7, 3, 13.875}, {7, 8, 18.000}, {4, 8, 9.000},
        {4, 3, 4.875}, {4, 2, 3.000}, {4, 1, 0.000}, {1, 15, 3.000}, {1, 12, 1.875},
        {0, 15, 0.000}, {5, 6, 10.875}, {6, 4, 12.000}, {2, 10, 4.125},
    };
    for (size_t i = 0; i < sizeof table / sizeof *table; ++i) {
        unsigned fnum = table[i].top << 6;
        CHECK(fabs(cok_opl2_ksl(fnum, table[i].block, 1) * 0.1875 - table[i].db) < 0.01);
        CHECK(fabs(cok_opl2_ksl(fnum, table[i].block, 2) * 0.1875 - table[i].db / 2) < 0.2);
        CHECK(fabs(cok_opl2_ksl(fnum, table[i].block, 3) * 0.1875 - table[i].db * 2) < 0.01);
        CHECK(cok_opl2_ksl(fnum, table[i].block, 0) == 0);
    }
    /* Played: KSL 3 at block 7, top bits 15: 42 dB down. */
    one_voice(&chip, 0x3c0, 7, 1);
    full = peak_of(&chip, 2000);
    one_voice(&chip, 0x3c0, 7, 1);
    cok_opl2_write(&chip, 0x43, 0xc0);
    double db = 20 * log10((double)full / peak_of(&chip, 2000));
    CHECK(fabs(db - 42) < 0.5);
}

/* An operator keyed on at an effective rate 4 * r + low: KSR on, low the
 * key scaling (block 0 or 1, the F-number's top bit). */
static void keyed(cok_opl2 *chip, uint8_t reg20, uint8_t reg60, uint8_t reg80, unsigned low)
{
    cok_opl2_init(chip);
    cok_opl2_write(chip, 0x23, reg20);
    cok_opl2_write(chip, 0x63, reg60);
    cok_opl2_write(chip, 0x83, reg80);
    cok_opl2_write(chip, 0xb0, (uint8_t)(0x20 | (low >> 1) << 2 | (low & 1) << 1));
}

/* Samples from key-on to full level. */
static unsigned attack_time(unsigned r, unsigned low)
{
    cok_opl2 chip;
    keyed(&chip, 0x30, (uint8_t)(r << 4), 0, low);
    unsigned n = 0;
    while (chip.op[3].env > 0 && n < 100000000) {
        cok_opl2_sample(&chip);
        ++n;
    }
    return n;
}

/* Samples from full level to silence (or to the sustain level held). */
static unsigned decay_time(unsigned r, unsigned low, bool sustained, unsigned sl, unsigned *final)
{
    cok_opl2 chip;
    keyed(&chip, sustained ? 0x30 : 0x10, (uint8_t)(0xf0 | r), (uint8_t)(sl << 4 | r), low);
    cok_opl2_sample(&chip); /* the attack, at once */
    CHECK(chip.op[3].env == 0);
    unsigned n = 0;
    while (chip.op[3].env < 511 && n < 200000000) {
        cok_opl2_sample(&chip);
        ++n;
        if (chip.op[3].state == COK_OPL2_SUSTAIN && sustained && n > 4000000) break;
    }
    *final = chip.op[3].env;
    return n;
}

/* Table 3-6, at 3.6 MHz (50,000 samples a second), for rates r.low:
 * attack 0-96 dB, 2,826.24, 2,252.80, 1,884.16 and 1,597.44 ms at 1.0-1.3,
 * halving each step of r up to 11 (0 at 15); decay 0-96 dB, 39,280.64, 31,416.32,
 * 26,173.44 and 22,446.08 ms, 2.40 at 15. The attack's times are the
 * model's to the sample (one either way where a step falls with the
 * counter); the table's decay times are 479.5 steps of 0.1875 dB, the
 * model's envelope runs all 511 (96 dB): 512 / 479.5 longer. */
static void test_envelope(void)
{
    static const double attack[4] = {2826.24, 2252.80, 1884.16, 1597.44};
    static const double decay[4] = {39280.64, 31416.32, 26173.44, 22446.08};
    /* 12.0-14.3, where the table stops halving. */
    static const double fast[3][4] = {{1.40, 1.12, 0.92, 0.80}, {0.70, 0.56, 0.46, 0.42},
                                      {0.38, 0.30, 0.24, 0.20}};
    for (unsigned r = 1; r <= 14; ++r)
        for (unsigned low = 0; low < 4; ++low) {
            double table = r >= 12 ? fast[r - 12][low] * 50 : attack[low] * 50 / (1 << (r - 1));
            unsigned n = attack_time(r, low);
            CHECK(fabs(n - table) <= 1.5 || fabs(n / table - 1) < 0.005);
        }
    for (unsigned low = 0; low < 4; ++low) CHECK(attack_time(15, low) <= 1); /* at once */
    for (unsigned r = 1; r <= 15; ++r)
        for (unsigned low = 0; low < 4; low += r < 10 ? 3 : 1) {
            unsigned final;
            double n = decay_time(r, low, false, 15, &final);
            double table = (r == 15 ? 2.40 * 50 : decay[low] * 50 / (1 << (r - 1))) * 512 / 479.5;
            CHECK(final == 511 && fabs(n / table - 1) < 0.01);
        }
    /* The rate's low bits: 4, 5, 6 or 7 steps of 8. */
    CHECK(cok_opl2_eg_step(0, 0) == 0);
    for (unsigned low = 0; low < 4; ++low) {
        unsigned sum = 0;
        for (uint32_t c = 0; c < 8 * 1024; ++c) sum += cok_opl2_eg_step(8 * 4 + low, c);
        CHECK(sum == (4 + low) * 64); /* rate 8: of a slot every 16 samples, (4 + low) / 8 */
        sum = 0;
        for (uint32_t c = 0; c < 8; ++c) sum += cok_opl2_eg_step(13 * 4 + low, c);
        CHECK(sum == (4 + low) * 2);
    }
    /* KSR: block 7 with its F-number's top bit adds 15 to the rate with
     * KSR, 3 without (15 / 4 and 3 / 4 of an octave of time); NTS (0x08 bit
     * 6) takes the F-number's top bit, else its second. */
    cok_opl2 chip;
    for (int k = 0; k < 4; ++k) {
        bool ksr = k & 1, nts = k & 2;
        cok_opl2_init(&chip);
        if (nts) cok_opl2_write(&chip, 0x08, 0x40);
        cok_opl2_write(&chip, 0x23, ksr ? 0x10 : 0x00);
        cok_opl2_write(&chip, 0x63, 0x40);
        cok_opl2_write(&chip, 0xa0, 0x00);
        cok_opl2_write(&chip, 0xb0, 0x3d); /* block 7, the F-number's top bit only */
        unsigned n = 0;
        while (chip.op[3].env > 0) {
            cok_opl2_sample(&chip);
            ++n;
        }
        unsigned rate = 16 + ((7u << 1 | (nts ? 1 : 0)) >> (ksr ? 0 : 2));
        double table = attack[rate & 3] * 50 / (1 << ((rate >> 2) - 1));
        CHECK(fabs(n - table) <= 1.5 || fabs(n / table - 1) < 0.005);
    }
    /* EG-TYP 1 holds the sustain level (SL 4: 12 dB) while the key is down;
     * 0 decays on at the release rate. */
    unsigned final;
    decay_time(8, 0, true, 4, &final);
    CHECK(final == 4 << 4);
    decay_time(8, 0, false, 4, &final);
    CHECK(final == 511);
    decay_time(8, 0, true, 15, &final);
    CHECK(final == 31 << 4);
    /* Key-off releases at RR. */
    one_voice(&chip, 0x244, 4, 1);
    for (int i = 0; i < 10; ++i) cok_opl2_sample(&chip);
    CHECK(chip.op[3].env == 0);
    cok_opl2_write(&chip, 0x83, 0x0f);
    cok_opl2_write(&chip, 0xb0, 0x12);
    CHECK(chip.op[3].state == COK_OPL2_RELEASE);
    unsigned n = 0;
    while (chip.op[3].env < 511) {
        cok_opl2_sample(&chip);
        ++n;
    }
    CHECK(n >= 125 && n <= 130); /* rate 15 + scaling: 4 a sample */
}

/* Feedback: FB 0 a pure sine, more distortion each step; FB 7 (4 pi) the
 * most. The two connections: FM (the carrier alone heard) and additive. */
static void test_feedback(void)
{
    double last = -1;
    for (unsigned fb = 0; fb < 8; ++fb) {
        cok_opl2 chip;
        cok_opl2_init(&chip);
        cok_opl2_write(&chip, 0x20, 0x21);
        cok_opl2_write(&chip, 0x60, 0xf0);
        cok_opl2_write(&chip, 0x43, 0x3f);  /* the carrier silent */
        cok_opl2_write(&chip, 0xc0, (uint8_t)(fb << 1 | 1));
        cok_opl2_write(&chip, 0xa0, 0x44);
        cok_opl2_write(&chip, 0xb0, 0x32);
        for (int i = 0; i < 10; ++i) cok_opl2_sample(&chip);
        /* The modulator's phase moved by its last two outputs' sum, shifted
         * right by 9 - FB (FB 1: pi / 16 at full level, 7: 4 pi). */
        double distortion = 0;
        int32_t p1 = chip.op[0].out[0], p2 = chip.op[0].out[1];
        for (int i = 0; i < 4000; ++i) {
            unsigned phase = chip.op[0].phase >> 9;
            int32_t sum = p1 + p2, shift = fb != 0 ? (int32_t)floor(sum / pow(2.0, 9 - fb)) : 0;
            int32_t expect = cok_opl2_wave((unsigned)((int32_t)phase + shift) & 0x3ff, 0, 0);
            int32_t v = cok_opl2_sample(&chip);
            CHECK(v == cok_opl2_dac(expect));
            p2 = p1;
            p1 = expect;
            double d = v - cok_opl2_dac(cok_opl2_wave(phase, 0, 0));
            distortion += d * d;
        }
        if (fb == 0) CHECK(distortion == 0);
        else if (fb <= 5) CHECK(distortion > last);
        last = distortion;
    }
    /* FM: the modulator alone is not heard (the carrier never attacks);
     * additive, it is. With the carrier loud and the modulator silent, the
     * carrier's sine; with both, its phase moved by the modulator. */
    cok_opl2 chip;
    cok_opl2_init(&chip);
    cok_opl2_write(&chip, 0x20, 0x21);
    cok_opl2_write(&chip, 0x60, 0xf0);
    cok_opl2_write(&chip, 0x23, 0x21);
    cok_opl2_write(&chip, 0x63, 0x00);
    cok_opl2_write(&chip, 0xa0, 0x44);
    cok_opl2_write(&chip, 0xb0, 0x32);
    CHECK(peak_of(&chip, 2000) == 0);
    cok_opl2_write(&chip, 0xc0, 0x01);
    CHECK(peak_of(&chip, 2000) >= 4000);
    cok_opl2_write(&chip, 0x63, 0xf0);
    cok_opl2_write(&chip, 0x40, 0x3f);
    cok_opl2_write(&chip, 0x60, 0x0f);  /* the modulator decays to silence */
    cok_opl2_write(&chip, 0x80, 0x0f);
    cok_opl2_write(&chip, 0x20, 0x01);
    cok_opl2_write(&chip, 0xc0, 0x00);
    cok_opl2_write(&chip, 0xb0, 0x12);
    cok_opl2_write(&chip, 0xb0, 0x32);
    for (int i = 0; i < 2000; ++i) cok_opl2_sample(&chip);
    CHECK(chip.op[0].env == 511 && chip.op[3].env == 0);
    for (int i = 0; i < 4000; ++i) {
        unsigned phase = chip.op[3].phase >> 9;
        CHECK(cok_opl2_sample(&chip) == cok_opl2_dac(cok_opl2_wave(phase, 0, 0)));
    }
    cok_opl2_write(&chip, 0x40, 0x00);
    cok_opl2_write(&chip, 0x60, 0xf0);
    cok_opl2_write(&chip, 0x20, 0x21);
    cok_opl2_write(&chip, 0xb0, 0x12);
    cok_opl2_write(&chip, 0xb0, 0x32);
    for (int i = 0; i < 10; ++i) cok_opl2_sample(&chip);
    for (int i = 0; i < 4000; ++i) {
        unsigned phase = chip.op[3].phase >> 9, mphase = chip.op[0].phase >> 9;
        int32_t m = cok_opl2_wave(mphase, 0, 0);
        CHECK(cok_opl2_sample(&chip) == cok_opl2_dac(cok_opl2_wave((phase + (unsigned)m) & 0x3ff, 0, 0)));
    }
}

/* Tremolo: 3.7 Hz, 0-4.875 dB (0xBD bit 7) or 0-1.125 dB; vibrato: 6.1 Hz,
 * the F-number's top three bits (14 cents) or half (7 cents). */
static void test_lfo(void)
{
    unsigned deepest = 0, shallowest = 0;
    for (uint32_t t = 0; t < 13440; ++t) {
        unsigned d = cok_opl2_tremolo(t, true), s = cok_opl2_tremolo(t, false);
        if (d > deepest) deepest = d;
        if (s > shallowest) shallowest = s;
        CHECK(cok_opl2_tremolo(t + 13440, true) == d);
    }
    CHECK(deepest == 26 && shallowest == 6);
    CHECK(fabs(RATE / 13440.0 - 3.7) < 0.01);
    int top = 0, bottom = 0;
    for (uint32_t t = 0; t < 8192; ++t) {
        int v = cok_opl2_vibrato(t, 0x3ff, true);
        if (v > top) top = v;
        if (v < bottom) bottom = v;
        CHECK(cok_opl2_vibrato(t + 8192, 0x3ff, true) == v);
        CHECK(cok_opl2_vibrato(t, 0x3ff, false) == (v < 0 ? -((-v) >> 1) : v >> 1));
    }
    CHECK(top == 7 && bottom == -7);
    /* 14 cents at the F-number 0x200: 4 / 512, 13.5 cents. */
    double cents = 1200 * log2((0x200 + cok_opl2_vibrato(2048, 0x200, true)) / 512.0);
    CHECK(fabs(cents - 13.5) < 0.1);
    /* Played: tremolo deep on the carrier dips the level by 4.875 dB at
     * most. */
    cok_opl2 chip;
    one_voice(&chip, 0x244, 4, 1);
    cok_opl2_write(&chip, 0x23, 0xa1);
    cok_opl2_write(&chip, 0xbd, 0x80);
    int32_t loudest = 0, quietest = 1 << 20;
    for (int window = 0; window < 120; ++window) {
        int32_t p = peak_of(&chip, 112);
        if (p > loudest) loudest = p;
        if (p < quietest) quietest = p;
    }
    double db = 20 * log10((double)loudest / quietest);
    CHECK(db > 4.5 && db < 5.1);
}

/* The timers: 80 us (4 samples) and 320 us (16) a count, the flags in the
 * status register (0x06 below them), masks, IRQ reset; CSM. */
static void test_timers(void)
{
    cok_opl2 chip;
    cok_opl2_init(&chip);
    CHECK(cok_opl2_status(&chip) == 0x06);
    cok_opl2_write(&chip, 0x02, 0xf0);   /* 16 counts: 64 samples */
    cok_opl2_write(&chip, 0x04, 0x01);
    unsigned n = 0;
    while (!(cok_opl2_status(&chip) & 0x40)) {
        cok_opl2_sample(&chip);
        ++n;
    }
    CHECK(n > 60 && n <= 64 && cok_opl2_status(&chip) == 0xc6); /* within a count */
    cok_opl2_write(&chip, 0x04, 0x80);
    CHECK(cok_opl2_status(&chip) == 0x06);
    cok_opl2_write(&chip, 0x04, 0x00);
    cok_opl2_write(&chip, 0x03, 0xff);   /* timer 2: 1 count, 16 samples */
    cok_opl2_write(&chip, 0x04, 0x02);
    for (n = 0; !(cok_opl2_status(&chip) & 0x20); ++n) cok_opl2_sample(&chip);
    CHECK(n <= 16 && cok_opl2_status(&chip) == 0xa6);
    cok_opl2_write(&chip, 0x04, 0x80);
    cok_opl2_write(&chip, 0x04, 0x22);   /* masked: the flag stays 0 */
    for (int i = 0; i < 1000; ++i) cok_opl2_sample(&chip);
    CHECK(cok_opl2_status(&chip) == 0x06);
    /* The usual AdLib detection: timer 1 at 0xff, started with timer 2
     * masked, overflows within 80 us. */
    cok_opl2_init(&chip);
    cok_opl2_write(&chip, 0x04, 0x60);
    cok_opl2_write(&chip, 0x04, 0x80);
    CHECK((cok_opl2_status(&chip) & 0xe0) == 0);
    cok_opl2_write(&chip, 0x02, 0xff);
    cok_opl2_write(&chip, 0x04, 0x21);
    for (int i = 0; i < 4; ++i) cok_opl2_sample(&chip);
    CHECK((cok_opl2_status(&chip) & 0xe0) == 0xc0);
    /* CSM: timer 1's overflow keys every operator on and off. */
    cok_opl2_init(&chip);
    cok_opl2_write(&chip, 0x08, 0x80);
    cok_opl2_write(&chip, 0x02, 0xff);
    cok_opl2_write(&chip, 0x04, 0x01);
    for (int i = 0; i < 4; ++i) cok_opl2_sample(&chip);
    CHECK(chip.op[7].state == COK_OPL2_RELEASE && chip.op[7].key == 0);
}

/* Writes to addresses the chip lacks change nothing. */
static void test_unused(void)
{
    cok_opl2 chip, before;
    cok_opl2_init(&chip);
    static const uint8_t unused[] = {0x00, 0x05, 0x06, 0x07, 0x09, 0x1f, 0x26, 0x27, 0x2e, 0x2f, 0x36,
                                     0x46, 0x4f, 0x56, 0x66, 0x76, 0x86, 0x96, 0x9f, 0xa9, 0xaf,
                                     0xb9, 0xbc, 0xbe, 0xc9, 0xcf, 0xd0, 0xdf, 0xe6, 0xf6, 0xff};
    for (size_t i = 0; i < sizeof unused; ++i) {
        before = chip;
        cok_opl2_write(&chip, unused[i], 0xff);
        CHECK(memcmp(&before, &chip, sizeof chip) == 0);
    }
    /* Through the ports: the address, then the data. */
    cok_opl2_write_address(&chip, 0xa3);
    cok_opl2_write_data(&chip, 0x5a);
    CHECK(chip.ch[3].fnum == 0x5a);
    /* The rhythm's keys: the bass drum keys both of channel 6's operators. */
    cok_opl2_write(&chip, 0xbd, 0x30);
    CHECK(chip.op[12].key == 2 && chip.op[15].key == 2 && chip.op[13].key == 0);
    cok_opl2_write(&chip, 0xbd, 0x00);
    CHECK(chip.op[12].state == COK_OPL2_RELEASE);
}

int main(void)
{
    test_roms();
    test_phase();
    test_waveforms();
    test_levels();
    test_envelope();
    test_feedback();
    test_lfo();
    test_timers();
    test_unused();
    puts("opl2: ok");
    return 0;
}
