#ifndef COK_STEP_H
#define COK_STEP_H

#include <stdint.h>

/* A step in direction 0-7, north first, clockwise; 8 for none: across and
 * down, as the original's DS:1ed6 and DS:1edf hold them. The compass, the
 * same in every game: the code's own constants, which tests/test_data.c
 * checks against the data segment's (COK_DATA_STEP_X, COK_DATA_STEP_Y);
 * the reads that run on past direction 8 go through those (data.h). */
extern const int8_t cok_step_x[9], cok_step_y[9];

#endif
