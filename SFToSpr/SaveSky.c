/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for SFSkyCol file
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

/* ANSI library files */
#include <stddef.h>
#include "stdlib.h"
#include <stdbool.h>
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "window.h"
#include "gadgets.h"
#include "wimp.h"
#include "wimplib.h"
#include "flex.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "FilePerc.h"
#include "PathTail.h"
#include "EventExtra.h"
#include "Loader3.h"
#include "Debug.h"
#include "WriterGKey.h"
#include "ReaderFlex.h"

/* Local headers */
#include "SFFormats.h"
#include "SFgfxconv.h"
#include "SaveSky.h"
#include "SFTSaveBox.h"
#include "Utils.h"

/* Window component IDs */
enum
{
  ComponentId_RenderOffset_NumRange = 0x00,
  ComponentId_StarsHeight_NumRange  = 0x01
};

enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
};

typedef struct
{
  SFTSaveBox super;
  int        wimp_handle;
  void      *sky_data; /* flex block */
  void      *sprites; /* flex block */
  SkySpritesContext context;
  SFTSaveBoxDeletedFn *deleted_cb;
} SaveSky;

/* -----------------------------------------------------------------------
 *                          Private functions
 */

static bool read_heights(ObjectId win, SkyHeader *const sky_data)
{
  int height;

  assert(win != NULL_ObjectId);
  assert(sky_data != NULL);
  DEBUG("Reading values from Window &%x into header anchored at %p",
        win, (void *)sky_data);

  if (E(numberrange_get_value(0, win, ComponentId_RenderOffset_NumRange, &height)))
    return false;

  sky_data->render_offset = height;

  if (E(numberrange_get_value(0, win, ComponentId_StarsHeight_NumRange, &height)))
    return false;

  sky_data->min_stars_height = height;
  return true;
}

/* ----------------------------------------------------------------------- */

static bool write_heights(ObjectId win, SkyHeader const *const sky_data)
{
  assert(win != NULL_ObjectId);
  assert(sky_data != NULL);
  DEBUG("Setting values in Window &%x from header anchored at %p",
        win, (void *)sky_data);

  if (E(numberrange_set_value(0, win, ComponentId_RenderOffset_NumRange,
                              sky_data->render_offset)))
    return false;

  if (E(numberrange_set_value(0, win, ComponentId_StarsHeight_NumRange,
                              sky_data->min_stars_height)))
    return false;

  return true;
}

/* ----------------------------------------------------------------------- */

static bool csv_loaded(Reader *const reader, int const estimated_size,
  int const file_type, char const *const filename, void *const client_handle)
{
  NOT_USED(filename);
  NOT_USED(estimated_size);
  NOT_USED(file_type);
  assert(file_type == FileType_CSV);

  SaveSky * const savefile_data = client_handle;
  bool success = false;

  /* Allocate memory for the just the header of a sky file */
  SkyHeader hdr = {0};
  if (read_heights(savefile_data->super.window_id, &hdr))
  {
    if (!handle_error(csv_to_sky(reader, &hdr), filename, ""))
    {
      success = write_heights(savefile_data->super.window_id, &hdr);
    }
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static bool write_sky(Writer *const writer, void *const handle,
  char const *const filename)
{
  SaveSky * const savefile_data = handle;
  assert(handle != NULL);

  /* Read displayed paint offsets into header
  N.B. This has the side effect of confirming the displayed offsets for
       use if the dbox is reset (e.g. ADJUST-click 'Cancel') */
  if (!read_heights(savefile_data->super.window_id, &savefile_data->context.hdr))
  {
    return false;
  }

  Writer gkwriter;
  bool success = writer_gkey_init_from(&gkwriter, FednetHistoryLog2,
                    sky_size(), writer);
  if (!success)
  {
    RPT_ERR("NoMem");
    return false;
  }

  Reader reader;
  reader_flex_init(&reader, &savefile_data->sprites);

  SFError err = sprites_to_sky(&reader, &gkwriter, &savefile_data->context);
  long int const out_bytes = writer_destroy(&gkwriter);
  if (out_bytes < 0 && err == SFError_OK)
  {
    err = SFError_WriteFail;
  }

  reader_destroy(&reader);
  return !handle_error(err, "RAM", filename);
}

/*
 * Wimp message handlers
 */

static int datasave_message(WimpMessage *const message, void *const handle)
{
  SaveSky * const savefile_data = handle;

  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataSave);
  assert(handle != NULL);

  DEBUG("Received a message of type %d (ref. %d in reply to %d)",
        message->hdr.action_code, message->hdr.my_ref, message->hdr.your_ref);

  DEBUG("Window handle is %d", message->data.data_save.destination_window);
  DEBUG("File type is &%X", message->data.data_save.file_type);

  /* Don't claim messages that are replies (dealt with by Entity module) or
     for windows other than the save box associated with the client handle. */
  if (message->hdr.your_ref != 0 ||
      message->data.data_load.destination_window != savefile_data->wimp_handle)
  {
    return 0; /* pass message on */
  }

  if (message->data.data_save.file_type == FileType_CSV)
  {
    ON_ERR_RPT(loader3_receive_data(message, csv_loaded, load_failed, handle));
  }
  else
  {
    RPT_ERR("NotCSV");
  }

  return 1; /* claim message */
}

static int dataload_message(WimpMessage *const message, void *const handle)
{
  SaveSky * const savefile_data = handle;

  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  assert(handle != NULL);

  DEBUG("Received a message of type %d (ref. %d in reply to %d)",
        message->hdr.action_code, message->hdr.my_ref, message->hdr.your_ref);

  DEBUG("Window handle is %d", message->data.data_load.destination_window);
  DEBUG("File type is &%X", message->data.data_load.file_type);

  /* Don't claim messages that are replies (dealt with by Entity module) or
     for windows other than the save box associated with the client handle. */
  if (message->hdr.your_ref != 0 ||
      message->data.data_load.destination_window != savefile_data->wimp_handle)
  {
    return 0; /* pass message on */
  }

  if (message->data.data_load.file_type == FileType_CSV)
  {
    if (loader3_load_file(message->data.data_load.leaf_name,
                          message->data.data_load.file_type,
                          csv_loaded, load_failed, handle))
    {
      message->hdr.your_ref = message->hdr.my_ref;
      message->hdr.action_code = Wimp_MDataLoadAck;
      if (!E(wimp_send_message(Wimp_EUserMessage, message, message->hdr.sender,
              0, NULL)))
      {
        DEBUG("Sent DataLoadAck message (ref. %d)", message->hdr.my_ref);
      }
    }
  }
  else
  {
    RPT_ERR("NotCSV");
  }

  return 1; /* claim message */
}

/*
 * Toolbox event handlers
 */

static int actionbutton_selected(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* ActionButton clicked on underlying window */
  const ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust) &&
      id_block->self_component == (SaveAs_ObjectClass << 4) + 2)
  {
    /* Reset dbox state */
    SaveSky * const savefile_data = handle;
    (void)write_heights(id_block->self_id, &savefile_data->context.hdr);
    return 1; /* claim event */
  }
  else
  {
    return 0; /* not interested */
  }
}

/* ----------------------------------------------------------------------- */

static int save_to_file(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  assert(event_code == SaveAs_SaveToFile);
  tbox_save_file(event, id_block, handle, write_sky);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  assert(event_code == SaveAs_FillBuffer);
  assert(event != NULL);
  assert(handle != NULL);

  SaveSky * const savefile_data = handle;
  tbox_send_data(event, id_block, handle, &savefile_data->sky_data, write_sky);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void destroy_savebox(SFTSaveBox *const savebox)
{
  SaveSky * const savefile_data = CONTAINER_OF(savebox, SaveSky, super);

  assert(savefile_data != NULL);
  SFTSaveBox_finalise(savebox);

  if (savefile_data->sprites)
  {
    flex_free(&savefile_data->sprites);
  }

  if (savefile_data->sky_data)
  {
    flex_free(&savefile_data->sky_data);
  }

  /* Deregister Wimp message handlers to load CSV files */
  ON_ERR_RPT(event_deregister_message_handler(Wimp_MDataSave,
                                              datasave_message,
                                              savefile_data));

  ON_ERR_RPT(event_deregister_message_handler(Wimp_MDataLoad,
                                              dataload_message,
                                              savefile_data));

  loader3_cancel_receives(savefile_data);

  /* Notify the creator of this dialogue box that it was deleted */
  if (savefile_data->deleted_cb != NULL)
    savefile_data->deleted_cb(savebox);

  free(savefile_data);
}

/* -----------------------------------------------------------------------
 *                         Public functions
 */

SFTSaveBox *SaveSky_create(char const *const save_path, int const x,
  bool const data_saved, flex_ptr sprites,
  SkySpritesContext const *const context, SFTSaveBoxDeletedFn *const deleted_cb)
{
  assert(save_path != NULL);

  /* Initialise status block */
  SaveSky * const savefile_data = malloc(sizeof(*savefile_data));
  if (savefile_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *savefile_data = (SaveSky){
    .context = *context,
    .deleted_cb = deleted_cb,
  };

  if (SFTSaveBox_initialise(&savefile_data->super,
                            save_path,
                            data_saved,
                            FileType_SFSkyCol,
                            "SprToSky",
                            "SprSkyList",
                            x,
                            destroy_savebox))
  {
    /* Register Wimp message handlers to load CSV files */
    if (!E(event_register_message_handler(Wimp_MDataSave,
                                          datasave_message,
                                          savefile_data)))
    {
      if (!E(event_register_message_handler(Wimp_MDataLoad,
                                            dataload_message,
                                            savefile_data)))
      {
        do
        {
          /* Get the Wimp handle of the window underlying the savebox */
          if (E(window_get_wimp_handle(0, savefile_data->super.window_id,
                                       &savefile_data->wimp_handle)))
          {
            break;
          }

          /* Can't know the final size yet because the user can edit things
             in the dialogue box so calculate the worst case. */
          if (E(saveas_set_file_size(0, savefile_data->super.saveas_id,
                worst_comp_size(sky_size()))))
          {
            break;
          }

          /* Register other event handlers for SaveAs object */
          if (E(event_register_toolbox_handler(savefile_data->super.saveas_id,
                                               SaveAs_SaveToFile,
                                               save_to_file,
                                               savefile_data)))
          {
            break;
          }

          if (E(event_register_toolbox_handler(savefile_data->super.saveas_id,
                                               SaveAs_FillBuffer,
                                               fill_buffer, savefile_data)))
          {
            break;
          }

          /* Register event handler for underlying Window object */
          if (E(event_register_toolbox_handler(savefile_data->super.window_id,
                                               ActionButton_Selected,
                                               actionbutton_selected,
                                               savefile_data)))
          {
            break;
          }

          /* Set up extra gadgets in the underlying Window object */
          if (!write_heights(savefile_data->super.window_id, &savefile_data->context.hdr))
          {
            break;
          }

          if (!flex_reanchor(&savefile_data->sprites, sprites))
          {
            assert("flex_reanchor failed!" == NULL);
            break;
          }

          return &savefile_data->super;
        }
        while (0);
        (void)event_deregister_message_handler(Wimp_MDataLoad,
                                               dataload_message,
                                               savefile_data);
      }
      (void)event_deregister_message_handler(Wimp_MDataSave,
                                             datasave_message,
                                             savefile_data);
    }
    SFTSaveBox_finalise(&savefile_data->super);
  }
  free(savefile_data);
  return NULL;
}
