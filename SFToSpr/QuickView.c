/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Quick views
 *  Copyright (C) 2016  Christopher Bazley
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
#include "stdio.h"

/* RISC OS library files */
#include "kernel.h"

/* My library files */
#include "Debug.h"
#include "Err.h"
#include "Macros.h"
#include "msgtrans.h"
#include "SFFormats.h"
#include "WriterRaw.h"
#include "ReaderGKey.h"
#include "FOpenCount.h"
#include "FileUtils.h"
#include "Hourglass.h"

/* Local headers */
#include "QuickView.h"
#include "Utils.h"
#include "SFgfxconv.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

#define COMMAND_PREFIX "Filer_Run "

enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
};

static SFError try_convert(const char *const read_filename,
                           const char *const write_filename,
                           int const file_type)
{
  _Optional FILE *const fr = fopen_inc(read_filename, "rb");
  if (fr == NULL)
  {
    return SFError_OpenInFail;
  }

  SFError err = SFError_OK;
  _Optional FILE *const fw = fopen_inc(write_filename, "wb");
  if (fw == NULL)
  {
    err = SFError_OpenOutFail;
  }
  else
  {
    Reader reader;
    if (!reader_gkey_init(&reader, FednetHistoryLog2, &*fr))
    {
      err = SFError_NoMem;
    }
    else
    {
      Writer writer;
      writer_raw_init(&writer, &*fw);

      if (file_type == FileType_SFSkyPic)
      {
        /* Convert to sprite area */
        err = planets_to_sprites(&reader, &writer);
      }
      else
      {
        /* Convert to sprite area */
        err = tiles_to_sprites(&reader, &writer);
      }

      long int const out_bytes = writer_destroy(&writer);
      DEBUGF("%ld bytes written in quick_view\n", out_bytes);
      if (out_bytes < 0)
      {
        err = SFError_WriteFail;
      }
      else if (reader_feof(&reader))
      {
        err = SFError_Trunc;
      }
      else if (err == SFError_OK && reader_fgetc(&reader) != EOF)
      {
        err = SFError_TooLong;
      }
      reader_destroy(&reader);
    }

    if (fclose_dec(&*fw)) {
      err = SFError_WriteFail;
    }
  }
  fclose_dec(&*fr);
  return err;
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void quick_view(const char *const read_filename, int const file_type)
{
  if (file_type != FileType_SFMapGfx &&
      file_type != FileType_SFSkyPic)
  {
    /* Not a file type that we understand */
    RPT_ERR("BadFileType");
    return;
  }

  char cmd[sizeof(COMMAND_PREFIX) + L_tmpnam] = COMMAND_PREFIX;
  char const *const write_filename = tmpnam(cmd + sizeof(COMMAND_PREFIX) - 1);

  hourglass_on();
  SFError const err = try_convert(read_filename, write_filename, file_type);
  hourglass_off();

  if (!handle_error(err, read_filename, write_filename) &&
      !E(set_file_type(write_filename, FileType_Sprite)))
  {
    /* Open temporary Sprite file (e.g. in Paint) */
    if (_kernel_oscli(cmd) == _kernel_ERROR)
    {
      ON_ERR_RPT(_kernel_last_oserror());
    }
  }
}
