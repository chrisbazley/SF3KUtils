/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Directory scan
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
#include "stdlib.h"
#include "stdio.h"
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <stdint.h>

/* RISC OS library files */
#include "wimp.h"
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "wimplib.h"
#include "window.h"
#include "gadgets.h"
#include "flex.h"

/* My library files */
#include "SFFormats.h"
#include "err.h"
#include "msgtrans.h"
#include "ViewsMenu.h"
#include "Macros.h"
#include "Scheduler.h"
#include "PathTail.h"
#include "Debug.h"
#include "StrExtra.h"
#include "DeIconise.h"
#include "GadgetUtil.h"
#include "ScreenSize.h"
#include "FileUtils.h"
#include "UserData.h"
#include "DirIter.h"
#include "StackViews.h"
#include "WriterRaw.h"
#include "ReaderRaw.h"
#include "WriterGKey.h"
#include "ReaderGKey.h"
#include "WriterFlex.h"
#include "ReaderFlex.h"
#include "FOpenCount.h"
#include "EventExtra.h"

/* Local headers */
#include "SFgfxconv.h"
#include "Utils.h"
#include "Scan.h"

/* Window component IDs */
enum
{
  ComponentId_Abort_ActButton   = 0x01,
  ComponentId_Skip_ActButton    = 0x02,
  ComponentId_Restart_ActButton = 0x03,
  ComponentId_Fourth_ActButton  = 0x04,
  ComponentId_Message_Button    = 0x05,
  ComponentId_Scanned_Button    = 0x0a,
  ComponentId_Converted_Button  = 0x0b,
  ComponentId_Activity_Button   = 0x0c,
  ComponentId_FilePath_Button   = 0x0d
};

typedef enum
{
  ScanStatus_Error,
  ScanStatus_Paused,
  ScanStatus_ExamineObject,
  ScanStatus_OpenInput, /* from ExamineObject */
  ScanStatus_StartScanSprites, /* from OpenInput */
  ScanStatus_ScanSprites, /* from StartScanSprites */
  ScanStatus_PickConversion, /* from ScanSprites */
  ScanStatus_DecideOutput, /* from PickConversion or OpenInput */
  ScanStatus_MakePath, /* from DecideOutput */
  ScanStatus_OpenOutput, /* from MakePath or CloseTmpOutput */
  ScanStatus_StartConvert, /* from OpenOutput or DecideOutput */
  ScanStatus_Convert, /* from StartConvert */
  ScanStatus_CloseInput, /* from StartConvert or Convert */
  ScanStatus_CloseTmpOutput, /* from CloseInput */
  ScanStatus_CopyTmp, /* from CopyTmp or OpenOutput */
  ScanStatus_CloseOutput, /* from CloseInput or CopyTmp */
  ScanStatus_SetFileType, /* from CloseOutput */
  ScanStatus_NextObject, /* from ExamineObject, PickConversion or SetFileType */
  ScanStatus_Finished, /* from NextObject or ExamineObject */
}
ScanStatus;

/* Constant numeric values */
enum
{
  PathElements      = 3,
  ErrorWindowWidth  = 736,
  ErrorWindowHeight = 596,
  ProgWindowWidth   = 620,
  ProgWindowHeight  = 252,
  ProgWindowXOffset = 60,
  Priority          = SchedulerPriority_Max,
  MaxDecimalLen     = 15,
  MaxActionLen      = 15,
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
};

typedef struct
{
  ObjectId window_id; /* dialogue window */
  DirIterator *iterator;
  ScanStatus phase; /* what is going on */
  unsigned int num_checked;
  unsigned int num_output;
  int input_type;
  int output_type;

  void *out_buf;
  ScanStatus return_phase; /* for pause, error */
  /* preserved data for retry */
  unsigned int retry_num_checked;
  unsigned int retry_num_output;
  int retry_position;
  char return_action[MaxActionLen + 1];
  StringBuffer load_path, save_path;
  char const *real_save_path;
  size_t make_path_offset; /* avoids creating directories that should already exist */
  FILE *in;
  FILE *out;
  Reader reader;
  Writer writer;
  ConvertIter *conv_iter;

  bool extract_images:1;
  bool extract_data:1;
  bool replace_input:1;
  bool has_reader:1;
  bool has_writer:1;
} ScanDataState;

typedef struct
{
  UserData list_node;
  ScanDataState state;
  ScanSpritesContext context;
  union {
    ScanSpritesIter scan_sprites;
    SpritesToPlanetsIter sprites_to_planets;
    PlanetsToSpritesIter planets_to_sprites;
    TilesToSpritesIter tiles_to_sprites;
    SpritesToTilesIter sprites_to_tiles;
    SpritesToSkyIter sprites_to_sky;
    SkyToSpritesIter sky_to_sprites;
  } iter;
  unsigned char copy_buf[256];
}
ScanData;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static SchedulerIdleFunction do_scan_idle;

static void scan_reader_destroy(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  if (scan_data->state.has_reader)
  {
    scan_data->state.has_reader = false;
    reader_destroy(&scan_data->state.reader);
  }
}

static long int scan_writer_destroy(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  long int n = 0;
  if (scan_data->state.has_writer)
  {
    scan_data->state.has_writer = false;
    n = writer_destroy(&scan_data->state.writer);
  }
  DEBUGF("%ld bytes written in scan_writer_destroy\n", n);
  return n;
}

static void scan_close_in(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  if (scan_data->state.in)
  {
    FILE *const f = scan_data->state.in;
    scan_data->state.in = NULL;
    fclose_dec(f);
  }
}

static int scan_close_out(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  int err = 0;
  if (scan_data->state.out)
  {
    FILE *const f = scan_data->state.out;
    scan_data->state.out = NULL;
    err = fclose_dec(f);
  }
  return err;
}

static void scan_finished(ScanData *const scan_data)
{
  if (scan_data != NULL)
  {
    DEBUGF("Destroying scan %p (object 0x%x)\n",
      (void *)scan_data, scan_data->state.window_id);

    userdata_remove_from_list(&scan_data->list_node);

    /* if we were null polling then stop */
    if (scan_data->state.phase != ScanStatus_Error &&
        scan_data->state.phase != ScanStatus_Paused)
    {
      scheduler_deregister(do_scan_idle, scan_data);
    }

    /* Destroy main Window object */
    ON_ERR_RPT(remove_event_handlers_delete(scan_data->state.window_id));
    ON_ERR_RPT(ViewsMenu_remove(scan_data->state.window_id));

    diriterator_destroy(scan_data->state.iterator);

    scan_reader_destroy(scan_data);
    (void)scan_writer_destroy(scan_data);

    scan_close_in(scan_data);
    (void)scan_close_out(scan_data);

    if (scan_data->state.out_buf)
    {
      flex_free(&scan_data->state.out_buf);
    }

    stringbuffer_destroy(&scan_data->state.load_path);
    stringbuffer_destroy(&scan_data->state.save_path);

    free(scan_data);
  }
}

/* ----------------------------------------------------------------------- */

static void display_error(const ScanData *const scan_data, const char *error_message)
{
  /* Opens progress window to centre of screen at expanded size
     and reconfigures buttons */
  assert(scan_data != NULL);
  assert(error_message != NULL);

  ON_ERR_RPT(set_gadget_hidden(scan_data->state.window_id, ComponentId_Skip_ActButton, false));
  ON_ERR_RPT(set_gadget_hidden(scan_data->state.window_id, ComponentId_Restart_ActButton, false));

  /* Can't 'Skip' if stuck at end of directory (error leaving it),
     or scan_data->state.position wasn't updated (error calling OS_GBPB) */
  ON_ERR_RPT(set_gadget_faded(scan_data->state.window_id,
                              ComponentId_Skip_ActButton,
                              scan_data->state.iterator == NULL));

  ON_ERR_RPT(actionbutton_set_text(
                   0,
                   scan_data->state.window_id,
                   ComponentId_Fourth_ActButton,
                   msgs_lookup("ScanBRetry")));

  ON_ERR_RPT(gadget_set_help_message(
                   0,
                   scan_data->state.window_id,
                   ComponentId_Fourth_ActButton,
                   msgs_lookup("ScanHRetry")));

  ON_ERR_RPT(button_set_value(
                   0,
                   scan_data->state.window_id,
                   ComponentId_Message_Button,
                   (char *)error_message));

  /* Alter visible area and centre */
  int width, height;
  if (E(get_screen_size(&width, &height)))
  {
    return;
  }

  WindowShowObjectBlock wsob = {
    .visible_area = {
      .xmin = width/2 - ErrorWindowWidth/2,
      .ymax = height/2 + ErrorWindowHeight/2,
      .xmax = width/2 + ErrorWindowWidth/2,
      .ymin = height/2 - ErrorWindowHeight/2,
    },
    .xscroll = 0,
    .yscroll = 0,
    .behind = WimpWindow_Top,
  };

  ON_ERR_RPT(DeIconise_show_object(0,
                                   scan_data->state.window_id,
                                   Toolbox_ShowObject_FullSpec,
                                   &wsob,
                                   NULL_ObjectId,
                                   NULL_ComponentId));
}

/* ----------------------------------------------------------------------- */

static void display_pause(const ScanData *const scan_data)
{
  assert(scan_data != NULL);

  ON_ERR_RPT(actionbutton_set_text(
             0,
             scan_data->state.window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanBPause")));

  ON_ERR_RPT(gadget_set_help_message(
             0,
             scan_data->state.window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanHPause")));
}

/* ----------------------------------------------------------------------- */

static void display_continue(const ScanData *const scan_data)
{
  assert(scan_data != NULL);

  ON_ERR_RPT(actionbutton_set_text(
             0,
             scan_data->state.window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanBCont")));

  ON_ERR_RPT(gadget_set_help_message(
             0,
             scan_data->state.window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanHCont")));
}

/* ----------------------------------------------------------------------- */

static void display_progress(const ScanData *const scan_data)
{
  assert(scan_data != NULL);

  /* Reduces progress window to normal size, restores normal buttons */
  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->state.window_id,
                   ComponentId_Skip_ActButton,
                   true));

  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->state.window_id,
                   ComponentId_Restart_ActButton,
                   true));

  if (scan_data->state.phase == ScanStatus_Paused)
  {
    display_continue(scan_data);
  }
  else
  {
    display_pause(scan_data);
  }

  /* Alter visible area but not position */
  WimpGetWindowInfoBlock windowinfo;
  if (E(window_get_wimp_handle(0, scan_data->state.window_id, &windowinfo.window_handle)))
  {
    return;
  }

  if (E(wimp_get_window_info_no_icon_data(&windowinfo)))
  {
    return;
  }

  WindowShowObjectBlock wsob = {
    .visible_area = {
      .xmin = windowinfo.window_data.visible_area.xmin,
      .ymax = windowinfo.window_data.visible_area.ymax,
      .xmax = windowinfo.window_data.visible_area.xmin + ProgWindowWidth,
      .ymin = windowinfo.window_data.visible_area.ymax - ProgWindowHeight,
    },
    .xscroll = ProgWindowXOffset,
    .yscroll = 0,
    .behind = windowinfo.window_data.behind,
  };

  ON_ERR_RPT(DeIconise_show_object(0,
                                   scan_data->state.window_id,
                                   Toolbox_ShowObject_FullSpec,
                                   &wsob,
                                   NULL_ObjectId,
                                   NULL_ComponentId));
}

/* ----------------------------------------------------------------------- */

static void update_window(const ScanData *const scan_data, const char *action, char *file_path)
{
  /* Only update action text if not the same */
  assert(scan_data != NULL);
  assert(action != NULL);
  assert(file_path != NULL);

  char current_action[MaxActionLen + 1];
  ON_ERR_RPT(button_get_value(
      0,
      scan_data->state.window_id,
      ComponentId_Activity_Button,
      current_action,
      sizeof(current_action),
      NULL));

  char * const new_action = msgs_lookup(action);
  if (strcmp(current_action, new_action) != 0)
  {
    ON_ERR_RPT(button_set_value(
        0,
        scan_data->state.window_id,
        ComponentId_Activity_Button,
        new_action));
  }

  /* Whereas file paths are v. unlikely to be the same (impossible?) */
  ON_ERR_RPT(button_set_value(
      0,
      scan_data->state.window_id,
      ComponentId_FilePath_Button,
      file_path));
}

/* ----------------------------------------------------------------------- */

static void display_nchecked(const ScanData *const scan_data)
{
  char num[MaxDecimalLen + 1];

  assert(scan_data != NULL);
  sprintf(num, "%u", scan_data->state.num_checked);
  ON_ERR_RPT(button_set_value(0, scan_data->state.window_id,
                              ComponentId_Scanned_Button, num));
}


/* ----------------------------------------------------------------------- */

static void display_nout(const ScanData *const scan_data)
{
  char num[MaxDecimalLen + 1];

  assert(scan_data != NULL);
  sprintf(num, "%u", scan_data->state.num_output);
  ON_ERR_RPT(button_set_value(0, scan_data->state.window_id,
                              ComponentId_Converted_Button, num));
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *append_to_string_buffer(
                               StringBuffer *sb,
                               DirIterator *it,
                               size_t (*get_string)(const DirIterator *it,
                                                    char *buffer,
                                                    size_t buff_size))
{
  const _kernel_oserror *e = NULL;
  bool retry;
  size_t buff_size = 0;

  assert(sb != NULL);
  assert(it != NULL);
  assert(get_string != NULL);

  do
  {
    char * const buffer = stringbuffer_prepare_append(sb, &buff_size);
    retry = false;
    if (buffer == NULL)
    {
      e = msgs_error(DUMMY_ERRNO, "NoMem");
    }
    else
    {
      const size_t nchars = get_string(it, buffer, buff_size);

      /* Check for truncation of the string */
      if (nchars >= buff_size)
      {
        /* String was truncated: try again with a larger buffer */
        retry = true;
        buff_size = nchars + 1;
      }
      else
      {
        /* No truncation: set the new string length and setup undo state */
        stringbuffer_finish_append(sb, nchars);
      }
    }
  }
  while (retry);

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *examine_object(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  scan_data->state.retry_num_output = scan_data->state.num_output;
  scan_data->state.retry_num_checked = scan_data->state.num_checked;

  if (diriterator_is_empty(scan_data->state.iterator))
  {
    scan_data->state.phase = ScanStatus_Finished;
    return NULL;
  }

  stringbuffer_truncate(&scan_data->state.load_path, 0);

  const _kernel_oserror *e = append_to_string_buffer(
    &scan_data->state.load_path, scan_data->state.iterator,
    diriterator_get_object_path_name);

  ScanStatus new_phase = ScanStatus_NextObject;
  if (e == NULL)
  {
    DirIteratorObjectInfo info;
    bool skip = true;
    switch (diriterator_get_object_info(scan_data->state.iterator, &info))
    {
      case ObjectType_File:
      case ObjectType_Image: /* (image files are treated as normal files) */
        scan_data->state.input_type = info.file_type;
        scan_data->state.output_type = FileType_Null;

        /* check whether we should load file */
        switch (info.file_type)
        {
          case FileType_SFMapGfx:
          case FileType_SFSkyPic:
          case FileType_SFSkyCol:
            if (scan_data->state.extract_images || scan_data->state.extract_data)
            {
              scan_data->state.output_type = scan_data->state.extract_images ?
                                             FileType_Sprite : FileType_CSV;
              new_phase = ScanStatus_OpenInput;
              skip = false;
            }
            break;

          case FileType_Sprite:
            if (!scan_data->state.extract_images && !scan_data->state.extract_data)
            {
              new_phase = ScanStatus_OpenInput;
              skip = false;
            }
            break;

          default:
            /* Ignore unsupported file types */
            break;
        }

        if (!skip)
        {
          /* Remove the previous sub-path (does nothing if already undone) */
          stringbuffer_undo(&scan_data->state.save_path);
          e = append_to_string_buffer(&scan_data->state.save_path,
                                      scan_data->state.iterator,
                                      diriterator_get_object_sub_path_name);
        }

        if (!e)
        {
          scan_data->state.num_checked ++;
          display_nchecked(scan_data);
        }
        break;

      case ObjectType_Directory: /* Object is directory - go down a level */
        skip = false;
        break;

    } /* what object type */

    if (skip)
    {
      /* File of no interest */
      update_window(scan_data, "ScanTIgnore",
                    stringbuffer_get_pointer(&scan_data->state.load_path));
    }
    else
    {
      update_window(scan_data, "ScanTOpen",
                    stringbuffer_get_pointer(&scan_data->state.load_path));
    }
  }

  if (e == NULL)
    scan_data->state.phase = new_phase;

  return e;
}

static const _kernel_oserror *scan_error(SFError err, ScanData *const scan_data)
{
  assert(scan_data);

  if (err == SFError_OK)
  {
    return NULL;
  }

  if (scan_data->state.replace_input)
  {
    if ((scan_data->state.phase == ScanStatus_CopyTmp) ||
        (scan_data->state.phase == ScanStatus_CloseOutput))
    {
      /* Can't fail to read from the temporary output buffer */
      assert(err != SFError_ReadFail);
    }
    else
    {
      /* Can fail to write to the temporary output buffer (out of memory) */
      if (err == SFError_WriteFail)
      {
        err = SFError_NoMem;
      }
    }
  }

  return conv_error(err, stringbuffer_get_pointer(&scan_data->state.load_path),
                         stringbuffer_get_pointer(&scan_data->state.save_path));
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *open_input(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  assert(!scan_data->state.in);
  scan_data->state.in = fopen_inc(
    stringbuffer_get_pointer(&scan_data->state.load_path), "rb");

  SFError err = SFError_OK;
  if (!scan_data->state.in)
  {
    err = SFError_OpenInFail;
  }
  else
  {
    assert(!scan_data->state.has_reader);
    switch (scan_data->state.input_type)
    {
    case FileType_SFMapGfx:
    case FileType_SFSkyPic:
    case FileType_SFSkyCol:
      if (!reader_gkey_init(&scan_data->state.reader, FednetHistoryLog2,
                            scan_data->state.in))
      {
        err = SFError_NoMem;
        scan_close_in(scan_data);
      }
      else
      {
        scan_data->state.has_reader = true;
        scan_data->state.phase = ScanStatus_DecideOutput;
      }
      break;

    case FileType_Sprite:
      reader_raw_init(&scan_data->state.reader, scan_data->state.in);
      scan_data->state.has_reader = true;
      scan_data->state.phase = ScanStatus_StartScanSprites;
      break;

    default:
      assert(!"Unexpected input filetype");
      break;
    }
  }
  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *start_scan_sprites(ScanData *const scan_data)
{
  assert(scan_data->state.in);
  SFError const err = scan_sprite_file_init(&scan_data->iter.scan_sprites,
                                            &scan_data->state.reader,
                                            &scan_data->context);
  if (err == SFError_OK)
  {
    scan_data->state.phase = ScanStatus_ScanSprites;
  }
  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *scan_sprites(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  SFError err = convert_advance(&scan_data->iter.scan_sprites.super);
  if (err == SFError_Done)
  {
    err = SFError_OK;
    scan_data->state.phase = ScanStatus_PickConversion;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *pick_conversion(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  /* Does the sprite file contain valid planet or tile graphics? */
  int const ntypes = count_spr_types(&scan_data->context);
  if (ntypes > 1)
  {
    return msgs_error(DUMMY_ERRNO, "AutoDouble");
  }

  if (ntypes == 0)
  {
    scan_reader_destroy(scan_data);
    scan_close_in(scan_data);
    scan_data->state.phase = ScanStatus_NextObject;
    return NULL;
  }

  SFError err = SFError_OK;
  if (reader_fseek(&scan_data->state.reader, 0, SEEK_SET))
  {
    err = SFError_BadSeek;
  }
  else if (scan_data->context.tiles.count > 0)
  {
    scan_data->state.output_type = FileType_SFMapGfx;
    if (!scan_data->context.tiles.got_hdr)
    {
      err = SFError_NoAnim;
    }
    else if (scan_data->context.tiles.fixed_hdr)
    {
      err = SFError_BadAnims;
    }
  }
  else if (scan_data->context.planets.count > 0)
  {
    scan_data->state.output_type = FileType_SFSkyPic;
    if (!scan_data->context.planets.got_hdr)
    {
      err = SFError_NoOffset;
    }
    else if (scan_data->context.planets.fixed_hdr)
    {
      err = SFError_BadPaintOff;
    }
  }
  else if (scan_data->context.sky.count > 0)
  {
    scan_data->state.output_type = FileType_SFSkyCol;
    if (!scan_data->context.sky.got_hdr)
    {
      err = SFError_NoHeight;
    }
    else if (scan_data->context.sky.fixed_render)
    {
      err = SFError_BadRend;
    }
    else if (scan_data->context.sky.fixed_stars)
    {
      err = SFError_BadStar;
    }
  }

  if (err == SFError_OK)
  {
    scan_data->state.phase = ScanStatus_DecideOutput;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static void decide_output(ScanData *const scan_data)
{
  assert(scan_data);

  if (scan_data->state.replace_input)
  {
    assert(!scan_data->state.has_writer);
    writer_flex_init(&scan_data->state.writer, &scan_data->state.out_buf);
    scan_data->state.has_writer = true;
    scan_data->state.phase = ScanStatus_StartConvert;
  }
  else
  {
    scan_data->state.phase = ScanStatus_MakePath;
  }
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *open_output(ScanData *const scan_data)
{
  assert(scan_data);
  assert(!scan_data->state.out);

  SFError err = SFError_OK;
  scan_data->state.out = fopen_inc(stringbuffer_get_pointer(&scan_data->state.save_path), "wb");

  if (!scan_data->state.out)
  {
    err = SFError_OpenOutFail;
  }
  else
  {
    long int min_size = -1;
    switch(scan_data->state.output_type)
    {
    case FileType_SFMapGfx:
      min_size = tiles_size(&scan_data->context.tiles.hdr);
      break;

    case FileType_SFSkyPic:
      min_size = planets_size(&scan_data->context.planets.hdr);
      break;

    case FileType_SFSkyCol:
      min_size = sky_size();
      break;
    }

    assert(!scan_data->state.has_writer);

    if (min_size >= 0)
    {
      if (!writer_gkey_init(&scan_data->state.writer, FednetHistoryLog2, min_size,
                            scan_data->state.out))
      {
        err = SFError_NoMem;
        (void)scan_close_out(scan_data);
      }
      else
      {
        scan_data->state.has_writer = true;
      }
    }
    else
    {
      writer_raw_init(&scan_data->state.writer, scan_data->state.out);
      scan_data->state.has_writer = true;
    }
  }

  if (err == SFError_OK)
  {
    scan_data->state.phase = scan_data->state.replace_input ?
                             ScanStatus_CopyTmp : ScanStatus_StartConvert;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *start_convert(ScanData *const scan_data)
{
  assert(scan_data != NULL);
  update_window(scan_data, "ScanTConvert",
                stringbuffer_get_pointer(&scan_data->state.load_path));

  SFError err = SFError_OK;

  switch(scan_data->state.output_type)
  {
  case FileType_SFMapGfx:
    assert(scan_data->state.input_type == FileType_Sprite);
    scan_data->state.conv_iter = &scan_data->iter.sprites_to_tiles.super;

    err = sprites_to_tiles_init(&scan_data->iter.sprites_to_tiles,
      &scan_data->state.reader, &scan_data->state.writer, &scan_data->context.tiles);

    break;

  case FileType_SFSkyPic:
    assert(scan_data->state.input_type == FileType_Sprite);
    scan_data->state.conv_iter = &scan_data->iter.sprites_to_planets.super;

    err = sprites_to_planets_init(&scan_data->iter.sprites_to_planets,
      &scan_data->state.reader, &scan_data->state.writer, &scan_data->context.planets);

    break;

  case FileType_SFSkyCol:
    assert(scan_data->state.input_type == FileType_Sprite);
    scan_data->state.conv_iter = &scan_data->iter.sprites_to_sky.super;

    err = sprites_to_sky_init(&scan_data->iter.sprites_to_sky,
      &scan_data->state.reader, &scan_data->state.writer, &scan_data->context.sky);

    break;

  case FileType_Sprite:
    switch (scan_data->state.input_type)
    {
    case FileType_SFMapGfx:
      scan_data->state.conv_iter = &scan_data->iter.tiles_to_sprites.super;

      if (scan_data->state.extract_data)
      {
        err = tiles_to_sprites_ext_init(&scan_data->iter.tiles_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }
      else
      {
        err = tiles_to_sprites_init(&scan_data->iter.tiles_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }

      break;

    case FileType_SFSkyPic:
      scan_data->state.conv_iter = &scan_data->iter.planets_to_sprites.super;

      if (scan_data->state.extract_data)
      {
        err = planets_to_sprites_ext_init(&scan_data->iter.planets_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }
      else
      {
        err = planets_to_sprites_init(&scan_data->iter.planets_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }

      break;

    case FileType_SFSkyCol:
      scan_data->state.conv_iter = &scan_data->iter.sky_to_sprites.super;

      if (scan_data->state.extract_data)
      {
        err = sky_to_sprites_ext_init(&scan_data->iter.sky_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }
      else
      {
        err = sky_to_sprites_init(&scan_data->iter.sky_to_sprites,
          &scan_data->state.reader, &scan_data->state.writer);
      }

      break;

    default:
      assert(!"Unexpected input filetype");
      break;
    }
    break;

  case FileType_CSV:
    scan_data->state.conv_iter = NULL;

    switch (scan_data->state.input_type)
    {
    case FileType_SFMapGfx:
      err = tiles_to_csv(&scan_data->state.reader, &scan_data->state.writer);
      break;

    case FileType_SFSkyPic:
      err = planets_to_csv(&scan_data->state.reader, &scan_data->state.writer);
      break;

    case FileType_SFSkyCol:
      err = sky_to_csv(&scan_data->state.reader, &scan_data->state.writer);
      break;

    default:
      assert(!"Unexpected input filetype");
      break;
    }
    break;

  default:
    assert(!"Unexpected output filetype");
    break;
  }

  if (err == SFError_OK)
  {
    scan_data->state.phase = scan_data->state.conv_iter ?
                             ScanStatus_Convert : ScanStatus_CloseInput;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *convert_data(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  SFError err = convert_advance(scan_data->state.conv_iter);

  if (err == SFError_Done)
  {
    if ((scan_data->state.input_type == FileType_SFMapGfx ||
         scan_data->state.input_type == FileType_SFSkyPic) &&
         reader_fgetc(&scan_data->state.reader) != EOF)
    {
      err = SFError_TooLong;
    }
    else
    {
      err = SFError_OK;
      scan_data->state.phase = ScanStatus_CloseInput;
    }
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static void close_input(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  scan_reader_destroy(scan_data);
  scan_close_in(scan_data);

  scan_data->state.phase = scan_data->state.replace_input ?
                           ScanStatus_CloseTmpOutput : ScanStatus_CloseOutput;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *close_output(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  SFError err = SFError_OK;

  long int const out_bytes = scan_writer_destroy(scan_data);
  if (scan_close_out(scan_data))
  {
    err = SFError_WriteFail;
  }
  else if (out_bytes < 0)
  {
    err = SFError_WriteFail;
  }

  if (err == SFError_OK)
  {
    /* Update count of files output */
    scan_data->state.num_output++;
    display_nout(scan_data);

    scan_data->state.phase = ScanStatus_SetFileType;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *start_copy_tmp(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  SFError err = SFError_OK;

  long int const out_bytes = scan_writer_destroy(scan_data);
  if (out_bytes < 0)
  {
    err = SFError_WriteFail;
  }
  else
  {
    assert(!scan_data->state.has_reader);
    reader_flex_init(&scan_data->state.reader, &scan_data->state.out_buf);
    scan_data->state.has_reader = true;
    scan_data->state.phase = ScanStatus_OpenOutput;
  }
  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *copy_tmp(ScanData *const scan_data)
{
  assert(scan_data != NULL);

  SFError err = SFError_OK;
  size_t const n = reader_fread(scan_data->copy_buf, 1, sizeof(scan_data->copy_buf),
                                &scan_data->state.reader);
  assert(n <= sizeof(scan_data->copy_buf));
  if (reader_ferror(&scan_data->state.reader))
  {
    err = SFError_ReadFail;
  }
  else if (writer_fwrite(scan_data->copy_buf, 1, n, &scan_data->state.writer) != n)
  {
    err = SFError_WriteFail;
  }

  if (err == SFError_OK && reader_feof(&scan_data->state.reader))
  {
    scan_reader_destroy(scan_data);
    scan_data->state.phase = ScanStatus_CloseOutput;
  }

  return scan_error(err, scan_data);
}

/* ----------------------------------------------------------------------- */

static bool item_is_safe(struct UserData *item)
{
  NOT_USED(item);
  assert(item != NULL);
  return false; /* Always warn upon quitting with scans in progress. */
}

/* ----------------------------------------------------------------------- */

static void destroy_item(struct UserData *item)
{
  assert(item != NULL);
  scan_finished((ScanData *)item);
}

/* ----------------------------------------------------------------------- */

static SchedulerTime do_scan_idle(void *handle, SchedulerTime new_time,
  const volatile bool *time_up)
{
  ScanData *const scan_data = handle;
  const _kernel_oserror *e = NULL;

  assert(handle != NULL);
  assert(time_up != NULL);

  while (e == NULL && !*time_up && scan_data->state.phase != ScanStatus_Finished)
  {
    DEBUGF("Idle handler, phase %d\n", scan_data->state.phase);
#ifdef FORTIFY
    Fortify_CheckAllMemory();
#endif
    switch (scan_data->state.phase)
    {
      case ScanStatus_ExamineObject:
        e = examine_object(scan_data);
        break;

      case ScanStatus_OpenInput:
        e = open_input(scan_data);
        break;

      case ScanStatus_StartScanSprites:
        e = start_scan_sprites(scan_data);
        break;

      case ScanStatus_ScanSprites:
        e = scan_sprites(scan_data);
        break;

      case ScanStatus_PickConversion:
        e = pick_conversion(scan_data);
        break;

     case ScanStatus_DecideOutput:
        decide_output(scan_data);
        break;

      case ScanStatus_MakePath:
        e = make_path(stringbuffer_get_pointer(&scan_data->state.save_path),
                      scan_data->state.make_path_offset);
        if (e != NULL)
        {
          e = msgs_error_subn(e->errnum, "DirFail", 1, e->errmess);
        }
        else
        {
          scan_data->state.phase = ScanStatus_OpenOutput;
        }
        break;

      case ScanStatus_OpenOutput:
        e = open_output(scan_data);
        break;

      case ScanStatus_StartConvert:
        e = start_convert(scan_data);
        break;

      case ScanStatus_Convert:
        e = convert_data(scan_data);
        break;

      case ScanStatus_CloseInput:
        close_input(scan_data);
        break;

      case ScanStatus_CloseTmpOutput:
        e = start_copy_tmp(scan_data);
        break;

      case ScanStatus_CopyTmp:
        e = copy_tmp(scan_data);
        break;

      case ScanStatus_CloseOutput:
        e = close_output(scan_data);
        break;

      case ScanStatus_SetFileType:
        e = set_file_type(stringbuffer_get_pointer(&scan_data->state.save_path),
                          scan_data->state.output_type);
        if (e == NULL)
        {
          scan_data->state.phase = ScanStatus_NextObject;
        }
        break;

      case ScanStatus_NextObject:
        if (scan_data->state.iterator == NULL)
        {
          scan_data->state.phase = ScanStatus_Finished;
        }
        else
        {
          e = diriterator_advance(scan_data->state.iterator);
          if (e == NULL)
          {
            scan_data->state.phase = ScanStatus_ExamineObject;
          }
        }
        break;

      default:
        /* This function should be deregistered upon entering Error, Paused or
           Finished state so that it won't be called any more. */
        assert("Unexpected state" == NULL);
        break;
    }
  }

  if (e != NULL)
  {
    /* Copy the original error in case its buffer is recycled. */
    const _kernel_oserror error = *e;

    scan_reader_destroy(scan_data);
    (void)scan_writer_destroy(scan_data);
    scan_close_in(scan_data);
    (void)scan_close_out(scan_data);

    DEBUGF("Error: 0x%x %s\n", error.errnum, error.errmess);
    assert(scan_data->state.phase != ScanStatus_Error);
    assert(scan_data->state.phase != ScanStatus_Paused);

    /* turn progress window into error box */
    display_error(scan_data, error.errmess);
    scheduler_deregister(do_scan_idle, handle); /* cease null-polling */

    scan_data->state.phase = ScanStatus_Error;
  }

  if (scan_data->state.phase == ScanStatus_Finished)
  {
    scan_finished(scan_data);
  }

  return new_time;
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  ScanData * const scan_data = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (scan_data->state.phase == ScanStatus_Error)
  {
    /* Houston, we have a problem */
    switch (id_block->self_component)
    {
      case ComponentId_Abort_ActButton:
        scan_finished(scan_data);
        break;

      case ComponentId_Skip_ActButton:
        if (scan_data->state.iterator == NULL)
          break;

        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->state.phase = ScanStatus_NextObject;
        display_progress(scan_data);
        break;

      case ComponentId_Restart_ActButton:
        if (scan_data->state.iterator == NULL)
          break;

        if (E(diriterator_reset(scan_data->state.iterator)))
          break;

        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->state.num_checked = 0;
        scan_data->state.num_output = 0;
        scan_data->state.phase = ScanStatus_ExamineObject;
        display_progress(scan_data);
        display_nout(scan_data);
        display_nchecked(scan_data);
        break;

      case ComponentId_Fourth_ActButton: /* Retry */
        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->state.num_checked = scan_data->state.retry_num_checked;
        scan_data->state.num_output = scan_data->state.retry_num_output;
        scan_data->state.phase = ScanStatus_ExamineObject;
        display_progress(scan_data);
        display_nout(scan_data);
        display_nchecked(scan_data);
        break;

      default:
        return 0; /* event not handled */
    }
  }
  else
  {
    switch (id_block->self_component)
    {
      case ComponentId_Abort_ActButton:
        scan_finished(scan_data);
        break;

      case ComponentId_Fourth_ActButton: /* Pause/Continue */
        if (scan_data->state.phase == ScanStatus_Paused)
        {
          /* Continue the operation */
          if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
            break;

          display_pause(scan_data);

          ON_ERR_RPT(button_set_value(
                     0,
                     scan_data->state.window_id,
                     ComponentId_Activity_Button,
                     scan_data->state.return_action));

          scan_data->state.phase = scan_data->state.return_phase;
        }
        else
        {
          /* Pause the operation */
          ON_ERR_RPT(button_get_value(
                     0,
                     scan_data->state.window_id,
                     ComponentId_Activity_Button,
                     scan_data->state.return_action,
                     sizeof(scan_data->state.return_action),
                     NULL));

          display_continue(scan_data);

          ON_ERR_RPT(button_set_value(
                     0,
                     scan_data->state.window_id,
                     ComponentId_Activity_Button,
                     msgs_lookup("ScanTPaused")));

          /* cease null-polling */
          scheduler_deregister(do_scan_idle, handle);

          scan_data->state.return_phase = scan_data->state.phase;
          scan_data->state.phase = ScanStatus_Paused;
        }
        break;

      default:
        return 0; /* pass event on */
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void scan_set_title(ScanData const *const scan_data)
{
  /* Set the title of the scan progress window */
  assert(scan_data);

  const char *token;
  if (scan_data->state.extract_data && scan_data->state.extract_images)
  {
    token = "ScanSFSprTitle";
  }
  else if (scan_data->state.extract_data)
  {
    token = "ScanExDatTitle";
  }
  else
  {
    token = scan_data->state.extract_images ? "ScanExImgTitle" : "ScanSprSFTitle";
  }

  ON_ERR_RPT(window_set_title(0, scan_data->state.window_id, msgs_lookup(token)));
}

/* ----------------------------------------------------------------------- */

static bool scan_add_to_menu(ScanData const *const scan_data,
  char const *const load_root)
{
  assert(scan_data);
  assert(load_root);

  const char *token;
  if (scan_data->state.extract_data && scan_data->state.extract_images)
  {
    token = "ScanSFSprList";
  }
  else if (scan_data->state.extract_data)
  {
    token = "ScanExDatList";
  }
  else
  {
    token = scan_data->state.extract_images ? "ScanExImgList" : "ScanSprSFList";
  }

  return !E(ViewsMenu_add(scan_data->state.window_id,
    msgs_lookup_subn(token, 1, pathtail(load_root, PathElements)), "" /* obsolete */));
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Scan_create(char *const load_root, const char *const save_root,
  bool const extract_images, bool const extract_data)
{
  assert(load_root != NULL);
  assert(save_root != NULL);

  /* Allocate memory for batch operation */
  ScanData * const scan_data = malloc(sizeof(*scan_data));
  if (scan_data == NULL)
  {
    RPT_ERR("NoMem");
    return;
  }

  /* Initialise state */
  scan_data->state = (ScanDataState){
    .phase = ScanStatus_ExamineObject,
    .num_checked = 0,
    .num_output = 0,
    .input_type = 0,
    .extract_data = extract_data,
    .extract_images = extract_images,
    .output_type = 0,
    .return_action[0] = '\0',
    .real_save_path = "",
    .replace_input = !stricmp(load_root, save_root),
  };

  /* We want to create the root output directory and all of its descendants
     but not any of its ancestors.
   e.g. save_root = "RAM::0.$.Landscapes", last_sep = ".Landscapes", make_path_offset = 9
        makes "RAM::0.$.Landscapes" and any descendants */
  const char * const last_sep = strrchr(save_root, '.');
  scan_data->state.make_path_offset = (last_sep == NULL) ? 0 : last_sep - save_root + 1;

  stringbuffer_init(&scan_data->state.load_path);
  stringbuffer_init(&scan_data->state.save_path);

  if (!E(toolbox_create_object(0, "Scan", &scan_data->state.window_id)))
  {
    if (scan_add_to_menu(scan_data, load_root))
    {
      do
      {
        if (E(event_register_toolbox_handler(scan_data->state.window_id,
                ActionButton_Selected, actionbutton_selected, scan_data)))
        {
          break;
        }

        /* Third append is a deliberate no-op to reset the undo state for the
           save path string buffer. */
        if (!stringbuffer_append(&scan_data->state.save_path, save_root, SIZE_MAX) ||
            !stringbuffer_append(&scan_data->state.save_path, ".", SIZE_MAX) ||
            !stringbuffer_append(&scan_data->state.save_path, NULL, 0))
        {
          RPT_ERR("NoMem");
          break;
        }

        if (E(diriterator_make(&scan_data->state.iterator,
                                DirIterator_RecurseIntoDirectories,
                                load_root, NULL)))
        {
          break;
        }

        /* Set up the contents of the progress window */
        scan_set_title(scan_data);
        update_window(scan_data, "ScanTOpen", load_root);
        display_nchecked(scan_data);
        display_nout(scan_data);
        display_progress(scan_data);

        /* Show the window in the default position for the next new document */
        if (E(StackViews_open(scan_data->state.window_id, NULL_ObjectId, NULL_ComponentId)))
        {
          break;
        }

        /* Register to receive null polls */
        if (E(scheduler_register_delay(do_scan_idle, scan_data, 0, Priority)))
        {
          break;
        }

        userdata_add_to_list(&scan_data->list_node, item_is_safe, destroy_item, "");
        return;
      }
      while(0);
      (void)ViewsMenu_remove(scan_data->state.window_id);
    }
    (void)remove_event_handlers_delete(scan_data->state.window_id);
  }

  diriterator_destroy(scan_data->state.iterator);
  stringbuffer_destroy(&scan_data->state.load_path);
  stringbuffer_destroy(&scan_data->state.save_path);
  free(scan_data);
}
