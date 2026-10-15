CC = cc
PYTHON = python3
CPPFLAGS = -Isrc
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror

.PHONY: all test sanitize merged
all: build/daxcheck build/daximages build/daxcompose build/ecldump build/eclplay

build:
	mkdir -p build

build/daxcheck: src/daxcheck.c src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/daxcheck.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

merged: build/START_FULL.EXE

build/START_FULL.EXE: tools/ovrmerge.py Assets/START.EXE Assets/GAME.OVR | build
	$(PYTHON) tools/ovrmerge.py Assets/START.EXE Assets/GAME.OVR $@ --map build/START_FULL.map

build/ecldump: src/ecldump.c src/ecl.c src/ecl.h src/text.h src/picture.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/ecldump.c src/ecl.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

ADVENTURE_SRC = src/adventure.c src/debug.c src/devices.c src/input.c src/ai.c src/cloud.c src/command.c src/start.c src/roster.c src/create.c src/train.c src/modify.c src/icon.c src/arena.c src/attack.c src/combat.c src/round.c src/monster.c src/treasure.c src/shop.c src/camp.c src/magic.c src/cast.c src/items.c src/sheet.c src/effect.c src/party.c src/ecl.c src/menu.c src/screen.c src/text.c src/view.c src/picture.c src/image.c src/sound.c src/config.c src/crt.c src/setup.c src/dax.c
ADVENTURE_DEPS = $(ADVENTURE_SRC) src/adventure.h src/debug.h src/input.h src/ai.h src/cloud.h src/command.h src/start.h src/roster.h src/create.h src/train.h src/modify.h src/icon.h src/arena.h src/attack.h src/combat.h src/round.h src/monster.h src/treasure.h src/shop.h src/camp.h src/magic.h src/cast.h src/items.h src/sheet.h src/effect.h src/party.h src/ecl.h src/menu.h src/screen.h src/text.h src/view.h src/picture.h src/image.h src/sound.h src/config.h src/crt.h src/setup.h src/dax.h

KEYSCRIPT = src/keyscript.c src/keyscript.h

# The SDL2 front end (build/cok) and its tests, only where pkg-config finds
# SDL2; its headers as system headers, so that their warnings do not fail
# the build.
HAVE_SDL := $(shell pkg-config --exists sdl2 2>/dev/null && echo yes)
SDL_CFLAGS := $(patsubst -I%,-isystem %,$(shell pkg-config --cflags sdl2 2>/dev/null))
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
COK_SRC = src/cok.c src/keymap.c src/keyscript.c $(ADVENTURE_SRC)
COK_DEPS = $(COK_SRC) src/keymap.h $(KEYSCRIPT) $(ADVENTURE_DEPS)

.PHONY: cok
cok: build/cok

# tests/test_cok.py's new game, for make sanitize.
COK_GAME = P\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH\v\v\vSS\v\v\v\v\v\vSLNM\>\r\@xN\r\eN\r\r\r\r\r\rELN98yA\@xN\r\eN\^\^\>\^L\vEERESANE

# Clicks and the joystick for make sanitize: creating BOB and modifying
# him by clicks; the stick's keys and its calibration (tests/test_cok.py's).
COK_CLICKS = \kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\v\rAE\m100,116,1.\m148,196,1.\m124,196,1.\m52,84,1.\m68,196,1.\m172,196,1.\e
COK_STICK = \kx\j129,129,1.\j129,129.\j129,250.\j129,129.\j129,250.\j129,129.\j129,250.\j129,129.\j129,129,1.\j129,129.\j129,250.\j129,129.\j129,129,1.\j129,129.\r\j10,12,1.\j129,129.\j250,240,1.\j129,129.\e\e
COK_STICK_CFG = E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n1\r\n1\r\n40\r\n40\r\n80\r\n80\r\n120\r\n120\r\n160\r\n160\r\n200\r\n200\r\n250\r\n250\r\n

build/cok: $(COK_DEPS) | build
	@$(if $(HAVE_SDL),true,echo "build/cok needs SDL2, found with pkg-config sdl2 (libsdl2-dev)" >&2; exit 1)
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) $(COK_SRC) $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

build/cok_sanitize: $(COK_DEPS) | build
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer $(COK_SRC) $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

# cok with ThreadSanitizer: the game's calls into the sound against the
# audio thread's rendering.
build/cok_tsan: $(COK_DEPS) | build
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) -g -fsanitize=thread $(COK_SRC) $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

build/test_keymap_sanitize: tests/test_keymap.c src/keymap.c src/keymap.h $(KEYSCRIPT) src/text.h | build
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_keymap.c src/keymap.c src/keyscript.c $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

build/test_keymap: tests/test_keymap.c src/keymap.c src/keymap.h $(KEYSCRIPT) src/text.h | build
	$(CC) $(CPPFLAGS) $(SDL_CFLAGS) $(CFLAGS) tests/test_keymap.c src/keymap.c src/keyscript.c $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@


build/eclplay: src/eclplay.c $(KEYSCRIPT) $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/eclplay.c src/keyscript.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_adventure: tests/test_adventure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_adventure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_debug: tests/test_debug.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_debug.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_debug_sanitize: tests/test_debug.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_debug.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_hooks: tests/test_hooks.c tests/test_loading_data.h $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_hooks.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_hooks_sanitize: tests/test_hooks.c tests/test_loading_data.h $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_hooks.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_input: tests/test_input.c tests/test_input_data.h $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_input.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_input_sanitize: tests/test_input.c tests/test_input_data.h $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_input.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_start: tests/test_start.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_start.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_camp: tests/test_camp.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_camp.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_cast: tests/test_cast.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_cast.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_monster: tests/test_monster.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_monster.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_combat: tests/test_combat.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_combat.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_round: tests/test_round.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_round.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_arena: tests/test_arena.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_arena.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_ai: tests/test_ai.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ai.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_command: tests/test_command.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_command.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_spell: tests/test_spell.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_spell.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_cloud: tests/test_cloud.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_cloud.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_attack: tests/test_attack.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_attack.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_treasure: tests/test_treasure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_treasure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_shop: tests/test_shop.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_shop.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_items: tests/test_items.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_items.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_sound: tests/test_sound.c tests/test_sound_data.h src/sound.c src/sound.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_sound.c src/sound.c src/dax.c $(LDFLAGS) $(LDLIBS) -lm -o $@

build/test_config: tests/test_config.c src/config.c src/config.h src/sound.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_config.c src/config.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_config_sanitize: tests/test_config.c src/config.c src/config.h src/sound.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_config.c src/config.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_setup: tests/test_setup.c src/setup.c src/input.c src/crt.c src/config.c src/text.c src/picture.c src/image.c src/dax.c tests/test_setup_data.h src/setup.h src/input.h src/crt.h src/config.h src/text.h src/picture.h src/image.h src/dax.h src/sound.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_setup.c src/setup.c src/input.c src/crt.c src/config.c src/text.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_setup_sanitize: tests/test_setup.c src/setup.c src/input.c src/crt.c src/config.c src/text.c src/picture.c src/image.c src/dax.c tests/test_setup_data.h src/setup.h src/input.h src/crt.h src/config.h src/text.h src/picture.h src/image.h src/dax.h src/sound.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_setup.c src/setup.c src/input.c src/crt.c src/config.c src/text.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_sound_sanitize: tests/test_sound.c tests/test_sound_data.h src/sound.c src/sound.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_sound.c src/sound.c src/dax.c $(LDFLAGS) $(LDLIBS) -lm -o $@

build/test_ecl: tests/test_ecl.c src/ecl.c src/ecl.h src/text.h src/picture.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ecl.c src/ecl.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_menu: tests/test_menu.c src/menu.c src/menu.h src/screen.c src/screen.h src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_menu.c src/menu.c src/screen.c src/text.c src/picture.c src/image.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_view: tests/test_view.c src/view.c src/view.h src/picture.c src/picture.h src/image.c src/image.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_view.c src/view.c src/picture.c src/image.c $(LDFLAGS) $(LDLIBS) -o $@

PARTY_SRC = src/party.c src/ecl.c src/text.c src/picture.c src/image.c src/dax.c
PARTY_DEPS = $(PARTY_SRC) src/party.h src/ecl.h src/text.h src/picture.h src/image.h src/dax.h

build/test_party: tests/test_party.c $(PARTY_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_party.c $(PARTY_SRC) $(LDFLAGS) $(LDLIBS) -o $@

EFFECT_SRC = src/effect.c $(PARTY_SRC)
EFFECT_DEPS = $(PARTY_DEPS) src/effect.c src/effect.h

build/test_effect: tests/test_effect.c $(EFFECT_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_effect.c $(EFFECT_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_dax: tests/test_dax.c src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_dax.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/daximages: src/daximages.c src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/daximages.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/daxcompose: src/daxcompose.c src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/daxcompose.c src/text.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_picture: tests/test_picture.c src/picture.c src/picture.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_picture.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_text: tests/test_text.c src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_text.c src/text.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_display: tests/test_display.c tests/test_display_data.h src/config.h src/picture.c src/picture.h src/text.c src/text.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_display.c src/picture.c src/text.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_display_sanitize: tests/test_display.c tests/test_display_data.h src/config.h src/picture.c src/picture.h src/text.c src/text.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_display.c src/picture.c src/text.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_image: tests/test_image.c src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_image.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

test: build/START_FULL.EXE build/test_dax build/test_image build/test_picture build/test_text build/test_display build/test_ecl build/test_menu build/test_view build/test_party build/test_effect build/test_adventure build/test_camp build/test_cast build/test_items build/test_monster build/test_combat build/test_round build/test_arena build/test_attack build/test_ai build/test_command build/test_spell build/test_cloud build/test_treasure build/test_shop build/test_start build/test_hooks build/test_debug build/test_sound build/test_config build/test_setup build/test_input build/daxcheck build/daximages build/daxcompose build/ecldump build/eclplay $(if $(HAVE_SDL),build/cok build/test_keymap)
	./build/test_dax
	./build/test_image
	./build/test_picture
	./build/test_text
	./build/test_display
	./build/test_ecl
	./build/test_menu
	./build/test_view
	./build/test_party
	./build/test_effect
	./build/test_adventure
	./build/test_camp
	./build/test_cast
	./build/test_items
	./build/test_monster
	./build/test_combat
	./build/test_round
	./build/test_arena
	./build/test_attack
	./build/test_ai
	./build/test_command
	./build/test_spell
	./build/test_cloud
	./build/test_treasure
	./build/test_shop
	./build/test_start
	./build/test_hooks
	./build/test_debug
	./build/test_sound
	./build/test_config
	./build/test_setup
	./build/test_input
	./build/ecldump --summary Assets/ECL*.DAX
	$(PYTHON) tests/test_images_cli.py
	$(PYTHON) tests/test_eclplay.py
	$(if $(HAVE_SDL),./build/test_keymap,@echo "SDL2 not found (pkg-config sdl2): skipped building and testing build/cok")
	$(if $(HAVE_SDL),$(PYTHON) tests/test_cok.py)
	$(PYTHON) tests/test_ovrmerge.py
	./build/daxcheck Assets/*.DAX

build/test_dax_sanitize: tests/test_dax.c src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_dax.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/daxcheck_sanitize: src/daxcheck.c src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer src/daxcheck.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_image_sanitize: tests/test_image.c src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_image.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_picture_sanitize: tests/test_picture.c src/picture.c src/picture.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_picture.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_ecl_sanitize: tests/test_ecl.c src/ecl.c src/ecl.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_ecl.c src/ecl.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_text_sanitize: tests/test_text.c src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_text.c src/text.c src/picture.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_menu_sanitize: tests/test_menu.c src/menu.c src/menu.h src/screen.c src/screen.h src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_menu.c src/menu.c src/screen.c src/text.c src/picture.c src/image.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_view_sanitize: tests/test_view.c src/view.c src/view.h src/picture.c src/picture.h src/image.c src/image.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_view.c src/view.c src/picture.c src/image.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_party_sanitize: tests/test_party.c $(PARTY_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_party.c $(PARTY_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_effect_sanitize: tests/test_effect.c $(EFFECT_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_effect.c $(EFFECT_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_adventure_sanitize: tests/test_adventure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_adventure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_camp_sanitize: tests/test_camp.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_camp.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_cast_sanitize: tests/test_cast.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_cast.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_items_sanitize: tests/test_items.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_items.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_monster_sanitize: tests/test_monster.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_monster.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_combat_sanitize: tests/test_combat.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_combat.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_round_sanitize: tests/test_round.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_round.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_ai_sanitize: tests/test_ai.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_ai.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_spell_sanitize: tests/test_spell.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_spell.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_cloud_sanitize: tests/test_cloud.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_cloud.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_command_sanitize: tests/test_command.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_command.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_attack_sanitize: tests/test_attack.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_attack.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_arena_sanitize: tests/test_arena.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_arena.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_start_sanitize: tests/test_start.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_start.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_shop_sanitize: tests/test_shop.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_shop.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_treasure_sanitize: tests/test_treasure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_treasure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/eclplay_sanitize: src/eclplay.c $(KEYSCRIPT) $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer src/eclplay.c src/keyscript.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

sanitize: build/START_FULL.EXE build/test_dax_sanitize build/test_image_sanitize build/test_picture_sanitize build/test_text_sanitize build/test_display_sanitize build/test_ecl_sanitize build/test_menu_sanitize build/test_view_sanitize build/test_party_sanitize build/test_effect_sanitize build/test_adventure_sanitize build/test_camp_sanitize build/test_cast_sanitize build/test_items_sanitize build/test_monster_sanitize build/test_combat_sanitize build/test_round_sanitize build/test_arena_sanitize build/test_attack_sanitize build/test_ai_sanitize build/test_command_sanitize build/test_spell_sanitize build/test_cloud_sanitize build/test_treasure_sanitize build/test_shop_sanitize build/test_start_sanitize build/test_hooks_sanitize build/test_debug_sanitize build/test_sound_sanitize build/test_config_sanitize build/test_setup_sanitize build/test_input_sanitize build/eclplay_sanitize build/daxcheck_sanitize $(if $(HAVE_SDL),build/cok_sanitize build/cok_tsan build/test_keymap_sanitize)
	./build/test_dax_sanitize
	./build/test_image_sanitize
	./build/test_picture_sanitize
	./build/test_text_sanitize
	./build/test_display_sanitize
	./build/test_ecl_sanitize
	./build/test_menu_sanitize
	./build/test_view_sanitize
	./build/test_party_sanitize
	./build/test_effect_sanitize
	./build/test_adventure_sanitize
	./build/test_camp_sanitize
	./build/test_cast_sanitize
	./build/test_items_sanitize
	./build/test_monster_sanitize
	./build/test_combat_sanitize
	./build/test_round_sanitize
	./build/test_arena_sanitize
	./build/test_attack_sanitize
	./build/test_ai_sanitize
	./build/test_command_sanitize
	./build/test_spell_sanitize
	./build/test_cloud_sanitize
	./build/test_treasure_sanitize
	./build/test_shop_sanitize
	./build/test_start_sanitize
	./build/test_hooks_sanitize
	./build/test_debug_sanitize
	./build/test_sound_sanitize
	./build/test_config_sanitize
	./build/test_setup_sanitize
	./build/test_input_sanitize
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --keys '\r\rE\r\r\r\r\r\r\r' Assets 16 | tail -n 1)" = "(out of keys in block 17 at 9d9b)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --start 899b --keys '\r22\r\rE\r\r\r\r' Assets 48 | tail -n 1)" = "(done in block 48)"
	test "$$(./build/eclplay_sanitize --woof --test-party 4 --effect 2,22,10,0 --keys '\cD\rxxxxxxxxxxxxxx\cZ\cC' Assets 16 | tail -n 1)" = "(quit to DOS in block 16)"
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rE\rm\^\^\^\r\r\>\^\<\<\v\rlsea' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 80c0)" && printf '%s\n' "$$out" | grep -q '^print: A MAN GIBBERING WITH FEAR' && printf '%s\n' "$$out" | grep -q '^print: The party makes camp'
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --start 835f --keys '\r\rE' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && test "$$(printf '%s\n' "$$out" | head -n 1)" = "print: MONSTERS ATTACK!" && test "$$(printf '%s\n' "$$out" | grep -c '^round: ')" = 15
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --load SAVE/SAVGAMA.DAT --play --keys '\r\r\r\r\r\r\r' Assets | tail -n 1)" = "(out of keys in block 17 at 86ec)"; fi
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rEerhar\kn\eamm\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemm\^m\esd\e\eaos\v\e\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --start 9891 --set 4be6=1 --keys '\r\rE' Assets 32 | tail -n 1)" = "(done in block 32)"; fi
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"
	test "$$(./build/eclplay_sanitize --test-party 6 --file 3 --start 99fd --combat won --shots build/sanitize_shots --keys '\r\r\r' Assets 97 | tail -n 1)" = "(out of keys in block 97 at 9a06)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemc\rn\^\^\^\r\^S\^\^\rSee' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --combat melee --keys '\r\rEN' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 1879734825$$' && test "$$(printf '%s\n' "$$out" | grep -c '^attack: ')" = 27
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --combat auto --keys '\r\rEN' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 8417)" && printf '%s\n' "$$out" | grep -q '^combat: seed 3958868712$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: Surrenders$$')" = 5
	test "$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 87b4 --seed 1 Assets 32 | tail -n 1)" = "(the party was killed in block 32)"
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 87b4 --seed 1 --keys 'TTVEDSSEQANTNM88DDGDDDA\@qN\rEN\r\r' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 1876183322$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: is turned$$')" = 10 && test "$$(printf '%s\n' "$$out" | grep -c '^print: is held$$')" = 2
	out="$$(./build/eclplay_sanitize --file 1 --test-party 6 --start 86a4 --set 4be6=1 --combat auto --keys '\r\r\r\r\r' Assets 33)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 33 at 86ac)" && printf '%s\n' "$$out" | grep -q '^combat: seed 457552249$$' && printf '%s\n' "$$out" | grep -q '^print: lost a spell$$'
	if [ -f SAVE/SAVGAMA.DAT ]; then out="$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --start 835f --seed 1 --keys '\rC\#50\#50CNTCCNTDQCCNTDQDQ' Assets 32)" && test "$$(printf '%s\n' "$$out" | grep -c '^print: is Blessed$$')" = 6 && test "$$(printf '%s\n' "$$out" | grep -c '^print: falls asleep$$')" = 2; fi
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --keys '\rM44\eDGANTDBAM8\e\eDQ\@qN\rEN' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 2970518320$$' && printf '%s\n' "$$out" | grep -q '^print: is bandaged$$'
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --start 828a --keys '\r\rE\r' Assets 33 | tail -n 1)" = "(done in block 33)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --set 4be6=1 --start 851e --keys 'wwps\r\r' Assets 33 | tail -n 1)" = "(out of keys in block 33 at 87b5)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --start 8bcd --keys '\rT\r3\r\eSE' Assets 32 | tail -n 1)" = "(done in block 32)"
	for how in won fled; do test "$$(./build/eclplay_sanitize --test-party 6 --combat $$how --start 8cda --keys '\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done
	test "$$(./build/eclplay_sanitize --test-party 6 --combat gods --seed 5 --start 8cda --keys 'Y\r\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --start 8cda --keys '\rE\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --test-party 6 --combat lost --start 8cda --keys '\r' Assets 16 | tail -n 1)" = "(the party was killed in block 16)"
	if [ -f SAVE/SAVGAMA.DAT ]; then for how in won fled; do test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat $$how --start 8cda --keys '\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat gods --start 8cda --keys '\r\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)"; fi
	test "$$(./build/eclplay_sanitize --test-party 4 --start 891c --keys 'y\rB\v\v\r\ePB\r\eEySE' Assets 16 | tail -n 1)" = "(out of keys in block 16 at 8dd1)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8cea --keys 'B\r\eE' Assets 50 | tail -n 1)" = "(out of keys in block 50 at 9d2f)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8782 --keys 'B\r\eVI\v\vSyI\ry\e\eE' Assets 17 | tail -n 1)" = "(out of keys in block 17 at 86ec)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8a8f --keys '\rPH\v\vHy\eEn\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --combat-map build/combat-map.txt --keys '\r\rE' Assets 32 | tail -n 1)" = "(done in block 32)"
	test "$$(head -n 1 build/combat-map.txt)" = "battle in block 32: view 24,10, 21 combatants"
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rE\ram\^\^\>eaa' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 8320)" && test "$$(printf '%s\n' "$$out" | grep '^area: ' | tr '\n' ' ')" = "area: on area: off area: on "
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --keys '\r\rea' Assets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 8287)" && printf '%s\n' "$$out" | grep -q '^print: Not Here$$'
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --helm --keys '\r\ream\^' Assets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 98e6)" && test "$$(printf '%s\n' "$$out" | grep '^area: \|^at: ' | tr '\n' ' ')" = "area: on at: 1,0,2 area: off "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --keys 'nm98yLn2' Assets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 8824)" && test "$$(printf '%s\n' "$$out" | grep '^overland: ' | tr '\n' ' ')" = "overland: 2,2,1 overland: 2,1,0 overland: 2,2,4 "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --seed 3 --combat won --keys 'nm\>\>\>\>\>\>\>\>\>\>\>\r\rENm\veee' Assets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 828b)" && printf '%s\n' "$$out" | grep -q '^print: YOU HAVE DISCOVERED SOME HOSTILE CREATURES.$$' && printf '%s\n' "$$out" | grep -q '^overland: 12,4,4$$'
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8890 --keys 'ye' Assets 16 | tail -n 1)" = "(done in block 16)"
	out="$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --combat won --keys '\r\eN' Assets 57)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 57)" && test "$$(printf '%s\n' "$$out" | grep -c '^party: .* joins')" = 3 && test "$$(printf '%s\n' "$$out" | grep -c '^sound: 10$$')" = 24
	test "$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --start 8b47 --shots build/sanitize_shots --keys '\>\rB\eE' Assets 50 | tail -n 1)" = "(done in block 50)"
	out="$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --start 87c0 --keys '\r' Assets 50)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 50 at 8904)" && printf '%s\n' "$$out" | grep -q '^party: DUNN leaves$$'
	test "$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --set 7c21=16 --start 9b82 --keys 'C\r' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 9ca7)"
	test "$$(./build/eclplay_sanitize --start-menu --combat won --keys '\kx\t' Assets | tail -n 1)" = "(out of keys at the title)"
	rm -rf build/sanitize_roster && mkdir -p build/sanitize_roster && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_roster --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH\v\v\vSS\v\v\v\v\v\vS' Assets | tail -n 1)" = "(out of keys in block 17 at 86ec)"
	rm -rf build/sanitize_cga && mkdir -p build/sanitize_cga && out="$$(./build/eclplay_sanitize --start-menu --display cga --seed 7 --helm --saves build/sanitize_cga --shots build/sanitize_cga --keys '$(COK_GAME)' Assets)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in the start menu)" && test "$$(ls build/sanitize_cga/*.bmp | wc -l)" -gt 90
	rm -rf build/sanitize_cga && mkdir -p build/sanitize_cga && test "$$(./build/eclplay_sanitize --start-menu --display cga --saves build/sanitize_cga --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\r\e' Assets | grep -c '^print: Sorry, not in CGA$$')" = 1
	out="$$(./build/eclplay_sanitize --test-party 6 --combat auto --seed 5 --start 8cda --keys '\r\r\r\r' Assets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 16)" && test "$$(printf '%s\n' "$$out" | grep -c '^print: breathes Fire$$')" = 3 && printf '%s\n' "$$out" | grep -q '^combat: seed 2266347106$$'
	out="$$(./build/eclplay_sanitize --test-party 8 --set 4be6=1 --start 835f --seed 1 --keys '\rCCNT\@qN\rEN\r\r' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^print: Creates a noxious cloud$$' && printf '%s\n' "$$out" | grep -q '^print: chokes and gags from nausea$$'
	out="$$(./build/eclplay_sanitize --file 2 --test-party 6 --start 8440 --seed 6 --combat auto --keys '\r\r\r\r\r' Assets 49)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 49 at 82c0)" && printf '%s\n' "$$out" | grep -q '^print: dissolves into acid!$$'
	out="$$(./build/eclplay_sanitize --file 3 --test-party 6 --combat auto --seed 148 --start 9afb --set 4be6=1 --effect 1,1,0,3 --effect 2,1,0,3 --effect 3,1,0,3 --effect 4,1,0,3 --effect 5,1,0,3 --effect 6,1,0,3 --keys '\r' Assets 82)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 82)" && printf '%s\n' "$$out" | grep -q '^effect: DUNN loses 0x01$$' && printf '%s\n' "$$out" | grep -q '^print: is affected$$' && ! printf '%s\n' "$$out" | grep -q '^unported'
	out="$$(./build/eclplay_sanitize --test-party 6 --combat auto --seed 1 --play --set 4be6=1 --effect 1,37,0,ff --effect 1,16,3,0 --effect 2,3e,2,ff --effect 3,3e,3,ff --effect 4,3e,2,ff --effect 3,22,1,7 --keys '\r\r' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 32)" && printf '%s\n' "$$out" | grep -q '^print: dies from poison$$' && printf '%s\n' "$$out" | grep -q '^print: is weakened$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: is partially healed$$')" = 3
	./build/daxcheck_sanitize Assets/*.DAX
	rm -rf build/sanitize_wav && mkdir -p build/sanitize_wav && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_wav --seed 3 --wav build/sanitize_wav/a.wav --keys '\p50.\t' Assets | tail -n 1)" = "(out of keys at the title)" && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_wav --seed 3 --sound speaker --wav build/sanitize_wav/b.wav --keys '\t' Assets | tail -n 1)" = "(out of keys at the title)"
	rm -rf build/sanitize_setup && mkdir -p build/sanitize_setup/assets build/sanitize_setup/saves build/sanitize_setup/cok && for f in Assets/*; do [ "$${f##*/}" = KRYNN.CFG ] || ln -s "$$PWD/$$f" build/sanitize_setup/assets/; done && out="$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_setup/saves --shots build/sanitize_setup --keys '91\#32x3xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\eq:\\x\b\r\kxP\e' build/sanitize_setup/assets)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in the start menu)" && printf '%s\n' "$$out" | grep -q '^crt: USING DEFAULT PATH: C:\\COK\\SAVE$$' && test "$$(head -n 4 build/sanitize_setup/saves/KRYNN.CFG | od -An -c | tr -d ' \n')" = 'C\r\nR\r\nN\r\nC:\COK\SAVE\\r\n'
	$(if $(HAVE_SDL),./build/test_keymap_sanitize,@echo "SDL2 not found (pkg-config sdl2): skipped build/cok_sanitize")
	$(if $(HAVE_SDL),test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --assets build/sanitize_setup/assets --saves build/sanitize_setup/cok --no-delay --keys '91\#32x3xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\eq:\\x\b\r\kxP\e')" = "(out of keys in the start menu)" && cmp build/sanitize_setup/saves/KRYNN.CFG build/sanitize_setup/cok/KRYNN.CFG)
	$(if $(HAVE_SDL),rm -rf build/sanitize_cok && test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --saves build/sanitize_cok --no-delay --seed 7 --helm --log --log-times --keys '$(COK_GAME)' 2>build/sanitize_cok.log)" = "(out of keys in the start menu)" && ! grep -E 'Sanitizer|runtime error' build/sanitize_cok.log && grep -q '^[0-9]* present: ' build/sanitize_cok.log)
	rm -rf build/sanitize_mouse && mkdir -p build/sanitize_mouse && test "$$(./build/eclplay_sanitize --start-menu --mouse --seed 5 --saves build/sanitize_mouse --shots build/sanitize_mouse --keys '$(COK_CLICKS)' Assets | tail -n 1)" = "(out of keys in the start menu)" && test -f build/sanitize_mouse/BOB.WHO
	$(if $(HAVE_SDL),rm -rf build/sanitize_stick && mkdir -p build/sanitize_stick && printf '$(COK_STICK_CFG)' > build/sanitize_stick/KRYNN.CFG && test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --saves build/sanitize_stick --no-delay --keys '$(COK_STICK)')" = "(out of keys in the start menu)" && test "$$(sed -n 3p build/sanitize_stick/KRYNN.CFG)" = "$$(printf 'J\r')")
	$(if $(HAVE_SDL),rm -rf build/sanitize_cok && test "$$(TSAN_OPTIONS='halt_on_error=1 exitcode=66' SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_tsan --saves build/sanitize_cok --no-delay --sound-clock real --seed 7 --helm --keys '\p50.$(COK_GAME)')" = "(out of keys in the start menu)")
