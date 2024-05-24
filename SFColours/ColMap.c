/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colour map file back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 */

#include <assert.h>

#include "Debug.h"
#include "Reader.h"
#include "Writer.h"

#include "ColMap.h"

enum
{
  NPixelColours = 256,
  DefaultPixelColour = 0, /* black */
};

static inline void set_colour(ColMap *const colmap, int const pos,
  int const colour)
{
  assert(colmap != NULL);
  assert(pos >= 0);
  assert(pos <= colmap->size);
  assert(colour >= 0);
  assert(colour < NPixelColours);

  DEBUG_VERBOSEF("Writing %d at %d in %p\n", colour, pos,
    (void *)colmap);

  colmap->map[pos] = colour;
}

static inline int get_colour(ColMap const *const colmap, int const pos)
{
  assert(colmap != NULL);
  assert(pos >= 0);
  assert(pos < colmap->size);
  int const colour = colmap->map[pos];

  DEBUG_VERBOSEF("Reading %d at %d in %p\n", colour, pos,
    (void *)colmap);

  return colour;
}

void colmap_init(ColMap *const colmap, int const size)
{
  assert(colmap != NULL);
  assert(size >= 0);
  DEBUGF("Initializing file %p of size %d\n", (void *)colmap, size);
  colmap->size = size;
  for (int pos = 0; pos < size; pos++)
  {
    set_colour(colmap, pos, DefaultPixelColour);
  }
}

int colmap_get_colour(ColMap const *const colmap, int const pos)
{
  return get_colour(colmap, pos);
}

void colmap_set_colour(ColMap *const colmap, int const pos, int colour)
{
  set_colour(colmap, pos, colour);
}

int colmap_get_size(ColMap const *const colmap)
{
  assert(colmap != NULL);
  assert(colmap->size >= 0);
  assert(colmap->size <= ColMap_MaxSize);
  return colmap->size;
}

ColMapState colmap_read_file(ColMap *const colmap, Reader *const reader)
{
  assert(colmap != NULL);
  assert(reader != NULL);
  assert(!reader_ferror(reader));

  colmap->size = reader_fread(colmap->map, 1, ColMap_MaxSize, reader);

  /* We should have reached the end of the file */
  if (colmap->size == ColMap_MaxSize && reader_fgetc(reader) != EOF)
  {
    return ColMapState_BadLen; /* File is too long */
  }

  return reader_feof(reader) ? ColMapState_OK : ColMapState_ReadFail;
}

void colmap_write_file(ColMap const *const colmap, Writer *const writer)
{
  assert(colmap != NULL);
  assert(writer != NULL);
  assert(!writer_ferror(writer));

  writer_fwrite(colmap->map, 1, colmap->size, writer);
}
