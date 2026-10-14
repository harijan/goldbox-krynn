#include "input.h"

#include <string.h>

/* The driver. */

enum { VIRTUAL_W = 640, SCREEN_H = 200 };

bool cok_mouse_driver_reset(cok_mouse_driver *driver, unsigned *buttons)
{
    if (!driver->installed) return false;
    driver->x = VIRTUAL_W / 2;
    driver->y = SCREEN_H / 2;
    driver->moved = true;
    driver->counter = -1;
    *buttons = 2;
    return true;
}

void cok_mouse_driver_show(cok_mouse_driver *driver)
{
    if (driver->counter < 0) ++driver->counter;
}

void cok_mouse_driver_hide(cok_mouse_driver *driver)
{
    --driver->counter;
}

void cok_mouse_driver_get(const cok_mouse_driver *driver, int *x, int *y, unsigned *buttons)
{
    *x = driver->x;
    *y = driver->y;
    *buttons = driver->buttons;
}

void cok_mouse_driver_set(cok_mouse_driver *driver, int x, int y)
{
    driver->x = x < 0 ? 0 : x > VIRTUAL_W - 1 ? VIRTUAL_W - 1 : x;
    driver->y = y < 0 ? 0 : y > SCREEN_H - 1 ? SCREEN_H - 1 : y;
    driver->moved = true;
}

int cok_pointer_axis(long p, long origin, long size, int units)
{
    if (size <= 0) return 0;
    long twice = 2 * (p - origin) + 1;
    if (twice < 0) return 0;
    long v = twice * units / (2 * size);
    return (int)(v > units - 1 ? units - 1 : v);
}

long cok_window_axis(int v, int group, long origin, long size, int units)
{
    int lo = v - v % group, hi = lo + group - 1;
    long first = size > 0 ? origin + (long)lo * size / units : origin;
    for (long p = first - 2; p <= first + size / units + 3; ++p) {
        int at = cok_pointer_axis(p, origin, size, units);
        if (p >= origin && at >= lo && at <= hi) return p;
    }
    return first;
}

/* DS:42b4 and DS:42f4. */
const uint16_t cok_mouse_arrow[32] = {
    0x3fff, 0x1fff, 0x0fff, 0x07ff, 0x03ff, 0x01ff, 0x00ff, 0x007f,
    0x003f, 0x003f, 0x01ff, 0x00ff, 0x10ff, 0xf87f, 0xf87f, 0xfc7f,
    0x0000, 0x4000, 0x6000, 0x7000, 0x7800, 0x7c00, 0x7e00, 0x7f00,
    0x7f80, 0x7c00, 0x6c00, 0x4600, 0x0600, 0x0300, 0x0300, 0x0000,
};
const uint16_t cok_mouse_arrow_cga[32] = {
    0x0fff, 0x03ff, 0x00ff, 0x003f, 0x000f, 0x0003, 0x0000, 0x0003,
    0x0003, 0x3f03, 0xff00, 0xff00, 0xffc3, 0xffff, 0xffff, 0xffff,
    0x0000, 0x3000, 0x3c00, 0x3f00, 0x3fc0, 0x3ff0, 0x3ffc, 0x3fc0,
    0x3cf0, 0x00f0, 0x003c, 0x003c, 0x0000, 0x0000, 0x0000, 0x0000,
};

void cok_mouse_draw(const cok_mouse_driver *driver, uint8_t *pixels, bool cga)
{
    if (!driver->installed || driver->counter < 0 || driver->masks == NULL) return;
    int left = driver->x / 2 - driver->hot_x, top = driver->y - driver->hot_y;
    int width = cga ? 8 : 16, bits = cga ? 2 : 1;
    unsigned all = cga ? 3u : 15u;
    for (int row = 0; row < 16; ++row) {
        int y = top + row;
        if (y < 0 || y >= SCREEN_H) continue;
        unsigned screen = driver->masks[row], cursor = driver->masks[16 + row];
        for (int i = 0; i < width; ++i) {
            int x = left + i;
            if (x < 0 || x >= VIRTUAL_W / 2) continue;
            unsigned shift = (unsigned)(16 - bits * (i + 1));
            unsigned and_bits = (screen >> shift) & ((1u << bits) - 1u);
            unsigned xor_bits = (cursor >> shift) & ((1u << bits) - 1u);
            if (!cga) {
                and_bits = and_bits ? all : 0;
                xor_bits = xor_bits ? all : 0;
            }
            uint8_t *at = &pixels[y * (VIRTUAL_W / 4) + x / 2];
            unsigned high = (x & 1) == 0;
            unsigned pixel = high ? *at >> 4 : *at & 15u;
            pixel = ((pixel & and_bits) ^ xor_bits) & all;
            *at = (uint8_t)(high ? (*at & 0x0f) | pixel << 4 : (*at & 0xf0) | pixel);
        }
    }
}

/* The mouse unit (1743). */

void cok_mouse_reset(cok_mouse *mouse)
{
    unsigned buttons = 0;
    mouse->on = cok_mouse_driver_reset(&mouse->driver, &buttons);
    if (mouse->on) mouse->button_count = (uint8_t)buttons;
}

void cok_mouse_show(cok_mouse *mouse)
{
    if (mouse->on) cok_mouse_driver_show(&mouse->driver);
}

void cok_mouse_hide(cok_mouse *mouse)
{
    if (mouse->on) cok_mouse_driver_hide(&mouse->driver);
}

void cok_mouse_get(const cok_mouse *mouse, unsigned *buttons, int16_t *x, int16_t *y)
{
    if (!mouse->on) return;
    int vx, vy;
    cok_mouse_driver_get(&mouse->driver, &vx, &vy, buttons);
    *x = (int16_t)(mouse->narrow ? vx : vx / 2);
    *y = (int16_t)vy;
}

void cok_mouse_set(cok_mouse *mouse, int16_t x, int16_t y)
{
    if (!mouse->on) return;
    cok_mouse_driver_set(&mouse->driver, mouse->narrow ? x : (int16_t)(x * 2), y);
}

void cok_mouse_cursor(cok_mouse *mouse, const uint16_t masks[32], int hot_x, int hot_y)
{
    if (!mouse->on) return;
    mouse->driver.masks = masks;
    mouse->driver.hot_x = hot_x;
    mouse->driver.hot_y = hot_y;
}

void cok_mouse_start(cok_mouse *mouse, bool cga)
{
    mouse->party_clicks = true;
    mouse->button_count = 0;
    cok_mouse_reset(mouse);
    if (!mouse->on) return;
    mouse->narrow = true;
    cok_mouse_cursor(mouse, cga ? cok_mouse_arrow_cga : cok_mouse_arrow, 0, 0);
    cok_mouse_set(mouse, 0x27f, 100);
    unsigned buttons;
    int16_t x, y;
    cok_mouse_get(mouse, &buttons, &x, &y);
    mouse->narrow = x < 0x141;
    cok_mouse_show(mouse);
}

/* 1743:0167. */
uint8_t cok_mouse_direction(int16_t fx, int16_t fy, int16_t tx, int16_t ty)
{
    int16_t dx = (int16_t)(tx - fx), dy = (int16_t)(ty - fy);
    /* cwd; xor; sub: abs(-32768) stays -32768. */
    dx = (int16_t)(dx < 0 ? (uint16_t)-(uint16_t)dx : (uint16_t)dx);
    dy = (int16_t)(dy < 0 ? (uint16_t)-(uint16_t)dy : (uint16_t)dy);
    /* mul keeps AX, cwd sign-extends it, idiv by 256 truncates. */
    int16_t steep = (int16_t)((int16_t)(uint16_t)(0x26a * (uint16_t)dx) / 256);
    int16_t flat = (int16_t)((int16_t)(uint16_t)(0x6a * (uint16_t)dx) / 256);
    bool diagonal = steep >= dy && flat <= dy;
    if (ty <= fy && steep <= dy) return 0x48;
    if (ty < fy && tx > fx && diagonal) return 0x49;
    if (tx >= fx && flat >= dy) return 0x4d;
    if (ty > fy && tx > fx && diagonal) return 0x51;
    if (ty >= fy && steep <= dy) return 0x50;
    if (ty > fy && tx < fx && diagonal) return 0x4f;
    if (tx <= fx && flat >= dy) return 0x4b;
    if (ty < fy && tx < fx && diagonal) return 0x47;
    return 0;
}

/* The joystick unit (177c). */

bool cok_stick_read(const cok_game_port *port, unsigned stick, uint16_t *x, uint16_t *y,
                    uint8_t *buttons)
{
    if (stick != 1 && stick != 2) return false;
    if (port == NULL || port->read == NULL) return false;
    return port->read(port->context, stick, x, y, buttons);
}

static void port_wait(const cok_game_port *port, unsigned ms)
{
    if (port != NULL && port->wait != NULL) port->wait(port->context, ms);
}

void cok_stick_init(cok_stick *stick, const cok_game_port *port)
{
    memset(stick->x, 0, sizeof stick->x);
    memset(stick->y, 0, sizeof stick->y);
    if (stick->on) {
        uint16_t x, y;
        uint8_t buttons;
        stick->on = cok_stick_read(port, 1, &x, &y, &buttons);
    }
}

/* DS:4334: rows the band down, columns across. */
static const uint8_t directions[6][6] = {
    {7, 7, 0, 0, 1, 1}, {7, 8, 8, 8, 8, 1}, {6, 8, 8, 8, 8, 2},
    {6, 8, 8, 8, 8, 2}, {5, 8, 8, 8, 8, 3}, {5, 5, 4, 4, 3, 3},
};

static unsigned band(const uint16_t thresholds[7], uint16_t value)
{
    unsigned found = 0;
    for (unsigned i = 1; i <= 5; ++i)
        if (thresholds[i] < value) found = i;
    return found;
}

uint8_t cok_stick_direction(const cok_stick *stick, const cok_game_port *port)
{
    uint16_t x, y;
    uint8_t buttons;
    if (!stick->on || !cok_stick_read(port, 1, &x, &y, &buttons)) return 8;
    return directions[band(stick->y, y)][band(stick->x, x)];
}

bool cok_stick_button(const cok_stick *stick, const cok_game_port *port, unsigned button)
{
    if (!stick->on) return false;
    uint16_t x, y;
    uint8_t buttons = 0, mask = 0;
    if (cok_stick_read(port, 1, &x, &y, &buttons)) mask = button == 1 ? 1 : button == 2 ? 2 : 0;
    return (buttons & mask) != 0;
}

void cok_stick_poll(const cok_stick *stick, const cok_game_port *port, uint8_t *direction,
                    uint16_t *buttons)
{
    *buttons = 0;
    if (cok_stick_button(stick, port, 2))
        *buttons = 2;
    else if (cok_stick_button(stick, port, 1))
        *buttons = 1;
    *direction = cok_stick_direction(stick, port);
}

char cok_stick_key(uint8_t direction)
{
    static const char keys[] = "89632147 ";
    return direction <= 8 ? keys[direction] : ' ';
}

/* Up to COK_STICK_POLLS polls, and the one more the original makes and
 * ignores, for button 1 on a read that works. */
static bool wait_button(const cok_game_port *port, uint16_t *x, uint16_t *y, uint8_t *buttons,
                        const bool *aborted)
{
    for (unsigned n = 0;;) {
        bool ok = cok_stick_read(port, 1, x, y, buttons);
        if (++n > COK_STICK_POLLS) return false;
        if (ok && (*buttons & 1) != 0) return true;
        port_wait(port, COK_STICK_READ_MS);
        if (aborted != NULL && *aborted) return false;
    }
}

/* The thresholds a third and two thirds of the way from t[from] to
 * t[from + 3], 16 bits unsigned (177c:03ca, 0507). */
static void thirds(uint16_t t[7], int from)
{
    uint16_t a = t[from], span = (uint16_t)(t[from + 3] - a);
    t[from + 1] = (uint16_t)(a + span / 3);
    t[from + 2] = (uint16_t)(a + (uint16_t)(span * 2u) / 3);
}

bool cok_stick_calibrate(cok_stick *stick, const cok_game_port *port,
                         void (*prompt)(cok_stick_prompt which, void *context), void *context,
                         const bool *aborted)
{
    cok_stick_init(stick, port);
    if (!stick->on) return true;
    uint16_t x = 0, y = 0, sx = 0, sy = 0;
    uint8_t buttons = 0;
    for (unsigned n = 1; n <= 20; ++n) {
        /* A failed read adds the counts it left (the last ones). */
        cok_stick_read(port, 1, &x, &y, &buttons);
        sx = (uint16_t)(sx + x);
        sy = (uint16_t)(sy + y);
    }
    stick->x[3] = sx / 20;
    stick->y[3] = sy / 20;
    for (int corner = 0; corner < 2; ++corner) {
        prompt(corner == 0 ? COK_STICK_UPPER_LEFT : COK_STICK_LOWER_RIGHT, context);
        if (!wait_button(port, &x, &y, &buttons, aborted)) {
            if (aborted == NULL || !*aborted) prompt(COK_STICK_FAILED, context);
            return false;
        }
        sx = sy = corner == 0 ? 0xffff : 0;
        for (unsigned n = 1; n <= 20; ++n) {
            if (!cok_stick_read(port, 1, &x, &y, &buttons)) {
                prompt(COK_STICK_FAILED, context);
                return false;
            }
            if ((buttons & 1) == 0) continue;
            if (corner == 0) {
                if (x < sx) sx = x;
                if (y < sy) sy = y;
            } else {
                if (x > sx) sx = x;
                if (y > sy) sy = y;
            }
        }
        if (corner == 0) {
            stick->x[0] = sx;
            stick->y[0] = sy;
            thirds(stick->x, 0);
            thirds(stick->y, 0);
            while (cok_stick_button(stick, port, 1)) {
                port_wait(port, COK_STICK_READ_MS);
                if (aborted != NULL && *aborted) return false;
            }
        } else {
            stick->x[6] = sx;
            stick->y[6] = sy;
            thirds(stick->x, 3);
            thirds(stick->y, 3);
        }
    }
    stick->on = true;
    return true;
}
