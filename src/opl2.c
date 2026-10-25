#include "opl2.h"

#include <string.h>

/* The YM3812's two ROMs, as Matthew Gambrell and Olli Niemitalo read them
 * off a decapsulated chip ("OPLx decapsulated", 2008): a quarter of a
 * sine's logarithm, round(-log2(sin((x + 0.5) * pi / 512)) * 256), and an
 * exponent, round((2^(x / 256) - 1) * 1024), x = 0-255.
 * tests/test_opl2.c regenerates both from those formulas. */
static const uint16_t logsin_rom[256] = {
    2137, 1731, 1543, 1419, 1326, 1252, 1190, 1137, 1091, 1050, 1013, 979, 949, 920, 894, 869,
    846, 825, 804, 785, 767, 749, 732, 717, 701, 687, 672, 659, 646, 633, 621, 609,
    598, 587, 576, 566, 556, 546, 536, 527, 518, 509, 501, 492, 484, 476, 468, 461,
    453, 446, 439, 432, 425, 418, 411, 405, 399, 392, 386, 380, 375, 369, 363, 358,
    352, 347, 341, 336, 331, 326, 321, 316, 311, 307, 302, 297, 293, 289, 284, 280,
    276, 271, 267, 263, 259, 255, 251, 248, 244, 240, 236, 233, 229, 226, 222, 219,
    215, 212, 209, 205, 202, 199, 196, 193, 190, 187, 184, 181, 178, 175, 172, 169,
    167, 164, 161, 159, 156, 153, 151, 148, 146, 143, 141, 138, 136, 134, 131, 129,
    127, 125, 122, 120, 118, 116, 114, 112, 110, 108, 106, 104, 102, 100, 98, 96,
    94, 92, 91, 89, 87, 85, 83, 82, 80, 78, 77, 75, 74, 72, 70, 69,
    67, 66, 64, 63, 62, 60, 59, 57, 56, 55, 53, 52, 51, 49, 48, 47,
    46, 45, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30,
    29, 28, 27, 26, 25, 24, 23, 23, 22, 21, 20, 20, 19, 18, 17, 17,
    16, 15, 15, 14, 13, 13, 12, 12, 11, 10, 10, 9, 9, 8, 8, 7,
    7, 7, 6, 6, 5, 5, 5, 4, 4, 4, 3, 3, 3, 2, 2, 2,
    2, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint16_t exp_rom[256] = {
    0, 3, 6, 8, 11, 14, 17, 20, 22, 25, 28, 31, 34, 37, 40, 42,
    45, 48, 51, 54, 57, 60, 63, 66, 69, 72, 75, 78, 81, 84, 87, 90,
    93, 96, 99, 102, 105, 108, 111, 114, 117, 120, 123, 126, 130, 133, 136, 139,
    142, 145, 148, 152, 155, 158, 161, 164, 168, 171, 174, 177, 181, 184, 187, 190,
    194, 197, 200, 204, 207, 210, 214, 217, 220, 224, 227, 231, 234, 237, 241, 244,
    248, 251, 255, 258, 262, 265, 268, 272, 276, 279, 283, 286, 290, 293, 297, 300,
    304, 308, 311, 315, 318, 322, 326, 329, 333, 337, 340, 344, 348, 352, 355, 359,
    363, 367, 370, 374, 378, 382, 385, 389, 393, 397, 401, 405, 409, 412, 416, 420,
    424, 428, 432, 436, 440, 444, 448, 452, 456, 460, 464, 468, 472, 476, 480, 484,
    488, 492, 496, 501, 505, 509, 513, 517, 521, 526, 530, 534, 538, 542, 547, 551,
    555, 560, 564, 568, 572, 577, 581, 585, 590, 594, 599, 603, 607, 612, 616, 621,
    625, 630, 634, 639, 643, 648, 652, 657, 661, 666, 670, 675, 680, 684, 689, 693,
    698, 703, 708, 712, 717, 722, 726, 731, 736, 741, 745, 750, 755, 760, 765, 770,
    774, 779, 784, 789, 794, 799, 804, 809, 814, 819, 824, 829, 834, 839, 844, 849,
    854, 859, 864, 869, 874, 880, 885, 890, 895, 900, 906, 911, 916, 921, 927, 932,
    937, 942, 948, 953, 959, 964, 969, 975, 980, 986, 991, 996, 1002, 1007, 1013, 1018,
};

/* MULT 0-15 as twice the multiple (the manual's Table 3-1: 1/2, 1-10,
 * 10, 12, 12, 15, 15). */
static const uint8_t mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

/* The manual's Table 3-5 (p. 14), KSL's attenuation at 3 dB an octave, in
 * thousandths of a dB: a row for each block (OCT 0-7), a column for the
 * F-number's top four bits. */
static const uint16_t ksl_table[8][16] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 750, 1125, 1500, 1875, 2250, 2625, 3000},
    {0, 0, 0, 0, 0, 1125, 1875, 2625, 3000, 3750, 4125, 4500, 4875, 5250, 5625, 6000},
    {0, 0, 0, 1875, 3000, 4125, 4875, 5625, 6000, 6750, 7125, 7500, 7875, 8250, 8625, 9000},
    {0, 0, 3000, 4875, 6000, 7125, 7875, 8625, 9000, 9750, 10125, 10500, 10875, 11250, 11625, 12000},
    {0, 3000, 6000, 7875, 9000, 10125, 10875, 11625, 12000, 12750, 13125, 13500, 13875, 14250, 14625, 15000},
    {0, 6000, 9000, 10875, 12000, 13125, 13875, 14625, 15000, 15750, 16125, 16500, 16875, 17250, 17625, 18000},
    {0, 9000, 12000, 13875, 15000, 16125, 16875, 17625, 18000, 18750, 19125, 19500, 19875, 20250, 20625, 21000},
};

/* The manual's Table 3-6 (p. 16), in hundredths of a ms at 3.6 MHz (50,000
 * samples a second, so a sample is 2 of them), for the rates 4 * RM + RL,
 * RM 1-15 by row, RL 0-3 by column: the decay's (and release's) time from
 * 0 to 96 dB, and the attack's from 96 to 0 dB (15: at once). */
static const uint32_t decay_time[15][4] = {
    {3928064, 3141632, 2617344, 2244608}, {1964032, 1570816, 1308672, 1122304},
    {982016, 785408, 654336, 561152},     {491008, 392704, 327168, 280576},
    {245504, 196352, 163584, 140288},     {122752, 98176, 81792, 70144},
    {61376, 49088, 40896, 35072},         {30688, 24544, 20448, 17536},
    {15344, 12272, 10224, 8768},          {7672, 6136, 5112, 4384},
    {3836, 3068, 2556, 2192},             {1920, 1536, 1280, 1096},
    {960, 768, 640, 548},                 {480, 384, 320, 274},
    {240, 240, 240, 240},
};

/* The attack, ours: the manual (3-3, p. 21) says its level changes
 * exponentially, from 96 dB down to 0 dB, where the decay takes over. We
 * take the attenuation A (in 0.1875 dB steps) plus an offset of 6.5 steps
 * to shrink by the same factor every sample, so that A + 6.5 falls from
 * 512 + 6.5 to 6.5 in the time of Table 3-6's attack column (0-96 dB):
 * a factor exp(-ln(518.5 / 6.5) / samples). The offset is what puts the
 * table's other column, the attack's 10% to 90% of the amplitude (20 dB
 * to 0.92 dB), where it is: that time over the whole one is 0.526 in the
 * table (its mean over the rates 1.0-14.3; 0.46-0.57 by rate) and ln((106.7 + c) / (4.9 + c)) /
 * ln((512 + c) / c) for an offset c, at most 0.5245, at c = 6.5. The
 * factors, as fractions of 2^32 for the rates 4-59, from
 * round(exp(-ln(518.5 / 6.5) / (attack time in samples)) * 2^32);
 * tests/test_opl2.c computes them again from Table 3-6's times. */
static const uint32_t attack_factor[56] = {
    4294834201u, 4294800323u, 4294767655u, 4294731823u, /* 1.0-1.3 */
    4294701110u, 4294633356u, 4294568022u, 4294496362u, /* 2.0-2.3 */
    4294434940u, 4294299441u, 4294168786u, 4294025480u, /* 3.0-3.3 */
    4293902649u, 4293631690u, 4293370425u, 4293083870u, /* 4.0-4.3 */
    4292838266u, 4292296500u, 4291774147u, 4291201269u, /* 5.0-5.3 */
    4290710292u, 4289627364u, 4288583372u, 4287438544u, /* 6.0-6.3 */
    4286457507u, 4284294072u, 4282208937u, 4279922990u, /* 7.0-7.3 */
    4277964578u, 4273647371u, 4269488477u, 4264931381u, /* 8.0-8.3 */
    4261029170u, 4252433277u, 4244160804u, 4235105515u, /* 9.0-9.3 */
    4227359217u, 4210320483u, 4193955318u, 4176078067u, /* 10.0-10.3 */
    4160815372u, 4127341920u, 4095319010u, 4060479816u, /* 11.0-11.3 */
    4034509887u, 3971901911u, 3904951222u, 3849585556u, /* 12.0-12.3 */
    3789847257u, 3673137350u, 3550351608u, 3486552993u, /* 13.0-13.3 */
    3410855029u, 3207530792u, 2981766132u, 2771892101u, /* 14.0-14.3 */
};

enum {
    /* The envelope's attenuation in steps of 0.1875 dB (3-3: 96 dB at
     * that resolution, 512 steps) with 22 bits of fraction. */
    EG_FRAC = 22,
    /* The attack's offset, 6.5 steps. */
    EG_OFFSET = 13 << (EG_FRAC - 1),
    /* The LFOs, ours: 32-bit phases a sample up by the frequency over
     * 50,000 Hz (the manual gives both rates at 3.6 MHz), 3.7 Hz (AM) and
     * 6.4 Hz (VIB, p. 13). */
    TREMOLO_STEP = 317828, /* round(3.7 / 50000 * 2^32) */
    VIBRATO_STEP = 549756, /* round(6.4 / 50000 * 2^32) */
    /* Their depths (3-1-10, p. 20): AM 4.8 dB or 1 dB, in eighths of 0.1875
     * dB (4.8 / 0.1875 * 8 = 204.8, 1 / 0.1875 * 8 = 42.7); VIB 14 or 7
     * cents either way, as (2^(cents / 1200) - 1) * 65536 = 532.1 and 266. */
    TREMOLO_DEEP = 205,
    TREMOLO_SHALLOW = 43,
    VIBRATO_DEEP = 532,
    VIBRATO_SHALLOW = 266,
    /* 96 dB in eighths of 0.1875 dB: silent. */
    LEVEL_SILENT = 4095,
};

static const uint32_t EG_SILENT = 512u << EG_FRAC;

/* The operator of a register's low five bits (0x00-0x15 but 06, 07, 0E,
 * 0F), or -1. Slots 0-17 run as the registers do: 0-5 at 00-05, 6-11 at
 * 08-0D, 12-17 at 10-15. */
static int slot_of(unsigned low)
{
    if (low > 0x15 || (low & 7) > 5) return -1;
    return (int)((low >> 3) * 6 + (low & 7));
}

/* A channel's operators: the modulator, and the carrier three after it. */
static unsigned modulator_of(unsigned ch)
{
    return ch / 3 * 6 + ch % 3;
}

static unsigned channel_of(unsigned slot)
{
    return slot / 6 * 3 + slot % 6 % 3;
}

/* A right shift rounding down, of a negative number too. */
static int32_t shift_down(int32_t v, unsigned n)
{
    return v >= 0 ? v >> n : -((-v - 1) >> n) - 1;
}

void cok_opl2_init(cok_opl2 *chip)
{
    memset(chip, 0, sizeof *chip);
    for (int i = 0; i < COK_OPL2_OPERATORS; ++i) {
        chip->op[i].eg = EG_SILENT;
        chip->op[i].env = 511;
        chip->op[i].state = COK_OPL2_RELEASE;
    }
    chip->noise = 1;
}

uint32_t cok_opl2_increment(unsigned fnum, unsigned block, unsigned mult)
{
    return ((uint32_t)(fnum & 0x7ff) << (block & 7)) * mult2[mult & 15] >> 2;
}

/* The operator's output at a level in eighths of 0.1875 dB (the log-sine
 * ROM's units: 256 an octave of amplitude). */
static int32_t wave_at(unsigned phase, unsigned wave, unsigned level8)
{
    phase &= 0x3ff;
    bool negative = (phase & 0x200) != 0;
    switch (wave & 3) {
    case 1: /* half a sine: the negative half silent */
        if (negative) return 0;
        break;
    case 2: /* the sine's absolute value */
        negative = false;
        break;
    case 3: /* the rising quarters alone, both positive */
        if (phase & 0x100) return 0;
        negative = false;
        break;
    default:
        break;
    }
    unsigned x = phase & 0xff;
    if (phase & 0x100) x ^= 0xff; /* the falling quarter, read backwards */
    unsigned level = logsin_rom[x] + (level8 > LEVEL_SILENT ? LEVEL_SILENT : level8);
    unsigned exponent = level >> 8;
    if (exponent >= 13) return 0;
    /* The exponent ROM read backwards gives 2^(-fraction), the hidden bit
     * 0x400 above it; the integer part shifts it down. */
    int32_t v = (int32_t)(((exp_rom[(level & 0xff) ^ 0xff] | 0x400u) << 1) >> exponent);
    return negative ? -v : v;
}

int32_t cok_opl2_wave(unsigned phase, unsigned wave, unsigned attenuation)
{
    return wave_at(phase, wave, (attenuation > 511 ? 511 : attenuation) << 3);
}

unsigned cok_opl2_ksl(unsigned fnum, unsigned block, unsigned ksl)
{
    /* Table 3-4: KSL 1 3 dB an octave, 2 1.5 dB, 3 6 dB; the table's
     * thousandths of a dB at 3 dB, times 2, 1 or 4 / 375, are 0.1875 dB
     * steps (every entry a multiple of 0.375 dB, so exact). */
    static const uint8_t times[4] = {0, 2, 1, 4};
    return ksl_table[block & 7][(fnum >> 6) & 15] * times[ksl & 3] / 375u;
}

uint32_t cok_opl2_eg_decay(unsigned rate)
{
    if (rate < 4) return 0;
    if (rate > 63) rate = 63;
    /* 512 steps in t / 2 samples: 2^32 / t of 2^-22 steps a sample. */
    uint32_t t = decay_time[(rate >> 2) - 1][rate & 3];
    return (uint32_t)(((1ull << 32) + t / 2) / t);
}

uint32_t cok_opl2_eg_attack(unsigned rate)
{
    if (rate >= 60) return 0; /* at once */
    return rate < 4 ? 0xffffffffu : attack_factor[rate - 4];
}

unsigned cok_opl2_tremolo(uint32_t samples, bool deep)
{
    /* Ours: a triangle, linear in dB, from 0 up to the depth and back. */
    uint32_t phase = samples * (uint32_t)TREMOLO_STEP;
    uint32_t rise = (phase & 0x80000000u) ? ~phase : phase; /* 0 to 2^31 - 1 and back */
    return (unsigned)((uint64_t)rise * (deep ? TREMOLO_DEEP : TREMOLO_SHALLOW) >> 31);
}

int32_t cok_opl2_vibrato(uint32_t samples, bool deep)
{
    /* Ours: a triangle in cents, 0 up to the depth, down to minus it and
     * back, applied to the phase's increment. */
    int32_t q = (int32_t)((samples * (uint32_t)VIBRATO_STEP) >> 16);
    int32_t t = q < 0x4000 ? q : q < 0xc000 ? 0x8000 - q : q - 0x10000; /* -0x4000 to 0x4000 */
    return t * (deep ? VIBRATO_DEEP : VIBRATO_SHALLOW) / 0x4000;
}

/* Key-on from a source (bit): from off, the phase restarts and the
 * envelope attacks from where it is; to off, it releases. */
static void key(cok_opl2_operator *op, uint8_t bit, bool on)
{
    uint8_t was = op->key;
    op->key = on ? (uint8_t)(op->key | bit) : (uint8_t)(op->key & ~bit);
    if (was == 0 && op->key != 0) {
        op->phase = 0;
        op->state = COK_OPL2_ATTACK;
    } else if (was != 0 && op->key == 0) {
        op->state = COK_OPL2_RELEASE;
    }
}

static void rhythm_keys(cok_opl2 *chip)
{
    /* 0xBD's bits: 4 BD (channel 6, both operators), 3 SD (operator 16), 2
     * TOM (14), 1 TC (17), 0 HH (13). */
    bool on = chip->rhythm;
    key(&chip->op[12], 2, on && (chip->drums & 0x10));
    key(&chip->op[15], 2, on && (chip->drums & 0x10));
    key(&chip->op[16], 2, on && (chip->drums & 0x08));
    key(&chip->op[14], 2, on && (chip->drums & 0x04));
    key(&chip->op[17], 2, on && (chip->drums & 0x02));
    key(&chip->op[13], 2, on && (chip->drums & 0x01));
}

void cok_opl2_write_address(cok_opl2 *chip, uint8_t value)
{
    chip->address = value;
}

void cok_opl2_write_data(cok_opl2 *chip, uint8_t value)
{
    cok_opl2_write(chip, chip->address, value);
}

void cok_opl2_write(cok_opl2 *chip, uint8_t reg, uint8_t value)
{
    int slot = slot_of(reg & 0x1f);
    cok_opl2_operator *op = slot >= 0 ? &chip->op[slot] : NULL;
    unsigned c = reg & 0x0f;
    switch (reg & 0xe0) {
    case 0x20:
        if (op == NULL) return;
        op->am = (value & 0x80) != 0;
        op->vib = (value & 0x40) != 0;
        op->sustained = (value & 0x20) != 0;
        op->ksr = (value & 0x10) != 0;
        op->mult = value & 0x0f;
        return;
    case 0x40:
        if (op == NULL) return;
        op->ksl = value >> 6;
        op->tl = value & 0x3f;
        return;
    case 0x60:
        if (op == NULL) return;
        op->ar = value >> 4;
        op->dr = value & 0x0f;
        return;
    case 0x80:
        if (op == NULL) return;
        op->sl = value >> 4;
        op->rr = value & 0x0f;
        return;
    case 0xe0:
        if (op == NULL) return;
        op->wave = value & 3;
        return;
    case 0xa0:
        if (reg >= 0xb0 && reg != 0xbd && c < 9) {
            cok_opl2_channel *ch = &chip->ch[c];
            ch->fnum = (uint16_t)((ch->fnum & 0xff) | (value & 3) << 8);
            ch->block = (value >> 2) & 7;
            ch->key = (value & 0x20) != 0;
            unsigned m = modulator_of(c);
            key(&chip->op[m], 1, ch->key);
            key(&chip->op[m + 3], 1, ch->key);
        } else if (reg < 0xb0 && c < 9) {
            chip->ch[c].fnum = (uint16_t)((chip->ch[c].fnum & 0x300) | value);
        } else if (reg == 0xbd) {
            chip->am_deep = (value & 0x80) != 0;
            chip->vib_deep = (value & 0x40) != 0;
            chip->rhythm = (value & 0x20) != 0;
            chip->drums = value & 0x1f;
            rhythm_keys(chip);
        }
        return;
    case 0xc0:
        if (reg < 0xc9) {
            chip->ch[c].feedback = (value >> 1) & 7;
            chip->ch[c].additive = (value & 1) != 0;
        }
        return;
    default:
        break;
    }
    switch (reg) {
    case 0x01:
        chip->test = value;
        break;
    case 0x02:
    case 0x03:
        chip->timer[reg - 2] = value;
        break;
    case 0x04:
        if (value & 0x80) {
            chip->flag[0] = chip->flag[1] = false; /* IRQ reset: the other bits ignored */
            break;
        }
        for (int t = 0; t < 2; ++t) {
            bool start = (value >> t) & 1;
            if (start && !chip->running[t]) chip->count[t] = chip->timer[t];
            chip->running[t] = start;
            chip->masked[t] = (value & (t == 0 ? 0x40 : 0x20)) != 0;
            if (chip->masked[t]) chip->flag[t] = false;
        }
        break;
    case 0x08:
        chip->csm = (value & 0x80) != 0;
        chip->nts = (value & 0x40) != 0;
        break;
    default:
        break; /* 00, 05-07, 09-1F: nothing there */
    }
}

uint8_t cok_opl2_status(const cok_opl2 *chip)
{
    uint8_t status = 0x06;
    if (chip->flag[0]) status |= 0x40;
    if (chip->flag[1]) status |= 0x20;
    if (status & 0x60) status |= 0x80;
    return status;
}

/* Timer 1 counts every 4 samples (80 us at 3.6 MHz), timer 2 every 16
 * (320 us); at 256 the flag is set (unless masked) and the preset loaded
 * again. In CSM mode timer 1's overflow keys every operator on; the key
 * is let go a sample later (how long the chip holds it the manual does
 * not say: one sample is ours), so each operator gets one step of its
 * attack, then releases from where that left it. */
static void timers(cok_opl2 *chip)
{
    if (chip->csm_key) {
        for (int i = 0; i < COK_OPL2_OPERATORS; ++i) key(&chip->op[i], 4, false);
        chip->csm_key = false;
    }
    for (int t = 0; t < 2; ++t) {
        if (!chip->running[t] || (chip->samples & (t == 0 ? 3u : 15u)) != 0) continue;
        if (++chip->count[t] != 0) continue;
        chip->count[t] = chip->timer[t];
        if (!chip->masked[t]) chip->flag[t] = true;
        if (t == 0 && chip->csm) {
            for (int i = 0; i < COK_OPL2_OPERATORS; ++i) key(&chip->op[i], 4, true);
            chip->csm_key = true;
        }
    }
}

static unsigned rate_of(const cok_opl2 *chip, const cok_opl2_operator *op,
                        const cok_opl2_channel *ch, unsigned r)
{
    if (r == 0) return 0;
    /* Key scaling of the rate (Table 3-2): the block and the F-number's
     * top bit (NTS 0: its second bit), all of it with KSR, a quarter
     * without. */
    unsigned split = chip->nts ? (ch->fnum >> 8) & 1 : (ch->fnum >> 9) & 1;
    unsigned scale = (ch->block << 1 | split) >> (op->ksr ? 0 : 2);
    unsigned rate = r * 4 + scale;
    return rate > 63 ? 63 : rate;
}

/* The envelope a sample on (3-3, p. 21, and Fig. 3-2): the attack
 * exponential to 0 dB, then linear in dB: the decay to the sustain level
 * (3 dB steps, 15 as 93 dB: 3-1-7), held there with EG-TYP 1 or going on
 * at the release rate with 0, and the release from key-off; a rate of 0
 * changes nothing (Table 3-6's note). Each linear rate goes up by its 96
 * dB in Table 3-6's time, an even amount every sample. */
static void envelope(const cok_opl2 *chip, cok_opl2_operator *op, const cok_opl2_channel *ch)
{
    uint32_t eg = op->eg;
    uint32_t sustain = (op->sl == 15 ? 31u << 4 : (uint32_t)op->sl << 4) << EG_FRAC;
    switch (op->state) {
    case COK_OPL2_ATTACK: {
        unsigned rate = rate_of(chip, op, ch, op->ar);
        if (rate != 0) {
            uint64_t x = (uint64_t)(eg + EG_OFFSET) * cok_opl2_eg_attack(rate) >> 32;
            eg = x > EG_OFFSET ? (uint32_t)(x - EG_OFFSET) : 0;
        }
        if (eg == 0) op->state = COK_OPL2_DECAY;
        break;
    }
    case COK_OPL2_DECAY:
        if (eg >= sustain) op->state = COK_OPL2_SUSTAIN;
        else eg += cok_opl2_eg_decay(rate_of(chip, op, ch, op->dr));
        break;
    case COK_OPL2_SUSTAIN:
        if (!op->sustained) eg += cok_opl2_eg_decay(rate_of(chip, op, ch, op->rr));
        break;
    case COK_OPL2_RELEASE:
        eg += cok_opl2_eg_decay(rate_of(chip, op, ch, op->rr));
        break;
    }
    if (eg > EG_SILENT) eg = EG_SILENT;
    op->eg = eg;
    op->env = (uint16_t)(eg >> EG_FRAC > 511 ? 511 : eg >> EG_FRAC);
}

/* The operator's attenuation now, in eighths of 0.1875 dB: the manual
 * (3-3) adds the total level (0.75 dB steps, 3-1-5), KSL and AM to the
 * envelope's level, 0.1875 dB steps over 96 dB; the tremolo in finer
 * steps (its 4.8 and 1 dB are not whole steps), and at 96 dB silent. */
static unsigned attenuation(const cok_opl2 *chip, const cok_opl2_operator *op,
                            const cok_opl2_channel *ch)
{
    unsigned a = (op->env + ((unsigned)op->tl << 2) + cok_opl2_ksl(ch->fnum, ch->block, op->ksl)) << 3;
    if (op->am) a += cok_opl2_tremolo(chip->samples, chip->am_deep);
    return a > LEVEL_SILENT ? LEVEL_SILENT : a;
}

static int32_t output(const cok_opl2 *chip, cok_opl2_operator *op, const cok_opl2_channel *ch,
                      int32_t modulation)
{
    unsigned wave = (chip->test & 0x20) != 0 ? op->wave : 0;
    return wave_at((unsigned)((int32_t)(op->phase >> 9) + modulation) & 0x3ff, wave,
                   attenuation(chip, op, ch));
}

/* A channel's two operators: the modulator, then the carrier,
 * phase-modulated by it (CNT 0) or added to it (1; 3-1-9).
 *
 * Feedback, from the manual's formula (3), F = A sin(wt + B F), and Table
 * 3-8's factor B: 0, pi / 16, pi / 8 ... 4 pi for FB 0-7. With a full-level
 * output (4,084, about 2^12) as F = 1 and 1,024 phase steps to a cycle, B
 * F is F's output times 2^(FB + 4) / 2^12: shifted right by 8 - FB. F is
 * implicit in (3); a sampled loop has to take it from outputs already
 * made. Ours: a running average, each sample half the last average and
 * half the new output. The last output alone would make the loop ring at
 * half the sample rate wherever B times the sine's slope passes 1 (FB 5
 * and up); averaged so, it holds up to 3, at a delay of about a sample. */
static int32_t voice(cok_opl2 *chip, unsigned c)
{
    cok_opl2_channel *ch = &chip->ch[c];
    cok_opl2_operator *mod = &chip->op[modulator_of(c)], *car = mod + 3;
    int32_t feedback = ch->feedback != 0 ? shift_down(mod->average, 8u - ch->feedback) : 0;
    int32_t m = output(chip, mod, ch, feedback);
    mod->average = shift_down(mod->average + m, 1);
    int32_t k = output(chip, car, ch, ch->additive ? 0 : m);
    return ch->additive ? m + k : k;
}

/* Rhythm mode, simplified (the shipped driver never sets it): the bass
 * drum is channel 6's voice, the tom-tom operator 14's sine, and the
 * hi-hat, snare and cymbal operator 13's, 16's and 17's envelopes on
 * full-level square waves made from operators 13's and 17's phases and the
 * noise; each doubled, as the chip doubles the rhythm voices. */
static int32_t rhythm(cok_opl2 *chip)
{
    cok_opl2_channel *c7 = &chip->ch[7], *c8 = &chip->ch[8];
    int32_t sum = voice(chip, 6);
    sum += output(chip, &chip->op[14], c8, 0);
    unsigned p13 = chip->op[13].phase >> 9, p17 = chip->op[17].phase >> 9;
    unsigned noise = chip->noise & 1;
    unsigned ring = ((p13 >> 3) ^ (p17 >> 5)) & 1;
    sum += wave_at((ring ^ noise) ? 0x300 : 0x100, 0, attenuation(chip, &chip->op[13], c7));
    sum += wave_at(((p13 >> 8) ^ noise) & 1 ? 0x300 : 0x100, 0, attenuation(chip, &chip->op[16], c7));
    sum += wave_at(ring ? 0x300 : 0x100, 0, attenuation(chip, &chip->op[17], c8));
    return 2 * sum;
}

int32_t cok_opl2_sample(cok_opl2 *chip)
{
    int32_t sum = 0;
    unsigned melodic = chip->rhythm ? 6 : 9;
    for (unsigned c = 0; c < melodic; ++c) sum += voice(chip, c);
    if (chip->rhythm) sum += rhythm(chip);
    /* Then every operator's phase and envelope moves on a sample; with
     * VIB, the phase's increment by the vibrato's factor (3-2). */
    int32_t vibrato = cok_opl2_vibrato(chip->samples, chip->vib_deep);
    for (unsigned i = 0; i < COK_OPL2_OPERATORS; ++i) {
        cok_opl2_operator *op = &chip->op[i];
        cok_opl2_channel *ch = &chip->ch[channel_of(i)];
        int32_t step = (int32_t)cok_opl2_increment(ch->fnum, ch->block, op->mult);
        if (op->vib) step += shift_down(step * vibrato, 16);
        op->phase = (op->phase + (uint32_t)step) & 0x7ffff;
        envelope(chip, op, ch);
    }
    unsigned bit = ((chip->noise >> 22) ^ (chip->noise >> 17)) & 1;
    chip->noise = ((chip->noise << 1) | bit) & 0x7fffff;
    timers(chip);
    ++chip->samples;
    return cok_opl2_dac(sum);
}

int32_t cok_opl2_dac(int32_t sum)
{
    /* The YM3014B takes a 10-bit mantissa and a 3-bit exponent: the sum
     * clipped to 16 bits loses low bits as it grows. */
    if (sum > 32767) sum = 32767;
    if (sum < -32768) sum = -32768;
    unsigned e = 0;
    while (e < 6 && (sum >= (512 << e) || sum < -(512 << e))) ++e;
    return shift_down(sum, e) * (1 << e);
}
