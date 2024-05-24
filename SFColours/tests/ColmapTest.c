/*
 *  SFColours test: Colour map file back-end functions
 *  Copyright (C) 2020 Christopher Bazley
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
#include "Colmap.h"

#include "FORTIFY.h"

enum
{
  DefaultPixelColour = 0,
  MaxColour = 255,
  NumColours = MaxColour + 1,
  ColourStart = 3,
  ColourEnd = 60,
  FileSize = 4096,
};

static void test1(void)
{
  /* Initialise */
  for (int s = 0; s <= ColMap_MaxSize; ++s)
  {
    ColMap colmap;
    colmap_init(&colmap, s);
    assert(colmap_get_size(&colmap) == s);
  }
}

static int get_colour(int i)
{
  i %= NumColours;
  return i % 2 ? i : MaxColour - i;
}

static void test2(void)
{
  /* Get/set colour */
  ColMap colmap;
  colmap_init(&colmap, ColMap_MaxSize);

  for (int i = 0; i < ColMap_MaxSize; ++i)
  {
    assert(colmap_get_colour(&colmap, i) == DefaultPixelColour);
  }

  for (int i = ColourStart; i < ColourEnd; ++i)
  {
    colmap_set_colour(&colmap, i, get_colour(i));
  }

  for (int i = 0; i < ColMap_MaxSize; ++i)
  {
    assert(colmap_get_colour(&colmap, i) ==
      (i >= ColourStart && i < ColourEnd ? get_colour(i) : DefaultPixelColour));
  }
}

static void test3(void)
{
  /* Read/write */
  for (int s = 0; s <= ColMap_MaxSize; ++s)
  {
    ColMap colmap;
    colmap_init(&colmap, s);

    for (int i = 0; i < s; ++i)
    {
      colmap_set_colour(&colmap, i, get_colour(i));
    }

    Writer writer;
    char buffer[FileSize] = {0};
    assert(writer_mem_init(&writer, buffer, sizeof(buffer)));
    colmap_write_file(&colmap, &writer);
    assert(!writer_ferror(&writer));
    long int const len = writer_destroy(&writer);
    assert(len >= (s  ? 1 : 0));
    assert(len <= FileSize);

    for (int i = 0; i < s; ++i)
    {
      assert(colmap_get_colour(&colmap, i) == get_colour(i));
    }

    colmap_init(&colmap, s);

    Reader reader;
    assert(reader_mem_init(&reader, buffer, (size_t)len));
    assert(colmap_read_file(&colmap, &reader) == ColMapState_OK);
    assert(!reader_ferror(&reader));
    assert(reader_feof(&reader));
    reader_destroy(&reader);

    assert(colmap_get_size(&colmap) == s);

    for (int i = 0; i < s; ++i)
    {
      assert(colmap_get_colour(&colmap, i) == get_colour(i));
    }
  }
}

static void test4(void)
{
  /* Read overlong */
  ColMap colmap;
  colmap_init(&colmap, ColMap_MaxSize);

  char buffer[FileSize] = {0};

  Reader reader;
  assert(reader_mem_init(&reader, buffer, sizeof(buffer)));
  assert(colmap_read_file(&colmap, &reader) == ColMapState_BadLen);
  assert(!reader_ferror(&reader));
  assert(!reader_feof(&reader));
  reader_destroy(&reader);
}

void Colmap_tests(void)
{
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Initialise", test1 },
    { "Get/set colour", test2 },
    { "Read/write", test3 },
    { "Read overlong", test4 },
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
