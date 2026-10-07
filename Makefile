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

ADVENTURE_SRC = src/adventure.c src/arena.c src/combat.c src/round.c src/monster.c src/treasure.c src/shop.c src/camp.c src/magic.c src/cast.c src/items.c src/sheet.c src/effect.c src/party.c src/ecl.c src/menu.c src/screen.c src/text.c src/view.c src/picture.c src/image.c src/dax.c
ADVENTURE_DEPS = $(ADVENTURE_SRC) src/adventure.h src/arena.h src/combat.h src/round.h src/monster.h src/treasure.h src/shop.h src/camp.h src/magic.h src/cast.h src/items.h src/sheet.h src/effect.h src/party.h src/ecl.h src/menu.h src/screen.h src/text.h src/view.h src/picture.h src/image.h src/dax.h

build/eclplay: src/eclplay.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/eclplay.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_adventure: tests/test_adventure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_adventure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

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

build/test_treasure: tests/test_treasure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_treasure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_shop: tests/test_shop.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_shop.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_items: tests/test_items.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_items.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

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

build/test_image: tests/test_image.c src/image.c src/image.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_image.c src/image.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

test: build/START_FULL.EXE build/test_dax build/test_image build/test_picture build/test_text build/test_ecl build/test_menu build/test_view build/test_party build/test_effect build/test_adventure build/test_camp build/test_cast build/test_items build/test_monster build/test_combat build/test_round build/test_arena build/test_treasure build/test_shop build/daxcheck build/daximages build/daxcompose build/ecldump build/eclplay
	./build/test_dax
	./build/test_image
	./build/test_picture
	./build/test_text
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
	./build/test_treasure
	./build/test_shop
	./build/ecldump --summary Assets/ECL*.DAX
	$(PYTHON) tests/test_images_cli.py
	$(PYTHON) tests/test_eclplay.py
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

build/test_arena_sanitize: tests/test_arena.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_arena.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_shop_sanitize: tests/test_shop.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_shop.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_treasure_sanitize: tests/test_treasure.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer tests/test_treasure.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/eclplay_sanitize: src/eclplay.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer src/eclplay.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

sanitize: build/START_FULL.EXE build/test_dax_sanitize build/test_image_sanitize build/test_picture_sanitize build/test_text_sanitize build/test_ecl_sanitize build/test_menu_sanitize build/test_view_sanitize build/test_party_sanitize build/test_effect_sanitize build/test_adventure_sanitize build/test_camp_sanitize build/test_cast_sanitize build/test_items_sanitize build/test_monster_sanitize build/test_combat_sanitize build/test_round_sanitize build/test_arena_sanitize build/test_treasure_sanitize build/test_shop_sanitize build/eclplay_sanitize build/daxcheck_sanitize
	./build/test_dax_sanitize
	./build/test_image_sanitize
	./build/test_picture_sanitize
	./build/test_text_sanitize
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
	./build/test_treasure_sanitize
	./build/test_shop_sanitize
	test "$$(./build/eclplay_sanitize --test-party 6 --keys '\r\rE\r\r\r\r\r\r\r' Assets 16 | tail -n 1)" = "(out of keys in block 17 at 9d9b)"
	test "$$(./build/eclplay_sanitize --test-party 6 --start 899b --keys '\r22\r\rE\r\r\r\r' Assets 48 | tail -n 1)" = "(done in block 48)"
	out="$$(./build/eclplay_sanitize --test-party 6 --play --set 4be6=1 --keys '\r\rE\rm\^\^\^\r\r\>\^\<\<\v\rlsea' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 80c0)" && printf '%s\n' "$$out" | grep -q '^print: A MAN GIBBERING WITH FEAR' && printf '%s\n' "$$out" | grep -q '^print: The party makes camp'
	out="$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 835f --keys '\r\rE' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(done in block 32)" && test "$$(printf '%s\n' "$$out" | head -n 1)" = "print: MONSTERS ATTACK!" && test "$$(printf '%s\n' "$$out" | grep -c '^round: ')" = 15
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --load SAVE/SAVGAMA.DAT --play --keys '\r\r\r\r\r\r\r' Assets | tail -n 1)" = "(out of keys in block 17 at 86ec)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --play --set 4be6=1 --keys '\r\rEerhar\kn\eamm\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemm\^m\esd\e\eaos\v\e\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --start 9891 --set 4be6=1 --keys '\r\rE' Assets 32 | tail -n 1)" = "(done in block 32)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"
	test "$$(./build/eclplay_sanitize --test-party 6 --file 3 --start 99fd --combat won --shots build/sanitize_shots --keys '\r\r\r' Assets 97 | tail -n 1)" = "(out of keys in block 97 at 9a06)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rEvi\v\vrhjtS\e\e' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 8320)"; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --play --set 4be6=1 --keys '\r\rE\v\v\v\vemc\rn\^\^\^\r\^S\^\^\rSee' Assets 32 | tail -n 1)" = "(out of keys in block 32 at 80c0)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --start 828a --keys '\r\rE\r' Assets 33 | tail -n 1)" = "(done in block 33)"
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --set 4be6=1 --start 851e --keys 'wwps\r\r' Assets 33 | tail -n 1)" = "(out of keys in block 33 at 87b5)"; fi
	test "$$(./build/eclplay_sanitize --test-party 6 --start 8bcd --keys '\rT\r3\r\eSE' Assets 32 | tail -n 1)" = "(done in block 32)"
	for how in won fled; do test "$$(./build/eclplay_sanitize --test-party 6 --combat $$how --start 8cda --keys '\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done
	test "$$(./build/eclplay_sanitize --test-party 6 --combat gods --start 8cda --keys 'Y\r\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --test-party 6 --start 8cda --keys '\rE\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --test-party 6 --combat lost --start 8cda --keys '\r' Assets 16 | tail -n 1)" = "(the party was killed in block 16)"
	if [ -f SAVE/SAVGAMA.DAT ]; then for how in won fled; do test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat $$how --start 8cda --keys '\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)" || exit 1; done; fi
	if [ -f SAVE/SAVGAMA.DAT ]; then test "$$(./build/eclplay_sanitize --party SAVE/SAVGAMA.DAT --combat gods --start 8cda --keys '\r\rSE\r' Assets 16 | tail -n 1)" = "(done in block 16)"; fi
	test "$$(./build/eclplay_sanitize --test-party 4 --start 891c --keys 'y\rB\v\v\r\ePB\r\eEySE' Assets 16 | tail -n 1)" = "(out of keys in block 16 at 8dd1)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8cea --keys 'B\r\eE' Assets 50 | tail -n 1)" = "(out of keys in block 50 at 9d2f)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8782 --keys 'B\r\eVI\v\vSyI\ry\e\eE' Assets 17 | tail -n 1)" = "(out of keys in block 17 at 86ec)"
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8a8f --keys '\rPH\v\vHy\eEn\r' Assets 16 | tail -n 1)" = "(done in block 16)"
	test "$$(./build/eclplay_sanitize --test-party 6 --set 4be6=1 --combat-map build/combat-map.txt --keys '\r\rE' Assets 32 | tail -n 1)" = "(done in block 32)"
	test "$$(head -n 1 build/combat-map.txt)" = "battle in block 32: view 24,10, 21 combatants"
	out="$$(./build/eclplay_sanitize --test-party 6 --play --set 4be6=1 --keys '\r\rE\ram\^\^\>eaa' Assets 32)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 32 at 8320)" && test "$$(printf '%s\n' "$$out" | grep '^area: ' | tr '\n' ' ')" = "area: on area: off area: on "
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --keys '\r\rea' Assets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 8287)" && printf '%s\n' "$$out" | grep -q '^print: Not Here$$'
	out="$$(./build/eclplay_sanitize --test-party 2 --play --set 4be6=1 --helm --keys '\r\ream\^' Assets 34)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 34 at 98e6)" && test "$$(printf '%s\n' "$$out" | grep '^area: \|^at: ' | tr '\n' ' ')" = "area: on at: 1,0,2 area: off "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --keys 'nm98yLn2' Assets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 8824)" && test "$$(printf '%s\n' "$$out" | grep '^overland: ' | tr '\n' ' ')" = "overland: 2,2,1 overland: 2,1,0 overland: 2,2,4 "
	out="$$(./build/eclplay_sanitize --test-party 4 --play --set 4bf2=11 --set 4bc3=1 --set 4bc4=3 --set 4c2d=1 --seed 3 --combat won --keys 'nm\>\>\>\>\>\>\>\>\>\>\>\r\rENm\veee' Assets 16)" && test "$$(printf '%s\n' "$$out" | tail -n 1)" = "(out of keys in block 16 at 828b)" && printf '%s\n' "$$out" | grep -q '^print: YOU HAVE DISCOVERED SOME HOSTILE CREATURES.$$' && printf '%s\n' "$$out" | grep -q '^overland: 12,4,4$$'
	test "$$(./build/eclplay_sanitize --test-party 4 --start 8890 --keys 'ye' Assets 16 | tail -n 1)" = "(done in block 16)"
	./build/daxcheck_sanitize Assets/*.DAX
