/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky file back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 */

#ifndef SFSky_h
#define SFSky_h

#include <stdbool.h>

#include "Reader.h"
#include "Writer.h"
#include "SFFormats.h"

enum
{
  NColourBands = SFSky_Height / 2,
  NPixelColours = 256,
  MinRenderOffset = 0,
  MaxRenderOffset = 3648,
  MinStarsHeight = -32768,
  MaxStarsHeight = 3648,
};

typedef struct {
  int render_offset;
  int stars_height;
  unsigned char bands[NColourBands];
} Sky;

/* Initialize a sky file. */
void sky_init(Sky *sky);

/* Write the sky file in the game's native format. */
void sky_write_file(Sky const *sky, Writer *writer);

typedef enum {
  SkyState_OK,
  SkyState_ReadFail,
  SkyState_BadLen,
  SkyState_BadRend,
  SkyState_BadStar,
  SkyState_BadDither,
} SkyState;

/* Read the sky file in the game's native format. Does not redraw. */
SkyState sky_read_file(Sky *sky, Reader *reader);

/* Get single colour for redraw */
int sky_get_colour(Sky const *sky, int pos);

void sky_set_colour(Sky *sky, int pos, int colour);

/* Get the colour bands compression offset at ground level. */
int sky_get_render_offset(Sky const *sky);

/* Set the colour bands compression offset at ground level. */
void sky_set_render_offset(Sky *sky, int render_offset);

/* Get the height at which to plot stars. */
int sky_get_stars_height(Sky const *sky);

/* Set the height at which to plot stars. */
void sky_set_stars_height(Sky *sky, int stars_height);

#endif
