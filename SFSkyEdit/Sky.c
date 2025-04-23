/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky file back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public Licence as published by
 *  the Free Software Foundation; either version 2 of the Licence, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ISO library files */
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>

/* My library files */
#include "Macros.h"
#include "SFFormats.h"
#include "Debug.h"
#include "Reader.h"
#include "Writer.h"

/* Local headers */
#include "Sky.h"

/* Constant numeric values */
enum
{
  DefaultPixelColour = 0, /* black */
  DefaultRenderOffset = 0,
  DefaultStarsHeight = 0,
};

static inline void set_colour(Sky *const sky, int const pos, int colour)
{
  assert(sky != NULL);
  assert(pos >= 0);
  assert(pos < NColourBands);
  assert(colour >= 0);
  assert(colour < NPixelColours);

  DEBUGF("Writing colour %d at position %d in file %p\n", colour, pos,
    (void *)sky);

  sky->bands[pos] = colour;
}

static inline int get_colour(Sky const *const sky, int const pos)
{
  assert(sky != NULL);
  assert(pos >= 0);
  assert(pos < NColourBands);

  int const colour = sky->bands[pos];

  DEBUGF("Reading colour %d at position %d in file %p\n", colour, pos,
    (void *)sky);

  return colour;
}

void sky_init(Sky *const sky)
{
  assert(sky != NULL);

  sky->render_offset = DefaultRenderOffset;
  sky->stars_height = DefaultStarsHeight;

  for (int pos = 0; pos < NColourBands; pos++)
  {
    set_colour(sky, pos, DefaultPixelColour);
  }
}

void sky_write_file(Sky const *const sky, Writer *const writer)
{
  assert(sky != NULL);

  writer_fwrite_int32(sky->render_offset, writer);
  writer_fwrite_int32(sky->stars_height, writer);

  int prev = get_colour(sky, 0);

  for (int pos = 0; pos < NColourBands && !writer_ferror(writer); pos++)
  {
    int const colour = get_colour(sky, pos);
    uint8_t pattern[SFSky_Width];

    /* Dither with preceding colour */
    for (int i = 0; i < SFSky_Width; i++)
    {
      pattern[i] = (pos + i) % 2 ? prev : colour;
    }

    writer_fwrite(pattern, sizeof(pattern), 1, writer);

    /* Set plain colour */
    for (int i = 0; i < SFSky_Width; i++)
    {
      pattern[i] = colour;
    }

    writer_fwrite(pattern, sizeof(pattern), 1, writer);

    prev = colour;
  }
}

SkyState sky_read_file(Sky *const sky, Reader *const reader)
{
  assert(sky != NULL);

  if (!reader_fread_int32(&sky->render_offset, reader))
  {
    return reader_feof(reader) ? SkyState_BadLen : SkyState_ReadFail;
  }

  if (sky->render_offset < MinRenderOffset ||
      sky->render_offset > MaxRenderOffset )
  {
    return SkyState_BadRend;
  }

  if (!reader_fread_int32(&sky->stars_height, reader))
  {
    return reader_feof(reader) ? SkyState_BadLen : SkyState_ReadFail;
  }

  if (sky->stars_height < MinStarsHeight ||
      sky->stars_height > MaxStarsHeight)
  {
    return SkyState_BadStar;
  }

  int prev = 0;
  for (int pos = 0; pos < NColourBands; pos++)
  {
    /* Read two rows at a time */
    uint8_t pattern[2][SFSky_Width];
    if (reader_fread(pattern, sizeof(pattern), 1, reader) != 1)
    {
      return reader_feof(reader) ? SkyState_BadLen : SkyState_ReadFail;
    }

    /* The second of each pair of rows is the plain colour */
    int const colour = pattern[1][0];

    /* First row should be identical to second row because there is
       no previous colour band to dither with. */
    if (pos == 0)
    {
      prev = colour;
    }

    /* Check that alternate pixels of the first row are identical */
    for (int i = 2; i < SFSky_Width; i++)
    {
      if (pattern[0][i] != pattern[0][i - 2])
      {
        return SkyState_BadDither;
      }
    }

    /* Check that the first row of the pair dithers the plain colour with
       the previous colour. (We could be strict about the alignment of
       the dithering, but it isn't terribly important and earlier versions
       of SFEditorEdit got it 'wrong'.) */
    for (int i = 0; i < SFSky_Width; i++)
    {
      if (pattern[0][i] != prev && pattern[0][i] != colour)
      {
        return SkyState_BadDither;
      }
    }

    /* Check plain colour */
    for (int i = 1; i < SFSky_Width; i++)
    {
      if (pattern[1][i] != colour)
      {
        return SkyState_BadDither;
      }
    }

    set_colour(sky, pos, pattern[1][0]);
    prev = colour;
  }

  /* We should have reached the end of the file */
  if (reader_fgetc(reader) != EOF)
  {
    return SkyState_BadLen; /* File is too long */
  }

  return reader_feof(reader) ? SkyState_OK : SkyState_ReadFail;
}

int sky_get_colour(Sky const *const sky, int const pos)
{
  return get_colour(sky, pos);
}

void sky_set_colour(Sky *const sky, int const pos, int colour)
{
  set_colour(sky, pos, colour);
}

int sky_get_render_offset(Sky const *const sky)
{
  assert(sky != NULL);
  assert(sky->render_offset >= MinRenderOffset);
  assert(sky->render_offset <= MaxRenderOffset);

  return sky->render_offset;
}

void sky_set_render_offset(Sky *const sky, int render_offset)
{
  assert(sky != NULL);
  assert(render_offset >= MinRenderOffset);
  assert(render_offset <= MaxRenderOffset);

  DEBUGF("Setting render offset %d in sky %p\n", render_offset, (void *)sky);
  sky->render_offset = render_offset;
}

int sky_get_stars_height(Sky const *const sky)
{
  assert(sky != NULL);
  assert(sky->stars_height >= MinStarsHeight);
  assert(sky->stars_height <= MaxStarsHeight);

  return sky->stars_height;
}

void sky_set_stars_height(Sky *const sky, int stars_height)
{
  assert(sky != NULL);
  assert(stars_height >= MinStarsHeight);
  assert(stars_height <= MaxStarsHeight);

  DEBUGF("Setting stars height %d in sky %p\n", stars_height, (void *)sky);
  sky->stars_height = stars_height;
}
