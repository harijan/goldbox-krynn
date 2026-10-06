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

ADVENTURE_SRC = src/adventure.c src/ecl.c src/menu.c src/screen.c src/text.c src/picture.c src/image.c src/dax.c
ADVENTURE_DEPS = $(ADVENTURE_SRC) src/adventure.h src/ecl.h src/menu.h src/screen.h src/text.h src/picture.h src/image.h src/dax.h

build/eclplay: src/eclplay.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) src/eclplay.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

build/test_ecl: tests/test_ecl.c src/ecl.c src/ecl.h src/text.h src/picture.h src/dax.c src/dax.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_ecl.c src/ecl.c src/dax.c $(LDFLAGS) $(LDLIBS) -o $@

build/test_menu: tests/test_menu.c src/menu.c src/menu.h src/screen.c src/screen.h src/text.c src/text.h src/picture.c src/picture.h src/image.c src/image.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_menu.c src/menu.c src/screen.c src/text.c src/picture.c src/image.c $(LDFLAGS) $(LDLIBS) -o $@

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

test: build/test_dax build/test_image build/test_picture build/test_text build/test_ecl build/test_menu build/daxcheck build/daximages build/daxcompose build/ecldump build/eclplay
	./build/test_dax
	./build/test_image
	./build/test_picture
	./build/test_text
	./build/test_ecl
	./build/test_menu
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

build/eclplay_sanitize: src/eclplay.c $(ADVENTURE_DEPS) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer src/eclplay.c $(ADVENTURE_SRC) $(LDFLAGS) $(LDLIBS) -o $@

sanitize: build/test_dax_sanitize build/test_image_sanitize build/test_picture_sanitize build/test_text_sanitize build/test_ecl_sanitize build/test_menu_sanitize build/eclplay_sanitize build/daxcheck_sanitize
	./build/test_dax_sanitize
	./build/test_image_sanitize
	./build/test_picture_sanitize
	./build/test_text_sanitize
	./build/test_ecl_sanitize
	./build/test_menu_sanitize
	./build/eclplay_sanitize --keys '\r\r\r\r\r\r\r\r' Assets 16 > /dev/null
	./build/eclplay_sanitize --start 899b --keys '\r22\r\r' Assets 48 > /dev/null
	./build/daxcheck_sanitize Assets/*.DAX
