/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Utility functions
 *  Copyright (C) 2000 Christopher Bazley
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
#include <assert.h>
#include <string.h>
#include <limits.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "wimp.h"
#include "wimplib.h"
#include "swis.h"
#include "flex.h"
#include "saveas.h"

/* My library files */
#include "err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "SprFormats.h"
#include "FilePerc.h"
#include "WimpExtra.h"
#include "Debug.h"
#include "FOpenCount.h"
#include "ReaderFlex.h"
#include "WriterFlex.h"
#include "WriterRaw.h"
#include "ReaderRaw.h"
#include "hourglass.h"
#include "FileUtils.h"
#include "NoBudge.h"

/* Local headers */
#include "Utils.h"
#include "SFTInit.h"

enum
{
  CopyBufferSize = BUFSIZ,
  OSByte_RWEscapeKeyStatus    = 229, /* _kernel_osbyte reason code */
  OSByte_ClearEscapeCondition = 124, /* _kernel_osbyte reason code */
  ContinueButton = 3,
  MinWimpVersion = 321, /* Oldest version of the window manager which
                           supports the extensions to Wimp_ReportError */
  PreExpandHeap = 512, /* No. of bytes to pre-allocate before disabling
                          flex budging (heap expansion). */
  WorstBitsPerChar = 9,
};

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

bool dialogue_confirm(const char *mess)
{
  _kernel_oserror err_block = {DUMMY_ERRNO, ""};

  assert(mess != NULL);
  STRCPY_SAFE(err_block.errmess, mess);

  if (wimp_version >= MinWimpVersion)
  {
    /* Nice error box */
    return (wimp_report_error(
      &err_block,
      Wimp_ReportError_UseCategory |
      Wimp_ReportError_CatQuestion,
      taskname,
      NULL,
      NULL,
      msgs_lookup("ConButtons")) ==
        ContinueButton);
  }
  else
  {
    /* Backwards compatibility */
    return (wimp_report_error(
      &err_block,
      Wimp_ReportError_OK | Wimp_ReportError_Cancel,
      taskname) ==
        Wimp_ReportError_OK);
  }
}

/* ----------------------------------------------------------------------- */

void load_failed(CONST _kernel_oserror *const error,
  void *const client_handle)
{
  NOT_USED(client_handle);
  if (error != NULL)
  {
    err_check_rep(msgs_error_subn(error->errnum, "LoadFail", 1,
      error->errmess));
  }
}

/* ----------------------------------------------------------------------- */

static SFError copy_data(Writer *const dst, Reader *const src,
  int const src_size)
{
  assert(dst != NULL);
  assert(src != NULL);
  assert(!writer_ferror(dst));
  assert(!reader_ferror(src));

  SFError err = SFError_OK;

  void *const buf = malloc(CopyBufferSize);
  if (buf == NULL)
  {
    return SFError_NoMem;
  }

  if (_kernel_osbyte(OSByte_RWEscapeKeyStatus, 0, 0) == _kernel_ERROR)
  {
    err = SFError_OSError;
  }
  else
  {
    _kernel_escape_seen();
    hourglass_on();

    while (!reader_feof(src))
    {
      if (_kernel_escape_seen())
      {
        err = SFError_Escape;
        break;
      }

      if (src_size > 0)
      {
        long int fpos = reader_ftell(src);
        if (fpos < 0)
        {
          err = SFError_ReadFail;
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
        err = SFError_ReadFail;
        break;
      }

      if (writer_fwrite(buf, 1, n, dst) != n)
      {
        err = SFError_WriteFail;
        break;
      }
    }

    hourglass_off();

    if (_kernel_osbyte(OSByte_RWEscapeKeyStatus, 1, 0) == _kernel_ERROR ||
        _kernel_osbyte(OSByte_ClearEscapeCondition, 0, 0) == _kernel_ERROR)
    {
      err = SFError_OSError;
    }
  }

  free(buf);
  return err;
}

/* ----------------------------------------------------------------------- */

static SFError copy_and_destroy_writer(Writer *const dst, Reader *const src,
  int const src_size)
{
  SFError err = copy_data(dst, src, src_size);
  long int const out_bytes = writer_destroy(dst);
  if (out_bytes < 0 && err == SFError_OK)
  {
    err = SFError_WriteFail;
  }
  return err;
}

/* ----------------------------------------------------------------------- */

static bool write_to_buf(flex_ptr dst, void *const handle,
  bool (*const write_method)(Writer *, void *, char const *))
{
  assert(dst != NULL);
  assert(write_method != NULL);

  hourglass_on();

  *dst = NULL;
  Writer writer;
  writer_flex_init(&writer, dst);

  bool success = write_method(&writer, handle, msgs_lookup("App"));
  long int const out_bytes = writer_destroy(&writer);

  hourglass_off();

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

  SFError err = copy_and_destroy_writer(&writer, src, src_size);
  if (err == SFError_WriteFail)
  {
    err = SFError_NoMem;
  }
  bool success = !handle_error(err, filename, "RAM");

  if (!success && *dst)
  {
    flex_free(dst);
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static bool save_file(char const *const filename, int const file_type,
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

  hourglass_on();

  Writer writer;
  writer_raw_init(&writer, f);
  bool success = write_method(&writer, handle, filename);
  long int const nbytes = writer_destroy(&writer);
  int const err = fclose_dec(f);

  hourglass_off();

  if ((err || nbytes < 0) && success)
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

void tbox_send_data(ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle, flex_ptr dst,
  bool (*write_method)(Writer *, void *, char const *))
{
  assert(event != NULL);
  assert(id_block != NULL);
  assert(dst != NULL);
  assert(write_method != NULL);

  const SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)event;
  assert(safbe->hdr.event_code == SaveAs_FillBuffer);
  DEBUGF("%d bytes received, requesting %d more\n",
    safbe->no_bytes, safbe->size);

  if (safbe->no_bytes == 0)
  {
    /* Force the dialogue box's values to be incorporated in the output */
    if (*dst)
    {
      flex_free(dst);
    }
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
         chunk_size, buffer, id_block->self_id);

  ON_ERR_RPT(saveas_buffer_filled(0, id_block->self_id, buffer, chunk_size));
  nobudge_deregister();

  /* Hide the dialogue box if saving is complete. ROOL's version of SaveAs
     doesn't do this automatically. :( */
  if (chunk_size < safbe->size)
  {
    ON_ERR_RPT(toolbox_hide_object(0, id_block->self_id));
  }
}

/* ----------------------------------------------------------------------- */

void tbox_save_file(ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle,
  bool (*const write_method)(Writer *, void *, char const *))
{
  assert(event != NULL);
  assert(id_block != NULL);
  assert(write_method != NULL);

  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)event;
  assert(sastfe->hdr.event_code == SaveAs_SaveToFile);

  unsigned int flags = SaveAs_SuccessfulSave;

  int file_type;
  if (E(saveas_get_file_type(0, id_block->self_id, &file_type)))
  {
    flags = 0;
  }
  else if (!save_file(sastfe->filename, file_type, handle, write_method))
  {
    flags = 0;
  }

  DEBUGF("Save was %ssuccessful for object 0x%x\n",
         flags & SaveAs_SuccessfulSave ? "" : "un", id_block->self_id);

  saveas_file_save_completed(flags, id_block->self_id, sastfe->filename);

  /* Hide the dialogue box if saving was successful. ROOL's version of SaveAs
     doesn't do this automatically. :( */
  if (flags & SaveAs_SuccessfulSave)
  {
    ON_ERR_RPT(toolbox_hide_object(0, id_block->self_id));
  }
}

/* ----------------------------------------------------------------------- */

const _kernel_oserror *conv_error(SFError const err,
  char const *const read_filename, char const *const write_filename)
{
  const _kernel_oserror *e = NULL;
  static char const *const ms_to_token[] = {
#define DECLARE_ERROR(ms) [SFError_ ## ms] = #ms,
#include "DeclErrors.h"
#undef DECLARE_ERROR
  };
  switch (err)
  {
  case SFError_OK:
    break;

  case SFError_OSError:
    e = _kernel_last_oserror();
    break;

  case SFError_ForceAnim:
  case SFError_ForceOff:
  case SFError_ForceSky:
    WARN(ms_to_token[err]);
    break;

  case SFError_OpenOutFail:
  case SFError_WriteFail:
    /* Most write errors are treated as WriteFail, including fseek failures */
    e = msgs_error_subn(DUMMY_ERRNO, ms_to_token[err], 1, write_filename);
    break;

  default:
    /* Assume everything else is a read error */
    e = msgs_error_subn(DUMMY_ERRNO, ms_to_token[err], 1, read_filename);
    break;
  }
  return e;
}

/* ----------------------------------------------------------------------- */

bool handle_error(SFError const err, char const *const read_filename,
  char const *const write_filename)
{
  return E(conv_error(err, read_filename, write_filename));
}

/* ----------------------------------------------------------------------- */

int worst_comp_size(int const orig_size)
{
  /* Worst-case estimate */
  return sizeof(int32_t) +
    ((orig_size * WorstBitsPerChar) / CHAR_BIT);
}
