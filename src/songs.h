#ifndef COK_SONGS_H
#define COK_SONGS_H

/* The songs' driver: a game whose music is a file of songs and one driver
 * for every device (Death Knights' MUSIC.DAX: record 0 the driver, records
 * 1-7 the songs, each a "parts file" tagged GLBR). The driver is compiled
 * C loaded as data (dkk 15dc:00d3), called far through its entry (record
 * offset 0, as CS:0100: 16fc:0000) with a function, and ticked from INT
 * 1Ch, which it hooks itself. See docs/sound.md, Death Knights' songs.
 *
 * Its addresses below are the driver's own: code as CS:0100 + the record
 * offset (so record offset 0 is 0100), data as its DS, CS + 0x2d1 (record
 * offset 0x2c10 is DS:0000). The data segment is kept as the record has
 * it (cok_songs.ds), the driver's variables at their offsets there, as
 * sound.c keeps the effects driver's. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    COK_SONGS_CODE = 0x2c10,   /* record 0's data segment starts here (CS + 0x2d1) */
    COK_SONGS_IDS = 128,       /* ECL's MUSIC takes songs 0-0x7f */
    COK_SONGS_BUFFER = 0x1800, /* 15dc:019a's buffer for a song */
};

typedef struct {
    /* MUSIC.DAX: record 0 (the driver) and the songs by record id, as
     * read; NULL where the archive has none. */
    uint8_t *driver;
    size_t driver_size;
    uint8_t *song[COK_SONGS_IDS];
    size_t song_size[COK_SONGS_IDS];
    /* The driver's data segment (record 0 from COK_SONGS_CODE on, as
     * loaded once at startup, 15dc:00d3) and the song's buffer (loaded at
     * offset 0 of its own segment; the driver adds offsets to the far
     * pointer it is given, which the heap leaves at 0-15). */
    uint8_t ds[0x10000];
    uint8_t buffer[0x10000];
    /* CS:2b53: the interrupts since the hook (each INT 1Ch), and whether
     * INT 1Ch is the driver's (CS:2ba9). */
    uint32_t count;
    bool hooked;
    /* DS:031e: the tick routine (09bd) the interrupt calls, set by 0726. */
    bool ticking;
    /* 076b:07c3: play waits for the count to leave 0 (the first interrupt
     * after the hook); the port finishes that play at the interrupt. */
    bool waiting;
    uint16_t waiting_song;
    /* The game's side (dkk 15dc, 16fc): DS:9896's 2 (a song started and
     * not stopped; 1, the effects driver's, is cok_sound.installed), the
     * song last loaded (DS:98a0, 0xff none) and Ctrl-U's flag (DS:9880,
     * 1 from 4f9a:00e1; off, it stops the song). */
    bool playing;
    uint8_t current;
    bool on;
    /* A song whose tempo divides to 0 (0649): the original's next tick
     * divides by 0 and ends it; here the song stops. Never in the game. */
    bool divide_error;
} cok_songs;

struct cok_sound;

/* 15dc:003d: song n (record n): with the device on, the effects driver
 * unhooked if it was hooked, the song playing stopped if it is another,
 * the record loaded and the driver started on it (function 0) unless
 * this song is loaded and playing, then played from its start (1). */
void cok_songs_play(struct cok_sound *s, unsigned n);
/* 16fc:00e2: the song stopped (function 3) if one plays. */
void cok_songs_stop(struct cok_sound *s);
/* The BIOS's INT 8 calling INT 1Ch (2ba9): every second a tick. */
void cok_songs_interrupt(struct cok_sound *s);
/* The driver's entry (0100 -> 0258) with a function and a far pointer's
 * offset (into the buffer), for tests: 0 start on the song in the buffer,
 * 1 play, 2 whether any voice plays, 3 stop. Returns AX where it is
 * defined (2), else 0. */
unsigned cok_songs_call(struct cok_sound *s, unsigned function, uint16_t offset);
/* The device the driver finds (2a61): 2 the AdLib, 1 the Tandy, 0 the PC
 * speaker; for tests. */
unsigned cok_songs_device(const struct cok_sound *s);

#endif
