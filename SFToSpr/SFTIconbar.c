/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Iconbar icon
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
#include <assert.h>
#include <stdbool.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "wimp.h"
#include "wimplib.h"
#include "window.h"
#include "saveas.h"
#include "event.h"
#include "iconbar.h"
#include "flex.h"

/* My library files */
#include "err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Loader3.h"
#include "SprFormats.h"
#include "SFformats.h"
#include "Debug.h"
#include "FileUtils.h"
#include "StrExtra.h"
#include "ReaderFlex.h"

/* Local headers */
#include "Utils.h"
#include "SaveSprites.h"
#include "SavePlanets.h"
#include "SaveMapTiles.h"
#include "SaveSky.h"
#include "SaveDir.h"
#include "SFTIconbar.h"
#include "SFgfxconv.h"

/* Constant numeric values */
enum
{
  WindowHandle_IconBar = -2, /* Pseudo window handle (icon bar) */
};

static ObjectId Iconbar_id = NULL_ObjectId;
static bool multi_saveboxes = false;
static SFTSaveBox *last_savebox = NULL;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void load_fail(CONST _kernel_oserror *const error,
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

static void savebox_deleted(SFTSaveBox *savebox)
{
  if (last_savebox == savebox)
    last_savebox = NULL;
}

/* ----------------------------------------------------------------------- */

static bool new_savebox(SFTSaveBox *const savebox)
{
  bool success = false;
  if (savebox != NULL)
  {
    /* If there is already a save box then remove it
       (unless we are allowing multiple save boxes) */
    if (!multi_saveboxes)
    {
      SFTSaveBox_destroy(last_savebox);
    }
    last_savebox = savebox;
    success = true;
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static SFTSaveBox *convert_sprites(char const *const file_path, int const x,
  bool const data_saved, flex_ptr buffer)
{
  DEBUGF("Creating savebox for compressed graphics, input size is %d\n",
    flex_size(buffer));

  ScanSpritesContext *const context = malloc(sizeof(*context));
  if (!context)
  {
    RPT_ERR("NoMem");
    return false;
  }

  Reader breader;
  reader_flex_init(&breader, buffer);
  SFError const err = scan_sprite_file(&breader, context);
  reader_destroy(&breader);

  SFTSaveBox *newbox = NULL;
  if (!handle_error(err, "RAM", ""))
  {
    /* Try to guess whether to convert sprites to planets or tiles */
    int const ntypes = count_spr_types(context);
    if (ntypes == 0)
    {
      RPT_ERR("AutoNoMatch");
    }
    else if (ntypes > 1)
    {
      RPT_ERR("AutoDouble");
    }
    else
    {
      if (!context->bad_sprite ||
          dialogue_confirm(
            msgs_lookup_subn("BadSpriteCont", 1, context->bad_name)))
      {
        if (context->planets.count > 0)
        {
          /* Convert sprites to planet images */
          if (context->planets.fixed_hdr)
          {
            WARN("ForceOff");
          }
          newbox = SavePlanets_create(file_path, x, data_saved,
                     buffer, &context->planets, savebox_deleted);
        }
        else if (context->tiles.count > 0)
        {
          /* Convert sprites to map tiles */
          if (context->tiles.fixed_hdr)
          {
            WARN("ForceAnim");
          }
          newbox = SaveMapTiles_create(file_path, x, data_saved,
                     buffer, &context->tiles, savebox_deleted);
        }
        else if (context->sky.count > 0)
        {
          /* Convert sprites to sky definition */
          if (context->sky.fixed_stars || context->sky.fixed_render)
          {
            WARN("ForceSky");
          }
          newbox = SaveSky_create(file_path, x, data_saved,
                     buffer, &context->sky, savebox_deleted);
        }
      }
    }
  }
  free(context);

  return newbox;
}

/* ----------------------------------------------------------------------- */

static bool read_file(Reader *const reader, int const estimated_size,
  int const file_type, char const *const filename, void *const client_handle)
{
  bool const is_safe = client_handle != NULL;

  /* We always need to buffer the input data: sprite files require two
     passes and the user may want to tweak conversion parameters. */
  void *data = NULL;
  flex_ptr buffer = &data;
  if (!copy_to_buf(buffer, reader, estimated_size, filename))
  {
    return false;
  }

  bool success = false;
  WimpGetPointerInfoBlock pointerinfo;
  if (!E(wimp_get_pointer_info(&pointerinfo)))
  {

    /* Create save dialogue box for a file */
    SFTSaveBox *savebox = NULL;
    switch(file_type)
    {
      case FileType_Sprite:
        savebox = convert_sprites(filename, pointerinfo.x, is_safe, buffer);
        break;

      case FileType_SFSkyPic:
      case FileType_SFMapGfx:
      case FileType_SFSkyCol:
        savebox = SaveSprites_create(
                     filename, pointerinfo.x, is_safe,
                     buffer, file_type, savebox_deleted);
        break;

      default:
        assert("Unrecognised file type" == NULL);
        break;
    }

    success = new_savebox(savebox);
  }

  /* If it hasn't been reanchored then we no longer require the input data */
  if (*buffer)
  {
    flex_free(buffer);
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
  else if (message->data.data_save.file_type != FileType_Sprite &&
           message->data.data_save.file_type != FileType_SFSkyPic &&
           message->data.data_save.file_type != FileType_SFMapGfx &&
           message->data.data_save.file_type != FileType_SFSkyCol)
  {
    /* not a file type that we understand */
    RPT_ERR("BadFileType");
  }
  else
  {
    /* The rest of the data transfer protocol is handled by CBLibrary */
    ON_ERR_RPT(loader3_receive_data(message, read_file, load_fail, NULL));
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
    return 0; /* message is a reply (will be dealt with by Loader3 module) */

  DEBUG("Window handle is %d", message->data.data_load.destination_window);
  if (message->data.data_load.destination_window != WindowHandle_IconBar)
    return 0; /* destination is not the iconbar (do not claim message) */

  bool success = false;

  DEBUG("File type is &%X", message->data.data_load.file_type);
  if (message->data.data_load.file_type == FileType_Sprite ||
      message->data.data_load.file_type == FileType_SFSkyPic ||
      message->data.data_load.file_type == FileType_SFMapGfx ||
      message->data.data_load.file_type == FileType_SFSkyCol ||
      message->data.data_load.file_type == FileType_Directory ||
      message->data.data_load.file_type == FileType_Application)
  {
    /* Canonicalise the file path to be loaded */
    char *canonical_path = NULL;
    if (!E(canonicalise(&canonical_path, NULL, NULL,
                        message->data.data_load.leaf_name)))
    {
      /* If there is already a save box for data loaded from this file path then
         just show that */
      const SFTSaveBox * const existing_dbox =
          (SFTSaveBox *)userdata_find_by_file_name(canonical_path);

      if (existing_dbox == NULL)
      {
        if (message->data.data_load.file_type == FileType_Directory ||
            message->data.data_load.file_type == FileType_Application)
        {
          WimpGetPointerInfoBlock pointerinfo;
          if (!E(wimp_get_pointer_info(&pointerinfo)))
          {
            success = new_savebox(
              SaveDir_create(canonical_path, pointerinfo.x, savebox_deleted));
          }
        }
        else
        {
          bool is_safe = true;
          success = loader3_load_file(canonical_path,
                                      message->data.data_load.file_type,
                                      read_file, load_failed, &is_safe);
        }
      }
      else
      {
        /* There is already a save box for data loaded from this file path so
           just show that */
        SFTSaveBox_show(existing_dbox);
        success = true;
      }
      free(canonical_path);
    }
  }
  else
  {
    /* Not a file type that we understand */
    RPT_ERR("BadFileType");
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
  EF(event_register_message_handler(Wimp_MDataSave, datasave_message, NULL));
  EF(event_register_message_handler(Wimp_MDataLoad, dataload_message, NULL));
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
