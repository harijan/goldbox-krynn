#ifndef COK_OPL2_H
#define COK_OPL2_H

/* The AdLib's sound chip, a Yamaha YM3812 (OPL2), written for this port
 * from the chip's documented behaviour (DEVELOPERS.md, The hardware): nine
 * channels of two operators, each a phase generator, a log-sine and
 * exponent lookup and an envelope generator, at the chip's own rate of
 * 3,579,545 / 72 = 49,716 samples a second, in integers. No SDL here: the
 * sound (sound.c) writes its ports and takes its samples. */

#include <stdbool.h>
#include <stdint.h>

enum {
    COK_OPL2_CLOCKS = 72, /* the chip's clocks to a sample */
    COK_OPL2_OPERATORS = 18,
    COK_OPL2_CHANNELS = 9,
};

/* The envelope generator's states. */
typedef enum { COK_OPL2_ATTACK, COK_OPL2_DECAY, COK_OPL2_SUSTAIN, COK_OPL2_RELEASE } cok_opl2_state;

typedef struct {
    /* 0x20: AM, VIB, EG-TYP, KSR, MULT; 0x40: KSL, TL; 0x60: AR, DR; 0x80:
     * SL, RR; 0xE0: the waveform. */
    bool am, vib, sustained, ksr;
    uint8_t mult, ksl, tl, ar, dr, sl, rr, wave;
    uint32_t phase;       /* 19 bits, the top 10 the place in the wave */
    uint16_t env;         /* attenuation, 0-511 in steps of 0.1875 dB */
    cok_opl2_state state;
    uint8_t key;          /* bit 0 the channel's key-on, 1 rhythm's, 2 CSM's */
    int16_t out[2];       /* the last two outputs (feedback) */
} cok_opl2_operator;

typedef struct {
    uint16_t fnum;        /* 10 bits */
    uint8_t block, feedback;
    bool key, additive;   /* 0xB0's KEY-ON; 0xC0's CNT (1: the operators added) */
} cok_opl2_channel;

typedef struct {
    uint8_t address;      /* the register the next data byte goes to */
    uint8_t test;         /* 0x01 (bit 5: the waveforms enabled) */
    uint8_t timer[2], count[2];
    bool running[2], masked[2], flag[2];
    bool csm, nts;        /* 0x08 */
    bool am_deep, vib_deep, rhythm; /* 0xBD bits 7-5 */
    uint8_t drums;        /* 0xBD bits 4-0: BD SD TOM TC HH */
    cok_opl2_operator op[COK_OPL2_OPERATORS];
    cok_opl2_channel ch[COK_OPL2_CHANNELS];
    uint32_t samples;     /* samples made: the envelope's counter, the LFOs, the timers */
    uint32_t noise;       /* the rhythm's 23-bit noise register */
} cok_opl2;

void cok_opl2_init(cok_opl2 *chip);
/* Port 0x388 (the address) and 0x389 (data to the register addressed). */
void cok_opl2_write_address(cok_opl2 *chip, uint8_t value);
void cok_opl2_write_data(cok_opl2 *chip, uint8_t value);
/* Both at once; writes to addresses the chip lacks do nothing. */
void cok_opl2_write(cok_opl2 *chip, uint8_t reg, uint8_t value);
/* The status port (a read of 0x388): bit 7 IRQ, 6 timer 1's flag, 5 timer 2's,
 * the low bits 0x06 as the YM3812 gives them. */
uint8_t cok_opl2_status(const cok_opl2 *chip);
/* The next sample, the nine channels' sum as the DAC (YM3014B) takes it. */
int32_t cok_opl2_sample(cok_opl2 *chip);

/* The pieces, for tests. */
/* A sum as the DAC gives it: clipped to 16 bits, then the low bits its
 * floating point (10 bits and a shift of up to 6) cannot hold dropped. */
int32_t cok_opl2_dac(int32_t sum);
/* The phase increment a sample, of 2^19 a cycle (vibrato aside). */
uint32_t cok_opl2_increment(unsigned fnum, unsigned block, unsigned mult);
/* An operator's output for a 10-bit phase, waveform and attenuation
 * (0-511, 0.1875 dB steps): -4084 to 4084. */
int32_t cok_opl2_wave(unsigned phase, unsigned wave, unsigned attenuation);
/* KSL's attenuation (0.1875 dB steps) for an F-number, block and KSL. */
unsigned cok_opl2_ksl(unsigned fnum, unsigned block, unsigned ksl);
/* The envelope's step this sample at an effective rate 0-63 (0: none). */
unsigned cok_opl2_eg_step(unsigned rate, uint32_t counter);
/* Tremolo's attenuation (0.1875 dB steps) and vibrato's F-number offset at
 * a sample count. */
unsigned cok_opl2_tremolo(uint32_t samples, bool deep);
int cok_opl2_vibrato(uint32_t samples, unsigned fnum, bool deep);

#endif
