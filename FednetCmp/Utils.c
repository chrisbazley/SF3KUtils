/*
 *  FednetCmp - Fednet file compression/decompression
 *  Utility functions
 *  Copyright (C) 2001 Christopher Bazley
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
#include <stdbool.h>
#include "stdio.h"
#include <limits.h>

/* RISC OS library files */
#include "toolbox.h"
#include "kernel.h"
#include "flex.h"
#include "saveas.h"

/* My library files */
#include "Hourglass.h"
#include "Err.h"
#include "Debug.h"
#include "SFFormats.h"
#include "Macros.h"
#include "msgtrans.h"
#include "ReaderFlex.h"
#include "ReaderGKey.h"
#include "WriterFlex.h"
#include "WriterGKey.h"
#include "NoBudge.h"
#include "FOpenCount.h"
#include "ReaderFlex.h"
#include "WriterFlex.h"
#include "WriterRaw.h"
#include "ReaderRaw.h"
#include "FileUtils.h"

/* Local headers */
#include "Utils.h"

enum
{
  CopyBufferSize = BUFSIZ,
  PreExpandHeap = 512, /* No. of bytes to pre-allocate before disabling
                          flex budging (heap expansion). */
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
  WorstBitsPerChar = 9,
  OSByte_RWEscapeKeyStatus    = 229, /* _kernel_osbyte reason code */
  OSByte_ClearEscapeCondition = 124  /* _kernel_osbyte reason code */
};

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

bool compressed_file_type(int file_type)
{
  static int const comp_types[] =
  {
    FileType_Fednet,
    FileType_SFObjGfx,
    FileType_SFBasMap,
    FileType_SFBasObj,
    FileType_SFOvrMap,
    FileType_SFOvrObj,
    FileType_SFSkyCol,
    FileType_SFMissn,
    FileType_SFSkyPic,
    FileType_SFMapGfx,
    FileType_SFMapAni
  };

  for (size_t i = 0; i < ARRAY_SIZE(comp_types); i++)
  {
    if (comp_types[i] == file_type)
      return true; /* file type is compressed */
  }

  return false; /* unrecognised file type */
}

/* ----------------------------------------------------------------------- */

typedef enum
{
  Copy_OK,
  Copy_WriteFail,
  Copy_ReadFail,
  Copy_OSError,
  Copy_UserInterrupt,
  Copy_NoMem,
} CopyResult;

static CopyResult copy_data(Writer *const dst, Reader *const src,
  int const src_size)
{
  assert(dst != NULL);
  assert(src != NULL);
  assert(!writer_ferror(dst));
  assert(!reader_ferror(src));

  CopyResult result = Copy_OK;

  void *const buf = malloc(CopyBufferSize);
  if (buf == NULL)
  {
    return Copy_NoMem;
  }

  if (_kernel_osbyte(OSByte_RWEscapeKeyStatus, 0, 0) == _kernel_ERROR)
  {
    result = Copy_OSError;
  }
  else
  {
    _kernel_escape_seen();
    hourglass_on();

    while (!reader_feof(src))
    {
      if (_kernel_escape_seen())
      {
        result = Copy_UserInterrupt;
        break;
      }

      if (src_size > 0)
      {
        long int fpos = reader_ftell(src);
        if (fpos < 0)
        {
          result = Copy_ReadFail;
          break;
        }

        if (fpos > src_size)
        {
          fpos = src_size;
        }

        hourglass_percentage(((int)fpos * 100) / src_size);
      }

      size_t const n = reader_fread(buf, 1, CopyBufferSize, src);
      assert(n <= CopyBufferSize);
      if (reader_ferror(src))
      {
        result = Copy_ReadFail;
        break;
      }

      if (writer_fwrite(buf, 1, n, dst) != n)
      {
        result = Copy_WriteFail;
        break;
      }
    }

    hourglass_off();

    if (_kernel_osbyte(OSByte_RWEscapeKeyStatus, 1, 0) == _kernel_ERROR ||
        _kernel_osbyte(OSByte_ClearEscapeCondition, 0, 0) == _kernel_ERROR)
    {
      result = Copy_OSError;
    }
  }

  free(buf);
  return result;
}

/* ----------------------------------------------------------------------- */

static CopyResult copy_and_destroy_writer(Writer *const dst, Reader *const src,
  int const src_size)
{
  CopyResult result = copy_data(dst, src, src_size);
  long int const out_bytes = writer_destroy(dst);
  if (out_bytes < 0 && result == Copy_OK)
  {
    result = Copy_WriteFail;
  }
  return result;
}

/* ----------------------------------------------------------------------- */

static bool copy_done(CopyResult const result)
{
  bool success = false;
  switch (result)
  {
  case Copy_OSError:
    ON_ERR_RPT(_kernel_last_oserror());
    break;

  case Copy_UserInterrupt:
    RPT_ERR("Escape");
    break;

  case Copy_NoMem:
    RPT_ERR("NoMem");
    break;

  default:
    success = true;
    break;
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static bool write_to_buf(flex_ptr dst, void *const handle,
  bool (*const write_method)(Writer *, void *, char const *))
{
  assert(dst != NULL);
  assert(write_method != NULL);

  *dst = NULL;
  Writer writer;
  writer_flex_init(&writer, dst);

  bool success = write_method(&writer, handle, msgs_lookup("App"));
  long int const out_bytes = writer_destroy(&writer);
  if (out_bytes < 0 && success)
  {
    RPT_ERR("NoMem");
    success = false;
  }

  if (!success && *dst != NULL)
  {
    flex_free(dst);
  }

  return success;
}

/* ----------------------------------------------------------------------- */

bool copy_to_buf(void *const handle, Reader *const src,
  int const src_size, char const *const filename)
{
  assert(handle != NULL);
  assert(src != NULL);
  assert(filename != NULL);

  Writer writer;
  flex_ptr dst = handle;
  *dst = NULL;
  writer_flex_init(&writer, dst);

  CopyResult const result = copy_and_destroy_writer(&writer, src, src_size);
  bool success = false;
  switch (result)
  {
  case Copy_WriteFail:
    RPT_ERR("NoMem");
    break;

  case Copy_ReadFail:
    err_complain(DUMMY_ERRNO, msgs_lookup_subn("ReadFail", 1, filename));
    break;

  default:
    success = copy_done(result);
    break;
  }

  if (!success && *dst)
  {
    flex_free(dst);
  }
  return success;
}

/* ----------------------------------------------------------------------- */

int get_comp_size(flex_ptr buffer)
{
  /* Worst-case estimate */
  return sizeof(int32_t) +
    ((flex_size(buffer) * WorstBitsPerChar) / CHAR_BIT);
}

/* ----------------------------------------------------------------------- */

int get_decomp_size(flex_ptr buffer)
{
  Reader reader;
  reader_flex_init(&reader, buffer);
  int32_t decomp_size = 0;
  (void)reader_fread_int32(&decomp_size, &reader);
  reader_destroy(&reader);
  return decomp_size;
}

/* ----------------------------------------------------------------------- */

bool decomp_from_buf(Writer *const dst, void *const handle,
  char const *const filename)
{
  assert(dst != NULL);
  assert(handle != NULL);

  Reader reader, gkreader;
  flex_ptr src = handle;
  reader_flex_init(&reader, src);
  bool success = reader_gkey_init_from(&gkreader, FednetHistoryLog2, &reader);
  if (!success)
  {
    RPT_ERR("NoMem");
  }
  else
  {
    CopyResult const result = copy_data(dst, &gkreader, get_decomp_size(src));
    reader_destroy(&gkreader);

    success = false;
    switch (result)
    {
    case Copy_WriteFail:
      err_complain(DUMMY_ERRNO, msgs_lookup_subn("WriteFail", 1, filename));
      break;

    case Copy_ReadFail:
      RPT_ERR("BitStream");
      break;

    default:
      success = copy_done(result);
      break;
    }
  }
  reader_destroy(&reader);

  return success;
}

/* ----------------------------------------------------------------------- */

bool comp_from_buf(Writer *const dst, void *const handle,
  char const *const filename)
{
  assert(dst != NULL);
  assert(handle != NULL);
  assert(filename != NULL);

  flex_ptr src = handle;
  Reader reader;
  reader_flex_init(&reader, src);

  Writer gkwriter;
  int const src_size = *src ? flex_size(src) : 0;
  bool success = writer_gkey_init_from(&gkwriter, FednetHistoryLog2, src_size, dst);
  if (!success)
  {
    RPT_ERR("NoMem");
  }
  else
  {
    CopyResult const result = copy_and_destroy_writer(&gkwriter, &reader,
      src_size);
    success = false;
    switch (result)
    {
    case Copy_WriteFail:
      err_complain(DUMMY_ERRNO, msgs_lookup_subn("WriteFail", 1, filename));
      break;

    default:
      assert(result != Copy_ReadFail);
      success = copy_done(result);
      break;
    }
  }
  reader_destroy(&reader);
  return success;
}

/* ----------------------------------------------------------------------- */

bool load_file(char const *const filename, void *const handle,
  bool (*read_method)(void *, Reader *, int, char const *))
{
  assert(filename != NULL);
  assert(read_method != NULL);
  DEBUGF("Loading from file %s\n", filename);

  int size;
  if (E(get_file_size(filename, &size)))
  {
    return false;
  }

  bool success = false;
  FILE *const f = fopen_inc(filename, "rb");
  if (f == NULL)
  {
    err_complain(DUMMY_ERRNO, msgs_lookup_subn("OpenInFail", 1, filename));
  }
  else
  {
    Reader reader;
    reader_raw_init(&reader, f);
    success = read_method(handle, &reader, size, filename);
    reader_destroy(&reader);
    fclose_dec(f);
  }
  return success;
}

/* ----------------------------------------------------------------------- */

bool save_file(char const *const filename, int const file_type,
  void *const handle, bool (*const write_method)(Writer *, void *, char const *))
{
  assert(filename != NULL);
  assert(write_method != NULL);
  DEBUGF("Saving to file %s\n", filename);

  FILE *const f = fopen_inc(filename, "wb");
  if (f == NULL)
  {
    err_complain(DUMMY_ERRNO, msgs_lookup_subn("OpenOutFail", 1, filename));
    return false;
  }

  Writer writer;
  writer_raw_init(&writer, f);

  bool success = write_method(&writer, handle, filename);
  long int const out_bytes = writer_destroy(&writer);
  int const err = fclose_dec(f);
  if ((err || out_bytes < 0) && success)
  {
    err_complain(DUMMY_ERRNO, msgs_lookup_subn("WriteFail", 1, filename));
    success = false;
  }

  if (success && E(set_file_type(filename, file_type)))
  {
    success = false;
  }
  return success;
}

/* ----------------------------------------------------------------------- */

void tbox_send_data(const SaveAsFillBufferEvent * const safbe,
  ObjectId const saveas_id, flex_ptr dst, void *const handle,
  bool (*write_method)(Writer *, void *, char const *))
{
  assert(safbe != NULL);
  assert(safbe->hdr.event_code == SaveAs_FillBuffer);
  assert(dst != NULL);
  assert(write_method != NULL);

  DEBUGF("%d bytes received, requesting %d more\n",
    safbe->no_bytes, safbe->size);

  if (*dst == NULL)
  {
    (void)write_to_buf(dst, handle, write_method);
  }

  /* Calculate number of bytes still to send */
  int const dst_size = *dst ? flex_size(dst) : 0;
  DEBUGF("%d bytes to send\n", dst_size);

  int not_sent = 0;
  if (safbe->no_bytes < dst_size)
  {
    not_sent = dst_size - safbe->no_bytes;
  }
  DEBUGF("%d bytes not sent yet\n", not_sent);

  int chunk_size = not_sent;
  if (chunk_size > safbe->size)
  {
    /* We can't fit all of remaining data in the recipient's buffer
       so just fill it */
    chunk_size = safbe->size;
  }

  nobudge_register(PreExpandHeap); /* protect de-reference of flex pointer */
  void *const buffer = *dst ? (char *)*dst + safbe->no_bytes : NULL;
  DEBUGF("Saved %d bytes to buffer %p for object 0x%x\n",
         chunk_size, buffer, saveas_id);

  ON_ERR_RPT(saveas_buffer_filled(0, saveas_id, buffer, chunk_size));
  nobudge_deregister();

  /* Hide the dialogue box if saving is complete. ROOL's version of SaveAs
     doesn't do this automatically. :( */
  if (chunk_size < safbe->size)
  {
    ON_ERR_RPT(toolbox_hide_object(0, saveas_id));
  }
}

/* ----------------------------------------------------------------------- */

void tbox_save_file(SaveAsSaveToFileEvent * const sastfe,
  ObjectId const saveas_id, void *const handle,
  bool (*const write_method)(Writer *, void *, char const *))
{
  assert(sastfe != NULL);
  assert(sastfe->hdr.event_code == SaveAs_SaveToFile);
  assert(saveas_id != NULL_ObjectId);
  assert(write_method != NULL);

  unsigned int flags = SaveAs_SuccessfulSave;

  int file_type;
  if (E(saveas_get_file_type(0, saveas_id, &file_type)))
  {
    flags = 0;
  }
  else if (!save_file(sastfe->filename, file_type, handle, write_method))
  {
    flags = 0;
  }

  DEBUGF("Save was %ssuccessful for object 0x%x\n",
         flags & SaveAs_SuccessfulSave ? "" : "un", saveas_id);

  saveas_file_save_completed(flags, saveas_id, sastfe->filename);

  /* Hide the dialogue box if saving was successful. ROOL's version of SaveAs
     doesn't do this automatically. :( */
  if (flags & SaveAs_SuccessfulSave)
  {
    ON_ERR_RPT(toolbox_hide_object(0, saveas_id));
  }
}
