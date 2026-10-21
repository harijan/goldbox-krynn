CC = cc
PYTHON = python3
CPPFLAGS = -Isrc
CFLAGS = -std=c17 -O2 -Wall -Wextra -Wpedantic -Wconversion -Wshadow -Werror
# cast.c's burst takes its cosines and sines from the C library.
LDLIBS = -lm
# make sanitize's builds: AddressSanitizer and UBSan; build/cok_tsan's:
# ThreadSanitizer.
ASAN_FLAGS = -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer
TSAN_FLAGS = -g -fsanitize=thread

.PHONY: all test sanitize merged death-merged dqk-merged
all: build/daxcheck build/daximages build/daxcompose build/ecldump build/eclplay

build:
	mkdir -p build

# Each source is compiled once for each set of flags, and the programs are
# linked from the objects: build/obj/ (CFLAGS), build/obj-asan/ (with
# ASAN_FLAGS: make sanitize's builds), build/obj-tsan/ (with TSAN_FLAGS:
# build/cok_tsan's); SDL2's flags only for SDL_SRC, which include SDL.h.
# Each object's headers are in its .d file (-MMD -MP), which make reads, so
# that a header's change rebuilds the objects that include it.
SDL_SRC = src/cok.c src/keymap.c tests/test_keymap.c
obj = $(patsubst %.c,build/obj/%.o,$(1))
asan = $(patsubst %.c,build/obj-asan/%.o,$(1))
tsan = $(patsubst %.c,build/obj-tsan/%.o,$(1))
COMPILE = $(CC) $(CPPFLAGS) $(if $(filter $<,$(SDL_SRC)),$(SDL_CFLAGS)) $(CFLAGS)

build/obj/%.o: %.c
	@mkdir -p $(@D)
	$(COMPILE) -MMD -MP -c $< -o $@

build/obj-asan/%.o: %.c
	@mkdir -p $(@D)
	$(COMPILE) $(ASAN_FLAGS) -MMD -MP -c $< -o $@

build/obj-tsan/%.o: %.c
	@mkdir -p $(@D)
	$(COMPILE) $(TSAN_FLAGS) -MMD -MP -c $< -o $@

-include $(wildcard build/obj*/src/*.d build/obj*/tests/*.d)

# A program from its objects ($^), linked with the flags they were compiled
# with.
LINK = $(CC) $(CFLAGS) $^ $(LDFLAGS) $(LDLIBS)
LINK_ASAN = $(CC) $(CFLAGS) $(ASAN_FLAGS) $^ $(LDFLAGS) $(LDLIBS)

merged: build/START_FULL.EXE

build/START_FULL.EXE: tools/ovrmerge.py COKAssets/START.EXE COKAssets/GAME.OVR | build
	$(PYTHON) tools/ovrmerge.py COKAssets/START.EXE COKAssets/GAME.OVR $@ --map build/START_FULL.map

# Death Knights of Krynn the same way: its START.EXE and GAME.OVR in DEATH.
DEATH = DKKAssets
death-merged: build/death-START_FULL.EXE

build/death-START_FULL.EXE: tools/ovrmerge.py $(DEATH)/START.EXE $(DEATH)/GAME.OVR | build
	$(PYTHON) tools/ovrmerge.py $(DEATH)/START.EXE $(DEATH)/GAME.OVR $@ --map build/death-START_FULL.map

# The Dark Queen of Krynn keeps its overlays inside DQK.EXE (tools/dqkmerge.py).
DQK = DQKAssets
dqk-merged: build/DQK_FULL.EXE

build/DQK_FULL.EXE: tools/dqkmerge.py tools/ovrmerge.py $(DQK)/DQK.EXE | build
	$(PYTHON) tools/dqkmerge.py $(DQK)/DQK.EXE $@ --map build/DQK_FULL.map

ADVENTURE_SRC = src/adventure.c src/debug.c src/devices.c src/input.c src/ai.c src/cloud.c src/command.c src/start.c src/roster.c src/create.c src/train.c src/modify.c src/icon.c src/arena.c src/attack.c src/combat.c src/round.c src/monster.c src/treasure.c src/shop.c src/camp.c src/magic.c src/cast.c src/items.c src/sheet.c src/effect.c src/party.c src/ecl.c src/menu.c src/screen.c src/text.c src/view.c src/picture.c src/image.c src/sound.c src/config.c src/crt.c src/setup.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c src/step.c

# The original in DOSBox-X (tools/dosbox, see DEVELOPERS.md's "DOSBox-X
# harness"): boot it to the title's menu and compare its screens with
# eclplay's. Not part of make test: it needs dosbox-x, Xvfb and xdotool.
.PHONY: dosbox-selftest
dosbox-selftest: build/eclplay build/START_FULL.EXE
	$(PYTHON) tools/dosbox/checks/run_all.py --self-test
	$(PYTHON) tools/dosbox/cokbox.py selftest --machine tandy
	$(PYTHON) tools/dosbox/cokbox.py selftest --machine ega
	$(PYTHON) tools/dosbox/cokbox.py selftest --machine cga

# Every check of DEVELOPERS.md's "Checks against the original" run in the
# original (tools/dosbox/checks/), side by side: one verdict a script.
# ARGS go to run_all.py (e.g. ARGS="--only b_amount --jobs 2"). build/cok
# for the checks that compare the frames cok presents (c_places.cok_frames).
.PHONY: dosbox-checks
dosbox-checks: build/eclplay build/cok build/START_FULL.EXE
	$(PYTHON) tools/dosbox/checks/run_all.py $(ARGS)

# The SDL2 front end (build/cok) and its tests, only where pkg-config finds
# SDL2; its headers as system headers, so that their warnings do not fail
# the build.
HAVE_SDL := $(shell pkg-config --exists sdl2 2>/dev/null && echo yes)
SDL_CFLAGS := $(patsubst -I%,-isystem %,$(shell pkg-config --cflags sdl2 2>/dev/null))
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
COK_SRC = src/cok.c src/keymap.c src/keyscript.c $(ADVENTURE_SRC)

.PHONY: cok
cok: build/cok

# tests/test_cok.py's new game, for make sanitize.
COK_GAME = P\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH\v\v\vSS\v\v\v\v\v\vSLNM\>\r\@xN\r\eN\r\r\r\r\r\rELN98yA\@xN\r\eN\^\^\>\^L\vEERESANE

# Clicks and the joystick for make sanitize: creating BOB and modifying
# him by clicks; the stick's keys and its calibration (tests/test_cok.py's).
COK_CLICKS = \kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\v\rAE\m100,116,1.\m148,196,1.\m124,196,1.\m52,84,1.\m68,196,1.\m172,196,1.\e
COK_STICK = \kx\j129,129,1.\j129,129.\j129,250.\j129,129.\j129,250.\j129,129.\j129,250.\j129,129.\j129,129,1.\j129,129.\j129,250.\j129,129.\j129,129,1.\j129,129.\r\j10,12,1.\j129,129.\j250,240,1.\j129,129.\e\e
COK_STICK_CFG = E\r\nT\r\nJ\r\nC:\\COK\\SAVE\\\r\nF\r\n1\r\n1\r\n40\r\n40\r\n80\r\n80\r\n120\r\n120\r\n160\r\n160\r\n200\r\n200\r\n250\r\n250\r\n


build/cok: $(call obj,$(COK_SRC)) | build
	@$(if $(HAVE_SDL),true,echo "build/cok needs SDL2, found with pkg-config sdl2 (libsdl2-dev)" >&2; exit 1)
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

build/cok_sanitize: $(call asan,$(COK_SRC)) | build
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $^ $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

# cok with ThreadSanitizer: the game's calls into the sound against the
# audio thread's rendering.
build/cok_tsan: $(call tsan,$(COK_SRC)) | build
	$(CC) $(CFLAGS) $(TSAN_FLAGS) $^ $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

KEYMAP_SRC = tests/test_keymap.c src/keymap.c src/keyscript.c

build/test_keymap_sanitize: $(call asan,$(KEYMAP_SRC)) | build
	$(CC) $(CFLAGS) $(ASAN_FLAGS) $^ $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

build/test_keymap: $(call obj,$(KEYMAP_SRC)) | build
	$(CC) $(CFLAGS) $^ $(LDFLAGS) $(SDL_LIBS) $(LDLIBS) -o $@

ECLPLAY_SRC = src/eclplay.c src/keyscript.c $(ADVENTURE_SRC)

build/eclplay: $(call obj,$(ECLPLAY_SRC)) | build
	$(LINK) -o $@

build/eclplay_sanitize: $(call asan,$(ECLPLAY_SRC)) | build
	$(LINK_ASAN) -o $@

# The tests of the adventure, each with the whole game: build/test_NAME from
# tests/test_NAME.c and ADVENTURE_SRC, build/test_NAME_sanitize likewise.
ADVENTURE_TESTS = test_adventure test_debug test_hooks test_input test_start test_camp test_cast test_monster test_combat test_round test_arena test_ai test_command test_spell test_cloud test_attack test_treasure test_shop test_items test_fight test_band

$(ADVENTURE_TESTS:%=build/%): build/%: build/obj/tests/%.o $(call obj,$(ADVENTURE_SRC)) | build
	$(LINK) -o $@

$(ADVENTURE_TESTS:%=build/%_sanitize): build/%_sanitize: build/obj-asan/tests/%.o $(call asan,$(ADVENTURE_SRC)) | build
	$(LINK_ASAN) -o $@

# The other tests and tools, each with the sources it needs: build/NAME from
# NAME_SRC, build/NAME_sanitize too for those make sanitize runs.
test_sound_SRC = tests/test_sound.c src/sound.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_config_SRC = tests/test_config.c src/config.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_setup_SRC = tests/test_setup.c src/setup.c src/input.c src/crt.c src/config.c src/text.c src/picture.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_ecl_SRC = tests/test_ecl.c src/ecl.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_menu_SRC = tests/test_menu.c src/menu.c src/screen.c src/text.c src/picture.c src/image.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_view_SRC = tests/test_view.c src/view.c src/picture.c src/image.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
PARTY_SRC = src/party.c src/ecl.c src/text.c src/picture.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_party_SRC = tests/test_party.c $(PARTY_SRC)
EFFECT_SRC = src/effect.c $(PARTY_SRC)
test_effect_SRC = tests/test_effect.c $(EFFECT_SRC)
test_dax_SRC = tests/test_dax.c src/dax.c
test_rules_SRC = tests/test_rules.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c src/dax.c
test_death_knights_SRC = tests/test_death_knights.c src/ecl.c src/view.c src/screen.c src/picture.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_layout_SRC = tests/test_layout.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_data_SRC = tests/test_data.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c src/step.c
test_magic_rules_SRC = tests/test_magic_rules.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
daximages_SRC = src/daximages.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
daxcompose_SRC = src/daxcompose.c src/text.c src/picture.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_picture_SRC = tests/test_picture.c src/picture.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_text_SRC = tests/test_text.c src/text.c src/picture.c src/image.c src/dax.c
test_display_SRC = tests/test_display.c src/picture.c src/text.c src/image.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_image_SRC = tests/test_image.c src/image.c src/dax.c
daxcheck_SRC = src/daxcheck.c src/dax.c
ecldump_SRC = src/ecldump.c src/ecl.c src/dax.c src/rules.c src/data_champions.c src/rules_death_knights.c src/data_death_knights.c
test_sound_LIBS = -lm

# Their rules (.SECONDEXPANSION: each one's NAME_SRC read for its stem), with
# NAME_LIBS after the libraries.
SRC_PROGRAMS = daxcheck ecldump test_sound test_config test_setup test_ecl test_menu test_view test_party test_effect test_dax test_rules test_death_knights test_layout test_data test_magic_rules daximages daxcompose test_picture test_text test_display test_image
SRC_SANITIZE = test_sound test_config test_setup test_ecl test_menu test_view test_party test_effect test_dax test_rules test_death_knights test_layout test_data test_magic_rules test_picture test_text test_display test_image daxcheck
.SECONDEXPANSION:
$(SRC_PROGRAMS:%=build/%): build/%: $$(call obj,$$($$*_SRC)) | build
	$(LINK) $($*_LIBS) -o $@

$(SRC_SANITIZE:%=build/%_sanitize): build/%_sanitize: $$(call asan,$$($$*_SRC)) | build
	$(LINK_ASAN) $($*_LIBS) -o $@

# make test and make sanitize: each test program's run is its own target
# (run-NAME runs build/NAME), and so is each test script's, so that make -j
# runs them side by side; plain make runs them one after another. They need
# build/START_FULL.EXE (the original's tables). tests/test_eclplay.py runs
# in parts, one for each of SHARDS (COK_TEST_SHARD=K/N: every Nth test from
# the Kth), and run-test_eclplay then checks from the ids they list
# (COK_TEST_SHARD_LIST) that together they ran each of its tests once.
# tests/test_cok.py plays in real time: it runs last, alone, once the
# others are done (make -j would otherwise run it beside them, and a loaded
# machine makes the game late), its own tests side by side, each in a
# process of its own (COK_TEST_JOBS of them at once, 1 for one at a time).
TEST_PROGRAMS = test_dax test_rules test_death_knights test_layout test_data test_magic_rules test_image test_picture test_text test_display test_ecl test_menu test_view test_party test_effect test_adventure test_camp test_cast test_items test_monster test_combat test_round test_arena test_attack test_fight test_band test_ai test_command test_spell test_cloud test_treasure test_shop test_start test_hooks test_debug test_sound test_config test_setup test_input
SHARDS = 1 2 3 4
ECLPLAY_RUNS = $(SHARDS:%=run-test_eclplay-%)
TEST_RUNS = $(TEST_PROGRAMS:%=run-%) run-ecldump run-test_images_cli run-test_death_knights_py $(ECLPLAY_RUNS) run-test_eclplay $(if $(HAVE_SDL),run-test_keymap) run-test_ovrmerge run-daxcheck
SANITIZE_RUNS = $(TEST_PROGRAMS:%=run-%_sanitize)
.PHONY: $(TEST_RUNS) $(SANITIZE_RUNS) run-test_cok run-test_death_knights_py

# The tests' temporary files (TMPDIR) in TEST_TMPDIR: the dev container's
# /tmp, a FUSE overlay holding many entries, is slow for their many small
# files (the tests but test_cok.py, at -j16: 40 s with /tmp, 14 s with
# build/tmp on the same overlay). The C tests make their folders there too
# (tests/test_temp.h).
TEST_TMPDIR = build/tmp
TEST_ENV = TMPDIR=$(abspath $(TEST_TMPDIR))
$(TEST_RUNS) $(SANITIZE_RUNS) run-test_cok: | $(TEST_TMPDIR)
$(TEST_TMPDIR):
	mkdir -p $@

test: $(TEST_RUNS) $(if $(HAVE_SDL),run-test_cok)
	$(if $(HAVE_SDL),,@echo "SDL2 not found (pkg-config sdl2): skipped building and testing build/cok")

$(TEST_PROGRAMS:%=run-%) $(SANITIZE_RUNS) run-test_keymap: run-%: build/% build/START_FULL.EXE
	$(TEST_ENV) ./build/$*

# Death Knights' checks read its merged image where DKKAssets/ holds its
# files (and skip them, saying so, where it does not).
DEATH_EXE = $(if $(wildcard $(DEATH)/START.EXE),build/death-START_FULL.EXE)
run-test_death_knights run-test_death_knights_sanitize run-test_death_knights_py: $(DEATH_EXE)
run-test_magic_rules run-test_magic_rules_sanitize: $(DEATH_EXE)

run-ecldump: build/ecldump
	$(TEST_ENV) ./build/ecldump --summary COKAssets/ECL*.DAX

run-test_images_cli: build/daximages build/daxcompose
	$(TEST_ENV) $(PYTHON) tests/test_images_cli.py

run-test_death_knights_py: build/eclplay build/ecldump build/daxcheck build/daximages build/daxcompose $(if $(HAVE_SDL),build/cok)
	$(TEST_ENV) $(PYTHON) tests/test_death_knights.py

SHARD_LIST = $(abspath $(TEST_TMPDIR))/eclplay-shard-$(1).txt
$(ECLPLAY_RUNS): run-test_eclplay-%: build/eclplay build/START_FULL.EXE
	rm -f $(call SHARD_LIST,$*)
	$(TEST_ENV) COK_TEST_SHARD=$*/$(words $(SHARDS)) COK_TEST_SHARD_LIST=$(call SHARD_LIST,$*) $(PYTHON) tests/test_eclplay.py

run-test_eclplay: $(ECLPLAY_RUNS)
	COK_TEST_SHARD_CHECK="$(foreach k,$(SHARDS),$(call SHARD_LIST,$(k)))" $(PYTHON) tests/test_eclplay.py

run-test_ovrmerge:
	$(TEST_ENV) $(PYTHON) tests/test_ovrmerge.py

run-daxcheck: build/daxcheck
	$(TEST_ENV) ./build/daxcheck COKAssets/*.DAX

run-test_cok: build/cok build/eclplay $(TEST_RUNS)
	$(TEST_ENV) $(PYTHON) tests/test_cok.py

# make sanitize: its test programs' runs (SANITIZE_RUNS) side by side, then
# eclplay's and cok's runs below one after another (some use the files of
# those before them).
sanitize: build/START_FULL.EXE $(SANITIZE_RUNS) build/eclplay_sanitize build/daxcheck_sanitize $(if $(HAVE_SDL),build/cok_sanitize build/cok_tsan build/test_keymap_sanitize)
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --keys '\r\rE\r\r\r\r\r\r\r' COKAssets 16 | tail -n 1)" = "(out of keys in block 17 at 9d9b)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --start 899b --keys '\r22\r\rE\r\r\r\r' COKAssets 48 | tail -n 1)" = "(done in block 48)"
	test "$$(./build/eclplay_sanitize --woof --test-party 4 --effect 2,22,10,0 --keys '\cD\rxxxxxxxxxxxxxx\cZ\cC' COKAssets 16 | tail -n 1)" = "(quit to DOS in block 16)"
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rE\rm\^\^\^\r\r\>\^\<\<\v\rlsea' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 80c0)" && printf '%s\n' "$$out" | grep -q '^print: A MAN GIBBERING WITH FEAR' && printf '%s\n' "$$out" | grep -q '^print: The party makes camp'
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --start 835f --keys '\r\rE' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && test "$$(printf '%s\n' "$$out" | head -n 1)" = "print: MONSTERS ATTACK!" && test "$$(printf '%s\n' "$$out" | grep -c '^round: ')" = 15
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --load SAVE/SAVGAMA.DAT --play --keys '\r\r\r\r\r\r\r' COKAssets | tail -n 1)" = "(out of keys in block 17 at 86ec)"; fi
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rEerhar\kn\eamm\e\e' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"
	if [ -f $(DEATH)/START.EXE ]; then test "$$(./build/eclplay_sanitize --game death-knights --set 4be6=1 --shots build/sanitize_dkk --keys '\r\r\r\r' $(DEATH) 64 | tail -n 1)" = "(done in block 64)" && test "$$(./build/eclplay_sanitize --game death-knights --file 2 --start 88f7 --keys 'Y\r' $(DEATH) 48 | tail -n 1)" = "(done in block 17)" && test "$$(./build/eclplay_sanitize --game death-knights --file 2 --start 83c7 --keys '\r\r' $(DEATH) 48 | tail -n 1)" = "(done in block 48)" && test "$$(./build/eclplay_sanitize --game death-knights --character $(DEATH)/SAVE/CHRDATA1.GAM --character $(DEATH)/SAVE/CHRDATB5.GAM --play --keys 'ev\r\e' $(DEATH) 17 | tail -n 1)" = "(out of keys in block 17 at 81fc)" && rm -rf build/sanitize_dkk_saves && mkdir -p build/sanitize_dkk_saves && test "$$(./build/eclplay_sanitize --game death-knights --start-menu --saves build/sanitize_dkk_saves --import SAVE --keys '\kx\kxP\v\v\rCA\v\v\v\v\rC\v\r\r\r' $(DEATH) | tail -n 1)" = "(out of keys in block 16 at 817b)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemm\^m\esd\e\eaos\v\e\e\e' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --start 9891 --set 4be6=1 --keys '\r\rE' COKAssets 32 | tail -n 1)" = "(done in block 32)"; fi
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"
	test "$$(./build/eclplay_sanitize --test-party 6 --file 3 --start 99fd --combat won --shots build/sanitize_shots --keys '\r\r\r' COKAssets 97 | tail -n 1)" = "(out of keys in block 97 at 9a06)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --combat pass --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemc\rn\^\^\^\r\^S\^\^\rSee' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --combat melee --keys '\r\rEN' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 1879734825$$' && test "$$(printf '%s\n' "$$out" | grep -c '^attack: ')" = 27
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --combat auto --keys '\r\rEN' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 8417)" && printf '%s\n' "$$out" | grep -q '^combat: seed 3958868712$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: Surrenders$$')" = 5
	test "$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 87b4 --seed 1 COKAssets 32 | tail -n 1)" = "(the party was killed in block 32)"
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 87b4 --seed 1 --keys 'TTVEDSSEQANTNM88DDGDDDA\@qN\rEN\r\r' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 1876183322$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: is turned$$')" = 10 && test "$$(printf '%s\n' "$$out" | grep -c '^print: is held$$')" = 2
	out="$$(./build/eclplay_sanitize --file 1 --test-party 6 --start 86a4 --set 4be6=1 --combat auto --keys '\r\r\r\r\r' COKAssets 33)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 33 at 86ac)" && printf '%s\n' "$$out" | grep -q '^combat: seed 457552249$$' && printf '%s\n' "$$out" | grep -q '^print: lost a spell$$'
	if [ -f SAVE/SAVGAMA.DAT ]; then out="$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --start 835f --seed 1 --keys '\rC\#50\#50CNTCCNTDQCCNTDQDQ' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | grep -c '^print: is Blessed$$')" = 6 && test "$$(printf '%s\n' "$$out" | grep -c '^print: falls asleep$$')" = 2; fi
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --seed 1 --keys '\rM44\eDGANTDBAM8\e\eDQ\@qN\rEN' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^combat: seed 2970518320$$' && printf '%s\n' "$$out" | grep -q '^print: is bandaged$$'
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --start 828a --keys '\r\rE\r' COKAssets 33 | tail -n 1)" = "(done in block 33)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --set 4be6=1 --start 851e --keys 'wwps\r\r' COKAssets 33 | tail -n 1)" = "(out of keys in block 33 at 87b5)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --start 8bcd --keys '\rT\r3\r\eSE' COKAssets 32 | tail -n 1)" = "(done in block 32)"
	for how in won fled; do test "$$(./build/eclplay_sanitize --test-party 6 --combat $$how --start 8cda --keys '\rSE\r' COKAssets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done
	test "$$(./build/eclplay_sanitize --test-party 6 --combat gods --seed 5 --start 8cda --keys 'Y\r\rSE\r' COKAssets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --start 8cda --keys '\rE\r' COKAssets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --test-party 6 --combat lost --start 8cda --keys '\r' COKAssets 16 | tail -n 1)" = "(the party was killed in block 16)"
	if [ -f SAVE/SAVGAMA.DAT ]; then for how in won fled; do test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat $$how --start 8cda --keys '\rSE\r' COKAssets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat gods --start 8cda --keys '\r\rSE\r' COKAssets 16 | tail -n 1)" = "(done in block 16)"; fi
	test "$$(./build/eclplay_sanitize --test-party 4 --start 891c --keys 'y\rB\v\v\r\ePB\r\eEySE' COKAssets 16 | tail -n 1)" = "(out of keys in block 16 at 8dd1)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8cea --keys 'B\r\eE' COKAssets 50 | tail -n 1)" = "(out of keys in block 50 at 9d2f)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8782 --keys 'B\r\eVI\v\vSyI\ry\e\eE' COKAssets 17 | tail -n 1)" = "(out of keys in block 17 at 86ec)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8a8f --keys '\rPH\v\vHy\eEn\r' COKAssets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --combat pass --test-party 6 --set 4be6=1 --combat-map build/combat-map.txt --keys '\r\rE' COKAssets 32 | tail -n 1)" = "(done in block 32)"
	test "$$(head -n 1 build/combat-map.txt)" = "battle in block 32: view 24,10, 21 combatants"
	out="$$(./build/eclplay_sanitize --combat pass --test-party 6 --play --set 4be6=1 --keys '\r\rE\ram\^\^\>eaa' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 8320)" && test "$$(printf '%s\n' "$$out" | grep '^area: ' | tr '\n' ' ')" = "area: on area: off area: on "
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --keys '\r\rea' COKAssets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 8287)" && printf '%s\n' "$$out" | grep -q '^print: Not Here$$'
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --helm --keys '\r\ream\^' COKAssets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 98e6)" && test "$$(printf '%s\n' "$$out" | grep '^area: \|^at: ' | tr '\n' ' ')" = "area: on at: 1,0,2 area: off "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --keys 'nm98yLn2' COKAssets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 8824)" && test "$$(printf '%s\n' "$$out" | grep '^overland: ' | tr '\n' ' ')" = "overland: 2,2,1 overland: 2,1,0 overland: 2,2,4 "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --seed 3 --combat won --keys 'nm\>\>\>\>\>\>\>\>\>\>\>\r\rENm\veee' COKAssets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 828b)" && printf '%s\n' "$$out" | grep -q '^print: YOU HAVE DISCOVERED SOME HOSTILE CREATURES.$$' && printf '%s\n' "$$out" | grep -q '^overland: 12,4,4$$'
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8890 --keys 'ye' COKAssets 16 | tail -n 1)" = "(done in block 16)"
	out="$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --combat won --keys '\r\eN' COKAssets 57)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 57)" && test "$$(printf '%s\n' "$$out" | grep -c '^party: .* joins')" = 3 && test "$$(printf '%s\n' "$$out" | grep -c '^sound: 10$$')" = 24
	test "$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --start 8b47 --shots build/sanitize_shots --keys '\>\rB\eE' COKAssets 50 | tail -n 1)" = "(done in block 50)"
	out="$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --start 87c0 --keys '\r' COKAssets 50)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 50 at 8904)" && printf '%s\n' "$$out" | grep -q '^party: DUNN leaves$$'
	test "$$(./build/eclplay_sanitize --test-party 4 --set 4be6=1 --set 7c21=16 --start 9b82 --keys 'C\r' COKAssets 32 | tail -n 1)" = "(out of keys in block 32 at 9ca7)"
	test "$$(./build/eclplay_sanitize --start-menu --combat won --keys '\kx\t' COKAssets | tail -n 1)" = "(out of keys at the title)"
	rm -rf build/sanitize_roster && mkdir -p build/sanitize_roster && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_roster --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\eYY\vS\rE\v\v\v\v\vSA\vS\r\r\r\r\r\r\rH\v\v\vSS\v\v\v\v\v\vS' COKAssets | tail -n 1)" = "(out of keys in block 17 at 86ec)"
	rm -rf build/sanitize_cga && mkdir -p build/sanitize_cga && out="$$(./build/eclplay_sanitize --start-menu --display cga --seed 7 --helm --saves build/sanitize_cga --shots build/sanitize_cga --keys '$(COK_GAME)' COKAssets)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in the start menu)" && test "$$(ls build/sanitize_cga/*.bmp | wc -l)" -gt 90
	rm -rf build/sanitize_cga && mkdir -p build/sanitize_cga && test "$$(./build/eclplay_sanitize --start-menu --display cga --saves build/sanitize_cga --keys '\kxP\r\v\v\v\v\v\v\r\r\v\r\rNBOB\r\r\e' COKAssets | grep -c '^print: Sorry, not in CGA$$')" = 1
	out="$$(./build/eclplay_sanitize --test-party 6 --combat auto --seed 5 --start 8cda --keys '\r\r\r\r' COKAssets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 16)" && test "$$(printf '%s\n' "$$out" | grep -c '^print: breathes Fire$$')" = 3 && printf '%s\n' "$$out" | grep -q '^combat: seed 2266347106$$'
	out="$$(./build/eclplay_sanitize --test-party 8 --set 4be6=1 --start 835f --seed 1 --keys '\rCCNT\@qN\rEN\r\r' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && printf '%s\n' "$$out" | grep -q '^print: Creates a noxious cloud$$' && printf '%s\n' "$$out" | grep -q '^print: chokes and gags from nausea$$'
	out="$$(./build/eclplay_sanitize --file 2 --test-party 6 --start 8440 --seed 6 --combat auto --keys '\r\r\r\r\r' COKAssets 49)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 49 at 82c0)" && printf '%s\n' "$$out" | grep -q '^print: dissolves into acid!$$'
	out="$$(./build/eclplay_sanitize --file 3 --test-party 6 --combat auto --seed 148 --start 9afb --set 4be6=1 --effect 1,1,0,3 --effect 2,1,0,3 --effect 3,1,0,3 --effect 4,1,0,3 --effect 5,1,0,3 --effect 6,1,0,3 --keys '\r' COKAssets 82)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 82)" && printf '%s\n' "$$out" | grep -q '^effect: DUNN loses 0x01$$' && printf '%s\n' "$$out" | grep -q '^print: is affected$$' && ! printf '%s\n' "$$out" | grep -q '^unported'
	out="$$(./build/eclplay_sanitize --test-party 6 --combat auto --seed 1 --play --set 4be6=1 --effect 1,37,0,ff --effect 1,16,3,0 --effect 2,3e,2,ff --effect 3,3e,3,ff --effect 4,3e,2,ff --effect 3,22,1,7 --keys '\r\r' COKAssets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(the party was killed in block 32)" && printf '%s\n' "$$out" | grep -q '^print: dies from poison$$' && printf '%s\n' "$$out" | grep -q '^print: is weakened$$' && test "$$(printf '%s\n' "$$out" | grep -c '^print: is partially healed$$')" = 3
	./build/daxcheck_sanitize COKAssets/*.DAX
	rm -rf build/sanitize_wav && mkdir -p build/sanitize_wav && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_wav --seed 3 --wav build/sanitize_wav/a.wav --keys '\p50.\t' COKAssets | tail -n 1)" = "(out of keys at the title)" && test "$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_wav --seed 3 --sound speaker --wav build/sanitize_wav/b.wav --keys '\t' COKAssets | tail -n 1)" = "(out of keys at the title)"
	rm -rf build/sanitize_setup && mkdir -p build/sanitize_setup/assets build/sanitize_setup/saves build/sanitize_setup/cok && for f in COKAssets/*; do [ "$${f##*/}" = KRYNN.CFG ] || ln -s "$$PWD/$$f" build/sanitize_setup/assets/; done && out="$$(./build/eclplay_sanitize --start-menu --saves build/sanitize_setup/saves --shots build/sanitize_setup --keys '91\#32x3xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\eq:\\x\b\r\kxP\e' build/sanitize_setup/assets)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in the start menu)" && printf '%s\n' "$$out" | grep -q '^crt: USING DEFAULT PATH: C:\\COK\\SAVE$$' && test "$$(head -n 4 build/sanitize_setup/saves/KRYNN.CFG | od -An -c | tr -d ' \n')" = 'C\r\nR\r\nN\r\nC:\COK\SAVE\\r\n'
	$(if $(HAVE_SDL),./build/test_keymap_sanitize,@echo "SDL2 not found (pkg-config sdl2): skipped build/cok_sanitize")
	$(if $(HAVE_SDL),test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --assets build/sanitize_setup/assets --saves build/sanitize_setup/cok --no-delay --keys '91\#32x3xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\eq:\\x\b\r\kxP\e')" = "(out of keys in the start menu)" && cmp build/sanitize_setup/saves/KRYNN.CFG build/sanitize_setup/cok/KRYNN.CFG)
	$(if $(HAVE_SDL),rm -rf build/sanitize_cok && test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --saves build/sanitize_cok --no-delay --seed 7 --helm --log --log-times --keys '$(COK_GAME)' 2>build/sanitize_cok.log)" = "(out of keys in the start menu)" && ! grep -E 'Sanitizer|runtime error' build/sanitize_cok.log && grep -q '^[0-9]* present: ' build/sanitize_cok.log)
	rm -rf build/sanitize_mouse && mkdir -p build/sanitize_mouse && test "$$(./build/eclplay_sanitize --start-menu --mouse --seed 5 --saves build/sanitize_mouse --shots build/sanitize_mouse --keys '$(COK_CLICKS)' COKAssets | tail -n 1)" = "(out of keys in the start menu)" && test -f build/sanitize_mouse/BOB.WHO
	$(if $(HAVE_SDL),rm -rf build/sanitize_stick && mkdir -p build/sanitize_stick && printf '$(COK_STICK_CFG)' > build/sanitize_stick/KRYNN.CFG && test "$$(SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_sanitize --saves build/sanitize_stick --no-delay --keys '$(COK_STICK)')" = "(out of keys in the start menu)" && test "$$(sed -n 3p build/sanitize_stick/KRYNN.CFG)" = "$$(printf 'J\r')")
	$(if $(HAVE_SDL),rm -rf build/sanitize_cok && test "$$(TSAN_OPTIONS='halt_on_error=1 exitcode=66' SDL_VIDEODRIVER=dummy SDL_RENDER_DRIVER=software SDL_AUDIODRIVER=dummy ./build/cok_tsan --saves build/sanitize_cok --no-delay --sound-clock real --seed 7 --helm --keys '\p50.$(COK_GAME)')" = "(out of keys in the start menu)")
