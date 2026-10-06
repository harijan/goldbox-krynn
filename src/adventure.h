#ifndef COK_ADVENTURE_H
#define COK_ADVENTURE_H

#include "ecl.h"
#include "menu.h"
#include "picture.h"
#include "text.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The adventure screen's side of the ECL opcodes, from the handlers in
 * overlay 2fd3: text, menus, input, pictures and delays. Other opcodes go
 * to hooks.unported. The screen is 320x200 in the Tandy layout. */

enum {
    COK_ADVENTURE_FRAMES = 16, /* Small picture frames kept (DS:6da2 holds 8). */
    COK_ADVENTURE_NO_PICTURE = 0xff,
};

typedef struct cok_adventure cok_adventure;

typedef struct {
    /* An opcode not carried out here, with its operands decoded in
     * game->vm. NULL ignores it. */
    void (*unported)(cok_adventure *game, void *context);
    /* Text as it is printed, menus as they are shown (items with their ~
     * marks), input as it is read, and pictures that fail to load: kind is
     * "print", "menu", "list", "item", "choice", "input" or "error". */
    void (*log)(cok_adventure *game, const char *kind, const char *text, void *context);
    /* Before each instruction, as cok_ecl_hooks.trace. */
    void (*trace)(cok_adventure *game, void *context);
    /* Wait ms milliseconds (Crt.Delay): after each printed character, speed
     * * 3, and for DELAY, speed * 100. NULL does not wait. */
    void (*delay)(cok_adventure *game, unsigned ms, void *context);
    void *context;
} cok_adventure_hooks;

struct cok_adventure {
    cok_ecl vm;
    char assets[512];      /* Directory holding the DAX files. */
    cok_picture screen;    /* 40 units by 200 rows. */
    cok_font font;         /* 8X8D1.DAX record 201. */
    cok_picture tiles;     /* Tile set 4, 8X8D1.DAX record 202; see screen.h. */

    /* The small picture (DS:6da2): frames from PIC<file>.DAX, each with its
     * delay in hundredths of a second, drawn at cell 3, 3. */
    cok_picture frames[COK_ADVENTURE_FRAMES];
    uint32_t delays[COK_ADVENTURE_FRAMES];
    size_t frame_count;
    size_t frame;          /* DS:6da3, 0-based here. */
    uint8_t picture_id;    /* DS:6e00, COK_ADVENTURE_NO_PICTURE if none. */
    uint8_t picture_file;
    cok_picture big;       /* BIGPIC<file>.DAX, drawn at cell 1, 1 (DS:6e02). */
    uint8_t big_id;        /* DS:6e06, COK_ADVENTURE_NO_PICTURE if none. */
    bool picture_shown;    /* DS:884a: the view holds a picture. */
    bool view_replaced;    /* DS:884b. */
    bool big_shown;        /* DS:4b4e: hides the status line. */

    uint8_t speed;         /* DS:4b38, the game speed; 4 by default. */
    bool animate;          /* DS:4b4f: load every frame of a picture. */
    uint8_t selected;      /* DS:6e0f, the menu item selected. */
    int list_top;          /* DS:6e0d, the first item a list shows. */
    bool input_ended;      /* The keyboard ran out; the run was aborted. */

    cok_keyboard keys;
    cok_adventure_hooks hooks;
    char error[600];       /* Why the last call failed. */
};

/* Load the font from assets and set up the VM with this module's hooks.
 * keys and hooks may be NULL. Returns false with game->error set. Free with
 * cok_adventure_close even on failure. */
bool cok_adventure_open(cok_adventure *game, const char *assets, const cok_keyboard *keys,
                        const cok_adventure_hooks *hooks);
void cok_adventure_close(cok_adventure *game);

/* Load ECL block from ECL<game->vm.file>.DAX and start it (3775:0361,
 * 3775:01e8), resetting picture state as the original does. */
cok_ecl_status cok_adventure_load(cok_adventure *game, uint8_t block);

/* Load block and enter it as 2fd3:3b47 does: run the load vector, then the
 * after-move and location vectors, and start over from the load vector
 * whenever NEWECL switches blocks. */
cok_ecl_status cok_adventure_enter(cok_adventure *game, uint8_t block);

/* Draw the adventure screen's frame (1128:0242). */
void cok_adventure_frame(cok_adventure *game);

/* Read a record by id from <name><file>.DAX in the asset directory, as
 * 169c:088e does; the first record with the id wins. Returns a malloc'd
 * buffer, or NULL with game->error set. */
uint8_t *cok_adventure_record(cok_adventure *game, const char *name, unsigned file,
                              uint8_t id, size_t *size);

#endif
