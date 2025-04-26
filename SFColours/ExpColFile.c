/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colours file format (includes relative positions)
 *  Copyright (C) 2006 Christopher Bazley
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
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include <string.h>
#include <limits.h>

/* RISC OS library files */
#include "flex.h"

/* My library files */
#include "Debug.h"
#include "Macros.h"
#include "Reader.h"
#include "Writer.h"

/* Local headers */
#include "ExpColFile.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


#define TAG "COLS"

enum
{
  NumColours = 256,
  CurrentVersion = 0,
  DefaultPixelColour = 0, /* black */
};

typedef struct
{
  int x_offset;
  int y_offset;
  unsigned char colour;
}
ExportColFileRecord;

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

bool ExpColFile_init(ExpColFile *const file, int const num_cols)
{
  assert(file != NULL);

  if (!flex_alloc(&file->records, num_cols * sizeof(ExportColFileRecord)))
  {
    return false;
  }

  file->num_cols = num_cols;
  for (int index = 0; index < num_cols; ++index)
  {
    ExpColFile_set_colour(file, index, 0, 0, DefaultPixelColour);
  }

  return true;
}

/* ----------------------------------------------------------------------- */

void ExpColFile_destroy(ExpColFile *const file)
{
  if (file->records)
  {
    flex_free(&file->records);
  }
}

/* ----------------------------------------------------------------------- */

int ExpColFile_get_colour(ExpColFile const *const file, int const index,
  _Optional int *const x_offset, _Optional int *const y_offset)
{
  DEBUGF("Reading record %d in export file %p\n", index, (void *)file);
  assert(file != NULL);

  if (index < 0 || index >= file->num_cols)
  {
    return -1;
  }

  const ExportColFileRecord *const record =
    (ExportColFileRecord *)file->records + index;

  int const x = record->x_offset;
  int const y = record->y_offset;
  int const colour = record->colour;

  DEBUGF("  Got colour %d at offset %d,%d\n", colour, x, y);
  assert(colour >= 0 && colour < NumColours);

  if (x_offset != NULL)
  {
    *x_offset = x;
  }

  if (y_offset != NULL)
  {
    *y_offset = y;
  }

  return colour;
}

/* ----------------------------------------------------------------------- */

bool ExpColFile_set_colour(ExpColFile *const file, int const index,
  int const x_offset, int const y_offset, int const colour)
{
  DEBUGF("Writing record %d in export file %p\n", index, (void *)file);
  assert(file != NULL);
  if (file->num_cols > 0)
  {
    assert(flex_size(&file->records) >=
      file->num_cols * (int)sizeof(ExportColFileRecord));
  }

  if (index < 0 || index >= file->num_cols)
  {
    return false;
  }

  if (colour < 0 || colour >= NumColours)
  {
    return false;
  }

  ExportColFileRecord *const record =
    (ExportColFileRecord *)file->records + index;

  record->x_offset = x_offset;
  record->y_offset = y_offset;
  record->colour = colour;
  DEBUGF("  Put colour %u at offset %d,%d\n", colour, x_offset, y_offset);

  return true;
}

/* ----------------------------------------------------------------------- */

int ExpColFile_get_size(ExpColFile const *const file)
{
  assert(file != NULL);
  DEBUGF("Export file %p size is %d\n", (void *)file, file->num_cols);
  return file->num_cols;
}

/* ----------------------------------------------------------------------- */

static ExpColFileState read_body(ExpColFile *const file, Reader *const reader)
{
  int const num_cols = ExpColFile_get_size(file);

  for (int index = 0; index < num_cols; ++index)
  {
    int32_t x, y, col;
    if (!reader_fread_int32(&x, reader) ||
        !reader_fread_int32(&y, reader) ||
        !reader_fread_int32(&col, reader))
    {
      return reader_feof(reader) ?
        ExpColFileState_BadLen : ExpColFileState_ReadFail;
    }

    if (!ExpColFile_set_colour(file, index, x, y, col))
    {
      return ExpColFileState_BadCol;
    }
  }

  /* We should have reached the end of the file */
  if (reader_fgetc(reader) != EOF)
  {
    return ExpColFileState_BadLen; /* File is too long */
  }

  return reader_feof(reader) ?
    ExpColFileState_OK : ExpColFileState_ReadFail;
}

/* ----------------------------------------------------------------------- */

ExpColFileState ExpColFile_read(ExpColFile *const file,
  Reader *const reader)
{
  assert(file != NULL);
  assert(reader != NULL);
  assert(!reader_ferror(reader));
  DEBUGF("Reading data into export file %p\n", (void *)file);

  char tag[sizeof(TAG) - 1];
  int32_t version, num_cols;

  if (reader_fread(tag, sizeof(tag), 1, reader) != 1 ||
      !reader_fread_int32(&version, reader) ||
      !reader_fread_int32(&num_cols, reader))
  {
    return reader_feof(reader) ?
      ExpColFileState_BadLen : ExpColFileState_ReadFail;
  }

  if (strncmp(tag, TAG, sizeof(tag)))
  {
    return ExpColFileState_BadTag;
  }

  if (version != CurrentVersion)
  {
    return ExpColFileState_UnknownVersion;
  }

  if (num_cols < 0 || num_cols > INT_MAX)
  {
    return ExpColFileState_BadNumCols;
  }

  if (!ExpColFile_init(file, num_cols))
  {
    return ExpColFileState_NoMem;
  }

  ExpColFileState const state = read_body(file, reader);
  if (state != ExpColFileState_OK)
  {
    ExpColFile_destroy(file);
  }

  return state;
}

/* ----------------------------------------------------------------------- */

int ExpColFile_estimate(int const num_cols)
{
  return sizeof(TAG) - 1 +
         (sizeof(int32_t) * 2) +
         (num_cols * sizeof(int32_t) * 3);
}

/* ----------------------------------------------------------------------- */

void ExpColFile_write(ExpColFile const *const file,
  Writer *const writer)
{
  assert(file != NULL);
  assert(writer != NULL);
  assert(!writer_ferror(writer));
  DEBUGF("Writing data from export file %p\n", (void *)file);

  writer_fwrite(TAG, sizeof(TAG) - 1, 1, writer);
  writer_fwrite_int32(CurrentVersion, writer);
  writer_fwrite_int32(file->num_cols, writer);

  int const ncols = file->num_cols;
  for (int index = 0; index < ncols && !writer_ferror(writer); ++index)
  {
    int x = 0, y = 0;
    int const col = ExpColFile_get_colour(file, index, &x, &y);

    writer_fwrite_int32(x, writer);
    writer_fwrite_int32(y, writer);
    writer_fwrite_int32(col, writer);
  }
}

/* ----------------------------------------------------------------------- */

int ExpColFile_estimate_CSV(int const num_cols)
{
  return sizeof("000") * num_cols;
}

/* ----------------------------------------------------------------------- */

void ExpColFile_write_CSV(ExpColFile const *const file,
  Writer *const writer)
{
  assert(file != NULL);
  assert(writer != NULL);
  assert(!writer_ferror(writer));
  DEBUGF("Writing CSV from export file %p\n", (void *)file);

  int const ncols = file->num_cols;
  for (int index = 0; index < ncols && !writer_ferror(writer); ++index)
  {
    int const col = ExpColFile_get_colour(file, index, NULL, NULL);
    char buf[16];
    int const nchars = sprintf(buf, "%d", col);
    assert(nchars > 0);
    assert(nchars < (int)sizeof(buf));

    writer_fwrite(buf, nchars, 1, writer);

    if (index < (ncols - 1))
    {
      writer_fputc(',', writer);
    }
  }
}
