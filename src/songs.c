#include "songs.h"

#include "sound.h"

#include <string.h>

/* The driver's data segment and the song's buffer. */
#define D (s->songs.ds)
#define B (s->songs.buffer)

/* Its variables (DS offsets). */
enum {
    PLAY_MODES = 0x000,   /* 03b0: the adlib's echo set by function 1's argument (00 02 03) */
    GUARD = 0x02c,        /* 09bd: 2 from the record, a tick inside a tick skipped */
    RHYTHM_OFF = 0x0bc,   /* 10d4: 0 keeps the chip's rhythm mode on (0xBD's bit 5) */
    CHANNEL = 0x0bd,      /* + voice: the AdLib channel the voice plays on */
    INSTRUMENT = 0x0c9,   /* + voice: its instrument */
    DETUNE = 0x0d5,       /* + voice: the echo voices' detune (03b0's 2295 clears 0-2) */
    OPERATORS = 0x0da,    /* + 2 slot: an operator's register offset (the carrier's 3 on) */
    DRUMS = 0x0db,        /* 0xBD's drum bits (the table's unused first entry's high byte) */
    SLOTS = 0x100,        /* + channel: its slot, with RHYTHM_OFF set */
    SLOTS_RHYTHM = 0x109, /* + channel: its slot, rhythm mode on */
    FNUMS = 0x114,        /* + 2 (note % 12): the F-number */
    FNUM_STEPS = 0x12c,   /* + 2 (note % 12): the F-number per detune step */
    DEVICE = 0x22e,       /* 2723: 0 speaker, 1 Tandy, 2 AdLib, 3 CMS, 4 Innovation, 5 Covox */
    PIT_NOTES = 0x230,    /* + 2 (note % 12): PIT counts, octave 5 */
    TANDY_NOTES = 0x248,  /* + 2 (note % 12): the Tandy's, octave 5 */
    SPEAKER = 0x260,      /* 270e: one voice, the song's first track (the speaker's) */
    CMS_BYTES = 0x262,    /* 27ba, 298b: the probes' test bytes */
    COVOX_PORTS = 0x266,  /* 2a0b: the ports a Covox is looked for at */
    ECHO = 0x294,         /* + 2 track: an echo voice's delay (none: 0) */
    VOICES = 0x29e,       /* six voices of 14 bytes */
    OFFSETS = 0x2f2,      /* + 2 song: its offset past the table (big-endian in the file) */
    LOOPING = 0x320,      /* the song 073c loops, -1 none */
    HEADER = 0x322,       /* the songs' header, 10 bytes; its byte 8 the last song */
    SONGS = 0x32c,        /* far pointer: past the offsets */
    NOTES_ON = 0x330,     /* 0600: 0 plays no notes (1 from 03f4) */
    CHANNELS_USED = 0x3b8,/* the AdLib channels taken, a bit each */
    OPL_DATA = 0x3ba,     /* 0fa0: the AdLib's ports */
    MODULATOR = 0x3bc,    /* 10fe: the operators' offsets being loaded */
    CARRIER = 0x3be,
    OPL_PORT = 0x3c0,
    INSTRUMENTS = 0x358,  /* 8 AdLib instruments of 12 bytes, from the song's parts 0-7 */
    PORT61 = 0x4de,       /* 2131: port 0x61 as the Tandy found it */
    ENABLED = 0x4df,      /* 2131: 1, the device's notes let through */
};

/* A voice (VOICES + 14 v): the time its next event is due (a dword, in
 * interrupts), the far pointer to it, the song's divisor, the remainder of
 * its divisions, its volume and its state: 0 a note that the same note
 * follows (cut a tick early), 1 a note, 2 a note tied to the next, 3
 * silent. */
enum { V_TIME = 0, V_PTR = 4, V_DIV = 8, V_REM = 10, V_VOLUME = 12, V_STATE = 13, V_SIZE = 14 };

static inline uint16_t rw(const uint8_t *seg, uint16_t at)
{
    return (uint16_t)(seg[at] | seg[(uint16_t)(at + 1)] << 8);
}

static inline void ww(uint8_t *seg, uint16_t at, unsigned value)
{
    seg[at] = (uint8_t)value;
    seg[(uint16_t)(at + 1)] = (uint8_t)(value >> 8);
}

static inline uint32_t rd(const uint8_t *seg, uint16_t at)
{
    return rw(seg, at) | (uint32_t)rw(seg, (uint16_t)(at + 2)) << 16;
}

static inline void wd(uint8_t *seg, uint16_t at, uint32_t value)
{
    ww(seg, at, value & 0xffff);
    ww(seg, (uint16_t)(at + 2), value >> 16);
}

/* 2be8: a word of the file, big-endian. */
static inline uint16_t be(const uint8_t *seg, uint16_t at)
{
    return (uint16_t)(seg[at] << 8 | seg[(uint16_t)(at + 1)]);
}

static inline int16_t s16(uint16_t v)
{
    return (int16_t)(v >= 0x8000 ? (int32_t)v - 0x10000 : (int32_t)v);
}

static inline int16_t s8(uint8_t v)
{
    return (int16_t)(v >= 0x80 ? (int)v - 0x100 : (int)v);
}

/* SAR on a word. */
static inline uint16_t sar(uint16_t v, unsigned n)
{
    return (uint16_t)(s16(v) >= 0 ? v >> n : ~((uint16_t)~v >> n));
}

/* SHL and SHR as the 8086 does them: the count is CL whole. */
static inline uint16_t shl(uint16_t v, unsigned n)
{
    return (uint16_t)(n >= 16 ? 0u : (unsigned)v << n);
}

static inline uint16_t shr(uint16_t v, unsigned n)
{
    return (uint16_t)(n >= 16 ? 0u : (unsigned)v >> n);
}

static inline uint16_t voice(unsigned v)
{
    return (uint16_t)(VOICES + V_SIZE * v);
}

static void out(cok_sound *s, uint16_t port, uint8_t value)
{
    cok_sound_out(s, port, value);
}

static uint8_t in(cok_sound *s, uint16_t port)
{
    return cok_sound_in(s, port);
}

static uint16_t device(const cok_sound *s)
{
    return rw(s->songs.ds, DEVICE);
}

/* The AdLib's part. */

/* 0fb3 through 2c5d: the register's address to the port, two reads of it,
 * the data to the next port, twelve reads (the delays; their values are
 * not used). */
static void opl(cok_sound *s, unsigned reg, unsigned value)
{
    uint16_t port = rw(D, OPL_PORT);
    out(s, port, (uint8_t)reg);
    out(s, (uint16_t)(port + 1), (uint8_t)value);
}

/* 10df: the channel's slot. */
static int16_t slot(cok_sound *s, unsigned ch)
{
    return s8(D[RHYTHM_OFF] != 0 ? D[(uint16_t)(SLOTS + ch)] : D[(uint16_t)(SLOTS_RHYTHM + ch)]);
}

static uint16_t operator_of(cok_sound *s, unsigned ch, unsigned which)
{
    return rw(D, (uint16_t)((int)(OPERATORS + 6 * which) + 2 * slot(s, ch)));
}

/* 0ff1: a channel for instrument i: its own (byte 1, a drum's) or the
 * first free of 0-6, taken (the "already assigned" and "all outputs
 * taken" messages print nothing: 022e is empty). */
static unsigned allocate(cok_sound *s, unsigned i)
{
    uint8_t own = D[(uint16_t)(INSTRUMENTS + 12 * i + 1)];
    uint16_t used = rw(D, CHANNELS_USED);
    if (own > 0) {
        ww(D, CHANNELS_USED, used | shl(1, own));
        return own;
    }
    unsigned n = 0;
    while ((used & shl(1, n)) != 0 && n <= 6) ++n;
    ww(D, CHANNELS_USED, used | shl(1, n));
    return n;
}

/* 10fe: instrument i's registers to channel ch: both operators, or the
 * modulator alone for a drum's channel past 6 in rhythm mode. */
static void load_instrument(cok_sound *s, unsigned ch, unsigned i)
{
    uint16_t at = (uint16_t)(INSTRUMENTS + 12 * i);
    if (D[RHYTHM_OFF] != 0 || s16((uint16_t)ch) <= 6) {
        ww(D, MODULATOR, operator_of(s, ch, 0));
        ww(D, CARRIER, operator_of(s, ch, 1));
    } else {
        if (D[(uint16_t)(at + 1)] != ch) ch = D[(uint16_t)(at + 1)];
        ww(D, MODULATOR, operator_of(s, ch, 0));
        ww(D, CARRIER, 0xffff);
    }
    for (unsigned k = 0; k <= 1; ++k) {
        if (k == 1 && s16(rw(D, CARRIER)) < 0) break;
        uint16_t op = rw(D, (uint16_t)(MODULATOR + 2 * k));
        opl(s, 0xbd, D[RHYTHM_OFF] != 0 ? 0 : 0x20);
        opl(s, 0x08, 0x40);
        opl(s, op + 0x40u, D[(uint16_t)(at + 4 + k)]);
        if (k == 0) opl(s, ch + 0xc0u, D[(uint16_t)(at + 10)]);
        opl(s, op + 0x60u, D[(uint16_t)(at + 6 + k)]);
        opl(s, op + 0x80u, D[(uint16_t)(at + 8 + k)]);
        opl(s, op + 0x20u, D[(uint16_t)(at + 2 + k)]);
    }
}

/* 15f2: voice v takes instrument i: its channel freed (10c1), one taken
 * for i and loaded. */
static void adlib_instrument(cok_sound *s, unsigned v, unsigned i)
{
    unsigned old = (unsigned)s8(D[(uint16_t)(CHANNEL + v)]);
    ww(D, CHANNELS_USED, rw(D, CHANNELS_USED) & (uint16_t)~shl(1, old & 0xff));
    unsigned ch = allocate(s, i);
    load_instrument(s, ch, i);
    D[(uint16_t)(CHANNEL + v)] = (uint8_t)ch;
    D[(uint16_t)(INSTRUMENT + v)] = (uint8_t)i;
}

/* 1365: voice v's note (volume 0-0x7f, below 0 off), its F-number moved
 * by detune steps. A melodic channel is keyed off at A0 0x4a and B0 0x0e
 * first, then its carrier's level and key-on; a drum's channel (past 6
 * in rhythm mode) loses its bit of 0xBD, takes its level, the pitch of
 * channel 7 or 8 and its bit again. */
static void adlib_note(cok_sound *s, unsigned v, uint16_t note, uint16_t volume, uint16_t detune)
{
    int16_t ch = s8(D[(uint16_t)(CHANNEL + v)]);
    uint16_t at = (uint16_t)(INSTRUMENTS + 12 * s8(D[(uint16_t)(INSTRUMENT + v)]));
    bool melodic = D[RHYTHM_OFF] != 0 || ch <= 6;
    unsigned fch;
    uint8_t level = D[(uint16_t)(at + (melodic ? 5 : 4))];
    uint8_t scaled = (uint8_t)sar((uint16_t)((0x3f - (level & 0x3f)) * s16(volume)), 7);
    uint8_t tl = (uint8_t)((uint8_t)(0x3f - scaled) | (level & 0xc0));
    uint16_t op;
    uint8_t bit = (uint8_t)(((uint8_t)(10 - ch) & 0xff) >= 8 ? 0 : 1u << (uint8_t)(10 - ch));
    if (melodic) {
        fch = (unsigned)ch;
        op = operator_of(s, (unsigned)ch, 1);
        opl(s, fch + 0xa0, 0x4a);
        opl(s, fch + 0xb0, 0x0e);
    } else {
        fch = ch > 8 ? 8 : 7;
        D[DRUMS] &= (uint8_t)~bit;
        op = operator_of(s, (unsigned)ch, 0);
        opl(s, 0xbd, D[DRUMS] | 0x20u);
    }
    if (s16(volume) < 0) return;
    opl(s, op + 0x40u, tl);
    int16_t n = s16(note);
    int16_t rem = (int16_t)(n % 12), octave = (int16_t)(n / 12 - 1);
    if (octave > 7) octave = 7;
    if (octave < 0) octave = 0;
    uint16_t fnum = (uint16_t)(rw(D, (uint16_t)(FNUMS + 2 * rem)) +
                               (uint32_t)rw(D, (uint16_t)(FNUM_STEPS + 2 * rem)) * detune);
    opl(s, fch + 0xa0, fnum & 0xff);
    uint8_t b = (uint8_t)(sar(fnum & 0x300, 8) | (unsigned)(octave << 2));
    if (melodic) {
        opl(s, fch + 0xb0, b | 0x20u);
    } else {
        opl(s, fch + 0xb0, b);
        D[DRUMS] |= bit;
        opl(s, 0xbd, D[DRUMS] | 0x20u);
    }
}

/* 158a: voices 0-2 and, with a detune, their echo 3-5 at 100/128 of the
 * volume; voices 3-5 at that volume. */
static void adlib_voice(cok_sound *s, unsigned v, uint16_t note, uint16_t volume)
{
    uint16_t quiet = sar((uint16_t)(s16(volume) * 100), 7);
    if (v < 3) {
        adlib_note(s, v, note, volume, 0);
        if (D[(uint16_t)(DETUNE + v)] != 0)
            adlib_note(s, v + 3, note, quiet, (uint16_t)s8(D[(uint16_t)(DETUNE + v)]));
    } else {
        adlib_note(s, v, note, quiet, (uint16_t)s8(D[(uint16_t)(DETUNE + v)]));
    }
}

/* 12c8: every channel keyed off (B0-B8 0x0e); in rhythm mode the drums
 * too. */
static void adlib_silence(cok_sound *s)
{
    for (unsigned c = 0; c <= 8; ++c) opl(s, 0xb0 + c, 0x0e);
    if (D[RHYTHM_OFF] == 0) {
        opl(s, 0xbd, 0x20);
        D[DRUMS] = 0;
    }
}

/* 1302 (127d): rhythm mode, NOTE-SEL, the operators' waveforms 0, the
 * waveforms' enable off; no drums, no channel taken. */
static void adlib_start(cok_sound *s)
{
    opl(s, 0xbd, D[RHYTHM_OFF] != 0 ? 0 : 0x20);
    opl(s, 0x08, 0x40);
    for (unsigned k = 1; k <= 0x12; ++k) opl(s, rw(D, (uint16_t)(OPERATORS + 2 * k)) + 0xe0u, 0);
    opl(s, 0x01, 0);
    D[DRUMS] = 0;
    ww(D, CHANNELS_USED, 0);
}

/* The Tandy's and the speaker's part. */

/* 24bb: voice v's pitch and volume: the Tandy's voices 0-3 (3 the noise,
 * its control the pitch's low bits) on the Tandy, else the speaker for
 * voice 0 and voices past 3 (a pitch of 0 turns it off). */
static void voice_out(cok_sound *s, uint8_t v, uint16_t pitch, uint8_t volume)
{
    if (!D[ENABLED]) return;
    if (device(s) == 1 && (int8_t)v < 4) {
        uint8_t level = (uint8_t)(sar((uint16_t)s8(volume), 3) & 0x0f);
        uint8_t bl = (uint8_t)(v << 5);
        out(s, 0xc0, (uint8_t)(bl | (pitch & 0x0f) | 0x80));
        if ((int8_t)v < 3) out(s, 0xc0, (uint8_t)((pitch >> 4) & 0x3f));
        out(s, 0xc0, (uint8_t)((uint8_t)(0x0f - level) | bl | 0x90));
        return;
    }
    if ((int8_t)v >= 1 && (int8_t)v <= 3) return;
    uint8_t bl = in(s, 0x61) & 0xfc;
    if (pitch != 0) {
        out(s, 0x43, 0xb6);
        out(s, 0x42, (uint8_t)pitch);
        out(s, 0x42, (uint8_t)(pitch >> 8));
        out(s, 0x61, bl | 3);
    } else {
        out(s, 0x61, bl);
    }
}

/* 2569's pitch from a table of octave 5 (25f5, 268f): shifted left for
 * lower octaves (at most 4), right for higher. */
static uint16_t pitch_of(cok_sound *s, uint16_t table, uint16_t note)
{
    int16_t n = s16(note);
    uint16_t p = rw(D, (uint16_t)(table + 2 * (n % 12)));
    int16_t octave = (int16_t)(n / 12 - 5);
    if (octave < 0)
        p = shl(p, (unsigned)(-octave < 4 ? -octave : 4) & 0xff);
    else if (octave != 0)
        p = shr(p, (unsigned)octave & 0xff);
    return p;
}

/* 2569: a note (volume below 0: off) on the device. */
static void note(cok_sound *s, unsigned v, uint16_t n, uint16_t volume)
{
    if (!D[ENABLED]) return;
    switch (device(s)) {
    case 0: /* 25d3: the speaker plays voice 0 */
        if (s16((uint16_t)v) > 0) return;
        if (s16(volume) < 0) {
            voice_out(s, (uint8_t)v, 0, 0);
            return;
        }
        voice_out(s, (uint8_t)v, pitch_of(s, PIT_NOTES, n), (uint8_t)volume);
        break;
    case 1: { /* 264d */
        if (s16(volume) < 0) {
            voice_out(s, (uint8_t)v, 0, 0);
            return;
        }
        uint16_t p = pitch_of(s, s16((uint16_t)v) < 3 ? TANDY_NOTES : PIT_NOTES, n);
        while (p >= 0x400) p >>= 1;
        voice_out(s, (uint8_t)v, p, (uint8_t)volume);
        break;
    }
    case 2:
        adlib_voice(s, v, n, volume);
        break;
    default:
        break;
    }
}

/* 2460: the speaker off (port 0x61's bits 0-1), then the device silenced. */
static void silence(cok_sound *s)
{
    out(s, 0x61, in(s, 0x61) & 0xfc);
    if (device(s) == 1) {
        out(s, 0xc0, 0x9f);
        out(s, 0xc0, 0xbf);
        out(s, 0xc0, 0xdf);
        out(s, 0xc0, 0xff);
    } else if (device(s) == 2) {
        adlib_silence(s);
    }
}

/* The detection (2a61): the AdLib, a CMS, an Innovation, a Covox, the
 * Tandy, else the speaker; each probe's writes as the original makes them,
 * reading what the machine answers (0xff where it has nothing). */

/* 2738: 200 reads of the port, each 0, 2, 4 or 6. */
static bool adlib_status_ok(cok_sound *s, uint16_t port)
{
    for (int i = 0; i < 200; ++i) {
        uint8_t v = in(s, port);
        if (v != 2 && v != 4 && v != 6 && v != 0) return false;
    }
    return true;
}

/* 27ba: a CMS: four test bytes written to base + 6 read back at base + 0xa. */
static bool cms_probe(cok_sound *s, uint16_t base)
{
    bool ok = true;
    for (int i = 0; i <= 3 && ok; ++i) {
        out(s, (uint16_t)(base + 6), D[(uint16_t)(CMS_BYTES + i)]);
        /* The CMS KRYNN.CFG names, at the first base, gives them back. */
        ok = (s->songs_cms && base == 0x220) ||
             in(s, (uint16_t)(base + 0xa)) == D[(uint16_t)(CMS_BYTES + i)];
    }
    return ok;
}

/* 288f: an Innovation's SID: its filter set up, then 3,000 reads of base
 * + 0x1b counted (284c) twice. 284c stores its two counts through near
 * pointers to 288f's locals, which DS reaches, not the stack (the driver
 * takes SS for DS, as Turbo C's small model does): they land in the
 * driver's data segment at the stack's offsets, and 288f reads its own
 * locals, never set. Taken as 0 here, as they were in the emulator (a
 * zeroed stack): no Innovation, after the first count. */
static bool innov_probe(cok_sound *s, uint16_t base)
{
    out(s, (uint16_t)(base + 0x18), 0x80);
    out(s, (uint16_t)(base + 0x0e), 0xff);
    out(s, (uint16_t)(base + 0x0f), 0xff);
    out(s, (uint16_t)(base + 0x12), 0x18);
    return false;
}

/* 2c2f: a register of the Covox's chip written and read back, ten tries. */
static bool covox_reg(cok_sound *s, uint16_t port, uint8_t reg, uint8_t value)
{
    out(s, port, reg);
    for (int i = 0; i < 10; ++i) {
        out(s, (uint16_t)(port + 1), value);
        out(s, port, reg);
        if (in(s, (uint16_t)(port + 1)) == value) return true;
    }
    return false;
}

/* 298b. */
static bool covox_probe(cok_sound *s, uint16_t port)
{
    bool ok = true;
    covox_reg(s, port, 7, 0xff);
    covox_reg(s, port, 0x0d, 0xae);
    covox_reg(s, port, 0x0e, 0x44);
    covox_reg(s, port, 0x0f, 0xf0);
    for (int i = 0; i < 3 && ok; ++i)
        if (!covox_reg(s, port, (uint8_t)(i + 2), D[(uint16_t)(CMS_BYTES + i)])) ok = false;
    return ok;
}

/* 2a61, with each probe's "look everywhere" (276c, 27fc, 2930, 2a0b with
 * -2): the first device found; its port kept for the AdLib (0fa0). The
 * music of a CMS, an Innovation or a Covox is not ported; the machine the
 * port models has a CMS where KRYNN.CFG names one, and never the others,
 * their probes reading 0xff. */
static unsigned detect(cok_sound *s)
{
    for (uint16_t port = 0x388; port <= 0x388; ++port)
        if (adlib_status_ok(s, port)) {
            ww(D, OPL_PORT, port);
            ww(D, OPL_DATA, port + 1u);
            return 2;
        }
    for (uint16_t base = 0x220; base <= 0x260; base = (uint16_t)(base + 0x10))
        if (cms_probe(s, base)) return 3;
    for (uint16_t base = 0x280; base <= 0x2e0; base = (uint16_t)(base + 0x20))
        if (innov_probe(s, base) || innov_probe(s, base)) return 4;
    for (int i = 0; i <= 3; ++i)
        if (covox_probe(s, rw(D, (uint16_t)(COVOX_PORTS + 2 * i)))) return 5;
    /* 02d1: the Tandy's BIOS, 0x21 at F000:C000. */
    return s->hardware == COK_SOUND_TANDY ? 1 : 0;
}

/* The sequencer. */

/* 0662: a duration code's length (0x6900 a whole note): the low 3 bits
 * halve it, 8 makes it dotted, 0x30 n/(n+1) of it (triplets). */
static uint16_t length(unsigned code)
{
    uint16_t si = (uint16_t)(0x6900 >> (code & 7));
    if (code & 8) si = (uint16_t)((si >> 1) * 3);
    if (code & 0x30) {
        int16_t di = (int16_t)((code & 0x30) >> 3);
        si = (uint16_t)(s16(si) / (di + 1));
        si = (uint16_t)(s16(si) * di);
    }
    return si;
}

/* 05d3: the voices 0-4 with a time. */
static unsigned playing(cok_sound *s)
{
    unsigned n = 0;
    for (unsigned v = 0; v < 5; ++v)
        if (rd(D, voice(v)) != 0) ++n;
    return n;
}

/* 05a4: no song loops, every voice ended, the device silenced. */
static void stop_voices(cok_sound *s)
{
    ww(D, LOOPING, 0xffff);
    for (unsigned v = 0; v < 6; ++v) wd(D, voice(v), 0);
    silence(s);
}

static void set_voice(cok_sound *s, unsigned v, uint16_t song, uint16_t tracks, unsigned track,
                      uint16_t divisor, uint32_t time)
{
    uint16_t at = voice(v);
    D[(uint16_t)(at + V_VOLUME)] = B[(uint16_t)(song + 2)];
    D[(uint16_t)(at + V_STATE)] = 3;
    ww(D, (uint16_t)(at + V_DIV), divisor);
    ww(D, (uint16_t)(at + V_PTR), (uint16_t)(tracks + be(B, (uint16_t)(song + 4 + 2 * track))));
    ww(D, (uint16_t)(at + V_REM), 0);
    wd(D, at, time);
}

/* The rest of 076b once the time is known: the speaker's single voice
 * plays the song's first track, the other devices voice k - 1 its track k
 * (and an echo voice where ECHO says, which nothing sets). */
static void start_voices(cok_sound *s, uint16_t k, uint32_t now)
{
    uint16_t song = (uint16_t)(rw(D, SONGS) + rw(D, (uint16_t)(OFFSETS + 2 * k)));
    uint16_t tracks = (uint16_t)(song + 0x0e);
    /* 0649: the tempo / (0x1110 / 0x34). */
    uint16_t divisor = (uint16_t)(be(B, song) / 84);
    if (divisor == 0) s->songs.divide_error = true;
    if (D[SPEAKER]) {
        set_voice(s, 0, song, tracks, 0, divisor, now);
        return;
    }
    unsigned v = 0;
    for (unsigned t = 1; B[(uint16_t)(song + 3)] > t; ++t, ++v) {
        if (rw(D, (uint16_t)(ECHO + 2 * t)) != 0) {
            uint32_t delay = rw(D, (uint16_t)(ECHO + 2 * v)); /* cwd */
            if (delay & 0x8000) delay |= 0xffff0000u;
            set_voice(s, v + 3, song, tracks, t, divisor, now + delay);
        }
        set_voice(s, v, song, tracks, t, divisor, now);
    }
}

/* 076b: song k from the time now, which play's 0 makes the count of
 * interrupts once it leaves 0 (07c3 waits for it). */
static void start(cok_sound *s, uint16_t k, uint32_t now)
{
    if (now == 0) {
        if (s->songs.count == 0) {
            s->songs.waiting = true;
            s->songs.waiting_song = k;
            return;
        }
        now = s->songs.count;
    }
    start_voices(s, k, now);
}

/* 0979. */
static void play_song(cok_sound *s, uint16_t k)
{
    uint16_t looping = rw(D, LOOPING);
    stop_voices(s);
    if (D[HEADER + 8] > k) start(s, k, 0); /* else "Song out of range": nothing */
    ww(D, LOOPING, looping);
}

/* 09bd: a tick at the count now: each voice due runs its events: 0x81 a
 * new instrument (the voice's number, past 3 instrument 1), 0x84 the
 * volume 0x7f, other bytes past 0x80 nothing, 0xff the end; then a note
 * (0x80 a rest) and its length (the second byte). */
static void tick(cok_sound *s, uint32_t now)
{
    if ((int8_t)--D[GUARD] <= 0) {
        ++D[GUARD];
        return;
    }
    bool ended = false;
    for (unsigned v = 0; v < 6; ++v) {
        uint16_t at = voice(v);
        uint32_t time = rd(D, at);
        if (time == 0) continue;
        int16_t hi = s16((uint16_t)(time >> 16)), now_hi = s16((uint16_t)(now >> 16));
        if (hi > now_hi || (hi == now_hi && (time & 0xffff) > (now & 0xffff))) continue;
        uint8_t *state = &D[(uint16_t)(at + V_STATE)];
        bool notes = D[NOTES_ON] != 0;
        if (*state == 0) {
            if (notes) note(s, v, 0, 0xffff);
            *state = 3;
            wd(D, at, time + 1);
            continue;
        }
        uint16_t p = rw(D, (uint16_t)(at + V_PTR));
        while (B[p] > 0x80 && B[p] != 0xff) {
            if (B[p] == 0x81) {
                if (notes && *state < 3) note(s, v, 0, 0xffff);
                *state = 3;
                if (device(s) == 2) adlib_instrument(s, v, v < 4 ? v : 1); /* 240f */
            } else if (B[p] == 0x84) {
                D[(uint16_t)(at + V_VOLUME)] = 0x7f;
            }
            ww(D, (uint16_t)(at + V_PTR), (uint16_t)(rw(D, (uint16_t)(at + V_PTR)) + 2));
            p = (uint16_t)(p + 2);
        }
        if (B[p] == 0xff) {
            wd(D, at, 0);
            if (notes && *state < 3) note(s, v, 0, 0xffff);
            *state = 3;
            ended = true;
            continue;
        }
        uint16_t divisor = rw(D, (uint16_t)(at + V_DIV));
        if (divisor == 0) {
            /* The original divides by 0 here (INT 0); the port stops. */
            s->songs.divide_error = true;
            return;
        }
        uint16_t len = length(B[(uint16_t)(p + 1)]);
        time += (uint32_t)(len / divisor);
        uint16_t rem = (uint16_t)(rw(D, (uint16_t)(at + V_REM)) + len % divisor);
        if (rem >= divisor) {
            ++time;
            rem = (uint16_t)(rem - divisor);
        }
        ww(D, (uint16_t)(at + V_REM), rem);
        wd(D, at, time);
        if (B[p] == 0x80) {
            if (notes && *state < 3) note(s, v, 0, 0xffff);
            *state = 3;
        } else {
            if (*state != 2 && notes) {
                if (*state < 3) note(s, v, 0, 0xffff);
                note(s, v, B[p], D[(uint16_t)(at + V_VOLUME)]);
            }
            if (B[(uint16_t)(p + 1)] & 0x40) {
                *state = 2;
            } else {
                uint16_t q = rw(D, (uint16_t)(at + V_PTR));
                if (B[q] == B[(uint16_t)(q + 2)]) {
                    wd(D, at, rd(D, at) - 1);
                    *state = 0;
                } else {
                    *state = 1;
                }
            }
        }
        ww(D, (uint16_t)(at + V_PTR), (uint16_t)(rw(D, (uint16_t)(at + V_PTR)) + 2));
    }
    if (ended && playing(s) == 0) {
        if (s16(rw(D, LOOPING)) >= 0)
            start(s, rw(D, LOOPING), now);
        else
            silence(s);
    }
    ++D[GUARD];
}

/* The entry's functions. */

/* 03f4 with no arguments: the device found (2abc, 270e, 2723), started
 * (2131), voices 0-4 and the echoes cleared, nothing looping, notes on, no
 * tick routine yet. */
static void init(cok_sound *s)
{
    unsigned dev = detect(s);
    D[SPEAKER] = dev == 0;
    ww(D, DEVICE, dev);
    /* 2131 */
    silence(s);
    D[ENABLED] = 1;
    if (dev == 1) {
        D[PORT61] = in(s, 0x61);
        out(s, 0x61, D[PORT61] | 0x60);
    } else if (dev == 2) {
        adlib_start(s);
    }
    for (unsigned v = 0; v < 5; ++v) {
        wd(D, voice(v), 0);
        ww(D, (uint16_t)(ECHO + 2 * v), 0);
    }
    ww(D, LOOPING, 0xffff);
    D[NOTES_ON] = 1;
    s->songs.ticking = false; /* 031e = 0: no tick routine until 0726 */
}

/* 0136: part n of the parts file at the buffer's offset: past its 12-byte
 * header ("GLBR", the parts' count at 10) and the parts' sizes (a word
 * each), the sizes of parts 0 to n - 1 on. Not a parts file, or no part
 * n: the original reads 0000:0000 (the interrupt table); here -1. */
static int32_t part(cok_sound *s, uint16_t file, int16_t n)
{
    if (memcmp(&B[file], "GLBR", 4) != 0) return -1;
    int16_t count = s16(be(B, (uint16_t)(file + 10)));
    if (n < 0 || n >= count) return -1;
    uint32_t sum = 0;
    for (int16_t k = 0; k < n; ++k) sum += be(B, (uint16_t)(file + 12 + 2 * k));
    return (uint16_t)(file + 12u + 2u * (uint16_t)count + sum);
}

/* 225c: for the AdLib (and the devices past it), its eight parts (the
 * part file's 0-7) as instruments 0-7 (1329), rhythm mode kept (10d4 0). */
static void instruments(cok_sound *s, uint16_t file)
{
    if (s16(device(s)) <= 1) return;
    if (device(s) != 2) return; /* the other devices' parts: not ported */
    for (int16_t k = 0; k <= 7; ++k) {
        D[RHYTHM_OFF] = 0;
        int32_t p = part(s, file, k);
        for (int i = 0; i < 12; ++i)
            D[(uint16_t)(INSTRUMENTS + 12 * k + i)] = p < 0 ? 0 : B[(uint16_t)(p + i)];
    }
}

/* 0726 (06b1): the songs' header at p, 10 bytes, the offsets after it (byte
 * 8 + 1 of them, big-endian), the songs past them; the tick routine set. */
static void header(cok_sound *s, uint16_t p)
{
    for (int i = 0; i < 10; ++i) D[(uint16_t)(HEADER + i)] = B[(uint16_t)(p + i)];
    p = (uint16_t)(p + 10);
    unsigned n = D[HEADER + 8] + 1u;
    for (unsigned i = 0; i < n; ++i) ww(D, (uint16_t)(OFFSETS + 2 * i), be(B, (uint16_t)(p + 2 * i)));
    ww(D, SONGS, (uint16_t)(p + 2 * n));
    s->songs.ticking = true;
}

/* 2b57 with 2ba9: INT 1Ch hooked, the count from 0. */
static void hook(cok_sound *s)
{
    s->songs.count = 0;
    s->songs.hooked = true;
}

unsigned cok_songs_call(cok_sound *s, unsigned function, uint16_t offset)
{
    switch (function) {
    case 0: /* 0371 */
        init(s);
        /* 02f0: INT 1Ah's time kept (unused), PIT channel 0 at 0x4000 in
         * mode 3 (72.8 interrupts a second), INT 1Ch hooked. */
        out(s, 0x43, 0x36);
        out(s, 0x40, 0x00);
        out(s, 0x40, 0x40);
        hook(s);
        instruments(s, offset);
        header(s, (uint16_t)(offset + 0x1bc));
        return 0;
    case 1: /* 03b0: the argument (0 from the game) picks the AdLib's echoes */
        if (device(s) == 2) {
            /* 2295 with PLAY_MODES[0], 0: no echo for voices 0-2 (0fc8). */
            D[DETUNE] = D[DETUNE + 1] = D[DETUNE + 2] = 0;
        }
        play_song(s, 0);
        return 0;
    case 2: /* 03cf */
        return playing(s) != 0;
    case 3: /* 03df: 031d (PIT channel 0 to 0, INT 1Ch back), 0599 */
        out(s, 0x40, 0);
        out(s, 0x40, 0);
        s->songs.hooked = false;
        s->songs.waiting = false;
        stop_voices(s);
        /* 2182: the device's stop */
        if (device(s) == 2) {
            adlib_silence(s);
        } else {
            silence(s);
            if (device(s) == 1) out(s, 0x61, D[PORT61]);
        }
        return 0;
    default:
        return 0;
    }
}

unsigned cok_songs_device(const cok_sound *s)
{
    return device(s);
}

void cok_songs_interrupt(cok_sound *s)
{
    if (!s->songs.hooked) return;
    ++s->songs.count;
    if (s->songs.waiting) {
        /* 07c3's wait over: the rest of 076b. */
        s->songs.waiting = false;
        start_voices(s, s->songs.waiting_song, s->songs.count);
    }
    if (s->songs.count & 1) return;
    if (s->songs.ticking && !s->songs.divide_error) tick(s, s->songs.count);
}

void cok_songs_stop(cok_sound *s)
{
    if (!s->songs.playing) return;
    s->songs.playing = false;
    cok_songs_call(s, 3, 0);
}

void cok_songs_play(cok_sound *s, unsigned n)
{
    /* Without MUSIC.DAX the original stops at startup (15dc:00d3); the
     * port plays no song. */
    if (s->device == COK_SOUND_NONE || s->songs.driver == NULL) return;
    if (s->installed) cok_sound_command(s, 0xff);
    if (s->songs.current != n && s->songs.playing) cok_songs_stop(s);
    if (!s->songs.playing) {
        s->songs.current = (uint8_t)n;
        /* 15dc:019a: the record read into the buffer (0x1800 bytes). */
        memset(B, 0, sizeof s->songs.buffer);
        if (n < COK_SONGS_IDS && s->songs.song[n] != NULL) {
            size_t size = s->songs.song_size[n];
            memcpy(B, s->songs.song[n], size < COK_SONGS_BUFFER ? size : COK_SONGS_BUFFER);
        }
        s->songs.playing = true;
        s->songs.divide_error = false;
        cok_songs_call(s, 0, 0);
    }
    cok_songs_call(s, 1, 0);
}
