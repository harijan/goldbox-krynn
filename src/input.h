#ifndef COK_INPUT_H
#define COK_INPUT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The mouse and the joystick, without SDL: the machine's mouse driver (INT
 * 33h) and game port (port 0x201) as the front end feeds them, and the
 * original's units over them, the mouse's (segment 1743) and the
 * joystick's (177c). KRYNN.CFG's third line picks one (11c7:031a: J the
 * joystick, DS:89ce; M the mouse, DS:8986); startup finds the mouse's
 * driver (3e99:005b) and the stick (177c:0000) or turns them off. */

/* The mouse driver, as a Microsoft-compatible one keeps its state in the
 * game's 320x200 modes: the pointer in its virtual coordinates, x 0-639
 * (two to a pixel) and y 0-199; the buttons (bit 0 the left, 1 the right,
 * 2 the middle); the cursor's show counter, drawn at 0 (reset leaves -1,
 * show (function 1) adds one up to 0, hide (2) takes one); and the
 * graphics cursor's masks (function 9). A front end sets x, y and buttons
 * from its pointer; the game's moves (function 4) set moved, so that it
 * can follow. */
typedef struct {
    bool installed;        /* A driver answers INT 33h (the front end's). */
    int x, y;              /* Virtual: 0-639, 0-199. */
    unsigned buttons;
    bool moved;            /* Set by function 4 and reset (0); the front end clears it. */
    int counter;
    const uint16_t *masks; /* 16 words of screen mask, then 16 of cursor mask; NULL none. */
    int hot_x, hot_y;
} cok_mouse_driver;

/* Function 0, reset: false without a driver; else the pointer at the
 * screen's centre (320, 100), hidden (-1), the buttons' count in
 * *buttons. Functions 1 and 2, show and hide. Function 3 into *x, *y and
 * *buttons; 4 to x, y, kept on the screen. */
bool cok_mouse_driver_reset(cok_mouse_driver *driver, unsigned *buttons);
void cok_mouse_driver_show(cok_mouse_driver *driver);
void cok_mouse_driver_hide(cok_mouse_driver *driver);
void cok_mouse_driver_get(const cok_mouse_driver *driver, int *x, int *y, unsigned *buttons);
void cok_mouse_driver_set(cok_mouse_driver *driver, int x, int y);

/* A front end's pointer, one axis: window pixel p of a screen shown from
 * origin across size pixels, as the driver's coordinate (units of them
 * across: 640, or 200 down), taken at the pixel's centre, kept on the
 * screen. cok_window_axis is its inverse: the first window pixel whose
 * centre gives coordinate v, or one in its group of group coordinates
 * (2 across, a game pixel; 1 down), or the nearest when the window is too
 * small to show it. */
int cok_pointer_axis(long p, long origin, long size, int units);
long cok_window_axis(int v, int group, long origin, long size, int units);

/* The cursors the game gives function 9 (3e99:005b): DS:42b4, an arrow one
 * bit a pixel, 16 pixels across, in the Tandy and EGA modes; DS:42f4, an
 * arrow two bits a pixel, 8 across, in CGA mode. 16 words of screen mask,
 * then 16 of cursor mask, each word's high bit the leftmost. */
extern const uint16_t cok_mouse_arrow[32];
extern const uint16_t cok_mouse_arrow_cga[32];

/* Draw the cursor where the driver draws it while shown (counter 0) on a
 * 320x200 screen of one pixel a nibble (the Tandy layout, high nibble
 * first, 160 bytes a row): at pixel (x / 2 - hot_x, y - hot_y), each pixel
 * ANDed with its screen mask bit and XORed with its cursor mask bit as the
 * driver does in 16-colour modes, 1 giving 15; in CGA mode (cga, the
 * screen's nibbles CGA colours 0-3) two bits a pixel, a mask's bit pairs
 * ANDed and XORed with the colour. Clipped at the screen's edges. Nothing
 * without a driver or with the cursor hidden. */
void cok_mouse_draw(const cok_mouse_driver *driver, uint8_t *pixels, bool cga);

/* The mouse unit (segment 1743), the game's side: DS:8986 the mouse on,
 * DS:8984 the driver's x in pixels (no halving), DS:8988 its buttons;
 * DS:8985 clicks on the party list pick a character (67b5:0674); DS:8987
 * the last key read (1614:025b) was a click, at DS:8989, 898b (pixels). */
typedef struct {
    cok_mouse_driver driver;
    bool on;               /* DS:8986 */
    bool narrow;           /* DS:8984 */
    uint8_t button_count;  /* DS:8988 */
    bool party_clicks;     /* DS:8985 */
} cok_mouse;

/* 1743:0000: on = the driver answers; its buttons' count. */
void cok_mouse_reset(cok_mouse *mouse);
/* 1743:0030, 0051: show and hide, only when on. */
void cok_mouse_show(cok_mouse *mouse);
void cok_mouse_hide(cok_mouse *mouse);
/* 1743:0072: the buttons and position, x halved unless narrow; nothing
 * written when off. */
void cok_mouse_get(const cok_mouse *mouse, unsigned *buttons, int16_t *x, int16_t *y);
/* 1743:00c9: move the pointer, x doubled unless narrow; only when on. */
void cok_mouse_set(cok_mouse *mouse, int16_t x, int16_t y);
/* 1743:0107: the graphics cursor, only when on. */
void cok_mouse_cursor(cok_mouse *mouse, const uint16_t masks[32], int hot_x, int hot_y);
/* The mouse's part of startup (3e99:005b, 3e99:0085-0134) and of turning
 * it on in Initialize Mouse/Joystick (4def:5eb9-5f4c): 8985 set, the
 * driver reset, and with one the cursor for the display (cga: DS:42f4),
 * the pointer to (0x27f, 100) unhalved and read back for 8984, and the
 * cursor shown. */
void cok_mouse_start(cok_mouse *mouse, bool cga);

/* 1743:0167: the direction from cell (fx, fy) to cell (tx, ty) as a key's
 * scan code: 0x48 up, 0x49 up-right, 0x4d right, 0x51 down-right, 0x50
 * down, 0x4f down-left, 0x4b left, 0x47 up-left, the first whose sector
 * (22.5 degrees each side of the axes, tan 67.5 and 22.5 as 0x26a/256 and
 * 0x6a/256 of the column distance in 16 bits) holds, in that order; the
 * same cell is up. The original loops for ever when none holds, which
 * needs a column distance past 300 (a 16-bit overflow): 0 here. */
uint8_t cok_mouse_direction(int16_t fx, int16_t fy, int16_t tx, int16_t ty);

/* The game port: 177c:060d's read of stick 1 or 2, as the front end has
 * it: false where the original's fails (no stick: an axis's bit never
 * clears); else the counts, the reads of port 0x201 an axis's bit stays
 * set for after the one-shots fire, plus one, and the four buttons (bit 0
 * A1, 1 A2, 2 B1, 3 B2). wait lets ms of polling pass, or less if the
 * stick changes, where the original polls in a loop (the calibration's
 * waits; each read takes the original about 2 x 8191 empty loops). Either
 * may be NULL: no stick. */
typedef struct {
    bool (*read)(void *context, unsigned stick, uint16_t *x, uint16_t *y, uint8_t *buttons);
    void (*wait)(void *context, unsigned ms);
    void *context;
} cok_game_port;

/* The joystick unit (177c): DS:89ce the joystick on, and stick 1's
 * thresholds, seven across (DS:8996) and seven down (89a4): the extreme,
 * a third and two thirds to the centre, the centre, and so on to the
 * other extreme. */
typedef struct {
    bool on;
    uint16_t x[7], y[7];
} cok_stick;

/* The time the port gives a read of the stick where the original polls
 * it (its loops, about 20 ms on a 286), and the calibration's polls
 * before it gives up (20000). */
enum { COK_STICK_READ_MS = 20, COK_STICK_POLLS = 20000 };

/* 177c:060d through the port; false for stick 0 or above 2. */
bool cok_stick_read(const cok_game_port *port, unsigned stick, uint16_t *x, uint16_t *y,
                    uint8_t *buttons);
/* 177c:0000: the thresholds cleared; on only if stick 1 reads. */
void cok_stick_init(cok_stick *stick, const cok_game_port *port);
/* 177c:003b for stick 1: 0 up, clockwise to 7 up-left, 8 none: each axis's
 * band is the last of thresholds 1-5 below its count, and only the outer
 * bands give a direction (DS:4334). 8 when off or the read fails. */
uint8_t cok_stick_direction(const cok_stick *stick, const cok_game_port *port);
/* 177c:0107 for stick 1: button 1 (A1) or 2 (A2) down; false when off. */
bool cok_stick_button(const cok_stick *stick, const cok_game_port *port, unsigned button);
/* 177c:018d: *buttons 2 for button 2, else 1 for button 1, else 0; then
 * the direction. */
void cok_stick_poll(const cok_stick *stick, const cok_game_port *port, uint8_t *direction,
                    uint16_t *buttons);
/* DS:4358: the key a direction types (1614:00fa), the keypad's digit. */
char cok_stick_key(uint8_t direction);

/* The calibrations' screens: the stick pushed to the upper left
 * (COK_STICK_UPPER_LEFT) or lower right, and the failures, after which the
 * caller waits two seconds: the button not pressed (COK_STICK_FAILED), or
 * a read lost while it is held at the upper left or the lower right
 * (dkk 49cb:58ec words them apart; Champions' are one line). */
typedef enum {
    COK_STICK_UPPER_LEFT,
    COK_STICK_LOWER_RIGHT,
    COK_STICK_FAILED,
    COK_STICK_LOST_UPPER_LEFT,
    COK_STICK_LOST_LOWER_RIGHT,
} cok_stick_prompt;

/* The calibration (177c:029d in the setup, 4def:638d in Initialize
 * Mouse/Joystick; the same but for their screens, shown by prompt): 177c:0000;
 * with the stick on, the centre the mean of 20 reads; then for each
 * corner, its prompt, up to COK_STICK_POLLS polls for button 1 (the
 * failure's prompt and false when it does not come, the thresholds
 * partly set and on left set), the least (upper left) or most counts of
 * 20 reads with it held (a read failing fails), and the thresholds a
 * third and two thirds of the way; between the corners a wait for the
 * button's release. True when calibrated or with no stick (on clear). The
 * arithmetic is the original's, 16 bits unsigned. aborted is checked
 * after each wait: input ended. */
bool cok_stick_calibrate(cok_stick *stick, const cok_game_port *port,
                         void (*prompt)(cok_stick_prompt which, void *context), void *context,
                         const bool *aborted);

#endif
