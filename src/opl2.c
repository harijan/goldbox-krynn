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

/* MULT 0-15 as twice the multiple (the datasheet's Table 3-1: 1/2, 1-10,
 * 10, 12, 12, 15, 15). */
static const uint8_t mult2[16] = {1, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 20, 24, 24, 30, 30};

/* KSL's attenuation for the F-number's top four bits, in 0.375 dB steps
 * at 3 dB an octave: the datasheet's Table 3-4, its row of block 7 (0, 9,
 * 12, 13.875 ... 21 dB) and an octave more (8) but for 0; each block below
 * takes an octave (8) off, down to 0. */
static const uint8_t ksl_rom[16] = {0, 32, 40, 45, 48, 51, 53, 55, 56, 58, 59, 60, 61, 62, 63, 64};

/* Which eighths of its period an envelope rate's step falls on, for the
 * rate's two low bits: 4, 5, 6 or 7 of 8 (rates 4-51). */
static const uint8_t eg_pattern[4][8] = {
    {0, 1, 0, 1, 0, 1, 0, 1},
    {0, 1, 0, 1, 1, 1, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1},
    {0, 1, 1, 1, 1, 1, 1, 1},
};

/* Rates 52-59 step every sample, by 1 (or 2 at 56-59), doubled on 0, 2, 4
 * or 6 of the 8 for the low bits: the datasheet's attack times there
 * (Table 3-6: 0.70, 0.56, 0.46, 0.42 ms at 13.0-13.3) need the steps
 * spread so, not bunched as the slower rates' are. */
static const uint8_t eg_extra[4][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 0, 0, 0, 1, 0, 0},
    {0, 1, 0, 1, 0, 1, 0, 1},
    {0, 1, 1, 1, 0, 1, 1, 1},
};

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
        chip->op[i].env = 511;
        chip->op[i].state = COK_OPL2_RELEASE;
    }
    chip->noise = 1;
}

uint32_t cok_opl2_increment(unsigned fnum, unsigned block, unsigned mult)
{
    return ((uint32_t)(fnum & 0x7ff) << (block & 7)) * mult2[mult & 15] >> 2;
}

int32_t cok_opl2_wave(unsigned phase, unsigned wave, unsigned attenuation)
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
    unsigned level = logsin_rom[x] + ((attenuation > 511 ? 511 : attenuation) << 3);
    unsigned exponent = level >> 8;
    if (exponent >= 13) return 0;
    /* The exponent ROM read backwards gives 2^(-fraction), the hidden bit
     * 0x400 above it; the integer part shifts it down. */
    int32_t v = (int32_t)(((exp_rom[(level & 0xff) ^ 0xff] | 0x400u) << 1) >> exponent);
    return negative ? -v : v;
}

unsigned cok_opl2_ksl(unsigned fnum, unsigned block, unsigned ksl)
{
    static const uint8_t shift[4] = {0, 1, 2, 0}; /* KSL 1: 3 dB an octave, 2: 1.5, 3: 6 */
    if ((ksl & 3) == 0) return 0;
    int k = (ksl_rom[(fnum >> 6) & 15] << 2) - ((8 - (int)(block & 7)) << 5);
    if (k <= 0) return 0;
    return (unsigned)k >> shift[ksl & 3];
}

unsigned cok_opl2_eg_step(unsigned rate, uint32_t counter)
{
    if (rate == 0) return 0;
    unsigned high = rate >> 2, low = rate & 3;
    if (high >= 15) return 4;
    if (high >= 13) {
        unsigned base = 1u << (high - 13);
        return eg_extra[low][counter & 7] ? 2 * base : base;
    }
    if (high == 12) return eg_pattern[low][counter & 7];
    unsigned shift = 12 - high;
    if ((counter & ((1u << shift) - 1)) != 0) return 0;
    return eg_pattern[low][(counter >> shift) & 7];
}

unsigned cok_opl2_tremolo(uint32_t samples, bool deep)
{
    /* A triangle of 210 places, one every 64 samples (3.7 Hz), 0-26 (4.875
     * dB); a quarter of it shallow (1.125 dB). */
    unsigned place = (samples >> 6) % 210;
    unsigned level = (place < 105 ? place : 209 - place) >> 2;
    return deep ? level : level >> 2;
}

int cok_opl2_vibrato(uint32_t samples, unsigned fnum, bool deep)
{
    /* Eight places, one every 1,024 samples (6.1 Hz): 0, a half, all, a
     * half, 0 and the same below, of the F-number's top three bits (an
     * eighth of a semitone's 6%, 14 cents) or half that (7 cents). */
    static const uint8_t size[8] = {0, 1, 2, 1, 0, 1, 2, 1};
    unsigned place = (samples >> 10) & 7;
    int top = (int)((fnum >> 7) & 7);
    int offset = size[place] == 2 ? top : size[place] == 1 ? top >> 1 : 0;
    if (!deep) offset >>= 1;
    return place >= 5 ? -offset : offset;
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
 * again; timer 1's overflow in CSM mode keys every operator on and off. */
static void timers(cok_opl2 *chip)
{
    for (int t = 0; t < 2; ++t) {
        if (!chip->running[t] || (chip->samples & (t == 0 ? 3u : 15u)) != 0) continue;
        if (++chip->count[t] != 0) continue;
        chip->count[t] = chip->timer[t];
        if (!chip->masked[t]) chip->flag[t] = true;
        if (t == 0 && chip->csm)
            for (int i = 0; i < COK_OPL2_OPERATORS; ++i) {
                key(&chip->op[i], 4, true);
                key(&chip->op[i], 4, false);
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

static void envelope(cok_opl2 *chip, cok_opl2_operator *op, const cok_opl2_channel *ch)
{
    uint32_t counter = chip->samples;
    int32_t env = op->env;
    unsigned sustain = op->sl == 15 ? 31u << 4 : (unsigned)op->sl << 4; /* 3 dB steps, 15 is 93 dB */
    switch (op->state) {
    case COK_OPL2_ATTACK: {
        unsigned rate = rate_of(chip, op, ch, op->ar);
        if (rate >= 60) {
            env = 0;
        } else {
            unsigned step = cok_opl2_eg_step(rate, counter);
            /* Exponential: each step an eighth of the way (rounded down,
             * so at least one) times the step's size. */
            if (step != 0) env += shift_down(~env * (int32_t)step, 3);
        }
        if (env <= 0) {
            env = 0;
            op->state = COK_OPL2_DECAY;
        }
        break;
    }
    case COK_OPL2_DECAY:
        if ((unsigned)env >= sustain) {
            op->state = COK_OPL2_SUSTAIN;
            if (!op->sustained) env += (int32_t)cok_opl2_eg_step(rate_of(chip, op, ch, op->rr), counter);
        } else {
            env += (int32_t)cok_opl2_eg_step(rate_of(chip, op, ch, op->dr), counter);
        }
        break;
    case COK_OPL2_SUSTAIN:
        /* EG-TYP 1 holds the sustain level; 0 goes on at the release rate. */
        if (!op->sustained) env += (int32_t)cok_opl2_eg_step(rate_of(chip, op, ch, op->rr), counter);
        break;
    case COK_OPL2_RELEASE:
        env += (int32_t)cok_opl2_eg_step(rate_of(chip, op, ch, op->rr), counter);
        break;
    }
    op->env = (uint16_t)(env > 511 ? 511 : env);
}

/* The operator's attenuation now: its envelope, total level (0.75 dB
 * steps), KSL and tremolo. */
static unsigned attenuation(const cok_opl2 *chip, const cok_opl2_operator *op,
                            const cok_opl2_channel *ch)
{
    unsigned a = op->env + ((unsigned)op->tl << 2) + cok_opl2_ksl(ch->fnum, ch->block, op->ksl);
    if (op->am) a += cok_opl2_tremolo(chip->samples, chip->am_deep);
    return a > 511 ? 511 : a;
}

static int32_t output(const cok_opl2 *chip, cok_opl2_operator *op, const cok_opl2_channel *ch,
                      int32_t modulation)
{
    unsigned wave = (chip->test & 0x20) != 0 ? op->wave : 0;
    return cok_opl2_wave((unsigned)((int32_t)(op->phase >> 9) + modulation) & 0x3ff, wave,
                         attenuation(chip, op, ch));
}

/* A channel's two operators: the modulator (with its feedback, half the
 * sum of its last two outputs shifted by 8 - FB: pi/16 to 4 pi), then the
 * carrier, phase-modulated by it or added to it. */
static int32_t voice(cok_opl2 *chip, unsigned c)
{
    cok_opl2_channel *ch = &chip->ch[c];
    cok_opl2_operator *mod = &chip->op[modulator_of(c)], *car = mod + 3;
    int32_t feedback = 0;
    if (ch->feedback != 0) feedback = shift_down(mod->out[0] + mod->out[1], 9 - ch->feedback);
    int32_t m = output(chip, mod, ch, feedback);
    mod->out[1] = mod->out[0];
    mod->out[0] = (int16_t)m;
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
    sum += cok_opl2_wave((ring ^ noise) ? 0x300 : 0x100, 0, attenuation(chip, &chip->op[13], c7));
    sum += cok_opl2_wave(((p13 >> 8) ^ noise) & 1 ? 0x300 : 0x100, 0,
                         attenuation(chip, &chip->op[16], c7));
    sum += cok_opl2_wave(ring ? 0x300 : 0x100, 0, attenuation(chip, &chip->op[17], c8));
    return 2 * sum;
}

int32_t cok_opl2_sample(cok_opl2 *chip)
{
    int32_t sum = 0;
    unsigned melodic = chip->rhythm ? 6 : 9;
    for (unsigned c = 0; c < melodic; ++c) sum += voice(chip, c);
    if (chip->rhythm) sum += rhythm(chip);
    /* Then every operator's phase and envelope moves on a sample. */
    for (unsigned i = 0; i < COK_OPL2_OPERATORS; ++i) {
        cok_opl2_operator *op = &chip->op[i];
        cok_opl2_channel *ch = &chip->ch[channel_of(i)];
        int fnum = ch->fnum;
        if (op->vib) fnum += cok_opl2_vibrato(chip->samples, ch->fnum, chip->vib_deep);
        op->phase = (op->phase + cok_opl2_increment((unsigned)fnum, ch->block, op->mult)) & 0x7ffff;
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
