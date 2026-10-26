#ifndef COK_SOUND_H
#define COK_SOUND_H

/* The sound: the effects driver built into START.EXE (17e8, assembler),
 * the title's music (1661, which loads its driver from MIDI.DAX and the
 * score from SOUND.DAX), the timer interrupt that runs them, and the
 * hardware they write to: the Tandy's three-voice sound chip with its noise
 * (an NCR 8496, port 0xC0), the AdLib's YM3812 (opl2.h, ports 0x388 and
 * 0x389) and the PC speaker (PIT channel 2, ports 0x42, 0x43 and 0x61). No SDL here: a front end feeds the game's calls in
 * (cok_sound_call) and takes samples out (cok_sound_render).
 *
 * Two layers, kept apart: the drivers compute what the original computes,
 * tick by tick, as the port writes it makes (cok_sound.trace sees each, as
 * an emulator running the original sees them); the synthesis turns those
 * writes into 16-bit mono samples at COK_SOUND_RATE, as the hardware would
 * have sounded. Deterministic: the same calls at the same sample positions
 * give the same samples, however the rendering is split. */

#include "opl2.h"
#include "rules.h"
#include "songs.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    COK_SOUND_RATE = 44100,
    COK_SOUND_FX_IMAGE = 0x12c5, /* 17e8:0000-12c4, the driver's data. */
    COK_SOUND_SEGMENT = 0x10000,
    /* What a front end rendering by the game's own clock (eclplay --wav,
     * krynn --no-delay) renders after the run, so that the last sound ends. */
    COK_SOUND_TAIL_MS = 1000,
};

/* What the game asks of the sound, as the original's calls. */
typedef enum {
    COK_SOUND_COMMAND,     /* 17e8:0020 with the command (value); see cok_sound_command. */
    COK_SOUND_INSTALL,     /* 17e8:00da: hook the timer, as main does after the title. */
    COK_SOUND_OVERLAY,     /* Before an overlay loads: 17e8:0020(0xff) if DS:577c. */
    COK_SOUND_OVERLAY_END, /* After it: 17e8:00da unless DS:577c. */
    COK_SOUND_RECORD,      /* A DAX record read (169c:088e): commands 0, then 1. */
    COK_SOUND_CTRL_S,      /* 1614:025b read Ctrl-S (0x13): sound off, or back on. */
    COK_SOUND_MUSIC,       /* The title starts its music (2f55:063c); a song (value) of
                            * a game with songs (dkk 15dc:003d). */
    COK_SOUND_MUSIC_END,   /* The title ends it (2f55:006d); the song stopped (dkk
                            * 15dc:00a4). */
    COK_SOUND_QUIT,        /* Quitting to DOS (1614:0000). */
    COK_SOUND_CTRL_U,      /* A key read Ctrl-U (dkk 16ab:01d4): the songs off, or on. */
} cok_sound_call;

/* KRYNN.CFG's sound device, DS:577a: 0 CMS, 1 an MT-32 (Roland LAPC), 2
 * AdLib, 3 Tandy, 4 the PC speaker, 5 none. The music of 0 and 1 is not
 * ported; the effects of 0-2 are the speaker's, as in the original. */
typedef enum {
    COK_SOUND_CMS = 0,
    COK_SOUND_ROLAND = 1,
    COK_SOUND_ADLIB = 2,
    COK_SOUND_TANDY = 3,
    COK_SOUND_SPEAKER = 4,
    COK_SOUND_NONE = 5,
} cok_sound_device;

/* Which handler the timer interrupt (INT 8) runs. */
typedef enum {
    COK_SOUND_BIOS,  /* Neither: the BIOS's 18.2 Hz tick. */
    COK_SOUND_FX,    /* 17e8:1333, PIT channel 0 at 0x13b1: 236.7 Hz. */
    COK_SOUND_TITLE, /* 1661:035a, PIT channel 0 at 0x5555: 54.6 Hz. */
} cok_sound_isr;

/* The Tandy's sound chip (an SN76489 variant, the NCR 8496), as MAME
 * models it: three square waves and a noise generator, each with a 4-bit
 * attenuation (2 dB steps, 15 off), clocked at 3,579,545 Hz / 16. */
typedef struct {
    uint16_t reg[8];   /* tone 0 (10 bits), vol 0, tone 1, vol 1, tone 2, vol 2, noise, vol 3 */
    uint8_t latched;   /* The register a data byte goes to. */
    int32_t period[4], count[4];
    uint8_t output[4];
    uint16_t rng;      /* The noise's 16-bit shift register. */
} cok_sound_chip;

typedef struct cok_sound {
    /* What the machine has: the Tandy's chip, a speaker only, or a speaker
     * and an AdLib. */
    cok_sound_device hardware;
    /* DS:577a, 577b, 577c, 5780: the device (5 when Ctrl-S turned it
     * off), the device saved, the effects driver installed, the title's
     * music playing (commands ignored). */
    uint8_t device, saved;
    bool installed, music;
    /* 17e8's segment: its data at 0120-12c4 (channels, scripts, tables). */
    uint8_t fx[COK_SOUND_SEGMENT];
    /* The music driver (MIDI.DAX record device + 1) and the score
     * (SOUND.DAX record 1) as loaded; a fresh copy of the driver is the
     * segment the title plays from (the original loads it each time). */
    uint8_t *driver, *score;
    size_t driver_size, score_size;
    uint8_t music_seg[COK_SOUND_SEGMENT], score_seg[COK_SOUND_SEGMENT];
    bool music_loaded;   /* The title's music runs (1661:000a to 00da). */
    /* The score's segment, which play stores at the driver's 0012: in the
     * original wherever the heap put it. Nothing reads it back but an
     * instrument pointer out of its table, which the shipped score never
     * makes; 0 here. */
    uint16_t score_segment;
    char music_error[1200]; /* Why there is no music, or empty. */

    /* The timer: who handles INT 8, what each hook saved, PIT channel 0's
     * count (0 for 65536), the next interrupt, and the count to the next
     * BIOS tick (17e8:0120, 1661:02e3), in clocks of the 14,318,181.8 Hz
     * crystal (the PIT counts every 12th, the chip every 64th). */
    cok_sound_isr isr, fx_saved, title_saved;
    uint16_t pit0;
    bool pit0_restart;   /* A control word for channel 0: the count restarts it. */
    uint8_t pit0_lo, pit2_lo;
    bool pit0_hi, pit2_hi; /* The next write to 0x40 / 0x42 is the high byte. */
    uint64_t now, next_tick;
    uint32_t frac;       /* The sample clock's remainder, in 1/485100 of a crystal clock. */
    uint8_t title_count;
    bool title_busy;     /* 1661:02e5 */

    /* The PC speaker: port 0x61 (bit 0 gates PIT channel 2, bit 1 lets it
     * through), channel 2's count and square wave. */
    uint8_t port61;
    uint16_t pit2;
    bool pit2_out, pit2_waiting;
    uint32_t pit2_left;  /* PIT clocks left in this half of the wave. */
    uint64_t pit2_next;  /* The next PIT clock, in crystal clocks. */

    cok_sound_chip chip;
    uint64_t chip_next;  /* The chip's next step, in crystal clocks. */
    /* The AdLib's chip, clocked by its own 3,579,545 Hz crystal, taken as
     * the PC's crystal / 4: a sample every 288 of its clocks. */
    cok_opl2 opl;
    uint64_t opl_next;   /* The chip's next sample, in crystal clocks. */
    int64_t dc_in, dc_out; /* The output's coupling capacitor (a high-pass). */
    unsigned volume;     /* 0-256, 256 full. */

    bool bomb;           /* 17e8's copy-protection counter ran out (never armed). */
    /* The music driver divided by too little: the original stops with
     * runtime error 200; here the music stops. */
    bool divide_error;
    /* A game's songs (MUSIC.DAX, songs.h): songs_game for a game with
     * songs (cok_sound_load sets it from the rules), whose calls go as dkk
     * 15dc's, songs.driver NULL if MUSIC.DAX could not be read. songs_cms: the
     * machine has the CMS KRYNN.CFG names, which the songs' driver finds
     * (its music there not ported). */
    cok_songs songs;
    bool songs_game, songs_cms;
    /* Each port write the drivers make (not 0x20, the interrupt
     * controller's), for tests and differential checks. */
    void (*trace)(void *context, uint16_t port, uint8_t value);
    void *trace_context;
} cok_sound;

/* After COK_SOUND_MUSIC: whether the title's music would play on a
 * device whose driver is not ported (0 and 1, and 2 for a game whose music
 * is not read), with text saying so. */
bool cok_sound_music_unported(const cok_sound *s, const cok_rules *rules, char *text,
                              size_t size);
/* The device's name, as the original's setup lists it. */
const char *cok_sound_device_name(cok_sound_device device);

/* Set up s for hardware, the device KRYNN.CFG would name (DS:577a), as
 * startup leaves it (17e8:0105): nothing hooked, the timer at 18.2 Hz.
 * It frees nothing: cok_sound_free a sound loaded before. */
void cok_sound_init(cok_sound *s, cok_sound_device hardware);
/* Load the music driver and the score from ASSETS (MIDI.DAX record
 * hardware + 1, SOUND.DAX record 1, the rules' music_drivers and
 * music_score). Without them the title is silent: returns false with
 * s->music_error set; the effects need no files. */
bool cok_sound_load(cok_sound *s, const cok_rules *rules, const char *assets);
void cok_sound_free(cok_sound *s);

/* Carry out one of the game's calls at the current time. */
void cok_sound_call_at(cok_sound *s, cok_sound_call call, unsigned value);

/* The driver's command n (17e8:0020): ignored while the title's music
 * plays or for n outside 0-15 and 0xff; with the device on, 0 stops the
 * sound, 1 lets the speaker sound again, 2-12 start sound n - 1 on the
 * Tandy (device 3) or the speaker (else), 0xff stops and unhooks the
 * timer; 15 arms the copy-protection counter (5,000 ticks, then the
 * original fakes a runtime error and quits; nothing sends it). */
void cok_sound_command(cok_sound *s, unsigned n);

/* How many times as fast as real time the DOS clock runs (rounded): the
 * BIOS's INT 8 counts 0040:006c at each interrupt that reaches it, which
 * GetTime reads. 4 while the songs' driver is hooked (PIT channel 0 at
 * 0x4000, each interrupt passed on: dkk 35c6:0000's and 6a69:0e74's
 * waits pass four times as fast), else 1 (the effects driver passes on
 * every 13th of 0x13b1, the title's music every 3rd of 0x5555). */
unsigned cok_sound_clock_speed(const cok_sound *s);

/* Run the timer's next interrupt (for tests): time moves to it. */
void cok_sound_tick(cok_sound *s);

/* Render frames samples, running the timer's interrupts as they fall due. */
void cok_sound_render(cok_sound *s, int16_t *out, size_t frames);

/* A port write as the hardware takes it (the drivers' writes go here). */
void cok_sound_out(cok_sound *s, uint16_t port, uint8_t value);
/* A port read: port 0x61 as last written, the AdLib's status at 0x388
 * on a machine with one, 0xff where the machine has nothing. */
uint8_t cok_sound_in(const cok_sound *s, uint16_t port);

/* The chip on its own (for tests): a byte written to port 0xC0, one step
 * of its clock (3,579,545 / 16 Hz), and its output, the sum of the
 * volumes of the voices whose output is high. */
void cok_sound_chip_init(cok_sound_chip *chip);
void cok_sound_chip_write(cok_sound_chip *chip, uint8_t value);
void cok_sound_chip_step(cok_sound_chip *chip);
int32_t cok_sound_chip_level(const cok_sound_chip *chip);
/* The amplitude of attenuation 0-15 (2 dB a step, 15 silent). */
int32_t cok_sound_volume(unsigned attenuation);

#endif
