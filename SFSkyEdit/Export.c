/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Export back-end functions
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
#include <stddef.h>
#include <assert.h>
#include <stdint.h>

/* My library files */
#include "Macros.h"
#include "SFformats.h"
#include "Debug.h"
#include "Writer.h"

/* Local headers */
#include "Utils.h"
#include "Export.h"

/* Constant numeric values */
enum
{
  SpriteAreaHdrSize = 16,
  SpriteHdrSize = 44,
  NumSprites = 1,
  BitsPerPixel = 8,
  SpriteType = 13, /* Type of sprite created from selected
                      colours (mode number: 45 dpi, 8 bpp) */
};

int estimate_CSV_file(int const ncols)
{
  return sizeof("000") * ncols;
}

void write_CSV_file(int const cols_array[], int const ncols,
  Writer *const writer)
{
  assert(cols_array != NULL);
  assert(ncols >= 1);
  assert(!writer_ferror(writer));
  DEBUGF("Making CSV file from %d colours at %p\n", ncols, (void *)cols_array);

  for (int index = 0; index < ncols && !writer_ferror(writer); index++)
  {
    char buf[16];
    int const nchars = sprintf(buf, "%d", cols_array[index]);
    assert(nchars > 0);
    assert((unsigned)nchars < sizeof(buf));

    writer_fwrite(buf, nchars, 1, writer);

    if (index < (ncols - 1))
    {
      writer_fputc(',', writer);
    }
  }
}

int estimate_sprite_file(int const ncols)
{
  int const sprite_size = SpriteHdrSize + (WORD_ALIGN(SFSky_Width) * ncols);
  return SpriteAreaHdrSize - sizeof(int32_t) + sprite_size;
}

void write_sprite_file(int const cols_array[], int const ncols,
  Writer *const writer)
{
  assert(cols_array != NULL);
  assert(ncols >= 1);
  assert(!writer_ferror(writer));
  DEBUGF("Making sprite file from %d colours at %p\n", ncols, (void *)cols_array);

  int const sprite_size = SpriteHdrSize + (WORD_ALIGN(SFSky_Width) * ncols);

  /* Write sprite area header */
  writer_fwrite_int32(NumSprites, writer);
  writer_fwrite_int32(SpriteAreaHdrSize, writer);
  writer_fwrite_int32(SpriteAreaHdrSize + sprite_size, writer);

  /* Write sprite header */
  writer_fwrite_int32(sprite_size, writer);
  static char const name[12] = "sky";
  writer_fwrite(name, sizeof(name), 1, writer);
  writer_fwrite_int32(WORD_ALIGN(SFSky_Width) / 4 - 1, writer);
  writer_fwrite_int32(ncols - 1, writer);
  writer_fwrite_int32(0, writer);
  writer_fwrite_int32(sprite_right_bit(SFSky_Width, BitsPerPixel), writer);
  writer_fwrite_int32(SpriteHdrSize, writer);
  writer_fwrite_int32(SpriteHdrSize, writer);
  writer_fwrite_int32(SpriteType, writer);

  /* Write bitmap (reversed top-bottom) */
  for (int index = ncols - 1; index >= 0; --index)
  {
    int const colour = cols_array[index];
    unsigned char image_row[WORD_ALIGN(SFSky_Width)];

    for (size_t i = 0; i < sizeof(image_row); i++)
    {
      image_row[i] = colour;
    }

    if (writer_fwrite(image_row, sizeof(image_row), 1, writer) != 1)
    {
      break;
    }
  }
}
