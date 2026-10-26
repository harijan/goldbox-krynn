# Champions of Krynn for Linux

A native port of SSI's 1990 *Champions of Krynn*, the first of the
Dragonlance "Gold Box" role-playing games, rebuilt in C from the original
DOS game. It plays the whole game in a window: the title and its music,
creating a party, the overland map, the 3D towns and dungeons, camp,
shops, temples and the turn-based battles. It plays the same way the
original did, down to its quirks.

You need your own copy of the original DOS game. Its files are not
included here.

> **Disclaimer:** This project was made almost entirely by AI. Nearly all
> of its code, tests and documentation, this README included, were
> written by an AI coding assistant, with a person directing the work
> and checking the results.

- [What you need](#what-you-need)
- [Building](#building)
- [Playing](#playing)
- [Controls](#controls)
- [Saved games](#saved-games)
- [Death Knights of Krynn](#death-knights-of-krynn)
- [Options](#options)
- [Troubleshooting](#troubleshooting)
- [How faithful is it?](#how-faithful-is-it)
- [For developers](#for-developers)

## What you need

- **The game's files.** Copy everything from your DOS *Champions of
  Krynn* directory (the `.DAX` files, `ITEMS` and the rest) into a folder
  named `COKAssets` in this project. Keep their names in capitals, as
  shipped. Any saved games you have from the original can go in `SAVE`
  (see [Saved games](#saved-games)).
- **A C compiler, Make and SDL2** (2.0.18 or later), found through
  `pkg-config`:

  | System | Install |
  | --- | --- |
  | Debian, Ubuntu | `sudo apt install build-essential pkg-config libsdl2-dev python3` |
  | Fedora | `sudo dnf install gcc make pkgconf-pkg-config SDL2-devel python3` |
  | macOS (Homebrew) | `brew install sdl2 pkg-config` |

  Python 3 is only needed for the tests.

## Building

From the project directory:

```sh
make krynn
```

That builds the game as `build/krynn`. Plain `make` builds the command-line
tools as well, and `make test` runs the test suite.

## Playing

```sh
./build/krynn
```

Run it from the project directory, so it finds `COKAssets`.

**The first time**, the original's setup screen asks four questions,
answered with a number key:

1. **Graphics:** `2` EGA (16 colours) is the usual choice. `3` Tandy looks
   the same, and `1` CGA has four colours.
2. **Sound:** `4` Tandy (three voices, with the title music), `3` AdLib
   (the title music on the AdLib's FM chip, the effects on the PC
   speaker, as the original plays them) or `5` PC speaker. The CMS and
   Roland choices give PC-speaker effects with no music (see [How
   faithful is it?](#how-faithful-is-it)). `6` is silent.
3. **Input:** `2` mouse, `1` joystick or `3` keyboard only.
4. **Path to save data:** just press Enter. The port always saves in
   `saves/`.

Your answers go into `saves/KRYNN.CFG`. To be asked again, delete that
file. If your `COKAssets` folder already has a `KRYNN.CFG` from the
original installation, the game uses it and skips the setup. Any of
these choices can also be changed for one run with an option (see
[Options](#options)).

Closing the window quits at once. Save first from camp (Encamp, then
Save).

## Controls

The game reads the keyboard just as the DOS version did. Every menu
shows its choices along the bottom of the screen.

| Key | Does |
| --- | --- |
| Highlighted letter | picks that menu item |
| Left, Right arrows | move along the menu |
| Up, Down arrows | pick a character or a list entry |
| Move, then arrows | in towns and dungeons: Up steps forward, Left and Right turn, Down turns around; Exit goes back to the commands |
| Enter | picks the highlighted item |
| Escape | Exit, cancel |
| Keypad 1-9 (or arrows, Home, End, PgUp, PgDn) | directions in combat and on the overland map, diagonals included |
| PgUp, PgDn | page through long lists |
| Space | in combat, take back characters that were on Auto |
| Alt-Q or Alt-A | in combat, put the party on Auto (Quick) |
| Alt-M | in combat, let Auto cast spells or not ("Magic On/Off") |
| Ctrl-S | sound off and on |
| F11 or Alt-Enter | full screen |
| F12 | save a screenshot as `screenshots/krynnNNN.bmp` |

**Mouse:** if you chose the mouse, the left button is Enter and the right
is Escape. Clicking a menu item, a character in the party list or a place
on the map acts on it. **Joystick:** pushing it gives a direction, the
first button is Enter and the second is Escape.

How fast text scrolls and how long messages stay up is set in camp with
Alter, then Speed.

## Saved games

The roster of characters and your saved games (A to J) are kept in
`saves/`, which is created on the first run.

To carry on a game saved by the DOS version, copy its save files
(`SAVGAM*.DAT` and the `CHRDATA*` files that go with them) into `saves/`,
then pick Load Saved Game from the start menu:

```sh
mkdir -p saves && cp SAVE/* saves/
```

The port reads and writes the same file format as the original, so games
also go back the other way.

## Death Knights of Krynn

The port also plays the sequel, *Death Knights of Krynn* (1991), so far
into its fights, which you fight from the combat menu or leave to the
computer. Copy your DOS *Death Knights* files into a folder named
`DKKAssets` and run:

```sh
./build/krynn --game death-knights
```

### Starting out

From the title you reach the start menu, where you can:

- **Load a saved game:** copy its `SAVGAM*.PTY` and `CHRDAT*` files, and
  the bank vault's `VAULT*.DAT`, into `saves/`.
- **Save** a game.
- **View, add, remove and drop** characters.
- **Begin** the adventure.

**Bringing over a Champions party:** as in the original, Add Character
to Party and Load Saved Game can bring over a party from *Champions of
Krynn*. The port looks for Champions' roster and saved games in `saves/`
too, or in the folder that `--import DIR` names.

**Creating characters:** Create New Character and Modify Character work
as the original's do: the lists, the rolls, the icon editor, and every
class starting with its own equipment and experience.

### Combat

The adventure runs into its fights. Leadership, the battlefield and the
combatants are set up as in the original.

- Each character's turn shows the original's two-row combat menu: Move,
  Aim, Use, Cast, Turn, Guard, Quick, Delay, Bandage, Yell, View, Speed,
  End. The arrow keys move between the items and the rows.
- **Move** with the arrow or keypad keys. Escape takes the moves back.
- **Aim** to aim and attack: Next, Prev, Manual's cursor, Target.
- **Guard**, **Delay** or **End** the turn.
- **Quick** lets the computer fight for that character, and Alt-Q
  (Alt-A) does it for the whole party. As in the original, it fights with
  its own dice to the battle's end and the results.

### Treasure and the bank

- **Treasure found** uses the treasure menu: Take, Pool, Share.
- **The bank vault of Gargath:** take coins and items out, pool and
  deposit coins, and deposit items from a character's items.

### Characters and items

- **View** shows the original's character sheet and its menu: Items,
  Spells, Trade, Drop, and a paladin's Heal.
- **Items** (or Use in combat) opens the items menu: Ready, Use, Trade,
  Drop, Halve, Join, and in shops Sell and Id.

### Town

- **Shops:** buy (from the character's steel, then the pooled steel),
  sell, identify, and have gems and jewelry appraised.
- **Temples:** the priests heal and cure for free, Raise Dead and
  Restoration included.
- **Training halls:** train characters up to level 18 (Train Character)
  and promote knights (Knight Change Order).

### Camp and magic

- **Magic:** Memorize, Scribe and Display work, with spell levels up to 8.
- **Cast** works in camp and in combat, by you or by the computer.
  Alt-M turns Magic On for the party on Auto.
- Saving throws and magic resistance work as in the original.
- Every spell is there, including the monsters' Slay Living, Feeblemind
  and Otto's Irresistible Dance, plus Heal, Harm, Death Spell, the clouds
  and the rest.
- **Fix** heals, and cures the stoned, poisoned, dead, cursed and blind
  with the spells your clerics have, as in the original.

### Mouse and joystick

- The mouse and the joystick work in the menus as in the original.
- Initialize Mouse/Joystick turns them on and off. As in the original, it
  writes what it set into `krynn.cfg`, not `DKK.CFG`, so the next start
  goes by `DKK.CFG` again.

## Options

`./build/krynn --help` lists them all. The useful ones:

| Option | |
| --- | --- |
| `--scale N` | window size, N times 320×200 (default 3). The window can also be resized freely. |
| `--fullscreen` | start in full screen |
| `--aspect 1:1` | square pixels. The default `4:3` stretches the picture as a 1990 monitor did, so the moons and portraits come out round. |
| `--display ega`, `tandy` or `cga` | graphics mode for this run |
| `--sound tandy`, `adlib`, `speaker` or `off` | sound device for this run |
| `--volume N` | 0 to 100 |
| `--input mouse`, `joystick` or `none` | mouse, joystick or keyboard only for this run |
| `--saves DIR` | keep the roster and saved games in DIR instead of `saves/` |
| `--assets DIR` | find the game's files in DIR instead of `COKAssets/` |
| `--game champions` or `death-knights` | the game to play; Champions of Krynn is the default (see [Death Knights of Krynn](#death-knights-of-krynn)) |
| `--import DIR` | Death Knights: where to find the Champions characters and saved games to bring over (default: the saves folder) |
| `--screenshots DIR` | where F12 saves screenshots |

## Troubleshooting

- **No sound:** if there is no sound device, `krynn` says so and plays on
  silently. SDL picks PulseAudio, PipeWire or ALSA by itself; set
  `SDL_AUDIODRIVER` to choose one. Also check that `KRYNN.CFG` doesn't say
  Silent: try `--sound tandy`.
- **No window in a container or over SSH:** `krynn` needs a display. Run it
  on the host, use `ssh -X`, or share the host's X server with the
  container.
- **"stopped in block N at …":** the game reached something the port
  doesn't handle. It shows the reason on screen and waits for a key. Please
  report it with that message, and with a saved game from just before if
  you have one.
- **The setup didn't appear, or I want to change my answers:** delete
  `saves/KRYNN.CFG` (and `COKAssets/KRYNN.CFG` if there is one), or use the
  options above.

## How faithful is it?

The port was written from a disassembly of the original program, routine
by routine. Its behaviour has been checked against the real game running
in the DOSBox-X emulator: given the same keys and the same random seed,
every one of 82 automated comparisons (screens, saved files and the
game's memory) matches the original.

What's different:

- **No music on CMS or Roland MT-32.** Those choices play the sound
  effects on the PC speaker, without the title music. The Tandy, AdLib and
  PC-speaker sound is complete (the AdLib's chip is emulated by the port's
  own model of the YM3812).
- **No copy-protection questions.** The original asks for a word from the
  rule book on every tenth save. The port skips it.
- A few checks need real 1990 hardware, real MS-DOS or a person to judge,
  so they haven't been run. [docs/checks.md](docs/checks.md) lists them.

## For developers

[DEVELOPERS.md](DEVELOPERS.md) has everything else: the command-line
tools (DAX archive reader, image exporter, ECL script disassembler and
the headless `eclplay` player), every routine of the original and how
the port follows it, the test suite, the DOSBox-X harness, and the full
list of checks against the original.

## License

MIT; see [LICENSE](LICENSE). This covers the port's own code only. The
game's files remain the property of their owners and are not included.
