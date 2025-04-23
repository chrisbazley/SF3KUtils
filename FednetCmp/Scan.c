/*
 *  FednetCmp - Fednet file compression/decompression
 *  Directory scan
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
#include "Err.h"
#include "msgtrans.h"
#include "FedCompMT.h"
#include "ViewsMenu.h"
#include "Macros.h"
#include "Scheduler.h"
#include "LoadSaveMT.h"
#include "AbortFOp.h"
#include "PathTail.h"
#include "Debug.h"
#include "StrExtra.h"
#include "DeIconise.h"
#include "GadgetUtil.h"
#include "ScreenSize.h"
#include "FileUtils.h"
#include "WimpExtra.h"
#include "DirIter.h"
#include "StackViews.h"
#include "FOpenCount.h"
#include "OSFile.h"
#include "StringBuff.h"
#include "UserData.h"
#include "EventExtra.h"

/* Local headers */
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
  ComponentId_Converted_Button  = 0x0b,
  ComponentId_Activity_Button   = 0x0c,
  ComponentId_FilePath_Button   = 0x0d,
  ComponentId_Progress_Slider   = 0x0e
};

typedef enum
{
  ScanStatus_Error,
  ScanStatus_Paused,
  ScanStatus_ExamineObject,
  ScanStatus_Load,
  ScanStatus_MakePath,
  ScanStatus_Save,
  ScanStatus_SetFileType,
  ScanStatus_NextObject,
  ScanStatus_Finished
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
  MaxActionLen      = 15
};

typedef struct
{
  UserData list_node;
  ObjectId window_id; /* dialogue window */
  DirIterator *iterator; /* NULL if processing a single file */
  ScanStatus phase; /* what is going on */
  unsigned int num_checked;
  unsigned int num_output;

  void *buffer; /* flex block */

  bool compress; /* action */
  int comp_type; /* file type to give compressed output */

  ScanStatus return_phase; /* for pause, error */
  /* preserved data for retry */
  unsigned int retry_num_checked;
  unsigned int retry_num_output;
  FILE **file_op;
  char return_action[MaxActionLen + 1];
  StringBuffer load_path, save_path;
  size_t make_path_offset; /* avoids creating directories that should already exist */
} ScanData;

static SchedulerIdleFunction do_scan_idle;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void display_error(const ScanData *const scan_data,
  char const *error_message)
{
  /* Opens progress window to centre of screen at expanded size
     and reconfigures buttons */
  assert(scan_data != NULL);
  assert(error_message != NULL);

  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->window_id,
                   ComponentId_Skip_ActButton,
                   false));

  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->window_id,
                   ComponentId_Restart_ActButton,
                   false));

  /* Can't 'Skip' or 'Restart' if doing single file (nowhere to skip to) */
  ON_ERR_RPT(set_gadget_faded(
                   scan_data->window_id,
                   ComponentId_Skip_ActButton,
                   scan_data->iterator == NULL));

  ON_ERR_RPT(set_gadget_faded(
                   scan_data->window_id,
                   ComponentId_Restart_ActButton,
                   scan_data->iterator == NULL));

  ON_ERR_RPT(actionbutton_set_text(
                   0,
                   scan_data->window_id,
                   ComponentId_Fourth_ActButton,
                   msgs_lookup("ScanBRetry")));

  ON_ERR_RPT(gadget_set_help_message(
                   0,
                   scan_data->window_id,
                   ComponentId_Fourth_ActButton,
                   msgs_lookup("ScanHRetry")));

  ON_ERR_RPT(button_set_value(
                   0,
                   scan_data->window_id,
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
                                   scan_data->window_id,
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
             scan_data->window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanBPause")));

  ON_ERR_RPT(gadget_set_help_message(
             0,
             scan_data->window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanHPause")));
}

/* ----------------------------------------------------------------------- */

static void display_continue(const ScanData *const scan_data)
{
  assert(scan_data != NULL);

  ON_ERR_RPT(actionbutton_set_text(
             0,
             scan_data->window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanBCont")));

  ON_ERR_RPT(gadget_set_help_message(
             0,
             scan_data->window_id,
             ComponentId_Fourth_ActButton,
             msgs_lookup("ScanHCont")));
}

/* ----------------------------------------------------------------------- */

static void display_progress(const ScanData *const scan_data)
{
  assert(scan_data != NULL);

  /* Reduces progress window to normal size, restores normal buttons */
  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->window_id,
                   ComponentId_Skip_ActButton,
                   true));

  ON_ERR_RPT(set_gadget_hidden(
                   scan_data->window_id,
                   ComponentId_Restart_ActButton,
                   true));

  if (scan_data->phase == ScanStatus_Paused)
  {
    display_continue(scan_data);
  }
  else
  {
    display_pause(scan_data);
  }

  /* Alter visible area but not position */
  WimpGetWindowInfoBlock windowinfo;
  if (E(window_get_wimp_handle(0, scan_data->window_id, &windowinfo.window_handle)))
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
                                   scan_data->window_id,
                                   Toolbox_ShowObject_FullSpec,
                                   &wsob,
                                   NULL_ObjectId,
                                   NULL_ComponentId));
}

/* ----------------------------------------------------------------------- */

static void update_window(const ScanData *const scan_data,
  char const *const action, char *const file_path)
{
  /* Only update action text if not the same */
  assert(scan_data != NULL);
  assert(action != NULL);
  assert(file_path != NULL);

  char current_action[MaxActionLen + 1];
  ON_ERR_RPT(button_get_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Activity_Button,
                   current_action,
                   sizeof(current_action),
                   NULL));

  char * const new_action = msgs_lookup(action);
  if (strcmp(current_action, new_action) != 0)
  {
    ON_ERR_RPT(button_set_value(
                     0,
                     scan_data->window_id,
                     ComponentId_Activity_Button,
                     new_action));
  }

  /* Whereas file paths are v. unlikely to be the same (impossible?) */
  ON_ERR_RPT(button_set_value(
             0,
             scan_data->window_id,
             ComponentId_FilePath_Button,
             file_path));
}

/* ----------------------------------------------------------------------- */

static void display_nout(const ScanData *const scan_data)
{
  char num[MaxDecimalLen + 1];

  assert(scan_data != NULL);
  sprintf(num, "%u", scan_data->num_output);

  ON_ERR_RPT(button_set_value(
             0,
             scan_data->window_id,
             ComponentId_Converted_Button,
             num));
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *append_to_string_buffer(
                               StringBuffer *const sb,
                               DirIterator *const it,
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
  scan_data->retry_num_output = scan_data->num_output;
  scan_data->retry_num_checked = scan_data->num_checked;

  if (diriterator_is_empty(scan_data->iterator))
  {
    scan_data->phase = ScanStatus_Finished;
    return NULL;
  }

  stringbuffer_truncate(&scan_data->load_path, 0);

  const _kernel_oserror *e = append_to_string_buffer(&scan_data->load_path,
                              scan_data->iterator,
                              diriterator_get_object_path_name);

  ScanStatus new_phase = ScanStatus_NextObject;
  if (e == NULL)
  {
    DirIteratorObjectInfo info;
    bool skip = true;
    switch (diriterator_get_object_info(scan_data->iterator, &info))
    {
      case ObjectType_File:
      case ObjectType_Image: /* (image files are treated as normal files) */
      {
        bool is_comp = compressed_file_type(info.file_type);

        /* check whether we should load file */
        if ((is_comp && !scan_data->compress) ||
            (!is_comp && scan_data->compress))
        {
          new_phase = ScanStatus_Load;
          skip = false;

          /* Remove the previous sub-path (does nothing if already undone) */
          stringbuffer_undo(&scan_data->save_path);
          e = append_to_string_buffer(&scan_data->save_path,
                                      scan_data->iterator,
                                      diriterator_get_object_sub_path_name);
        }

        if (!e)
        {
          scan_data->num_checked ++;
        }
        break;
      }

      case ObjectType_Directory: /* Object is directory - go down a level */
      {
        update_window(scan_data, "ScanTOpen",
                      stringbuffer_get_pointer(&scan_data->load_path));
        skip = false;
        break;
      }
    } /* what object type */

    if (skip)
    {
      /* File of no interest */
      update_window(scan_data, "ScanTIgnore",
                    stringbuffer_get_pointer(&scan_data->load_path));
    }
  }

  if (e == NULL)
    scan_data->phase = new_phase;

  return e;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *scan_load_file(ScanData *const scan_data,
  volatile const bool *const time_up)
{
  assert(scan_data != NULL);
  assert(time_up != NULL);

  char * const path = stringbuffer_get_pointer(&scan_data->load_path);

  if (scan_data->file_op == NULL)
  {
    update_window(scan_data, "ScanTLoad", path);

    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   0));

    ON_ERR_RPT(slider_set_colour(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   WimpColour_LightGreen,
                   0));
  }

  const _kernel_oserror *e = NULL;
  if (scan_data->compress)
    e = load_fileM2(path, &scan_data->buffer, time_up, &scan_data->file_op);
  else
    e = load_compressedM(path, &scan_data->buffer, time_up, &scan_data->file_op);

  if (e != NULL)
  {
    if (scan_data->buffer != NULL)
      flex_free(&scan_data->buffer);

    return msgs_error_subn(e->errnum, "LoadFail", 1, e->errmess);
  }

  if (scan_data->file_op == NULL)
  {
    /* Have finished decompression */
    DEBUG("Have finished loading");
    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   100));
    scan_data->phase = (scan_data->iterator == NULL) ?
                       ScanStatus_Save : ScanStatus_MakePath;
  }
  else
  {
    /* We will have to come back another time */
    unsigned int perc;

    DEBUG("Loading incomplete");
    if (scan_data->compress)
      perc = get_loadsave_perc(&scan_data->file_op);
    else
      perc = get_decomp_perc(&scan_data->file_op);

    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   perc));
  }

  return NULL;
}

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *scan_save_file(ScanData *const scan_data, volatile const bool *const time_up)
{
  assert(scan_data != NULL);
  assert(time_up != NULL);

  char * const path = stringbuffer_get_pointer(&scan_data->save_path);

  if (scan_data->file_op == NULL)
  {
    update_window(scan_data, "ScanTSave", path);

    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   0));

    ON_ERR_RPT(slider_set_colour(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   WimpColour_Red,
                   0));
  }

  /* Time to save */
  const _kernel_oserror *e = NULL;
  if (scan_data->compress)
  {
    e = save_compressedM2(path, &scan_data->buffer, time_up, 0,
                          flex_size(&scan_data->buffer), &scan_data->file_op);
  }
  else
  {
    e = save_fileM2(path, &scan_data->buffer, time_up, 0,
                    flex_size(&scan_data->buffer), &scan_data->file_op);
  }

  if (e != NULL)
  {
    return msgs_error_subn(e->errnum, "SaveFail", 1, e->errmess);
  }

  if (scan_data->file_op == NULL)
  {
    /* Have finished saving data */
    DEBUG("Have finished saving data");

    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   100));

    /* Update count of files output */
    scan_data->num_output++;
    display_nout(scan_data);

    flex_free(&scan_data->buffer);
    scan_data->phase = ScanStatus_SetFileType;
  }
  else
  {
    /* We will have to come back another time */
    unsigned int perc;

    DEBUG("Saving incomplete");
    if (scan_data->compress)
      perc = get_comp_perc(&scan_data->file_op);
    else
      perc = get_loadsave_perc(&scan_data->file_op);

    ON_ERR_RPT(slider_set_value(
                   0,
                   scan_data->window_id,
                   ComponentId_Progress_Slider,
                   perc));
  }

  return NULL;
}

/* ----------------------------------------------------------------------- */

static void scan_finished(ScanData *const scan_data)
{
  if (scan_data != NULL)
  {
    ObjectId const window_id = scan_data->window_id;

    DEBUGF("Destroying scan %p (object 0x%x)\n", (void *)scan_data, window_id);

    userdata_remove_from_list(&scan_data->list_node);

    /* if we were null polling then stop */
    if (scan_data->phase != ScanStatus_Error &&
        scan_data->phase != ScanStatus_Paused)
    {
      scheduler_deregister(do_scan_idle, scan_data);
    }

    /* Destroy main Window object */
    ON_ERR_RPT(remove_event_handlers_delete(window_id));
    ON_ERR_RPT(ViewsMenu_remove(window_id));

    diriterator_destroy(scan_data->iterator);

    /* free state and close down any running operation */
    if (scan_data->file_op != NULL)
      abort_file_op(&scan_data->file_op);

    if (scan_data->buffer != NULL)
      flex_free(&scan_data->buffer);

    stringbuffer_destroy(&scan_data->load_path);
    stringbuffer_destroy(&scan_data->save_path);
    free(scan_data);
  }
}

/* ----------------------------------------------------------------------- */

static SchedulerTime do_scan_idle(void *const handle, SchedulerTime new_time,
  volatile const bool *const time_up)
{
  ScanData *const scan_data = handle;
  const _kernel_oserror *e = NULL;

  assert(handle != NULL);
  assert(time_up != NULL);

  while (e == NULL && !*time_up && scan_data->phase != ScanStatus_Finished)
  {
    DEBUGF("Idle handler, phase %d\n", scan_data->phase);
    switch (scan_data->phase)
    {
      case ScanStatus_ExamineObject:
        e = examine_object(scan_data);
        break;

      case ScanStatus_Load:
        e = scan_load_file(scan_data, time_up);
        break;

      case ScanStatus_MakePath:
        e = make_path(stringbuffer_get_pointer(&scan_data->save_path),
                      scan_data->make_path_offset);
        if (e != NULL)
        {
          e = msgs_error_subn(e->errnum, "DirFail", 1, e->errmess);
        }
        else
        {
          scan_data->phase = ScanStatus_Save;
        }
        break;

      case ScanStatus_Save:
        e = scan_save_file(scan_data, time_up);
        break;

      case ScanStatus_SetFileType:
        e = set_file_type(stringbuffer_get_pointer(&scan_data->save_path),
                          scan_data->compress ? scan_data->comp_type : FileType_Data);
        if (e == NULL)
        {
          scan_data->phase = ScanStatus_NextObject;
        }
        break;

      case ScanStatus_NextObject:
        if (scan_data->iterator == NULL)
        {
          scan_data->phase = ScanStatus_Finished;
        }
        else
        {
          e = diriterator_advance(scan_data->iterator);
          if (e == NULL)
          {
            scan_data->phase = ScanStatus_ExamineObject;
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

    DEBUGF("Error: 0x%x %s\n", error.errnum, error.errmess);
    assert(scan_data->phase != ScanStatus_Error);
    assert(scan_data->phase != ScanStatus_Paused);
    assert(scan_data->file_op == NULL);

    /* turn progress window into error box */
    display_error(scan_data, error.errmess);
    scheduler_deregister(do_scan_idle, handle); /* cease null-polling */

    scan_data->phase = ScanStatus_Error;
  }

  if (scan_data->phase == ScanStatus_Finished)
    scan_finished(scan_data);

  return new_time;
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  ScanData *const scan_data = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (scan_data->phase == ScanStatus_Error)
  {
    /* Houston, we have a problem */
    switch (id_block->self_component)
    {
      case ComponentId_Abort_ActButton:
        scan_finished(scan_data);
        break;

      case ComponentId_Skip_ActButton:
        if (scan_data->iterator == NULL)
          break;

        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->phase = ScanStatus_NextObject;
        display_progress(scan_data);
        break;

      case ComponentId_Restart_ActButton:
        if (scan_data->iterator == NULL)
          break;

        if (E(diriterator_reset(scan_data->iterator)))
          break;

        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->num_checked = 0;
        scan_data->num_output = 0;
        scan_data->phase = ScanStatus_ExamineObject;
        display_nout(scan_data);
        display_progress(scan_data);
        break;

      case ComponentId_Fourth_ActButton:/* Retry */
        if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
          break;

        scan_data->num_checked = scan_data->retry_num_checked;
        scan_data->num_output = scan_data->retry_num_output;
        scan_data->phase = ScanStatus_ExamineObject;
        display_progress(scan_data);
        display_nout(scan_data);
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
        if (scan_data->phase == ScanStatus_Paused)
        {
          /* Continue the operation */
          if (E(scheduler_register_delay(do_scan_idle, handle, 0, Priority)))
            break;

          display_pause(scan_data);

          ON_ERR_RPT(button_set_value(
                     0,
                     scan_data->window_id,
                     ComponentId_Activity_Button,
                     scan_data->return_action));

          scan_data->phase = scan_data->return_phase;
        }
        else
        {
          /* Pause the operation */
          ON_ERR_RPT(button_get_value(
                     0,
                     scan_data->window_id,
                     ComponentId_Activity_Button,
                     scan_data->return_action,
                     sizeof(scan_data->return_action),
                     NULL));

          display_continue(scan_data);

          ON_ERR_RPT(button_set_value(
                     0,
                     scan_data->window_id,
                     ComponentId_Activity_Button,
                     msgs_lookup("ScanTPaused")));

          /* cease null-polling */
          scheduler_deregister(do_scan_idle, handle);

          scan_data->return_phase = scan_data->phase;
          scan_data->phase = ScanStatus_Paused;
        }
        break;

      default:
        return 0; /* pass event on */
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static bool item_is_safe(struct UserData *const item)
{
  NOT_USED(item);
  assert(item != NULL);
  return false; /* Always warn upon quitting with scans in progress. */
}

/* ----------------------------------------------------------------------- */

static void destroy_item(struct UserData *const item)
{
  assert(item != NULL);
  scan_finished((ScanData *)item);
}

/* ----------------------------------------------------------------------- */

static bool examine_root(ScanData * const scan_data, char const *const load_root)
{
  assert(scan_data);
  assert(load_root);

  /* Are we doing a batch scan or a single file? */
  OS_File_CatalogueInfo cat;
  if (E(os_file_read_cat_no_path(load_root, &cat)))
  {
    return false;
  }

  switch (cat.object_type)
  {
    case ObjectType_NotFound:
      /* Report an error along the lines of "File 'wibble' not found" */
      err_check_rep(os_file_generate_error(load_root,
                    OS_File_GenerateError_FileNotFound));
      return false;

    case ObjectType_File:
      if (!stringbuffer_append(&scan_data->load_path, load_root, SIZE_MAX))
      {
        RPT_ERR("NoMem");
        return false;
      }
      scan_data->phase = ScanStatus_Load;
      break;

    default: /* assume object is accessible like a directory */
      /* Second append is a deliberate no-op to reset the undo state for the
         save path string buffer. */
      if (!stringbuffer_append(&scan_data->save_path, ".", SIZE_MAX) ||
          !stringbuffer_append(&scan_data->save_path, NULL, 0))
      {
        RPT_ERR("NoMem");
        return false;
      }
      if (E(diriterator_make(&scan_data->iterator,
                             DirIterator_RecurseIntoDirectories,
                             load_root, NULL)))
      {
        return false;
      }
      scan_data->phase = ScanStatus_ExamineObject;
      break;
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static void scan_set_title(ScanData const *const scan_data)
{
  assert(scan_data);

  const char *const token = scan_data->compress ? "ScanCompTitle" : "ScanDeCompTitle";
  ON_ERR_RPT(window_set_title(0, scan_data->window_id, msgs_lookup(token)));
}

/* ----------------------------------------------------------------------- */

static bool scan_add_to_menu(ScanData const *const scan_data,
  char const *const load_root)
{
  assert(scan_data);
  assert(load_root);

  const char *const token = scan_data->compress ? "ScanCompList" : "ScanDeCompList";
  return !E(ViewsMenu_add(scan_data->window_id,
    msgs_lookup_subn(token, 1, pathtail(load_root, PathElements)), "" /* obsolete */));
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Scan_create(char *const load_root, char const *const save_root,
  bool const compress, int const comp_type)
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
  *scan_data = (ScanData){
    .phase = ScanStatus_Error,
    .num_checked = 0,
    .num_output = 0,
    .buffer = NULL,
    .compress = compress,
    .comp_type = comp_type,
    .iterator = NULL,
    .file_op = NULL,
    .return_action[0] = '\0',
  };

  /* We want to create the root output directory and all of its descendants
     but not any of its ancestors.
   e.g. save_root = "RAM::0.$.Landscapes", last_sep = ".Landscapes", make_path_offset = 9
        makes "RAM::0.$.Landscapes" and any descendants */
  char const * const last_sep = strrchr(save_root, '.');
  scan_data->make_path_offset = (last_sep == NULL) ? 0 : last_sep - save_root + 1;

  stringbuffer_init(&scan_data->load_path);
  stringbuffer_init(&scan_data->save_path);

  if (!E(toolbox_create_object(0, "Scan", &scan_data->window_id)))
  {
    if (scan_add_to_menu(scan_data, load_root))
    {
      do
      {
        if (E(event_register_toolbox_handler(scan_data->window_id,
                ActionButton_Selected, actionbutton_selected, scan_data)))
        {
          break;
        }

        if (!stringbuffer_append(&scan_data->save_path, save_root, SIZE_MAX))
        {
          RPT_ERR("NoMem");
          break;
        }

        if (!examine_root(scan_data, load_root))
        {
          break;
        }

        /* Set up the contents of the progress window */
        scan_set_title(scan_data);
        update_window(scan_data,
                      scan_data->phase == ScanStatus_Load ?
                           "ScanTLoad" : "ScanTOpen",
                      load_root);
        display_nout(scan_data);
        display_progress(scan_data);

        /* Show the window in the default position for the next new document */
        if (E(StackViews_open(scan_data->window_id, NULL_ObjectId, NULL_ComponentId)))
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
      (void)ViewsMenu_remove(scan_data->window_id);
    }
    (void)remove_event_handlers_delete(scan_data->window_id);
  }

  diriterator_destroy(scan_data->iterator);
  stringbuffer_destroy(&scan_data->load_path);
  stringbuffer_destroy(&scan_data->save_path);
  free(scan_data);
}
