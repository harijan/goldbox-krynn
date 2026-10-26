# Sound

Part of the [developer notes](../DEVELOPERS.md).

`src/sound.c` is the sound, without SDL: the original's effects driver
(`17e8`, assembler linked into `START.EXE`), its title music (`1661`,
which loads its driver from `MIDI.DAX` and the score from `SOUND.DAX`),
Death Knights' songs (`src/songs.c`: `MUSIC.DAX`'s driver, see [Death
Knights' songs](#death-knights-songs)), the timer interrupt that runs
them, and the hardware they write to (the AdLib's chip in `src/opl2.c`). The
game calls it through `hooks.sound` (`cok_sound_call`) as the original
calls the drivers; `krynn` plays it through SDL's audio device and
`eclplay --wav FILE` renders it to a WAV file (16-bit mono, 44,100 Hz).
The drivers (what the original computes) are kept apart from the
synthesis (what the hardware did): each port write they make goes to
`cok_sound.trace` as well as to the hardware, and those writes were
compared with the original's, run in an 8086 emulator (see below).

### Devices

Startup (`11c7:031a`) reads `krynn.cfg` in the game's directory, as Turbo
Pascal's text reads go: `Read` of the first line's first character (the
display: `E` EGA, `T` Tandy, else CGA), `ReadLn`, then `Read` of the second
line's first character, the sound device, into `DS:577a`: `C` 0 (CMS, the
Game Blaster, ports 0x220-0x223), `R` 1 (Roland LAPC, an MT-32 through an
MPU-401, 0x330), `A` 2 (AdLib, 0x388), `T` 3 (the Tandy's sound chip,
0xC0), `P` 4 (the PC speaker), anything else 5, none (the setup's `[6]
Silent` writes `S`). Only those upper-case letters count; nothing is
skipped before them, so a space, a lower-case letter or an empty line is 5.
`ReadLn` ends a line at a CR (and a LF after it) and stops at a Ctrl-Z or
the end, where `Read` gives Ctrl-Z: a file of one line, or with LF line
ends only (one line to Turbo Pascal), is 5 too. Without the file the
original shows its setup (see [Startup and the setup](front-end.md#startup-and-the-setup)), writes it and reads
it. There is no other choice of the device, in the game or on its command
line. The effects play on the Tandy's chip for `T` and on the PC speaker
for `C`, `R`, `A` and `P`; the music's driver is `MIDI.DAX` record device +
1.

The port reads `KRYNN.CFG` the same way (`cok_config_parse`,
`src/config.c`, which reads the other lines too; `COKAssets/KRYNN.CFG` says
`E` and `T`), from `--config`, the saves directory or the assets (see
[Startup and the setup](front-end.md#startup-and-the-setup)); without one, the setup asks and writes it.
`eclplay` without `--start-menu` runs no setup: without a file it says so
on standard error and takes the Tandy, for the display as well. `--sound
tandy`, `--sound adlib`, `--sound speaker` or `--sound off`, in `krynn`
and `eclplay`, override it. With `A` the title's music plays on the
AdLib (`MIDI.DAX` record 3, see The title's music) and the effects on the
speaker, the machine having both. With `C` or `R` the effects play on
the speaker and the title is silent: their music drivers (`MIDI.DAX`
records 1 and 2) are not ported, which is logged as `unported: the
title's music on C/MS (MIDI.DAX record 1)` (or `Roland LAPC`, 2) when
the title starts, and said on standard error at startup. Death Knights'
songs play on the device its driver finds, which is the machine's: see
[Death Knights' songs](#death-knights-songs).

### The effects driver

`17e8:0020` takes a command (a word; the set at `17e8:0000` lets 0-15
and 0xff through), unless the title's music plays (`DS:5780`); with the
device off (5) only 15 does anything:

| Command | Does |
| --- | --- |
| 0 | stop: both chips' channels start sound 0, which silences them |
| 1 | let the channels sound (`17e8:0156`, which both chips' output checks; 1 from the start, and nothing clears it) |
| 2-12 | sound command - 1, on the Tandy (device 3) or the speaker |
| 13, 14 | nothing |
| 15 | arm a copy-protection counter (`DS:4b22` points at `17e8:0124`): 5,000 ticks later the interrupt prints `Run-time  Error 209 at 0BAD:0038` and quits to DOS; nothing sends it |
| 0xff | stop, unhook the timer (`12fa`: `INT 8` back, PIT channel 0 to 0xffff, the speaker and the four voices silenced), `DS:577c` 0 |

`17e8:00da` hooks it: `DS:577b` = `DS:577a`, `DS:577c` 1, and unless the
device is off `INT 8` to `17e8:1333` and PIT channel 0 to 0x13b1 (written
after channel 2's control word 0xb6, so channel 0 keeps its mode), 236.7
interrupts a second, every 13th passed on to the BIOS's (13 × 0x13b1 is
65,533), then sound 0. Main hooks it after the title (`1000:0160`).
Around every overlay the game loads, main's (`1000:01d8`, `0282`, `02b8`,
`02de`) and `COMBAT`'s (`2fd3:191c`: before the battle, around the end
of combat, which so runs with the driver off, around a shop's, the
temple's or treasure's overlay, and after them), it unhooks the driver
with 0xff if `DS:577c` is set and hooks it again after unless it is.
Before the title that follows a
demonstration it sends 0xff and hooks it after (`1000:0304`, `030e`),
and stops the sound after its menu (`1000:036d`). Every DAX record read
(`169c:088e`) sends 0 before reading and 1 after, so a picture, an icon,
a monster or a script loaded cuts the sound playing; the port sends them
from its record reads (`find_record`).

Each chip has four channels of 0x30 bytes (the speaker's at `17e8:0176`,
the Tandy's at `0310`); a sound's table entry (`04a8` + 8n, `0550` + 8n)
names a script for each, and every sound names all four (an empty script
for those it does not use), so a sound replaces the one playing whole:
sounds cut each other, nothing queues, and the game never waits for one.
Each interrupt, unless a sound is being started, runs the speaker's
channels, then the Tandy's, whatever the device: per channel a volume
slide, a pitch slide, vibrato from a table, a note-off countdown, the
script when its count runs out, and an envelope (pairs of a slide and a
count, 0xffff setting the level). Scripts set any field (0xff), loop on a
counter (0xfe), switch to another channel (0xfd), call and return (0xfc,
0xfb), clear the channel (0xfa) and play groups of three-byte notes (the
octaves from a table at `02f6` or `0490`, shifted right); the shipped
sounds use the field writes alone. Then the speaker plays the first
channel sounding (PIT channel 2's count, port 0x61 bits 0-1 its volume &
3), and the Tandy's four voices are all written, pitch and volume, every
interrupt (the noise voice only its control byte); after a stop
(`17e8:0148` 0) the speaker is turned off and the voices silenced each
interrupt. The sounds, where they play, and how long they sound (from
the command to the driver's last tick with a voice louder than
attenuation 15 on the Tandy, or with port 0x61's bits 0 and 1 both set
on the speaker):

| Command | Sound | Where | Tandy, speaker |
| --- | --- | --- | --- |
| 2 | 1 | a spell cast in combat (`5b04:166e`), but Fireball and Lightning Bolt; each missile of a spell that sends one to each target but the last picked (`5b04:1d26`) | 0.81 s, 1.01 s |
| 3 | 2 | text flashed over a combatant, kind 0 (`6346:2316`) | 0.21 s, 0.15 s |
| 4 | 3 | text flashed over a combatant, other kinds (`6346:230b`) | 1.01 s, 0.89 s |
| 5 | 4 | a combatant drops (`6beb:0e19`, `0eb8`) | 1.59 s, 0.74 s |
| 6 | 5 | item types 0x36 and 0x37 and sling stones flying (`432f:2d15`, `2d66`) | 1.01 s, 0.63 s |
| 7 | 6 | a hit (`432f:15d0`, `1873`) | 0.08 s, 0.27 s |
| 8 | 7 | Lightning Bolt (`5b04:1663`) | 2.38 s, 2.53 s |
| 9 | 8 | a miss (`432f:1926`); hand axes, clubs, hammers and other thrown things flying (`2ceb`, `2da7`) | 0.25 s, 0.04 s |
| 10 | 9 | a step or turn in 3D (`475c:081a`, `0c58`, `0ca6`), the party on a new square (`2fd3:3eb9`), `CALL [b203]` (the scripts' steps on the overland map), a combatant's step (`432f:094a`) | 0.07 s, 0.03 s |
| 11 | 10 | Fireball (`5b04:1654`); `CALL [b203]` with `0x3de` 10 | 1.81 s, 1.44 s |
| 12 | 11 | a missile set off (`432f:2bf5`), then darts, javelins, quarrels and arrows flying (`2cbd`) | 0.13 s, 0.13 s |

The port logs the commands it logged before as `sound:` (combat's, `CALL
[b203]`'s); the steps and turns, the record reads, the hooks and
unhooks are not logged, but `eclplay --sound-calls` prints every call as
`call:`.

Ctrl-S (0x13) read by `1614:025b`, through which every key is read but
text input's (`1521:0739` reads `Crt.ReadKey` itself), turns the sound
off (`DS:577b` = the device, command 0, the device 5) or back on (the
device = `DS:577b` and command 1), the latter only for the Tandy or the
speaker (the set at `1614:022c`), and not while the title's music plays.
The key is still returned, and Alt-R's scan code (0x13 after a 0) does
the same. Since `17e8:00da` copies the device to `DS:577b`, a title after
a demonstration played with the sound off leaves it off for good.
Quitting to DOS (`1614:0000`) puts the device back from `DS:577b`, then
stops the title's music or sends 0xff.

### The title's music

The title (`2f55:063c`) sets `DS:5780` and, unless the device is off,
loads the driver (`1661:000a`: `MIDI.DAX` record device + 1, afresh
each time), hooks `INT 8` (`1661:02e6`, PIT channel 0 at 0x5555: 54.6
interrupts a second, every third passed on), loads the score (`014d`:
`SOUND.DAX` record 1, 14,064 bytes, one for every device) and plays it
(`02b3`: the driver called with AH 1). Each interrupt calls the driver
with AH 0; at the title's end (`2f55:006d`, after the credits' wait or
a key) AH 2 stops it, `00da` unhooks it and `DS:5780` is cleared. Only
the title has music. The score is MIDI-like: delta times in ticks, notes
on and off, programs, pitch bends, running status, `FC` ending it (`FC
80` would loop; the title's does not). It lasts 7,860 ticks (2 min 24 s),
longer than the title, which always cuts it.

The Tandy's driver (record 4) plays four voices, the MIDI channels at
score bytes 0x43-0x46 (1, 3, 2 and 9) transposed by 0x47-0x4a, notes
folded into its table's 51 by octaves; each voice takes its channel's
instrument (an attenuation envelope, a vibrato table and its depth); a
note finding its channel's voices busy takes the one sounding longest;
channel 9's notes 0x24 and 0x26 are a bass drum and a snare on the noise
voice. The speaker's (record 5) plays one voice, the channel at byte
0x4b transposed by 0x4c, and channel 15's notes 0 and 1 as short pitch
sweeps (one pseudo-random). A pitch bend or vibrato that would overflow
the driver's divides ends the original with runtime error 200; the port
stops the music there and says so (`sound.divide_error`); the shipped
score never does, and the AdLib's driver divides nothing.

The AdLib's driver (record 3, 2,151 bytes; its entry `035b` takes AH's
routine from `0355`: 0 `03ee`, 1 `0376`, 2 `03e3`) plays nine voices,
one for each of the YM3812's channels, two operators each (it never sets
rhythm mode, `0xBD` is only ever cleared): the MIDI channels at score
bytes 0x22-0x2a (1, 2, 2, 3, 3, 4, 4, 5 and 9). It writes the chip's
address to port 0x388 and its data to 0x389, a delay loop after each
(`082e`, 12 rounds after an address, `0810`, 32 after data); it reads
nothing back, neither the status port nor a timer, so it does not detect
the card. Play clears the chip (`0836`: 102 registers at `00d6`, read
from the last, to 0, then the 18 at `013c`, 0x80-0x95, to 0x3f: sustain
level 3, release 15), copies the channel map and gives channel 9's
voices their drums' programs (`030a`); stop clears it again. Each tick
first counts the age of every voice sounding, then runs the score's
events due:

- a program (`0734`) loads an instrument into each of the channel's
  voices (not channel 9's): ten bytes from `01af` on, the modulator's
  and the carrier's 0x20, 0x40, 0x60 and 0x80 bytes, 0xC0's and a
  transpose, through an index at `0150` (programs 0-0x13 their own,
  0x15-0x36 instrument 0's) and 20 pointers at `0187` (14 instruments);
  program 0x14's pointer is the first instrument's bytes (0x1cb1, past
  the driver), and programs past 0x36 take indexes from the pointers'
  bytes (the shipped score's are 0-5);
- a note on takes the channel's first voice not sounding, or keys off
  (pitch 0) the one sounding longest and takes it (none if every one's
  age is 0); it writes the carrier's 0x40 byte less a quarter of the
  velocity (a borrow reaching KSL's bits), then 0xA0 and 0xB0 with
  key-on from the pitch table at `0014` (96 notes: F-numbers 0x158-0x289
  in blocks 0-7) at the note plus the instrument's transpose; channel 9's
  note goes to the first of its voices whose drum note (`02f8`: 0x36,
  0x2a, 0x24 and 0x26 for voices 5-8) it is, at that voice's own pitch
  (`0301`), without the transpose;
- a note off (or a note on of velocity 0) writes 0xA0 and 0xB0 again
  without key-on to the channel's first voice on that note, sounding or
  not, a drum's at its pitch plus the transpose its note on left out;
- a pitch bend multiplies each sounding voice's F-number by the bend /
  0x2000 (an F-number past 0x3ff carries into the block's bits), from the
  bytes its note wrote, so bends do not add up;
- `FC 80` loops, any other `FC` ends: the voices sounding go on until
  AH 2.

The title's score uses channels 1-5 (no drums: voice 8 takes program
0x11 and never sounds), programs 0-5 (instruments with tremolo, vibrato,
KSR, KSL, both EG types, feedback up to 7 and one additive voice; the
waveforms are never enabled, register 1 is 0), no bends, and ends with
`FC 81`. Without `MIDI.DAX` or `SOUND.DAX` the title is silent
and `krynn` and `eclplay --wav` say why on standard error.

### Death Knights' songs

Death Knights has no `MIDI.DAX`, and its `SOUND.DAX` is never read; its
music is `MUSIC.DAX`: record 0 a driver for every device, records 1-7 its
songs (the descriptor's `archives.music_songs`). The effects driver is
Champions' again (`170d`, its routines and calls as `17e8`'s), and the two
take turns: a song unhooks the effects driver, and a sound stops the song.
`src/songs.c` is the driver and the game's calls; `sound.c` sends a game
with songs its calls (`songs_call`).

**The driver.** Startup (`15dc:00d3`, once, after `4f9a:00e1`) reads
record 0, 12,528 bytes, into a 13,000-byte block of the heap from its
first paragraph, and calls it far at that paragraph - 0x10, offset 0x100,
as a `.COM` is laid out (`16fc:001f`). It is Turbo C's small model: code
at record offsets 0-0x2c0f (`CS:0100-2d0f`, the addresses below), data
from 0x2c10 (its entry sets `DS` to `CS` + 0x2d1), its variables and
tables there as the record has them (the port keeps that data segment,
`cok_songs.ds`, as `sound.c` keeps the effects driver's). Its entry
(`0100` to `0258`) takes a function and a far pointer (`16fc:0000`):

| Function | Does |
| --- | --- |
| 0 (`0371`) | start on the song file at the pointer: find the device (`03f4` with no arguments: `2abc`, `2a61`), start it (`2131`), voices 0-4 ended; INT 1Ah's time read, PIT channel 0 to 0x4000 in mode 3 (0x36: 72.8 interrupts a second) and INT 1Ch hooked (`02f0`, `2b57`: the count of interrupts from 0); the AdLib's instruments from the file's parts (`225c`); the songs' header (`0726`) and the tick routine (`09bd`) |
| 1 (`03b0`) | play song 0 of the file (`0979`): the voices ended and the device silenced (`05a4`), then each voice set at the count of interrupts, which play waits for to leave 0 (`076b:07c3`: the first interrupt after the hook). Its argument picks the AdLib's echoes (`2295` with `DS:0000`'s byte), which the game's 0 leaves off |
| 2 (`03cf`) | whether a voice of 0-4 plays (the game never asks) |
| 3 (`03df`) | stop: PIT channel 0 to 0 (18.2 a second), INT 1Ch back (`031d`), the voices ended and the device silenced and stopped (`0599`) |

Each INT 1Ch (`2ba9`), which the BIOS's INT 8 calls, counts; every second
(an even count) calls the tick, 36.4 a second, then the handler it
replaced; the odd ones return. Every interrupt reaches the BIOS, so the
BIOS's count (`0040:006c`) and DOS's time, which `GetTime` reads, run four
times as fast from function 0 to function 3, and with them the two waits
the game times by `GetTime` (`14e4:09bd`): the title's (`35c6:0000`: its
pictures' 5, 5, 5 and 20 seconds pass in a quarter of that, the credits'
169.6 in 42.4) and the title's menu's timeout (`6a69:0e74`: 30 seconds in
7.5). The port's are as fast (`cok_sound_clock_speed`, 4 while the
driver's PIT count is in place, through `hooks.clock_speed`: `title_wait`
and `krynn`'s timed menu; `eclplay`'s `\t` times out at once anyway); with
the sound off (no song) they take their whole time, as the original's do.
The drivers take the game's calls whether or not anything is heard: `krynn`
without an audio device or with `--no-sound`, and `eclplay` without
`--wav`, keep a `cok_sound` that renders nothing, so the title is as fast
there as with sound (the device `KRYNN.CFG` or `--sound` names deciding). `0124`'s delay loop, the argument parser (`03f4`'s, given no
arguments), `073c` (play a song looping, which `09bd` would start again
at its end), `0600` and `0622` are never called.

**The devices.** The driver looks for its device itself (`2a61`): an
AdLib (200 reads of port 0x388, each 0, 2, 4 or 6: `2738`), a CMS (four
bytes written to base + 6 and read back at base + 0xa, bases 0x220-0x260:
`27ba`), an Innovation SSI-2001's SID (bases 0x280-0x2e0, `288f`), a Covox
Sound Master (0x220, 0x240, 0x280, 0x2c0: `298b`), the Tandy (0x21 at
`F000:C000`, the Tandy 1000's BIOS: `02d1`), else the PC speaker; each
probe's port writes are made on every machine that lacks the devices
before it. `DKK.CFG`'s sound line chooses nothing here: with `S` (or
anything not `C`, `A`, `T`, `P`) no song starts (`DS:987e` 5), otherwise
the driver plays where it finds hardware, the effects where `DKK.CFG`
says. The port's machine is the one `DKK.CFG` (or `--sound`) names: `A`
an AdLib beside the speaker, `T` the Tandy, `P` a speaker alone, and `C`
a CMS, whose part of the driver is not ported (`sound: no music: the
songs on C/MS ... are not ported` at startup, and `unported:` at each
song); the Innovation's and the Covox's never come. The Innovation's
probe counts 3,000 reads twice (`284c`) and stores the counts through
near pointers to its caller's locals, which `DS` reaches, not the stack
(the driver takes `SS` for `DS`): the counts land in the driver's data
segment at the stack's offsets and the caller reads its own locals,
never set; the port takes them as 0 (no SID), as the emulator below had
them. Ours, since what the original reads there is whatever the stack
held.

**The songs.** Each record is a "parts file": `GLBR`, a zero word, the
word 0x01bc, 0xffff and the count of parts (0x20; words big-endian, as
all the file's), then each part's size, then the parts (`0136`): 0-7 the
AdLib's instruments (12 bytes: byte 1 a drum's channel, 0 a melodic
instrument; 2-3 the modulator's and the carrier's 0x20 registers, 4-5
their 0x40, 6-7 0x60, 8-9 0x80, 10 0xC0), 8-15 the CMS's (18 bytes),
16-23 the Innovation's and 24-31 the Covox's (8). At 0x1bc (fixed in
`0371`, the parts' sizes giving the same) the songs' header (10 bytes,
`SLBR`, byte 8 the last song's number), the songs' offsets, and the
songs: a tempo (the divisor, tempo / 84: `0649`), a volume, the number of
tracks and their offsets from the song + 0x0e. Track 0 is the speaker's
own arrangement, its only voice; on the others voice k - 1 plays track k
(the shipped songs have four: the speaker's and three). A track's events
are two bytes, a note (0-0x7f, MIDI's numbers: 60 middle C) or a command and a
length: 0x80 a rest, 0x81 a new instrument (voice v's own instrument v,
past 3 instrument 1, `15f2`), 0x84 the volume 0x7f, other bytes past 0x80
nothing, 0xff the end. A length is 0x6900 (a whole note) shifted right by
its bits 0-2, bit 3 dotted, bits 4-5 two, four or six of three, five or
seven (`0662`), divided by the divisor in interrupts with the remainder
carried (`09bd`); bit 6 ties the note to the next (not struck again). A
note followed by the same note ends a tick early, so the next is struck
again. Each tick (`09bd`, skipped inside another) runs each voice's
events once its time has come; when a voice reaches its end and none of
voices 0-4 plays, the device is silenced (no song loops).

| Song | Plays | Length (interrupts, s) | Tempo, divisor |
| --- | --- | --- | --- |
| 1 | the title (`35c6:051c`) | 5,376, 73.8 | 18,944, 225 |
| 2-7 | ECL's `MUSIC` 130-135 (`3632:2878`) in ECL1-3 | 814-3,462, 11.2-47.5 | 9,856-16,640 |

- **The AdLib** (`0fa0`-`162d`, port 0x388): started with rhythm mode on
  (0xBD 0x20, `127d`), NOTE-SEL (0x08 0x40), every operator's waveform 0
  and the waveforms off (register 1 0); an instrument goes to the first
  free channel of 0-6 (`0ff1`, a drum's to its own), both operators'
  registers written, 0xBD and 0x08 again before each (`10fe`); a note
  keys the channel off (A0 0x4a, B0 0x0e), sets the carrier's level to 63
  - (63 - its level) * volume / 128, then the F-number (`DS:0114`, by the
  note in its octave, plus `DS:012c`'s step times an echo's detune) and
  block (the octave - 1, 0-7) with key-on (`1365`). A drum's channel past
  6 would be keyed by its bit of 0xBD, on channel 7's or 8's pitch; the
  shipped instruments are all melodic, so no drum sounds, and the chip's
  rhythm mode (ours, see The hardware) only keeps channels 6-8 quiet,
  which the songs' three voices never reach. Voices 3-5 (none in the
  songs) play at 100/128 of the volume.
- **The Tandy** (`264d`, `24bb`): voices 0-2 on its tone voices (`DS:0248`'s
  divisors for octave 5, shifted left (at most 4) or right by the octave,
  halved below 0x400), attenuation 15 - volume / 8; port 0x61's bits 5-6
  set at the start and put back at the stop (not modelled). Voice 3
  would be the noise's, voices past it the speaker's.
- **The speaker** (`25d3`, `24bb`): voice 0, track 0, PIT channel 2 at
  `DS:0230`'s counts, port 0x61's bits 0-1 on, a note's end off.

**The game's calls.** `DS:9896` is 2 while a song is started (`16fc`), 1
while the effects driver is hooked (`170d:00a9`), else 0:

- the title (`35c6:051c`) starts song 1 (`15dc:003d`), and it plays on
  through the title's menu, main stopping it at each round of its loop
  (`1000:015b`, `15dc:00a4`); after a demonstration the title's song is
  stopped again before the menu (`15dc:0027`), but the demonstration's
  ECL5 is not shipped;
- `15dc:003d` with the sound on (`DS:987e` not 5): the effects driver
  unhooked (`170d:0000` with 0xff) if hooked, the song playing stopped if
  another (function 3), the record read (`15dc:019a`) and function 0 if
  none plays, then function 1: the same song again plays from its start;
- ECL's `MUSIC n` (`3632:2878`): past 0x7f song n & 0x7f, 1-6 a sound
  (`15dc:0000` with `DS:0002`-`0007`'s 12, 9, 4, 7, 10 or 11; the scripts
  use 2, 4 and 5), others nothing;
- every sound (`15dc:0000`) stops the song (`16fc:00e2`: function 3) and
  hooks the effects driver if neither was (`170d:00a9`), as `15dc:0027`
  does; a record read (`15fe:0903`) and `15fe:0000`/`0020` unhook and
  hook the effects driver again if it was, a song playing on;
- Ctrl-S (`16ab:01d4`) turns the effects off or back on as Champions'
  does, the song playing on (no new one while off); Ctrl-U (0x15, and
  Alt-Y's scan code) toggles `DS:9880` (1 at startup), its off stopping
  the song;
- quitting (`16ab:0000`) stops it, and the effects driver if hooked.

The port's calls are Champions' (`cok_sound_call`): `COK_SOUND_MUSIC`
with the song (the descriptor's `start.title_song` for the title),
`COK_SOUND_MUSIC_END` main's stop (`start.music_past_title`: the title
sends none and main hooks no effects driver after it), and
`COK_SOUND_CTRL_U`. `eclplay --sound-calls` prints `call: music N`. A
song the archive lacks starts on an empty buffer and plays nothing (the
original reads a record of 0 bytes); a tempo below 84 (a divisor of 0)
would end the original at its next tick with a divide error, the port
stopping the song (`songs.divide_error`); the shipped songs have neither.

### Time

The original's drivers run from the timer interrupt between any two of
the game's instructions; the game never waits for them. `cok_sound`
counts time in clocks of the PC's 14,318,181.8 Hz crystal (157.5 MHz /
11; the PIT counts every 12th, the Tandy's chip steps every 64th, the
AdLib's makes a sample every 288th), runs each interrupt when its time
comes, and renders samples at 44,100 Hz; a call lands at the next
sample. The AdLib has a crystal of its own, 3,579,545 Hz, which the
model takes as the PC's / 4 (the two are nominally the same frequency;
a real card's drifts against the PC's, which nothing here depends on):
the chip's samples, 72 of its clocks each (49,716 a second), are
averaged over each output sample as the Tandy's chip's steps are. The
drivers' delay loops between the chip's writes take no time here (on a
4.77 MHz PC about 60 us after each data byte); their writes land
together at the interrupt's moment. Rendering in one piece or in any number gives the same
samples. A count written to PIT channel 0 after a control word restarts
its interrupts; one written alone (`17e8:12c5`, `12fa`) takes over at
the next interrupt.

`krynn` by default lets the audio device time the drivers: its callback
renders as the device plays, so the music and the sounds go on through
the game's delays and while it waits for a key, as the interrupt-driven
original's did, and the game's calls land as they come, a buffer (23 ms)
ahead of the speaker. With `--no-delay` (or `--sound-clock game`) the
game's own clock times them instead: its delays (`Crt.Delay`: the text's
characters, `DELAY`, the title's waits, the missiles' steps) and the
`\pN.` pauses of `--keys`; a key read takes no time. That is how `eclplay
--wav` renders, with a second more at the end, and `krynn` then gives SDL's
disk writer (`SDL_AUDIODRIVER=disk`) the same samples, played out to the
end of that second before it closes. The game runs on as it renders; at most 12
seconds of samples wait for a device that plays slower (a real one with
`--no-delay`), the oldest dropped beyond that (`--log` counts them),
while the disk writer is waited for instead. By the game's clock a sound cut
short by the next command or by a key read that took no time is cut
there; in real time it plays as long as the player takes.

### The hardware

The Tandy's chip is modelled as MAME has the NCR 8496 of the Tandy 1000:
three square waves of 3,579,545 / (32 N) Hz, N 0 counting as 1024 (MAME's
`sn76496_base_device::write` gives every chip but Sega's PSGs a period
of 0x400 for 0: `if ((m_register[r] != 0) || m_sega_style_psg)
m_period[c] = m_register[r]; else m_period[c] = 0x400;`; the effects
driver writes 0 on the first tick of most sounds), and a noise
voice shifting a 16-bit register every 32, 64 or 128 steps or at tone 2's
rate, fed back at bit 15 from bit 1 (periodic noise) or from bits 1 and 5
XNOR'd (white), the register reset to 0x8000 only when the
white/periodic bit changes (the effects driver writes the noise's control
every interrupt); a data byte goes to the register latched last;
attenuation in 2 dB steps from 8,191, 15 silent. Each voice adds its
level while its output is high; the sum is averaged over the chip's
steps in each sample. The PC speaker is PIT channel 2's square wave
(mode 3, a new count taking over at a half-wave's end, the gate in port
0x61 bit 0 holding it high and restarting it), heard while bit 1 is set,
at 8,191. A machine with a speaker only hears no `0xC0` writes. The sum
goes through the coupling capacitor (a high-pass at about 14 Hz), is
scaled by `--volume` and clipped to 16 bits. Averaging over each sample
is the only filter, so the square waves' harmonics above 22 kHz alias
somewhat; the game's tones are low enough that this is faint.

The AdLib's YM3812 (OPL2) is `src/opl2.c`, written for this port from
two documents only, with our own choices where they leave something
open (each said so below); no emulator's code or tables are used, and no
choice is made because an emulator makes it: Yamaha's YM3812 Application Manual (the 1994 scan at the Internet
Archive, "Yamaha YM3812 Application Manual"; "the manual" below, by its
section, table and printed page), and Matthew Gambrell and Olli
Niemitalo's "OPLx decapsulated" (2008), which read the chip's two ROMs
off its die and gave the formulas that regenerate them. Where the manual
leaves something open, the choice is ours and said so. The manual gives
its times at a 3.6 MHz clock, 50,000 samples a second; the model takes
them in samples, so on the AdLib's 3,579,545 Hz they come out 0.6%
longer, as on the chip. It runs at the chip's rate, 3,579,545 / 72 =
49,716 samples a second, in integers. Feature by feature:

- **The ROMs** (decap): a quarter sine's logarithm,
  `round(-log2(sin((x + 0.5) * pi / 512)) * 256)`, and an exponent,
  `round((2^(x / 256) - 1) * 1024)`, x = 0-255, and the decap's
  `exp(logsin(phase) + gain)` for an operator: the log-sine (its quarter
  mirrored and negated) plus the attenuation, through the exponent ROM,
  its integer part a shift: the decap's significand, the ROM's value and
  the hidden 1,024, at most 2,042 either way (in the code as
  tables, `tests/test_opl2.c` regenerates them). The waveforms (Table
  3-10's half sine, |sine|, rising quarters) only while register 1's bit 5
  is set (3-1-11).
- **The phase** (3-2, 3-1-8's formula `F = fmus * 2^19 / fsam / 2^(b -
  1)`, Table 3-1): a 19-bit accumulator a sample up by (F-number <<
  block) times twice the multiple (MULT 0 a half, 11 as 10, 13 as 12, 15
  as 15) / 4, its top 10 bits the place in the wave: F-number * 49,716 *
  2^(block - 20) Hz. A key-on restarts the phase at 0 (ours: the manual
  does not say; a note then starts the same way each time).
- **The envelope's rates** (3-1-4 to 3-1-7, Tables 3-2 and 3-6): 4 * R
  + the key scaling (the keyboard split number, 3-1-3 on p. 11: the
  block and one F-number bit, with NOTE SEL 0 the second from the top,
  bit 8, with 1 the top, bit 9; all of it with KSR, a quarter without), R
  0 no change. Table 3-6's
  times are in the code, 0-96 dB for the decay (and release) and for the
  attack. Ours: the attenuation is kept to 2^-22 of a 0.1875 dB step, and
  a linear rate adds the same amount every sample, 512 steps over its
  table time (2^32 / the time in hundredths of a ms), so every decay
  time is the table's to the sample (rates 60-63 2.40 ms, the attack at
  once). The manual places no individual steps in time; the chip's
  staircase is not modelled.
- **The attack's shape** (3-3 and Fig. 3-2: exponential, from 96 dB to 0
  dB, where the decay takes over). Ours: the attenuation plus 6.5 steps
  shrinks by the same factor every sample, from 518.5 to 6.5 in Table
  3-6's 0-96 dB time: a factor `exp(-ln(518.5 / 6.5) / samples)` (in the
  code as fractions of 2^32, the test computes them from the times). The
  6.5 is what fits the table's other column, 10% to 90% of the amplitude
  (20 dB to 0.92 dB): over the whole time the table gives 0.526 on
  average (0.46-0.57 by rate), the curve ln((106.7 + c) / (4.9 + c)) /
  ln((512 + c) / c), at most 0.5245 at c = 6.5. Every 0-96 dB attack time
  is the model's to a sample (18 samples in 141,312 at the slowest), the
  10-90% times within 4% for each rate and 1% on average.
- **Sustain and EG-TYP** (3-1-4's Fig. 3-1, 3-1-7): the decay stops at
  the sustain level (3 dB steps, 15 as 93 dB) and holds there with
  EG-TYP 1 while the key is down, goes on at the release rate with 0;
  key-off releases.
- **KSL** (3-1-5, Tables 3-4 and 3-5): Table 3-5's 128 entries (dB at 3
  dB an octave, by block and the F-number's top four bits) are in the
  code in thousandths of a dB; KSL 1 is them, 2 half (1.5 dB an octave),
  3 twice (6 dB), each an exact number of 0.1875 dB steps.
- **The attenuation sum** (3-3: "the effects of total level, level key
  scaling, and amplitude modulation are added to the envelope
  generator", 96 dB at 0.1875 dB): envelope + total level (0.75 dB
  steps, Table 3-3) + KSL, in 0.1875 dB steps, then in eighths of a step
  (the log-sine ROM's units, as the decap's formula adds them) with the
  tremolo, at 96 dB silent. Ours: the eighths, which let the tremolo's
  4.8 and 1 dB through without rounding to whole steps.
- **Tremolo** (3-1-4 and 3-1-10: 3.7 Hz at 3.6 MHz, 4.8 dB with 0xBD's
  bit 7, 1 dB without). Ours: its shape, a triangle linear in dB from 0
  down to the depth and back, on a 32-bit phase (3.7 / 50,000 of a cycle
  a sample), the depth 205 or 43 eighths of a step (4.80 and 1.01 dB),
  the phase counted from the chip's reset.
- **Vibrato** (3-1-4 and 3-1-10: 6.4 Hz at 3.6 MHz, 14 cents with 0xBD's
  bit 6, 7 without; 3-2: it combines with the frequency in the phase
  generator). Ours: a triangle in cents from 0 up to the depth, down to
  minus it and back (the depth read as either way), multiplying the
  phase's increment by up to 1 + 532 / 65,536 (14.0 cents) or 266 /
  65,536, at the manual's 6.4 Hz.
- **Feedback** (1-3's formula (3), `F = A sin(wt + B F)`, and Table 3-8's B:
  0, pi / 16, pi / 8, pi / 4, pi / 2, pi, 2 pi, 4 pi for FB 0-7): with a
  full-level output (2,042, about 2^11) as F = 1 and 1,024 phase steps a
  cycle, B F is F's output shifted right by 7 - FB (FB 7 the output
  whole). (3) is implicit in F, and a sampled loop must take F from
  outputs already made. Ours, measured (DOSBox-X's OPL2 as a black box,
  its audio alone, as for the connections below): at FB 5 with the
  modulator at full level its note rings at about 16.6 kHz, a third of
  the sample rate, and a trace of that shows at FB 3. The loop whose
  input is the mean of the last two outputs is the one that turns over
  at exactly a third of the rate (z^-1 + z^-2 = -1 there) and not at
  half, so F is that mean; its notes at FB 3, 5 and 7, and the title's FB
  6 and 7 instruments, then agree with the measurement band for band
  (the running average used before rang nowhere, was darker from FB 5
  up and left FB 7's fundamental 37 dB short). FB 6 and 7 are noisy, as
  Table 5-1 says the feedback's top end is ("Normal tone -> (Noise)").
- **The connections** (3-1-9): CNT 0 the modulator's output added to the
  carrier's phase, 1 both outputs added. The manual gives no modulation
  index for the carrier; ours, measured: DOSBox-X's OPL2 taken as a black
  box (its audio alone: one held note per setting, written to 0x388 by a
  small .COM), a modulator at total level t gave the spectrum Table 3-8's
  scale gave here at t - 8, 6 dB louder, at TL 0, 8 and 16 alike, and the
  title's CNT 0 instruments' first 16 harmonics then agreed within about
  1 dB. So the carrier's phase input takes the modulator's output
  doubled, twice FB 7's scale: a modulation index of up to 8 pi.
- **The timers and status** (3-1-2, 3-5: 80 us and 320 us a count, 4
  and 16 samples): the flags in the status register (bit 7 IRQ, 6 and 5
  the flags; the low bits D4-D0, which 3-5's figure on p. 23 leaves
  empty, read 0), masks and IRQ reset; writes to addresses the chip
  lacks ignored. The shipped driver never reads the status.
- **Keys** (3-1-8's KEY-ON, 3-1-10 and Table 3-9): an operator's key is
  down while its channel's KEY-ON is set or, in rhythm mode, its
  instrument's bit of 0xBD (BD slots 13 and 16, SD 17, TOM 15, TOP-CY
  18, HH 14); down it attacks, up it releases.
- **CSM** (2-1 c, 3-1-3: speech synthesis, every channel keyed off while
  it is used): ours, a burst. Timer 1's overflow starts every operator's
  attack (and phase); a sample later every operator whose key is up
  releases from where that left it. The manual does not say how the chip
  does it.
- **Rhythm mode** (2-1 b, 3-1-10, Table 3-9, 5-4 on p. 28): ours, not the
  chip's, and the shipped driver never sets it. The manual: the bass
  drum is FM on channel 7's two slots, the tom-tom a sine, and the
  hi-hat, top cymbal and snare channels 8's and 9's frequencies combined
  with white noise, each on its own slot's envelope. Ours: the bass drum
  is channel 7's voice (6 in the code) and the tom-tom slot 15's sine;
  "combined" is the product of the square waves of slots 14's and 18's
  phases (their waves' signs). The top cymbal is that square wave at
  slot 18's level; the hi-hat half it and half the noise as a square
  wave, at slot 14's; the snare half slot 17's own sine and half the
  noise, at its level. Each is added once, as a melodic voice is.
- **The noise**, ours (the manual says only "white noise"): Marsaglia's
  xorshift on 32 bits (shifts 13, 17, 5) from 1, a step a sample, its low
  bit taken.
- **The DAC** (3-4, Fig. 3-3: a 10-bit mantissa with the sign and a 3-bit
  exponent, the YM3014B): the channels' sum clipped to 16 bits, then the
  low bits the mantissa cannot hold dropped (rounding down; ours), the
  exponent at most 6.

What is not exact, or not known: the chip's own envelope staircase
(the model's rates are smooth, fitted to Table 3-6), the attack's exact
curve, the LFOs' shapes and phase at reset, the feedback's sampling
(here the mean of two outputs, as measured), the binary point between
an operator's output and another's phase (here Table 3-8's for
feedback, twice it for the carrier, as measured), rhythm mode, its noise and CSM as above, the status's low
bits, where in its count a timer is against a write
(one sample either way), the rounding of the phase increment for odd
F-numbers at block 0, the order of the chip's per-sample pipeline (here
every operator's output, then the phases and envelopes), and what one's
complement or the DAC's analog stages would add. The shipped driver uses
none of rhythm mode, CSM, the timers, the waveforms or the deep tremolo
and vibrato; it uses the phase, KSR, KSL, total level, the envelopes of
both types, shallow tremolo and vibrato, feedback (FB 0, 6 and 7) and
both connections.

The chip's samples are averaged over each 44,100 Hz sample, added to the
speaker's (the effects') and go through the same high-pass and volume.
Their level, 2 (`OPL_GAIN` / `OPL_GAIN_DIV`), was chosen by
rendering the title on both: the first 34 seconds' RMS is 1,427 on the
AdLib and 1,456 on the Tandy (the loudest sample 7,847 and 7,993), so
the AdLib's title sounds about as loud as the Tandy's (nine voices at
full level could reach 18,378 at the DAC, 36,756 in the mix, which the
mix would clip at 32,767; the title never comes near).

### Checks

The drivers were run against the original's code in an 8086 emulator
booting nothing: `START_FULL.EXE` loaded and relocated, `17e8:0105`,
`0020`, `00da` and the interrupt `1333`, `1661:02e6`, `02b3`, `0353`,
`0321` and the interrupt `035a` with `MIDI.DAX` records 4 and 5 loaded,
`1614:025b` reading Ctrl-S and `1614:0010`-`0039` for quitting; each
operation's and each interrupt's port writes (0x40-0x43, 0x61, 0xC0)
compared, write for write. 2,704 scenarios on devices 0-5 agreed, 0
differed (3.2 million lines): 800 random runs of commands, installs,
overlay pairs, record reads, Ctrl-S and quitting with interrupts between
(every command, overlapping sounds, the toggle on every device), some
with the title's music first; the whole score 16 times on both drivers;
800 scores made up to take every path (bends, the divide overflows,
unknown statuses, running status, loops, every instrument); 200 runs of
the speaker's drums with every opcode of their scripts; 800 sets of
effect scripts made up with every opcode, notes, envelopes and vibrato;
the sounds no command plays (12-20); and the copy protection's
counter. Every instruction of the effects driver and of both music
drivers ran but their padding and one unused routine. `tests/test_sound.c`
checks the port's writes against digests of the original's for 112 of
these scenarios and the first ticks byte for byte, the driver's data
against `START_FULL.EXE`, Ctrl-S, the timer's rates, the chip's tones,
noise and volumes, the speaker's tone and gate, and rendering in pieces;
`tests/test_hooks.c` and `tests/test_adventure.c` where the game calls
the driver, and `tests/test_eclplay.py` and `tests/test_krynn.py` the
calls in whole games, `--wav` and the audio device (see [Playing](front-end.md#playing)).
`make sanitize` also builds `krynn` with ThreadSanitizer (`build/krynn_tsan`)
and plays sound through it in real time, the game's calls crossing the
audio thread's rendering, so that a call made without the lock fails.

The AdLib's driver was compared the same way: `1661:02e6`, `02b3`,
`0353`, `0321` and the interrupt `035a` with `MIDI.DAX` record 3
loaded, every port write (0x40, 0x43, 0x388, 0x389) compared. 3,048
scenarios agreed, 0 differed (7.3 million writes): the title's score
whole (8,000 ticks, once, twice, and started again after 1,911 ticks),
cut short after 400 and 3 ticks and stopped at once, 24 times; 3,024
scores made up to take every path (programs 0-0xff, notes on every
channel with velocities 0-0xff, drums matching each drum voice and
none, voices stolen, running status after every status, bends of any
two bytes, other statuses, `FC 80` loops and ends), some with the
driver's drum notes, pitches and transposes poked, stopped and played
again. Every instruction of record 3 ran but the `nop`s that pad its
short jumps (`049c`, `04b3`, `04bb`, `04d2`, `04e5`, `061f`), its unused
pseudo-random routine (`0818`-`082d`) and the jumps at `0003`-`000f`
that nothing calls; it read no port. `tests/test_sound.c` checks 30 of
these (`tests/test_sound_adlib.h`: the title's six, and 24 made-up
scores that together run every instruction the whole set does) and the
title's first four ticks byte for byte, an A written to the ports
(0x244 at block 4) at 440 Hz after the render (the chip's sample clock),
the title's first 2 seconds as rendered, by digest (the model and the
mixing's average), the AdLib's title never clipped and within a quarter
of the Tandy's RMS, and rendering in pieces. `tests/test_opl2.c` checks
the chip: the ROMs against the formulas, the frequencies of the
multiples and blocks, the waveforms and their enable bit, total level
and every KSL setting against Tables 3-4 and 3-5, the attack's factors
from Table 3-6's times, the attack times (0-96 dB and 10-90%) of rates
1.0-14.3 and the decay times against it, KSR and NOTE SEL (p. 11's
split bit both ways), EG-TYP, release, feedback against Table 3-8 at FB
0-7, both connections, tremolo's and vibrato's depths and rates, and
both played on a voice (AM's 4.8 and 1 dB, VIB's 14 and 7 cents by
0xBD's bits, none without the operator's bit), the DAC against values
worked out by hand, the timers, status, IRQ reset, masks and CSM's
burst, rhythm mode's cymbal and hi-hat and the noise, and writes to
addresses the chip lacks.

Death Knights' songs' driver was compared the same way: `MUSIC.DAX`
record 0 loaded at a paragraph and called through its entry as `16fc`
calls it (functions 0-3, play's wait given the next interrupt), and INT
1Ch, on machines where it finds the AdLib, the Tandy or the speaker
(the stack zeroed before each call, the Innovation's probe then reading
0); every port write of each call and interrupt compared. 741 scenarios
agreed, 0 differed (1,135,519 writes): every song whole on each device
(21: 49,746 writes); 320 random runs of starts, plays from the start
again, polls, stops and songs in turn on one driver (666,322 writes);
400 made-up songs, each on a device, with stops and plays between
(drums, ties, dots, triplets, rests, every command, one to seven
tracks, notes in every octave, no songs, 419,451 writes). The shipped
songs ran 2,349 of the driver's 5,123 instructions; of the 3,209 in the
routines the port carries out, 2,349 with them and 2,601 with the
made-up songs: the rest are the other devices' branches, the
argument parser, the echo set-ups (function 1's argument past 0),
looping (`073c`), the tick's re-entry guard, times past 2^31 and the
error messages' empty calls. `tests/test_songs.c` checks 28 of these
(`tests/test_songs_data.h`: every song on each device, three made-up
songs that run every instruction the 400 did and one at another
volume, a run of songs on each device) and each device's first 200
writes one by one, the parts and header as read, the game's calls, the
timer's rate and the DOS clock's, and the AdLib's and the Tandy's title
rendered; `tests/test_death_knights.py` ECL's `MUSIC` and song 2
rendered on each device. Of a dozen mutations of `src/songs.c`, the
tests catch eight; the four left change what no song reaches (a seventh
AdLib channel taken, a pitch of exactly 0x400, voice 3 on the speaker)
or only a port read (the AdLib's probe's count of reads).
`tools/dosbox/checks/dkk_sound.py` (in `make dkk-checks`) records the
title's song in DOSBox-X on the Tandy and on the AdLib (machine `tandy`
without a Sound Blaster; `ega` with an OPL2) and compares it with
`eclplay --wav`: the driver's device as expected, the pitches (93 % and
91 % of the frames) and the tempo within 0.05 s over the 51 s to the
title's menu (0-0.035 s in four runs; a divisor off by one would drift
0.23 s). `f_wav.envelope`'s steps were a whole number of samples,
220 at 44.1 kHz against 240 at 48 kHz, which stretched `eclplay`'s time
by 0.227 % against DOSBox-X's; they now start at each hop's exact time
(DOSBox-X's 4,451 interrupts of the title's song took 61.125 s of WAV,
61.118 s at 72.826 a second); the levels on the Tandy agree within 5 %.

What the comparison does not show: the time between interrupts (it
compares one operation or interrupt at a time; the PIT's rates and the
sample clock are `tests/test_sound.c`'s), the BIOS's tick every 13th or
3rd interrupt (only its count, `17e8:0120`, as data), the music's
loading (`1661:000a` and `014d`: the emulator gets the records' bytes put
in place; their record reads come after `DS:5780` is set, so they do
nothing), and what happens after a divide overflow or the copy
protection's counter ends the original (it compares up to there; that
the port stops the music or the driver then is `tests/test_sound.c`'s).
The emulator starts from the executable's own data segment and
`17e8:0105`, as the game does.

Not modelled: the time the original's disk reads and overlay loads took
(the sound stays stopped meanwhile there; here they take none); the
port's record reads stand for `169c:088e`'s, and where the port reads a
record twice or keeps one the original read again, the sound stops at
the same moment or one read sooner or later; port 0x61's other bits
(read as 0); and, in the hardware, anything MAME's model of the NCR 8496
leaves out (see [Checks against the original](checks.md#checks-against-the-original)).
