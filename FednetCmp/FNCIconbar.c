/*
 *  FednetCmp - Fednet file compression/decompression
 *  Iconbar icon
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
#include <stdbool.h>
#include "stdio.h"
#include <assert.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "iconbar.h"
#include "wimp.h"
#include "wimplib.h"

/* My library files */
#include "FOpenCount.h"
#include "StrExtra.h"
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Loader3.h"
#include "Debug.h"
#include "FileUtils.h"
#include "DeIconise.h"
#include "ReaderRaw.h"

/* Local headers */
#include "Utils.h"
#include "SaveFile.h"
#include "SaveComp.h"
#include "SaveDir.h"
#include "FNCSaveBox.h"
#include "FNCIconbar.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

static ObjectId Iconbar_id = NULL_ObjectId;
static bool multi_saveboxes = false;
static _Optional FNCSaveBox *last_savebox = NULL;

enum
{
  WindowHandle_IconBar = -2, /* Pseudo window handle (icon bar) */
};

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void load_fail(_Optional CONST _kernel_oserror *const error,
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

static void savebox_deleted(FNCSaveBox *savebox)
{
  if (last_savebox == savebox)
    last_savebox = NULL;
}

/* ----------------------------------------------------------------------- */

static bool new_savebox(_Optional FNCSaveBox *const savebox)
{
  bool success = false;
  if (savebox != NULL)
  {
    /* If there is already a save box then remove it
       (unless we are allowing multiple save boxes) */
    if (!multi_saveboxes)
    {
      FNCSaveBox_destroy(last_savebox);
    }
    last_savebox = &*savebox;
    success = true;
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static bool read_file(Reader *const reader, int const estimated_size,
  int const file_type, char const *const filename, void *const client_handle)
{
  bool success = false;
  WimpGetPointerInfoBlock pointerinfo;

  if (!E(wimp_get_pointer_info(&pointerinfo)))
  {
    _Optional FNCSaveBox *savebox = NULL;
    bool const *const is_safe = client_handle;

    /* Create save dialogue box for a file */
    if (compressed_file_type(file_type))
    {
      savebox = SaveFile_create(filename, *is_safe,
                  reader, estimated_size, pointerinfo.x, savebox_deleted);
    }
    else
    {
      savebox = SaveComp_create(filename, *is_safe,
                  reader, estimated_size, pointerinfo.x, savebox_deleted);
    }
    success = new_savebox(savebox);
  }

  return success;
}

/* ----------------------------------------------------------------------- */

static int datasave_message(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataSave);
  NOT_USED(handle);
  DEBUG("Received a DataSave message (ref. %d in reply to %d)",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref)
    return 0; /* message is a reply (will be dealt with by Entity module) */

  DEBUG("Window handle is %d", message->data.data_save.destination_window);
  if (message->data.data_save.destination_window != WindowHandle_IconBar)
    return 0; /* destination is not the iconbar (do not claim message) */

  /* Reject directory or application (can't assume that a temporary directory
     will persist across task switches, as required by our scanning code). */
  DEBUG("File type is &%X", message->data.data_save.file_type);
  if (message->data.data_save.file_type == FileType_Directory ||
      message->data.data_save.file_type == FileType_Application)
  {
    RPT_ERR("AppDir");
  }
  else
  {
    /* The rest of the data transfer protocol is handled by CBLibrary */
    static bool is_safe = false;
    ON_ERR_RPT(loader3_receive_data(message, read_file, load_fail, &is_safe));
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int dataload_message(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  NOT_USED(handle);
  DEBUG("Received a DataLoad message (ref. %d in reply to %d)",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref)
    return 0; /* message is a reply (will be dealt with by Loader2 module) */

  DEBUG("Window handle is %d", message->data.data_load.destination_window);
  if (message->data.data_load.destination_window != WindowHandle_IconBar)
    return 0; /* destination is not the iconbar (do not claim message) */

  bool success = false;

  /* Canonicalise the file path to be loaded */
  _Optional char *canonical_path = NULL;
  if (!E(canonicalise(&canonical_path, NULL, NULL,
                      message->data.data_load.leaf_name)) &&
    canonical_path)
  {
    /* If there is already a save box for data loaded from this file path then
       just show that */
    const FNCSaveBox * const existing_dbox =
        (FNCSaveBox *)userdata_find_by_file_name(&*canonical_path);

    if (existing_dbox == NULL)
    {
      if (message->data.data_load.file_type == FileType_Directory ||
          message->data.data_load.file_type == FileType_Application)
      {
        WimpGetPointerInfoBlock pointerinfo;
        if (!E(wimp_get_pointer_info(&pointerinfo)))
        {
          success = new_savebox(
            SaveDir_create(&*canonical_path, pointerinfo.x, savebox_deleted));
        }
      }
      else
      {
        static bool is_safe = true;
        success = loader3_load_file(&*canonical_path,
                                    message->data.data_load.file_type,
                                    read_file, load_fail, &is_safe);
      }
    }
    else
    {
      /* There is already a save box for data loaded from this file path so
         just show that */
      FNCSaveBox_show(existing_dbox);
      success = true;
    }
    free(canonical_path);
  }

  if (success)
  {
    /* Acknowledge that the file was loaded successfully
       (just a courtesy message, we don't expect a reply) */
    message->hdr.your_ref = message->hdr.my_ref;
    message->hdr.action_code = Wimp_MDataLoadAck;
    if (!E(wimp_send_message(Wimp_EUserMessage, message, message->hdr.sender,
            0, NULL)))
    {
      DEBUG("Sent DataLoadAck message (ref. %d)", message->hdr.my_ref);
    }
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Iconbar_initialise(ObjectId id)
{
  Iconbar_id = id;

  /* Register Wimp message handlers to load files dropped on iconbar icon */
  EF(event_register_message_handler(Wimp_MDataSave, datasave_message, (void *)NULL));
  EF(event_register_message_handler(Wimp_MDataLoad, dataload_message, (void *)NULL));
}

/* ----------------------------------------------------------------------- */

bool Iconbar_get_multi_dboxes(void)
{
  return multi_saveboxes;
}

/* ----------------------------------------------------------------------- */

void Iconbar_set_multi_dboxes(bool multi)
{
  multi_saveboxes = multi;
}
