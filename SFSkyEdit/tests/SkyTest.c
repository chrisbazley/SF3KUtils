/*
 *  SFSkyEdit test: Sky file back-end functions
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

/* ANSI library files */
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* My library files */
#include "Macros.h"
#include "Debug.h"
#include "WriterMem.h"
#include "ReaderMem.h"

/* Local headers */
#include "Tests.h"
#include "Sky.h"

#include "FORTIFY.h"

enum
{
  RenderOffset = 10,
  StarsHeight = 99,
  DefaultPixelColour = 0,
  DefaultRenderOffset = 0,
  MinColour = 0,
  MaxColour = NPixelColours - 1,
  DefaultStarsHeight = 0,
  ColourStart = 3,
  ColourEnd = 60,
  Colour = 76,
  FileSize = 4096,
  Marker = 0x43
};

static void test1(void)
{
  /* Initialise */
  Sky sky;
  sky_init(&sky);
}

static void test2(void)
{
  /* Render offset */
  Sky sky;
  sky_init(&sky);

  assert(sky_get_render_offset(&sky) == DefaultRenderOffset);

  sky_set_render_offset(&sky, RenderOffset);
  assert(sky_get_render_offset(&sky) == RenderOffset);

  sky_set_render_offset(&sky, RenderOffset + 10);
  assert(sky_get_render_offset(&sky) == RenderOffset + 10);

  sky_set_render_offset(&sky, MinRenderOffset);
  assert(sky_get_render_offset(&sky) == MinRenderOffset);

  sky_set_render_offset(&sky, MaxRenderOffset);
  assert(sky_get_render_offset(&sky) == MaxRenderOffset);
}

static void test3(void)
{
  /* Stars height */
  Sky sky;
  sky_init(&sky);

  assert(sky_get_stars_height(&sky) == DefaultStarsHeight);

  sky_set_stars_height(&sky, StarsHeight);
  assert(sky_get_stars_height(&sky) == StarsHeight);

  sky_set_stars_height(&sky, StarsHeight + 10);
  assert(sky_get_stars_height(&sky) == StarsHeight + 10);

  sky_set_stars_height(&sky, MinStarsHeight);
  assert(sky_get_stars_height(&sky) == MinStarsHeight);

  sky_set_stars_height(&sky, MaxStarsHeight);
  assert(sky_get_stars_height(&sky) == MaxStarsHeight);
}

static void test4(void)
{
  /* Get colour */
  Sky sky;
  sky_init(&sky);

  for (int i = 0; i < NColourBands; ++i)
  {
    assert(sky_get_colour(&sky, i) == DefaultPixelColour);
  }

  for (int i = ColourStart; i < ColourEnd; ++i)
  {
    sky_set_colour(&sky, i, Colour);
  }

  for (int i = 0; i < NColourBands; ++i)
  {
    assert(sky_get_colour(&sky, i) ==
      (i >= ColourStart && i < ColourEnd ? Colour : DefaultPixelColour));
  }
}

static int get_colour(int i)
{
  return i % 2 ? i : MaxColour - i;
}

static void test5(void)
{
  /* Read/write */
  Sky sky;
  sky_init(&sky);

  for (int i = 0; i < NColourBands; ++i)
  {
    sky_set_colour(&sky, i, get_colour(i));
  }

  sky_set_render_offset(&sky, RenderOffset);
  sky_set_stars_height(&sky, StarsHeight);

  Writer writer;
  char buffer[FileSize] = {0};
  assert(writer_mem_init(&writer, buffer, sizeof(buffer)));
  sky_write_file(&sky, &writer);
  assert(!writer_ferror(&writer));
  long int const len = writer_destroy(&writer);
  assert(len > 1);
  assert(len <= FileSize);

  for (int i = 0; i < NColourBands; ++i)
  {
    assert(sky_get_colour(&sky, i) == get_colour(i));
  }

  assert(sky_get_render_offset(&sky) == RenderOffset);
  assert(sky_get_stars_height(&sky) == StarsHeight);

  sky_init(&sky);

  Reader reader;
  assert(reader_mem_init(&reader, buffer, (size_t)len));
  assert(sky_read_file(&sky, &reader) == SkyState_OK);
  assert(!reader_ferror(&reader));
  assert(reader_feof(&reader));
  reader_destroy(&reader);

  for (int i = 0; i < NColourBands; ++i)
  {
    assert(sky_get_colour(&sky, i) == get_colour(i));
  }

  assert(sky_get_render_offset(&sky) == RenderOffset);
  assert(sky_get_stars_height(&sky) == StarsHeight);
}

static void test6(void)
{
  /* Read empty */
  Sky sky;
  sky_init(&sky);

  char buffer[FileSize] = {0};

  Reader reader;
  assert(reader_mem_init(&reader, buffer, 0));
  assert(sky_read_file(&sky, &reader) == SkyState_BadLen);
  assert(!reader_ferror(&reader));
  assert(reader_feof(&reader));
  reader_destroy(&reader);
}

static void test7(void)
{
  /* Read overlong */
  Sky sky;
  sky_init(&sky);

  char buffer[FileSize] = {0};

  Reader reader;
  assert(reader_mem_init(&reader, buffer, sizeof(buffer)));
  assert(sky_read_file(&sky, &reader) == SkyState_BadLen);
  assert(!reader_ferror(&reader));
  assert(!reader_feof(&reader));
  reader_destroy(&reader);
}

void Sky_tests(void)
{
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Initialise", test1 },
    { "Render offset", test2 },
    { "Stars height", test3 },
    { "Get colour", test4 },
    { "Read/write", test5 },
    { "Read empty", test6 },
    { "Read overlong", test7 },
  };

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); ++count)
  {
    DEBUGF("Test %zu/%zu : %s\n",
           1 + count,
           ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);

    Fortify_EnterScope();
    unit_tests[count].test_func();
    Fortify_LeaveScope();
  }
}
