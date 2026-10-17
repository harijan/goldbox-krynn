/* KRYNN.CFG (src/config.c): its first line, the display, and its second,
 * the sound device, read as 11c7:031a reads them with Turbo Pascal's Read
 * and ReadLn; and the file found in the assets directory. */
#define _POSIX_C_SOURCE 200809L /* mkdtemp */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)

static cok_config parsed(const char *text)
{
    return cok_config_parse((const uint8_t *)text, strlen(text));
}

static cok_sound_device sound(const char *text)
{
    return parsed(text).sound;
}

static cok_display display(const char *text)
{
    return parsed(text).display;
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL);
    fputs(text, f);
    fclose(f);
}

static void test_display(void)
{
    CHECK(display("E\r\nT\r\nM\r\n") == COK_DISPLAY_EGA);
    CHECK(display("T\r\nT\r\nM\r\n") == COK_DISPLAY_TANDY);
    CHECK(display("C\r\nT\r\nM\r\n") == COK_DISPLAY_CGA);   /* the setup's [1] CGA */
    CHECK(display("X\r\nT\r\n") == COK_DISPLAY_CGA);        /* anything else */
    CHECK(display("e\r\nT\r\n") == COK_DISPLAY_CGA);        /* upper case only */
    CHECK(display(" E\r\nT\r\n") == COK_DISPLAY_CGA);       /* no space skipped */
    CHECK(display("EGA\r\nT\r\n") == COK_DISPLAY_EGA);      /* the first character */
    CHECK(display("Tandy\r\n") == COK_DISPLAY_TANDY);
    CHECK(display("\r\nT\r\n") == COK_DISPLAY_CGA);         /* an empty line: CR */
    CHECK(display("") == COK_DISPLAY_CGA);                  /* Read gives Ctrl-Z */
    CHECK(display("\x1a" "E\r\n") == COK_DISPLAY_CGA);
    CHECK(display("E") == COK_DISPLAY_EGA);                 /* one line */
    CHECK(display("T\nE\n") == COK_DISPLAY_TANDY);
    CHECK(strcmp(cok_display_name(COK_DISPLAY_CGA), "CGA") == 0);
    CHECK(strcmp(cok_display_name(COK_DISPLAY_EGA), "EGA") == 0);
    CHECK(strcmp(cok_display_name(COK_DISPLAY_TANDY), "Tandy") == 0);
}

static void test_sound(void)
{
    CHECK(sound("E\r\nC\r\nM\r\n") == COK_SOUND_CMS);
    CHECK(sound("E\r\nR\r\nM\r\n") == COK_SOUND_ROLAND);
    CHECK(sound("E\r\nA\r\nM\r\n") == COK_SOUND_ADLIB);
    CHECK(sound("E\r\nT\r\nM\r\n") == COK_SOUND_TANDY);
    CHECK(sound("E\r\nP\r\nM\r\n") == COK_SOUND_SPEAKER);
    CHECK(sound("E\r\nS\r\nM\r\n") == COK_SOUND_NONE); /* [6] Silent */
    CHECK(sound("E\r\nTandy\r\n") == COK_SOUND_TANDY); /* the first character */
    CHECK(sound("E\r\nt\r\n") == COK_SOUND_NONE);      /* upper case only */
    CHECK(sound("E\r\n T\r\n") == COK_SOUND_NONE);     /* no space skipped */
    CHECK(sound("E\r\n\r\nT\r\n") == COK_SOUND_NONE); /* an empty line: CR */
    CHECK(sound("Tandy-display\r\nP") == COK_SOUND_SPEAKER); /* the rest of line 1 skipped */
    CHECK(sound("E\rP\r") == COK_SOUND_SPEAKER);         /* CR alone ends a line */
    CHECK(sound("E\nP\n") == COK_SOUND_NONE);            /* LF alone does not */
    CHECK(sound("E") == COK_SOUND_NONE);                 /* one line */
    CHECK(sound("") == COK_SOUND_NONE);
    CHECK(sound("E\x1a\r\nT\r\n") == COK_SOUND_NONE);  /* ReadLn stops at Ctrl-Z */
    CHECK(sound("\x1a\r\nT\r\n") == COK_SOUND_TANDY);  /* Read passes over one */
}

static void test_files(void)
{
    cok_config config = {.display = COK_DISPLAY_TANDY, .sound = COK_SOUND_NONE};
    CHECK(cok_config_read("COKAssets", &config));
    CHECK(config.display == COK_DISPLAY_EGA && config.sound == COK_SOUND_TANDY);
    char dir[] = "/tmp/cok-config-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    char path[100];
    config = (cok_config){.display = COK_DISPLAY_TANDY, .sound = COK_SOUND_CMS};
    CHECK(!cok_config_read(dir, &config)); /* none: unchanged */
    CHECK(config.display == COK_DISPLAY_TANDY && config.sound == COK_SOUND_CMS);
    snprintf(path, sizeof path, "%s/krynn.cfg", dir);
    write_file(path, "C\r\nP\r\nN\r\n");
    CHECK(cok_config_read(dir, &config));
    CHECK(config.display == COK_DISPLAY_CGA && config.sound == COK_SOUND_SPEAKER);
    unlink(path);
    snprintf(path, sizeof path, "%s/KRYNN.CFG", dir);
    CHECK(mkdir(path, 0755) == 0); /* not a file: unreadable */
    config = (cok_config){.display = COK_DISPLAY_TANDY, .sound = COK_SOUND_CMS};
    CHECK(!cok_config_read(dir, &config));
    CHECK(config.display == COK_DISPLAY_TANDY && config.sound == COK_SOUND_CMS);
    rmdir(path);
    rmdir(dir);
}

int main(void)
{
    test_display();
    test_sound();
    test_files();
    puts("config: ok");
    return 0;
}
