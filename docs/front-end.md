# The front end

`krynn`: playing in a window, startup and the setup, the mouse and the joystick, the display modes. Part of the
[developer notes](../DEVELOPERS.md).

## Playing

`build/krynn [options]` plays the game in a window, as `eclplay
--start-menu` plays it without one: main's loop (`1000:0134`, see [The
start menu](start-menu.md#the-start-menu)), the title, its menu and the demonstration, the start menu,
then the adventure, camp, shops and combat, game after game, the
player's turns in a battle reading the keyboard (`3995:0573`,
`COK_COMBAT_PLAYER`). The game's code calls the keyboard and the delay
hook and waits in them; they handle the window's events, show the screen
when it has changed and sleep until a key comes, the delay is over, a
timed menu runs out or the small picture's next frame is due.

| Option | |
| --- | --- |
| `--game NAME` | the game to play (see [Games](games.md#games)): `champions`, the default, or `death-knights`, which plays from its title through the start menu (its saved games, the vault's files, Champions' parties) into the game, up to what is not ported yet (its first fight, creating a character: see Games' [Death Knights](games.md#death-knights)), where the run stops with the reason on the screen; any other name is an error that lists the games (status 2) |
| `--assets DIR` | the game's DAX files (default `COKAssets`, the game's: see [Games](games.md#games)) |
| `--saves DIR` | the roster and saved games (default `saves`, created if missing; ignored by git); never `SAVE/` itself: to play the original's games, copy them into it (`mkdir -p saves && cp SAVE/* saves/`) and pick Load Saved Game; and the port's `KRYNN.CFG`, read before the assets' and written by the setup (see Startup and the setup) |
| `--import DIR` | where a game that imports another's characters finds them: Death Knights' Champions roster (`*.WHO`) and saved games for Add Character to Party and Load Saved Game, Champions (the original's `DKK.CFG` fifth line, a DOS path, `DS:99a4`); default `--saves`, so that one saves directory serves both games |
| `--config FILE` | the `KRYNN.CFG` to read, or, if it is missing, for the setup to write |
| `--scale N` | the window's size, N (1-16) times the screen (default 3: 960×720) |
| `--aspect 4:3`, `--aspect 1:1` | 4:3, the default, shows the 200 lines as 240, as a 4:3 monitor showed the 320×200 mode: the game was drawn for it (the moons and the portraits are round so); 1:1 keeps the pixels square |
| `--fullscreen` | start full screen |
| `--screenshots DIR` | where F12 saves the screen (default `screenshots`, created when needed, ignored by git) |
| `--seed N` | start Turbo Pascal's `Random` from N; by default the clock seeds it, as startup's `Randomize` (`1a46:1200` from `3e99:005b`) does |
| `--woof`, `--helm` | start as `START Woof` (no title, and the debug keys Ctrl-D, Ctrl-Z and Ctrl-C: see [Debug mode](scripts.md#debug-mode)) or with `Helm` (`Area` anywhere, the Gods' cheat Alt-X, free training) |
| `--log` | print the game's text, menus, choices, sounds and what is not ported to standard error, as `eclplay` prints them; the window's size and where the screen is shown in it; keys typed ahead that a flush drops (`flush:`); startup's text screen, a row at a time (`crt:`); the display mode (`display:`); the alternate input found (`input:`); the audio driver and the sound device (`sound:`); and a check that the window keeps up: `stale:` if, through the game's delays, the window kept an older screen than the game's for more than 160 ms |
| `--log-times` | with `--log`, start each line with SDL's clock (`SDL_GetTicks64`, the milliseconds the delays and waits go by), and log each frame the window presents (`present: N`, numbered as SDL's dummy driver numbers the frames it saves with `SDL_VIDEO_DUMMY_SAVE_FRAMES`) and each frame of the small picture's animation as it is drawn (`frame: N`, 0-based): what `tests/test_krynn.py` measures the pacing by in real time |
| `--keys KEYS` | type KEYS first, in `eclplay`'s escapes (see [Playing scripts](scripts.md#playing-scripts)), each turned into the key that gives it and sent through SDL's key events as the game asks for a key (`\k` as it polls, `\t` the timed menu running out, `\[KEYS\]` typed ahead and `\wN.` as in Playing scripts; besides, `\pN.` pauses N hundredths of a second before the next key is typed as the game reads it, and `\#85` and `\#86`, codes the game never gets, press F11 and F12; `\mX,Y,B.` and `\jX,Y,B.` click the mouse and move the stick through SDL's events, see The mouse and the joystick; a key repeated in a group typed ahead is held, its repeats sent as a held key's are; each a real keystroke, so that a flush after its 0 is read loses its scan code, which `eclplay` keeps for keys not typed ahead); the run then ends as `eclplay`'s does, printing `(out of keys ...)`, unless `--continue` hands over to the keyboard |
| `--no-delay` | do not wait where the game waits (for tests); a rest keeps the front end's pace (see below); the sound then goes by the game's clock (see [Sound](sound.md#sound)) |
| `--display ega`, `--display tandy`, `--display cga` | the display mode in place of the one `KRYNN.CFG` names (its first line: `E`, `T`, anything else CGA, see Display modes): EGA and the Tandy's look the same; CGA shows four colours |
| `--sound tandy`, `--sound adlib`, `--sound speaker`, `--sound off` | the sound device in place of the one `KRYNN.CFG` names (its second line: `T`, `A`, `P`, `S`; `C` and `R` play the effects on the speaker and no music, see [Sound](sound.md#sound)): the Tandy's three voices and noise, the AdLib's music with the effects on the PC speaker, the PC speaker, or none |
| `--no-sound` | open no audio device |
| `--volume N` | 0-100 (default 100) |
| `--sound-clock real`, `--sound-clock game` | what times the sound drivers: the audio device as it plays (the default; with `--no-delay`, `game`), or the game's delays and `\pN.` pauses, as `eclplay --wav` renders them |
| `--input mouse`, `--input joystick`, `--input none` | the alternate input in place of the one `KRYNN.CFG` names (its third line: `M`, `J`, `N`; see The mouse and the joystick): `krynn` always has SDL's mouse, and has a joystick when SDL finds one; `--input joystick` over a file whose third line is not `J` (which gives no thresholds) calibrates the stick at startup, as the setup's `[1]` does (`177c:029d`, on the text screen before the note to Tandy users), for the run only: the file is not written |
| `--shots DIR` | save `DIR/NNN.bmp` each time the game waits for a key and when a battle is set up, as `eclplay --shots` does, but read back from the window: each pixel of the screen where its area's middle is shown must be one of the screen's colours (16, or CGA's 4), and at a whole multiple of 320×200 its whole block; in 4:3 it needs `--scale 2` or more, as at 320×240 the smoothing blends every row |

The window can be resized; the screen keeps its aspect, letterboxed. It is
scaled up by whole numbers with square pixels to at least the size shown,
then smoothly to that size, so the pixels stay even at any size and are
sharp at whole multiples. Colours are the 16 of the Tandy and EGA modes,
or in CGA mode the four of its palette, as the BMP export gives them
(`cok_image_rgb`).

Keys, as Turbo Pascal's `Crt.ReadKey` reads them through INT 16h function
0 (`src/keymap.h`; `tests/test_keymap.c` checks each, that every code
the port compares a key against can be typed, and NumLock and Alt's
keypad numbers as the BIOS keeps them, `cok_keymap_event`):

| Key | The game reads | For |
| --- | --- | --- |
| letters | `a`-`z`, with Shift or CapsLock `A`-`Z` | menu hotkeys (either case), names and other text |
| digits, punctuation | ASCII; with Shift as on a US keyboard | text input (0x20-0x7a), amounts, digit hotkeys; 1-9 as the keypad's in menus that take directions |
| Space | 32 | menus that take it; in combat on Auto, the player back in charge |
| Enter, keypad Enter | 13 (with Ctrl 10) | the selected item; the end of a line |
| Escape | 27 | Exit, cancel |
| Backspace | 8 (with Ctrl 127) | erase in text input |
| Tab, Shift-Tab | 9; 0, 0x0f | nothing |
| arrows | 0, then 0x48 up, 0x50 down, 0x4b left, 0x4d right | menu items, picking a character, lists, Move in 3D (step, turn), facing on the overland map, combat directions, Modify's fields, Rest and Speed |
| Home, PgUp, End, PgDn | 0, then 0x47, 0x49, 0x4f, 0x51 | the diagonals in combat and on the overland map; PgUp and PgDn page lists |
| Ins, Del | 0, then 0x52, 0x53 | Del deletes in Modify's name |
| keypad digits and `.` | `0`-`9`, `.`: NumLock is on, as startup sets it (`3e99:005b`, `0040:0017` bit 5), whatever the host's; with it off the cursor key's code (none for 5) | directions (`DS:1fdf`): 8 up, 2 down, 4 left, 6 right, 7 9 1 3 the diagonals, 5 the centre in combat's aim |
| Shift and a keypad key | the cursor key's code (none for 5); with NumLock off the digit | as the arrows |
| NumLock | nothing: NumLock off and on again, as the BIOS's keyboard handler (INT 9) turns it, the host's left as it is; with Ctrl it is Pause, not ported, and turns nothing | the keypad's digits or cursor keys |
| Alt and keypad digits | on Alt's release, the character code they make, as a byte (DOS's `Alt`+number; none for 0), Ctrl held or not; Shift, Ctrl, the lock keys and space (which types a space) keep the number, another key with Alt starts it again | text input, as any key typing it |
| keypad `/ * - +` | ASCII | nothing |
| Ctrl and Left, Right, Home, End, PgUp, PgDn | 0, then 0x73, 0x74, 0x77, 0x75, 0x84, 0x76 | nothing |
| F1-F10 | 0, then 0x3b-0x44; with Shift 0x54-0x5d, Ctrl 0x5e-0x67, Alt 0x68-0x71 | F10 picks Demo on the title's menu; Ctrl-F8 camps on the overland map; menus that do not tell scan codes from letters take F7-F10 (0x41-0x44) as A-D, Home, Up and PgUp as G, H and I, and so on (saved games A-J, Yes and No) |
| Alt and a letter | 0, then the letter's scan code | in combat Alt-Q and Alt-A Auto, Alt-M Magic on and off, Alt-X the Gods (with `--helm`), Alt-R nothing |
| Alt and a digit, `-` or `=` | 0, then 0x78-0x83 | nothing |
| Ctrl and a letter | 1-26; Ctrl `[ \ ] 6 -` 27-31; Ctrl-2 0, 0x03 | Ctrl-S the sound off and on again (`1614:025b`, see [Sound](sound.md#sound)); with `--woof`, Ctrl-D the debug mode on and off, Ctrl-Z the party's effects to `DEBUG.TXT`, Ctrl-C (and Ctrl-2, whose scan code is 3) quit to DOS (see [Debug mode](scripts.md#debug-mode)); in the setup's path line, Crt's editor's: Ctrl-S erases a character, Ctrl-A the line, Ctrl-D and Ctrl-F recall (`1962:0394`, see Startup and the setup), and the debug keys and Ctrl-S's sound do nothing there |
| F11 or Alt-Enter | nothing (function 0 drops them) | full screen on and off |
| F12 | nothing | save the screen as shown (the mouse's cursor too), 320×200 (startup's text screen 640×200), as `krynnNNN.bmp` in `screenshots/` or `--screenshots DIR` |

With the mouse on (`KRYNN.CFG`'s `M`, or `--input mouse`), its left button
is Enter and its right Escape where the game reads a key, and a click goes
where it lands in the menus (see The mouse and the joystick); the game draws
its arrow in place of the window's pointer. With a joystick, a push is a
keypad digit, its first button Enter, its second Escape.

Keys the game cannot read give nothing: those only an enhanced keyboard's
function 0x10 returns (Ctrl or Alt with the arrows, Alt-Enter, F11, F12),
Ctrl with Alt (but for the keypad's digits), keys with the GUI (Windows)
key or AltGr, and non-ASCII keys. Letters and unshifted punctuation follow
the host's layout; shifted punctuation is the US layout's. Keys typed
ahead wait, up to 15 as in the BIOS's buffer
(more are lost), and are thrown away where the original flushes the
keyboard (`1614:045c`: `ReadKey` while `KeyPressed`, the scan code owed
after a 0 too), through the adventure's `flush` hook. Its 21 calls in
`START_FULL.EXE`, each ported but the last:

| Call | In | When |
| --- | --- | --- |
| `67b5:0c28` | `67b5:03e2` | every menu, once it has its key or has timed out |
| `1521:06ea` | `1521:04ac` | paging running text, after its key |
| `2f55:0006`, `005f` | `2f55:0000` | before and after each of the title's waits |
| `1000:0313` | main's loop | after the title that follows a demonstration, before its menu |
| `127f:029a` | `127f:0111` | an image loaded from a DAX file (title pictures, portraits' pieces, combat icons and tiles, big pictures), not when the record is not there |
| `6961:0514` | `6961:00e4` | the small picture or a sprite loaded, not one already held |
| `6961:06b2` | `6961:05b9` | a portrait shown, its pieces held or not |
| `6d21:04a5` | `6d21:01d0` | a combatant's icons loaded |
| `6d21:00ec` | `6d21:002c` | combat's terrain tiles loaded |
| `4b6d:0a66` | `4b6d:0817` | a character's icon composed |
| `4b6d:15ce` | `4b6d:11e5` | a character's files read (a saved game's party, the roster's Add) |
| `4b6d:18ec` | `4b6d:161b` | a monster's records read (`LOAD MONSTER`, `ADD NPC`) |
| `4b6d:20ef` | `4b6d:1b34` | a saved game loaded |
| `58e7:0229`, `0ab6` | `58e7:01f2`, `0a81` | "Overloaded" taking money from the pool, after its pause |
| `58e7:0506` | `58e7:044b` | "Overloaded" trading money, after its pause |
| `3995:145f` | `3995:121c` | combat's Auto, before its 200 ms |
| `432f:34ae` | `432f:33ce` | each step of combat's aiming cursor |
| `3afb:12eb` | `3afb:1200` | the computer's turn's poll of the keys, as it returns, with no key waiting too |
| `6e22:017d` | `6e22:0050` | after "Bad symbol number..." and quitting to DOS (`1614:0000`): never reached, as the run has ended |

The delays are the game's: each character of running text waits speed × 3
ms and `DELAY` and the notices speed × 100 ms, the speed being camp's Alter
Speed (`DS:4b38`, 4 at startup, 9 in the demonstration); the title waits
5, 10, 10 and 10 seconds, polling for a key each hundredth; the title's
menu runs out after 30 seconds (10 after a demonstration) and plays the
demonstration, from when it opens, arrows not restarting it (`DS:6e11`; the
original never runs out past midnight, when `GetTime` wraps; the front end
does). While a menu that the original asks to (`67b5:03e2`'s flag)
waits, the small picture animates as there: each frame stays its delay ×
10 hundredths of a second (the pictures' delays count tenths there), then
the next is drawn at cell 3, 3, the first after the last, for pictures of
two frames or more loaded since the slot was freed. The menus that ask are
the camp's (Save, View, Magic, Rest, Alter and Alter's, Fix), treasure's,
Pick Character in camp and treasure (modes 2 and 6), Save Which Game in
camp, and the scripts' `HORIZONTAL MENU` and `ENCOUNTER MENU` over a
picture that replaced the view (`DS:884a`, `884b`; for `ENCOUNTER MENU` in
a 3D area and not PIC 9). The frame advances in the game's state
(`DS:6da3`), as in the original, so a script's `CALL [6803]` goes on from
it; the frame's time starts again each time a menu reads a key (the
original's at `67b5:03e2`'s start and after each frame), so arrow keys
delay the next frame a little. Nothing else animates while the game waits.
The screen is shown when it has changed, at most 60 times a second while
the game runs and at once when it waits for a key, which it does asleep in
`SDL_WaitEventTimeout`: `tests/test_krynn.py` measures the CPU time over two
seconds at the title's menu and in the title's waits, which must be under
a tenth of a core (it is under 1% here). A present the rate holds back is
owed, and every wait wakes for it, so the window never keeps an old screen
through a delay or while the game waits for a key; nor after it is
exposed or resized. Frames whose delay is 0 (every sprite's groups have
it), which the original's loop redraws as fast as it runs, stay a
hundredth.

Resting (`57e4`'s loop, from camp's Rest) polls the keyboard once each
five minutes rested and waits for nothing, so the original rested at its
machine's pace, showing the time left every five steps (`57e4:0764`); a
key stops it with `Stop Resting?`. The front end gives each step a
hundredth, so that eight hours take about a second, the count shows and a
key can stop it.

Closing the window (or `SIGINT`, `SIGTERM`, even where a shell started it
in the background with `SIGINT` ignored) ends the run at once: the key
read returns the end of input, or the delay sets the abort flag the end of
input sets, and the game returns as `eclplay`'s does when its keys run out,
writing nothing more; a save under way finishes first, as saving reads no
key. `krynn` then exits with status 0. Quitting to DOS (the start menu's
Exit to DOS, Yes to `Quit TO DOS` after camp's Save, or Ctrl-C with
`--woof`) ends it too, printing `(quit to DOS)`.

Where the port stops (`COK_ECL_UNDEFINED`, `COK_ECL_EFFECT_FAILED`, an
unported routine, a file missing or unreadable), `krynn` prints `stopped in
block N at ADDR: ` and the reason to standard error, writes the reason in
light red on the screen's last rows over black, with `Press a key to quit.`
in white (in CGA mode magenta and white), waits for a key or the window's
close, and exits with status 1 (2 for bad options or a `--keys` code no key
gives). Without the font, a message box says it.

Not done: the music of CMS and the MT-32 (see [Sound](sound.md#sound)).
`tests/test_krynn.py` plays, with `--input none` (the keyboard alone, as
`eclplay` without `--mouse` has it), the title and the demonstration, and a
new game from the title (creating a character, the overland map, two fights
with the player's keys, camps in an inn and in 3D, walking in Throtl,
saving the game) with the same keys as `eclplay --start-menu --shots`,
typed through SDL's events, and checks that every frame the window shows,
read back at 640×400, at 960×720 and full screen letterboxed at 1024×640,
is `eclplay`'s byte for byte, and the saved game written the same; keys
typed ahead dropped by a menu's flush and kept by the name's line, and
pressed in the middle of the title's waits and of a rest, as `eclplay` has
them; from the frames SDL's dummy driver saves as they are presented
(`SDL_VIDEO_DUMMY_SAVE_FRAMES`), that the window shows the title while it
waits and the title's menu, once, as `eclplay` draws it, and the
hobgoblin's picture animating in Throtl, each frame for its delay; the
title's menu timing out 10 seconds after it opens following a
demonstration, an arrow half way not starting it again; a rest taking its
pace with the time left shown; closing in the middle of the demonstration's
delays ending at once; F11's full screen shown at once and F12's
screenshot; NumLock, the keypad and Alt's keypad numbers pressed as keys
(`\n`, `\dC`, `\aN.`) giving `eclplay`'s frames; SIGINT with SIGINT
ignored; and, with real delays through the
first fight, that the window never kept an old screen (`--log`'s check) nor
showed more than about 60 frames a second; and the sound through SDL's disk
writer: by the game's clock (`--no-delay`) the same samples as `eclplay
--wav` for the same keys and seed, after the silence the device starts
with; in real time the title's music playing on through the title's wait,
and two steps' sounds each playing out whole while the game waits for a
key, and without a device, with `--no-sound` or without `SOUND.DAX` the
game playing on,
saying why; a copy of the assets whose `KRYNN.CFG` says `P` playing the
speaker; and, with a copy of the assets without `KRYNN.CFG`, the setup's
text screens at 640×200 frame for frame as `eclplay` draws them, the file
it writes and the game in the mode it chose, the next start reading it, and
keys that end in the setup (the game then with the mouse the setup chose,
as `eclplay --mouse` plays it); that the window shows startup's note to
Tandy users before the title, and the music in real time after it; and the
mouse's clicks and the joystick (see The mouse and the joystick).

The checks in real time hold on a busy machine (the tests run while
other builds and tests load every core) and still fail on a change to the
pacing they check. They time the game by SDL's clock as `krynn --log
--log-times` logs it (its lines, the frames it presents and the picture's
frames as it draws them), not by the test's clock, which is late by however
long the test takes to read the lines, nor by when the frames' files were
written. A pace must never be shorter than the original's, since every
wait is measured on that clock; it may be longer by the machine's lateness
in waking, which is bounded only where the port itself could be too slow,
and then in the least late of the frames or steps that repeat: the
picture's frames each under 20 ms over their delays in one of three rounds
and presented within 16 ms of being drawn, the rest's count shown under
56 ms apart (five steps of 10 ms) at least once, the note to Tandy users
under 2.5 seconds, the menu's timeout under 10.5 seconds after it opens and
the arrow's redraw under 5.5; and in the demonstration's busiest second
from 55 to 66 frames presented (63 here, idle or busy). Waits for something
to happen are bounded by the wall clock only to end a failing run. SDL's
dummy driver writes each frame inside the present, so the tests have it
write them in `/dev/shm` where there is one with 32 MB free (else the temp
folder): on a disk slow to write (here a FUSE overlay, starved of CPU on a
busy machine) a frame took seconds, and the game was late by that. The
driver leaves a frame short without a word when its disk is full, which
the tests report as such (its size against its header's). Their temp
folders are named `test_krynn-*`. The sound's checks count the device's
samples, which SDL's disk writer produces at its own thread's pace, slower
on a busy machine (0.8 seconds of them in 3 once): the music is checked
over 2.5 seconds of them, and the steps' sounds, each after a pause, once
0.4 seconds of them follow the second step's `overland:` line, however
long they take to come; the pause between the two sounds only for being
several times a sound's length. Halving or doubling the picture's delays
or the rest's step, each of its frames 40 ms longer, steps of 12 ms,
presenting a frame 100 ms late, every delay twice its time, timing the
menu out at 5, 15 (again from the arrow) or 20 seconds, presenting up to
120 or only 30 frames a second, not presenting in a delay's loop,
presenting again at each wake, the note to Tandy users for 1 second, the
device silent through a delay or a key's wait, or a delay not seeing the
window close: each fails its test.

`tests/test_hooks.c` checks each flush, which menus animate, the sound
driver's calls, and `Loading...Please Wait` and `PIC not found` against
what the original's loaders did in an 8086 emulator
(`tests/test_loading_data.h`, see [Playing scripts](scripts.md#playing-scripts)).

## Startup and the setup

Startup (`11c7:031a`) works in the text mode DOS left, before it sets the
graphics mode, through Turbo Pascal 6's `Crt` unit (`1962`) and `System`'s
text files: Ctrl-Break is turned off (`Crt.CheckBreak`, `DS:8a44`), it
waits for `GAME.OVR` (`169c:01c8`: `Please insert overlay disk.` and a key
while it is missing) and looks for `krynn.cfg` in the current directory
(`169c:04c1`, `FindFirst` with no attributes: a hidden one, or a directory
of that name, counts as missing under MS-DOS; DOSBox-X's own DOS returns
hidden files to such a search, so there a hidden one is found). Without
it the setup runs: `Rewrite`
makes the file first, and if that fails it says `Sorry, disk A can't be
write-protected.`, a blank line and `If these are your masters, use
DISKCOPY to make duplicates.` and halts with status 1. Then four screens,
each after `ClrScr`, in `TextAttr` (the colour under DOS's cursor, normally
light grey on black; nothing changes it):

| Screen | Asks | Writes |
| --- | --- | --- |
| `GRAPHICS ADAPTER TYPE:  [1] CGA  [2] EGA  [3] Tandy  ` | a digit 1-3 | `C`, `E`, `T` |
| `SOUND TYPE:  [1] C/MS` and five more lines to `[6] Silent` | 1-6 | `C`, `R`, `A`, `T`, `P`, `S` |
| `ALTERNATE INPUT TYPE:  [1] JOYSTICK  [2] MOUSE  [3] NEITHER  ` | 1-3 | `J`, `M`, `N` |
| `PATH TO SAVE DATA (DEFAULT - C:\COK\SAVE):  ` | a line | the path, then `F` and 14 numbers |

A digit (`11c7:0020`) is read with `ReadKey` until it is one in range,
then echoed; other keys do nothing, but an extended key's scan code is read
as a character too, so Alt-N (0x31) answers 1 and Alt-M (0x32) 2. The path
is read with `ReadLn` through `Crt`'s line editor (`1962:0394`): up to 126
characters, echoed, wrapping at the 80th column; Backspace or Ctrl-S erases
one, Escape or Ctrl-A all; Ctrl-D brings back one character the buffer
still holds past the cursor (one typed and erased), Ctrl-F all of them;
Enter ends it; other control keys and extended keys do nothing (Ctrl-Z
would end it with `CheckEOF`, which is off). The string keeps the first 80.
Its first character upper-cased, it is taken if it has at least 4
characters, the second a colon and the first a drive from A to P, and not
the floppy (A or B) the game runs on; else `USING DEFAULT PATH: ` and the
default, for 2 seconds. Then every letter is upper-cased and a `\` added
(unless 80 characters already fill the string). The default is the current
directory (`FExpand('*.exe')`, cut to FSplit's 67 characters) and `SAVE`,
or on drive A or B the other drive's root (`B:\SAVE`, `A:\SAVE`). Joystick
calibration follows (`177c:029d`, see The mouse and the joystick): with a
stick on port 0x201 and `[1]`, two screens (`Push JOYSTICK #1 to the
extreme upper left position and press BUTTON #1.`, then lower right;
`Couldn't read the joystick.  Aborting calibration.` on failure, the
numbers as far as it got); without one it asks nothing and the numbers are
0. The file, each line ended by CR LF:

| Line | Holds | Read into | In the port |
| --- | --- | --- | --- |
| 1 | the display's letter | `DS:4b76` (see Display modes) | the display |
| 2 | the sound device's | `DS:577a` (see [Sound](sound.md#sound)) | the device |
| 3 | `J`, `M` or `N` | the joystick `DS:89ce`, the mouse `DS:8986`; another letter leaves both; then `177c:0000` (`11c7:0b16`) keeps the joystick only if stick 1 can be read, and the game's start resets the mouse (`1743:0000` at `3e99:0085`), on only with a driver | `cok_config.joystick` (with a stick the front end has, `cok_config_parse_stick`), `.mouse` (as the file says; the game's start finds the driver or not, see The mouse and the joystick) |
| 4 | the save path | `DS:5784`, string[255]: the roster and saved games, the save disk's checks (`4b6d:0555`), free space | `cok_config.save_path`; the port saves in `--saves` |
| 5 | `F` | `DS:4b26`, 1 for `F`: nothing reads it | `cok_config.f_line` |
| 6-19 | the stick's 7 X and 7 Y thresholds in pairs, read only with the joystick on after `177c:0000` (`Read` of an integer: blanks skipped, a sign, decimal or `$` hex; error 106 makes every later read do nothing) | `DS:8996`, `89a4` | `cok_config.joystick_x`, `_y`: 0 without a stick |

Then, file or not, `ClrScr`, `TANDY 1000 USERS - Make sure NUMLOCK is on.`
and `Delay(2000)` (a busy loop: keys typed wait in the BIOS's buffer), and
the file is read (`Read` and `ReadLn`, see [Sound](sound.md#sound)'s [Devices](sound.md#devices)) as the mode is
set. No argument or key reaches the setup again; the start menu's
Initialize Mouse/Joystick (`4def:5cc7`) rewrites the file (see [The start
menu](start-menu.md#the-start-menu)).

The port does the same (`src/setup.c`, on the text screen of `src/crt.c`),
in `krynn` and in `eclplay --start-menu`, before the game opens; `--display`
and `--sound` then override what the file says. What the original showed
depends on the machine's adapter, not on `KRYNN.CFG`, as no mode is set
yet: on a CGA or a Tandy 1000, 80×25 at 640×200 in the ROM's 8×8
characters, the cursor on scan lines 6 and 7; on an EGA 640×350 in 8×14
cells; on a VGA 720×400 in 9×16. The port shows the CGA's and the Tandy's:
640×200 in 8×8 cells, light grey on black, with the cursor's two bottom
lines, as the setup runs before the port knows the display (it is what the
setup asks) and 8×8 is the mode of the two adapters the game draws its
four colours on. Its characters are a PC-style 8×8 font of all 256 of code
page 437, the ROM font's place (the port does not copy IBM's ROM, nor
font8x8, whose ASCII is IBM's CGA font): unscii 2.1's `unscii-8` by
viznut (<http://viznut.fi/unscii/>), in the Public Domain ("Everything
made by me (viznut) is in the Public Domain", its `unscii.txt`), its own
2-pixel-line design after several 8-bit system fonts; each byte as its
Unicode meaning, 0x01-0x1f and 0x7f as the PC's pictures (smileys, suits,
arrows, ⌂). Four that unscii lacks are drawn by the port in its style:
☼ (0x0f), ⌂ (0x7f, from Ctrl-Backspace), ⌐ (0xa9) and ∙ (0xf9); 0x00,
0x20 and 0xff are blank. Lower case shows as typed, as do the symbols,
accented letters, box drawing and blocks. The table is in `src/crt.c`
(`cok_crt_glyph`); `tools/pcfont.py unscii-8.hex` prints it. The game's
own font (`8X8D1.DAX`) draws everything after the setup. `krynn` shows it
in the window (F12 saves it, 640×200), `krynn --log` and `eclplay` log each
row as the cursor leaves it (`crt: `), and `eclplay --shots` saves it at
each key read. The game's directory is taken as `C:\COK\`, where the
assets' own `KRYNN.CFG` says the game was installed (its save path
`C:\COK\SAVE\`), so the default path is `C:\COK\SAVE`. The sound's time
starts after the note: `eclplay --wav` renders nothing for its 2 seconds.
The note's `Delay(2000)` is the game's first wait, so a key script's group
typed ahead at the start (`\[...\]`) is typed there and waits in the buffer
until the title's flush drops it, as keys typed during the note did, and
`\wN.` counts it as a wait.

Where the port keeps the file: the original reads and writes it in its own
directory, which the port splits into the assets (read only, the original's
files) and `--saves` (the port's own). It reads `--config FILE` if given;
else `KRYNN.CFG` (or `krynn.cfg`) in the saves directory; else the one in
the assets directory, as a copy of the original's directory has it
(`COKAssets/KRYNN.CFG` says `E`, `T`, `M`, `C:\COK\SAVE\`, `F` and 14 zeros).
A file found is a regular file, as `FindFirst` finds one: a directory of
that name counts as missing (the setup then fails to make it, as the
original's `Rewrite` does), and a file the port cannot read stops the run,
never made again. Without any, the setup writes `--config FILE` or
`SAVES/KRYNN.CFG`, which later runs then read; the port never writes into
the assets directory, which may be the repository's `COKAssets/` or a
read-only copy of the game, not even with `--saves` naming it (the setup
then writes nowhere). So the saves directory's copy, once written (by the
setup, or by Initialize Mouse/Joystick, which copies the assets' with its
third line changed), shadows the assets': later edits of the assets' file
are not read, and one saves directory used with several copies of the
assets gives them all its file. `eclplay` without `--saves` or `--config`
writes it nowhere, saying so, and plays on with the answers. Where the
original makes the file as the setup starts and fills it as it ends, so
that a setup cut short leaves an empty `krynn.cfg` (read next time as CGA
with no sound), the port makes it at the start too, to say at once that it
cannot, but removes it again if the run ends in the setup (only a file it
made: what was there before is left). A file it cannot make shows the
original's message on the text screen; then, there as for a file it cannot
read or write, a blank row and `The port stops here: ` with the reason,
also on standard error; the run ends with status 1 (`krynn` waits for a key
or the window's close first). A run whose keys end in the setup ends `(out
of keys in the setup)`.

Death Knights' setup (dkk `1187:011b`, without `DKK.CFG`; the rules'
`setup`) is Champions' with its own words and five screens: `GRAPHICS
ADAPTER TYPE: [1] CGA  [2] EGA  [3] Tandy  ` (one space after the colon,
`DS:2744`), whose `[3]` sets `DS:8b8f`; five sound lines (`[1] C/MS`,
`[2] AdLib`, `[3] Tandy 1000`, `[4] PC`, `[5] Silent`, `DS:2795`-`2811`)
over digits 1-6, writing `C`, `A`, `T`, `P`, `S`, and for `6` no line at
all (`1187:03db`'s cases end at 5), so that the file's later lines move up
one; `ALTERNATE INPUT TYPE: [1] Joystick  [2] Mouse  [3] Neither  `
(`DS:2830`), where `[2]` writes `N` after a Tandy (`1187:0449`); the save
path as Champions' (its default `C:\DKK\SAVE`); then, after `ClrScr`, `PATH
TO SAVE DATA FOR CHAMPIONS OF KRYNN` (`DS:28e0`) and `(DEFAULT - ` with the
save path's drive letter and `:\SAVE` below C or `:\KRYNN\SAVE`
(`DS:28c0`, `28cb`), read and checked as the first, the fifth line. The
calibration (`18bc:01e1`) is Champions' with Initialize Mouse/Joystick's
words (`DS:1b2e` and `1b57` joined by a space, `1b9f` and `1bc8`), every
failure `unable to read joystick` (`DS:1b80`); the note is `Tandy 1000 users
make sure num lock is on.` (`DS:2931`). Startup's read (`1187:08a1`) takes
no `R` for the sound (`1187:0a2c`: none). Its input flags, the joystick
`DS:dbf2` and the mouse `DS:dbaa`, lie past the image and are set only by
a third line of `J`, `M` or `N` (the setup sets the joystick's too); with
another letter (a hand-made file, or the sound's `6`, which leaves the save
path's drive on the third line) the mouse's is never set and the game's
start reads it (`4f9a:0123`): the port stops after the note, saying so
(`COK_SETUP_UNSET`; a drive `J:`, `M:` or `N:` is read as that input, as
the original reads it). `tools/dosbox/checks/dkk_setup.py` compares every
key read's text screen, the note and `DKK.CFG` with the original's, on a
Tandy and with the sound's `6` on an EGA.

`tests/test_setup.c` replays 96 setups (the digits with noise around them,
extended keys, paths typed with every editing key, long lines, floppies,
long directories, each kind of path refused or taken on each kind of drive,
erasing back over a wrapped line, more keys than the line holds, input
ending early, the file that cannot be made, the file found) against what
the original's `11c7:031a`, `Crt` and `System` left in an 8086 emulator,
with BIOS text output, the keyboard and DOS's files modelled: the text
screen and cursor at each key read and each delay, how it ended and the
file written. 456 such cases agreed; every instruction of `11c7:0020` and
of `11c7:031a` up to the file's reading ran, all of the line editor but
Ctrl-Z with `CheckEOF`, and of `Crt`'s output all but the BIOS path
(`DirectVideo` is on), scrolling (the setup never reaches the last row:
`tests/test_setup.c` checks the port's), the bell and CheckSnow's waits. It
also checks the file's lines read, where the file is found and written, the
rewrite, the timeout key (`\t`) waiting on, the text screen's cells as it
scrolls, its rows logged as the setup writes them, its drawing, and its
font (the table's digest, glyphs of its own for every character but the
blanks, lower case apart from capitals, box drawing to the cell's edges);
`tests/test_eclplay.py` and `tests/test_krynn.py` play the setup from the
title's start.

## The mouse and the joystick

`KRYNN.CFG`'s third line picks the alternate input (see Startup and the
setup): `M` the mouse (`DS:8986`), `J` the joystick (`DS:89ce`), `N`
neither. Startup then looks for them: the joystick's unit (`177c`) reads
the stick and turns it off if it does not answer (`177c:0000` at
`11c7:0b16`), taking its fourteen thresholds from the file only then; main's
startup (`3e99:0085`) resets the mouse's driver (INT 33h through the unit
`1743`), off if none answers, and with one sets its cursor (`1743:0107`: the
arrow at `DS:42b4`, one bit a pixel, or in CGA mode `DS:42f4`, two bits a
pixel; the hot spot at its tip), moves the pointer to x 639 and reads it
back to learn whether the driver counts x in pixels (`DS:8984`: a driver of
the 320×200 modes counts 0-639, two to a pixel, which the unit halves and
doubles), shows the cursor, and puts the pointer at the screen's middle.
`src/input.c` ports both units and models the machine under them without
SDL: the mouse driver's pointer (virtual x 0-639, y 0-199), buttons, show
counter and cursor, and the game port's reads of the stick; `src/devices.c`
is the game's side.

| | The original | The port |
| --- | --- | --- |
| a key (`1614:025b`) | with either on and no key waiting, polls them until a key or: the mouse's left button Enter, the right Escape, the last key a click (`DS:8987`) at the pointer (`DS:8989`, `898b`, pixels; `1614:01ad`); the stick pushed its keypad digit (`DS:4358`), else button 1 Enter, button 2 Escape (`1614:00fa`); a key typed meanwhile is read in its place, the click kept; in the demonstration it waits for them rather than give 0 | the front end's read gives `COK_KEY_DEVICE` while the game waits on them (`game->device_wait`) and `cok_adventure_device_ready` says they give a key; `cok_adventure_device_key` makes it. Both buttons (or the middle) leave the key unset: from a menu, or wherever `03c2` polled just before (`1614:0479`, the computer's turn), it holds `BH` of the mouse's state from `03c2`'s frame, 0 (`67b5:05ac`/`05b8`, `3afb:120a`/`1216`, `4def:2a69`/`2a72`): a menu takes it as the extended key's prefix, reads a second key as its scan code (`05c6`, 0 again while both are held) and gives scan code 0 after the release wait; paging (`1521:09ac`), the disk prompts (`169c:0136`, `0201`), `69ea:1109`, `1394` and `6e22:016b`, `022f` throw the key away; only the digit read at `58e7:02f7` gets a byte unknown (SS's low byte, from `0a46:131f`). The port gives 0 everywhere |
| KeyPressed (`1614:03c2`) | a key, a mouse button down (the position kept), the stick's button or push | `cok_adventure_key_pending` |
| a menu (`67b5:03e2`) | Enter from a click goes to its place's cell (x / 8, y / 8): the party list (`065c`, with `DS:8985`, outside the overland map without the list, Move mode and combat: the member on the row is selected, `DS:6096`, or picked, `DS:6e09` with `DS:6e08`, as the special key Up; a row past the party takes the click and gives nothing); in combat during Move (`DS:71a6`) or Manual's cursor (`DS:71a5`), cells 1-21: a step from the mover or the cursor toward the 24-pixel cell clicked (`1743:0167`, below, from `col / 3 + 1`); in Move mode (`DS:8858`): the 3D view's cells 3-13 forward, turn left, right or about (`DS:1ff5`), or the overland map's cells 1-38 by 1-15 a step from the party toward the cell; row 24: the item clicked, as Enter on it, selected; Modify's sheet (`DS:7141`): Home, the field clicked kept (`DS:6e35`); a list's columns (`DS:6e2f`, `6e30`): Enter if the row is at or below one of the list's rows (`DS:6e16`); else nothing, and it reads on. Then it flushes the keyboard and waits for the mouse's buttons to come up after a click, or the joystick's after any key (`67b5:0c2d`) | `menu.c` (row 24, the list's columns), the game's regions in `devices.c` (`cok_pointer.click`), the wait `cok_pointer.release` |
| a list (`67b5:1368`) | a click on a row shown picks it out (a heading too); Select on row 24 takes it | `cok_menu_rows` |
| the special keys (`546c:3334`) | do nothing after a click, which picked already | `cok_adventure_special` |
| Pick Character (`546c:36cc`), the picker (`6346:32c7`) | the party list takes clicks while they ask (`DS:8985` set; the picker's only in the start menu, mode 0); Up from a click takes the member clicked | `cok_start_pick`, `pick_character` |
| `DS:8985` | set at startup with `M`, at a new game (`3e99:0933`), turning the mouse on, leaving the start menu by Begin; cleared entering the start menu | `game->mouse.party_clicks` |
| the cursor (`1743:0030`, `0051`) | the driver draws it in video memory while its show counter is 0: hidden through the title's waits (`2f55:0651` to `006d`) and through running text and its paging (`1521:04ac`); every other routine hides it around its drawing and shows it again before any wait | the counter kept where it matters: startup, the title, running text (`cok_adventure_wrap`), the drift below, Initialize Mouse/Joystick; the cursor composed over the screen as the window or `--shots` shows it (`cok_mouse_draw`): each pixel ANDed with the screen mask's bit and XORed with the cursor mask's, 15 for a bit in the 16-colour modes, CGA's two-bit pairs in CGA mode |
| Initialize Mouse/Joystick (`4def:5cc7`) | see [The start menu](start-menu.md#the-start-menu) | `cok_start_mouse` |
| the joystick's calibration (`177c:029d` in the setup, `4def:638d` in Initialize Mouse/Joystick) | the centre the mean of 20 reads; then each corner's prompt, up to 20000 polls for button 1 (`Couldn't read the joystick.  Aborting calibration.` or `Unable to read joystick` and 2 seconds), the least (upper left) or most counts of 20 reads with it held, the thresholds a third and two thirds of the way, all in 16 bits; a wait for the button to come up between | `cok_stick_calibrate` |

The direction of a click (`1743:0167`): from cell (fx, fy) to (tx, ty),
with dx and dy the distances, the first of up (ty ≤ fy and 0x26a × dx / 256 ≤
dy, tan 67.5°), up-right (ty < fy, tx > fx, between), right (tx ≥ fx and
0x6a × dx / 256 ≥ dy, tan 22.5°), down-right, down, down-left, left and
up-left, so ties go to the axes and the same cell is up; the products in 16
bits, so past about 300 cells across none holds and the original loops for
ever (the port gives 0; only a corrupt var 0x4bc3 gets there). The stick's
direction (`177c:003b`): its counts against the thresholds (the last of the
1st to 5th below the count, unsigned), and only the outer bands, past two
thirds of the way to an edge, push (`DS:4334`).

The quirks kept: a click in the title menu's column 0 gives Enter (the
list's columns `DS:6e2f`, `6e30` and rows are 0 before any list: they are
uninitialised memory, taken as the 0s DOSBox gives; and the party list's
rectangle `DS:6e31`-`6e34`, never cleared, likewise); the party list keeps
taking clicks over a list drawn on it (a shop's), as Up; Modify's sheet
gives Home for a click on a field's row beside the field, which goes back to
the field clicked last; a click on a row of the view or map outside Move
mode does nothing; the list takes a click below its rows from the menu as
Enter and then does nothing; text input drawing or erasing at column 40,
which ECL's INPUT STRING reaches with its 40 characters (`2fd3:0a57`),
hides the cursor for good (`1521:0353` and `030a` hide it and leave
without the show past column 39), until the mouse is turned off and on.
Not reproduced: where the original draws under a shown cursor, the
overland map's mark after a picture, a battle or a camp (`4877:0005` from
`2fd3:096c`, `1c5a`, `346a`, unlike the step's `475c:08d5`), two
artifacts: it saves the cell under it with the cursor's image in it
(`4877:0026`, `127f:0edb`), which the next step paints back; and the cell
it puts (`4877:008b`, `127f:10e7`) covers the arrow, so when the pointer
next moves the driver restores the background it saved before the mark,
and the part of the mark under the arrow's 16×16 box vanishes until the
next redraw. The port's cursor is never in the screen. In DOSBox-X with
its own driver (`f_mouse.py`) the second was seen after a camp (25 pixels
of the mark gone where the arrow had been); the first, the arrow's image
painted back after a step, was not. A read of the stick
that fails as the calibration's first of the centre's twenty adds the
words at `[bp-7]`, `[bp-9]`, which the probe did not write (it reads into
`177c:0000`'s own locals): what the caller's previous call left there,
fixed by the code's layout (`0a46:084a`, the WriteLn at `11c7:0840`, for
`029d`; `0a46:056c`, the Rewrite at `4def:60aa`, for `638d`), not computed
here; the port adds 0.

The machine: `krynn` always has a mouse driver (SDL's pointer) and has the
first joystick SDL finds; `eclplay` has neither unless `--mouse` and
`--joystick` give them, so its transcripts with the assets' `M` are those
of a machine without a mouse. Both take `--input mouse`, `joystick` or
`none` in place of the file's third line (`none` is `N`; `M` without a
driver ends the same); `--input joystick` over a file without `J` calibrates
the stick at startup (see the options). In `krynn` the pointer follows the
window's: its place over the screen as shown, letterboxed and at any scale,
each window pixel taken at its centre, as the driver's x (0-639) and y; the game's moves of the pointer (startup's) move the
window's where it is over it; the window's own cursor is hidden while the
game's mouse is on. The stick's axes give counts 1-256 (the original's
depend on the machine's speed, which is why it calibrates), its first two
buttons buttons 1 and 2; a read of it is instant, so `krynn` gives the
calibration's polls 20 ms each (20000 of them: about 400 seconds) and a
pushed stick a key every 60 ms, about the original's pace on a 286. Keys
for scripts (`--keys`, both): `\mX,Y,B.` puts the pointer on pixel X, Y and
presses buttons B (1 left, 2 right, 3 both; `,B` left out for a move
alone), as the game reads its next key, the buttons coming up once the game
has read the click and waits or reads again; `\jX,Y,B.` puts the stick at
counts X, Y with buttons B (1 button 1, 2 button 2) until the next `\j`;
where the game polls them in a loop (the menus' wait for the buttons, the
calibration) the next `\j` or move comes, else the stick's buttons come up,
and with nothing to change the keys' end ends the run. `krynn` presses them
through SDL's mouse events and a virtual joystick (attached at the start
when the keys have `\j`), so the same keys give the same frames.

`tests/test_input.c` replays 150 random clicks of 67b5:03e2's and 740 on
the edges of its regions (the key, the extended flag, the selection,
Modify's field, the selected character and the pick), 1743:0167
from (0, 0) to every cell within 30, and 150 of the joystick unit's reads
and both calibrations (prompts, thresholds, reads, failures) against what
the original gave in an 8086 emulator, the menu's routine run whole with
the keyboard and drawing stubbed and the game's state laid out in memory,
`177c:060d` stubbed to give the stick's reads. Of 3,000 random clicks and
880 on the regions' edges all agreed, every instruction of the click's code (`67b5:065c-0a88`) run but
row 24's after a timeout in the same pass (the port's timeout is its own
read); of 50,625 directions, all; of 2,000 joystick cases, all, every
instruction of `177c:0000`, `003b` and the calibrations run but their outer
loops' repeats (never taken) and stick 2's buttons (no caller). It also
checks the cursor's drawing, the units, the keys they give, the cursor's
hiding around text and INPUT STRING's drift, a list's clicks, the file
Initialize Mouse/Joystick writes and the setup's calibration;
`tests/test_eclplay.py` plays the menus, Modify, a walk in Throtl, the
overland map and a battle's Move and aiming by clicks against the keys that
do the same, frame for frame, the party list, the title's hidden cursor, the
mouse off and on, the joystick's keys and both calibrations; and
`tests/test_krynn.py` the clicks and the joystick through SDL's events frame
for frame as `eclplay` draws them, at two scales.

### Death Knights' mouse

Death Knights' units are Champions' (`1882` is `1743` with the INT 33h
function numbers read from a table of the same values, `DS:838c`; its
cursors `DS:83ee`, `83ae` are Champions' bytes; `18bc` is `177c`; the
device part of `16ab:01d4` is `1614:025b`'s), startup's part is
`3e99:005b`'s (`4f9a:00e1`, which also clears the party click's member,
`DS:a2b6`), and the cursor is hidden and shown at the same places. What
differs:

- `DKK.CFG` (`1187:011b`) has two paths after the third line, its save
  path (`DS:98a4`) and Champions' saved games' (`DS:99a4`), then `F` and
  the stick's thresholds: the `files` section's `config_paths`
  (`cok_config_parse_game`; Champions' 1). The setup Death Knights runs
  without the file differs too (a Tandy display makes `[2] MOUSE` write
  `N`, `DS:8b8f`, and it asks for Champions' path) and is not ported: the
  port's is Champions' but for the file's layout: it writes the
  `config_paths` paths, the others' lines empty, and reads them back so.
- The start menu clears the party list's clicks again after a Drop or
  Remove that ends the game (`49cb:010b`, after `4f9a:08ad` set them).
- Its menus by index (`6a69:0f1b`) take a click as 67b5:03e2 does, region
  by region (`11fd-12c7`, `cok_menu_pick` with `cok_pointer.click_indexed`,
  `devices.c`), but: the party list (`0490`, `0513`) is off in Move mode
  only in a 3D area's mode (4), and a click on a member keeps it
  (`DS:a2b6`) for the special key's routine to select (`580d:2bb2`, once)
  or, while a pick picks into its own (`DS:c0f0`, `c0f1`), picks it there;
  a click on the one selected (or picked) is Enter on the menu's selected
  item. The view in Move mode (`07d3`) takes clicks only in mode 4 (the
  overland map's, `085d`, as Champions'). The menu's own items are on its
  rows, 23 and 24 for two (`0940`: row 24's counted on past the `@`,
  row 23's only before it). A click on none of them: on the battlefield
  (mode 5, rows 0-22) the hotkey `T` on a menu of two rows and `M` on
  aiming's (`aim: `), else read on; elsewhere read on, but in a menu of
  one item done with Enter's 0x0d, which no item has: index 0xff
  (`12af`). A click on an item not selected, or on a list's columns, sets
  `DS:c0f9`.
- Its lists (`6a69:1b82`) take a click as a pick only where it set no
  `DS:c0f9`: a click on a row picks it out and asks again, a click on the
  row picked picks it; a click on row 24's item not selected only selects
  it. A click off the list and the menu reads on in Initialize
  Mouse/Joystick (`Select Exit`); in the start menu, whose list has
  `Select` alone and no Exit, it is 0xff, past Select, which `1ea6`
  compares with Exit's index, a stack byte (`[bp-0x242]`) only written
  when Exit is shown: the port stops there (`COK_MENU_UNSET_EXIT`).
- The pick (`65cd:33c9`, WHO's and Trade's) has Move off while it asks
  (`DS:db7c`), the party list's clicks on in mode 0, and takes Up after a
  click as the click's member: the pick's own, or `DS:a2b6`, which it also
  selects (not clearing it). A member a click kept that the party no
  longer has stops the run (the original selects the freed record).

The menus Death Knights was found to read by Champions' loop are read
by index now (the last, the spell lists and the ECL's VERTICAL MENU,
HORIZONTAL MENU, PRESS KEY, YES NO and the results' key: `dkk-menus3`),
and take Death Knights' clicks.
`tests/test_input.c` checks Death Knights' clicks (row 24's and two rows'
items, the prompt, nowhere, the battlefield, the party list and the pick
into, Move mode, Modify's sheet, Pick Character, a list's rows and row
24), `tests/test_start.c` Initialize Mouse/Joystick's file,
`tests/test_config.c` `DKK.CFG`'s thresholds, `tests/test_death_knights.c`
the words and the code's constants against the executable, and
`tests/test_death_knights.py` Initialize Mouse/Joystick and Pick Character
by clicks. `tools/dosbox/checks/dkk_mouse.py` (`make dkk-checks`)
clicks the same in the original with DOSBox-X's own driver, as
`f_mouse.py` does: the start menu's rows (picked out, then picked),
Initialize Mouse/Joystick's mouse row and Exit, Pick Character's party
list, camp's and Alter's items on row 24, each screen whole with the
arrow, and the files it leaves.

## Display modes

Startup (`11c7:031a`) takes the display from `KRYNN.CFG`'s first line
(see [Sound](sound.md#sound)'s [Devices](sound.md#devices)): `E` sets `DS:4b76` to 1, EGA, video mode 0xd
(`127f:00cf`), with the shown and drawn pages 0 (`DS:4b72`, `4b73`,
`1521:0071`, `00a0`); `T` 2, the Tandy's mode 9, after lowering the
heap's end by 16 KB for its video memory (`DS:438e`, the old end kept in
`DS:483e`, never read); anything else 0, CGA, mode 4, 320×200 in four
colours. The setup the original shows without the file offers `[1] CGA
[2] EGA [3] Tandy` (see Startup and the setup). The port reads it with the
sound device (`cok_config_parse`): the assets' `E` plays EGA; `--display
ega`, `tandy` or `cga` in `krynn` and `eclplay` override it, and `krynn --log`
prints `display: EGA`.
`cok_adventure_open_display` opens the game in a mode (`game->display`,
`DS:4b76`; `cok_adventure_open` in the Tandy's).

The original reads the mode in these places (a scan of `START_FULL.EXE`
for `DS:4b76`, and the tables and variables that go with it):

| Where | Tandy | EGA | CGA |
| --- | --- | --- | --- |
| `127f:1c2f` pictures | 4 bytes a unit and row, packed nibbles | 4 plane bytes a unit | 2 bytes, 2 bits a pixel |
| `127f:02a5` load | the record's nibbles; transparent ones masked and 0 | planes, the same pixels | each colour's CGA colour from the record's map (header bytes 9-16, nibble n for colour n, `127f:007b`, `DS:1e0e`); the mask from the colour |
| `127f:08d7`, `09c3` draws (`2678`/`270d`, `27af`; `23d1`/`2460`, `24fd`; `2151`/`21d8`, `226c`) | | the same arithmetic scaled: the same pixels | the same, on 2-bit pixels |
| `127f:0b94` mirror (`0b63`, `0a51`, `0ad6`) | nibbles swapped | plane bits reversed | bit pairs reversed |
| `127f:0edb`, `10e7` save and put | | through the planes | |
| `127f:12e8` the view to the screen | | onto page A200, then latched to A000 | |
| `127f:15fd` recolour | nibbles | planes | CGA colours through the picture's map |
| `127f:1d71` fill | colour × 0x11 | planes | the colour's CGA colour in the map passed, always `DS:1e1e`: 0-15 to 0 0 1 1 2 2 1 2 0 1 2 1 3 0 3 3 |
| `1521:01df` text (`0e1d`, `0dbd`, `0d1e`) | | write mode 2 | colours through `DS:1de9`: 0 0 0 1 2 2 2 3 0 1 1 1 2 2 3 3 |
| `1521:0a34`, `0b60`, `1128:07e6` clears | four banks | pages 0 and 1 | two banks |
| `3e99:0731` startup | `SKY.DAX` 250-252 loaded | the same | none |
| `69ea:0184` backdrop | 44 rows of sky, 2 of `DS:6d82` (0), 42 of `6d83` (8); the sun, the strip, the horizon | the same | 43 rows of sky, 1 of `DS:6d81`, 2 of `6d82`, 42 of `6d83`, through `DS:1e1e`; no pictures (Death Knights' `6d21:0177`: as in the other modes, the view's `cga_backdrop`) |
| `6945:012c` after a step | `DS:6d83` 8 | the same | where `DS:884f` is set (a block entered, 0xc059 or 0xc05f written): `6d81` 0 for 0xc05f 8 or 0x41, else 15; `6d82` 8 for 8, else 7; `6d83` 8 for 0xc059 0x0b, 1 for 6, else 6 (`6945:0014`, `0048`, `007c`) |
| `6d21:0408`, `0478` icons; `4b6d:08e0`, `0938` characters' | colour 8 drawn black (`DS:1e26` to `1e36`) | the same | not |
| `6961:0497` sprites | colour 13 drawn black (`DS:0dd4` to `0de4`) | the same | not |
| `4def:3d46` the icon editor's icons | on colour 0, COMSPR 25's frame behind | the same | on colour 3 (cyan), no frame |
| `4def:4276`, `42d0` its 1st-color, 2nd-color | their menu | the same | `Sorry, not in CGA` in yellow (white) on row 24 for speed × 100 ms (`1521:0af1`); the menu stays |
| `3e99:00ac`, `4def:5edb` the mouse's cursor | `DS:42b4` | the same | `DS:42f4`, two bits a pixel (see The mouse and the joystick) |
| `169c:0000` BIOS text | | | the colour through `DS:1de9`; no caller |

No file or record is the CGA's alone: every mode loads the same (`CPIC`,
`CBODY` and `CHEAD` are the combat icons of all), and every record's map
holds CGA colours 0-3 (`SKY.DAX`'s are all 0). No code sets a palette (no
INT 10h AH=0Bh or 10h, no write to port 0x3D9 or the attribute
controller), so CGA shows the BIOS's mode 4: palette 1 with intensity,
black, light cyan, light magenta and white (`cok_cga_palette`); EGA the
16 colours of mode 0xd, the Tandy's.

EGA looks as the Tandy: its routines give the Tandy's 16-colour pixels for
everything the game passes. They differ only past that: colours above 15
(the EGA takes their low four bits, as the port does in every mode),
recolouring's random flag (two `Random(256)` a unit; no caller passes it),
fills 128 units wide or more (a signed byte counts them), and a row of the
view marked with nothing drawn (a draw of no width on a clean row), where
`12e8` copies 16 KB of the page A200 to the screen. The game passes none of
the first three; of the last, a probe of 68 `eclplay` runs (the Makefile's,
these notes', the start menu's) met no draw of no width. The port draws EGA
as the Tandy; its game-level code reads only CGA against the others. What
differs is two stack leftovers (below).

In CGA mode the port keeps its screen of nibbles, each a CGA colour 0-3:
a picture made or loaded for it (`cok_picture.cga`) holds them, so draws,
mirrors, saves and the icons' composition (`4b6d:0784`) are the Tandy's
routines on 2-bit pixels, while loading (`cok_picture_load_cga`), fills,
text and recolouring map colours as the table says. Recolouring is lossy:
it matches the CGA colour, so it turns every colour that shares it, and
transparent pixels where the map gives 0 (a masked draw then ORs the new
colour into what lies under them, as the original does). A map value
above 3 ORs bytes of the tables after `DS:1e0e` into its byte's four
pixels, as `127f:02a5` and `15fd` do; no record has one. A CGA picture
shows in `cok_cga_palette` (`cok_image.palette`), in `eclplay`'s
`--shots` and `--screen` and in `krynn`'s window and screenshots. A value
above 3 on CGA mode's screen can only come from a picture drawn on it
without CGA's colours, a bug the palette would hide: `eclplay` stops there
when it saves the screen, and `krynn --shots` fails
(`cok_picture_cga_valid`).

Two stack leftovers depend on the blit routine (see [Spells in combat](combat-spells.md#spells-in-combat)):
Fear aimed at its caster's own cell reads the locals the clipped blit of
its icon left (`drawing_locals`); EGA's (`24fd`) are the Tandy's but for
the unit's x, which no cone reads; CGA's (`226c`) halve every byte count
(a unit is 2 bytes): 6 (6 - x), 6, 144, 1008 (y + 1), 6 x. A breath at a
target drawn mirrored reads what `127f:0b94` left (`drawn_bytes`): in
EGA mode the bit-reversed mask byte of the 8 leftmost pixels of the
icon's bottom row, twice (0, sight to one cell, when all 8 are opaque,
not the Tandy's 2); in CGA mode the count of its last `Move`, 2 (sight to
3 cells), and as the side the low byte of the offset of the mirror's mask
in the heap, not known: the run stops if its 256 values give different
targets. Both were compared with the three routines run in the emulator.
Memory is not modelled in any mode: CGA's pictures take half the memory
and the Tandy's heap is 16 KB smaller, so where `6961:00e4` stops loading
a small picture's frames (`MaxAvail` under 0xf3c) differs by mode; the
port loads them all. In DOSBox-X (`f_frames.py`, the guards' close-up at
Throtl's gate, PIC1 record 12's four frames, free memory cut by
`HOOK.COM`'s pad): with `MaxAvail` 40,000 or more before, all four in
Tandy mode (the run at 32,000 failed), 32,000 or more in EGA and CGA mode; about 25,300, two in Tandy and EGA mode, four in CGA's;
about 18,300, three in CGA's, and in Tandy and EGA mode no close-up but
`WARNING: Insufficient Memory` on row 24 in yellow and a wait for a key:
Turbo Pascal's `HeapError` (`DS:439a`, set by `2f48:00b2`) is
`2f48:0042`, which shows that (`1521:096c`) and makes `GetMem` return nil
when `DS:4b27` is set, else prints `Insufficient Memory. Exiting
Program`. About 10,000 reached no wait. The port has neither message.

`tests/test_display.c` replays each screen type's drawing (the title, big
and small pictures, sprites, portraits, the 3D view and its backdrop, the
overhead map, combat with mirrored icons, menus in every colour pair, the
icon editor, recolouring, edges) on the port's routines in each mode and
compares the screen, the view buffer, pictures and masks, 714 in all, with
what the original's `127f` and `1521` left in an 8086 emulator (its EGA's
planes, map mask and graphics controller modelled; the scripts and digests
are in `tests/test_display_data.h`). There every instruction of the
unclipped blits (`2151` to `270d`), the glyphs' (`0d1e`, `0dbd`, `0e1d`)
and the mirrors' helpers ran, nine tenths of the clipped blits' (`226c`,
`24fd`, `27af`: not their rejections of a picture wholly off the buffer),
and each mode's branch of `02a5`, `0b94`, `0edb`, `10e7`, `12e8`, `15fd`
and `1d71`. `tests/test_eclplay.py` plays the new game in each mode: EGA's
and the assets' are the Tandy's frame for frame, CGA's the same lines in
four colours, its menus' text through `DS:1de9`, its 3D view, its icon
editor; `tests/test_krynn.py` that `krynn --display cga` shows `eclplay`'s
frames. `tests/test_picture.c`, `test_text.c` and `test_view.c` check the
CGA mappings and backdrop; `test_adventure.c` and `test_arena.c` the game's
CGA paths (no sky, sprites' and icons' colours, the backdrop's colours by
0xc05f and 0xc059); `test_cloud.c` and `test_spell.c` the leftovers in each
mode; `test_config.c` `KRYNN.CFG`.
