/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for SFMapGfx file
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
#include <stdbool.h>
#include <assert.h>

/* RISC OS library files */
#include "wimp.h"
#include "wimplib.h"
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "window.h"
#include "gadgets.h"
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
#include "Utils.h"
#include "SFgfxconv.h"
#include "SaveMapTiles.h"
#include "SFTSaveBox.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

/* Window component IDs */
enum
{
  ComponentId_S2TriggerA_NumRange = 0x00,
  ComponentId_Splash1_NumRange    = 0x0c,
  ComponentId_Splash2_NumRange    = 0x10,
  ComponentId_S2TriggerB_NumRange = 0x100,
  ComponentId_LastTile_NumRange   = 0x116
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
  void      *tiles_data; /* flex block */
  void      *sprites; /* flex block */
  MapTileSpritesContext context;
  _Optional SFTSaveBoxDeletedFn *deleted_cb;
}
SaveMapTiles;

/* -----------------------------------------------------------------------
 *                          Private functions
 */

static bool read_anims(ObjectId win, MapTilesHeader *const tiles_data)
{
  assert(tiles_data != NULL);
  DEBUG("Updating animations anchored at %p from window %d",
        (void *)tiles_data, win);

  /* Read animations data from text fields */
  for (size_t byte = 0; byte < ARRAY_SIZE(tiles_data->splash_anim_1); byte++)
  {
    int value;

    if (E(numberrange_get_value(
        0, win, ComponentId_Splash1_NumRange + byte, &value)))
      return false;

    tiles_data->splash_anim_1[byte] = value;

    if (E(numberrange_get_value(0,
        win, ComponentId_Splash2_NumRange + byte, &value)))
      return false;

    tiles_data->splash_anim_2[byte] = value;

    if (E(numberrange_get_value(
        0, win, ComponentId_S2TriggerA_NumRange + byte, &value)))
      return false;

    tiles_data->splash_2_triggers[byte] = value;
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static bool write_anims(ObjectId win, MapTilesHeader const *const tiles_data)
{
  /* Fill text fields with animations data */
  assert(tiles_data != NULL);
  DEBUG("Showing animations anchored at %p in window %d",
        (void *)tiles_data, win);

  for (size_t byte = 0; byte < ARRAY_SIZE(tiles_data->splash_anim_1); byte++)
  {
    int temp;

    if (E(numberrange_set_value(0, win, ComponentId_Splash1_NumRange + byte,
                                tiles_data->splash_anim_1[byte])))
      return false;

    if (E(numberrange_set_value(0, win, ComponentId_Splash2_NumRange + byte,
                                tiles_data->splash_anim_2[byte])))
      return false;

    temp = tiles_data->splash_2_triggers[byte];

    if (E(numberrange_set_value(0, win, ComponentId_S2TriggerA_NumRange + byte,
                                temp)))
      return false;

    if (E(numberrange_set_value(0, win, ComponentId_S2TriggerB_NumRange + byte,
                                temp + 1)))
      return false;
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static bool set_limits(ObjectId win, int last_tile)
{
  /* Fill text fields with animations data */
  assert(last_tile >= 0 && last_tile < 255);
  DEBUG("Limiting number ranges to %d in window %d", last_tile, win);

  for (size_t byte = 0; byte < ARRAY_SIZE(((MapTilesHeader *)0)->splash_anim_1); byte++)
  {
    if (E(numberrange_set_bounds(NumberRange_UpperBound, win,
                                 ComponentId_Splash1_NumRange + byte,
                                 0, last_tile, 0, 0)))
      return false;

    if (E(numberrange_set_bounds(NumberRange_UpperBound, win,
                                 ComponentId_Splash2_NumRange + byte,
                                 0, last_tile, 0, 0)))
      return false;
  }
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

  SaveMapTiles * const savefile_data = client_handle;
  MapTilesHeader header = savefile_data->context.hdr;
  bool success = false;
  if (read_anims(savefile_data->super.window_id, &header))
  {
    if (!handle_error(csv_to_tiles(reader, &header), filename, ""))
    {
      success = write_anims(savefile_data->super.window_id, &header);
    }
  }
  return success;
}

/* ----------------------------------------------------------------------- */

static bool check_triggers(MapTilesHeader const *const tiles)
{
  assert(tiles != NULL);

  /* Friendly check that the splash triggers cover the splash animation */
  bool one_not_covered = false, anims_eq = true;
  for (size_t a = 0; a < ARRAY_SIZE(tiles->splash_anim_2); a++)
  {
    bool covered = false;

    /* Check that one of the triggers covers this splash tile... */
    for (size_t b = 0; b < ARRAY_SIZE(tiles->splash_2_triggers) && !covered; b++)
    {
      if (tiles->splash_2_triggers[b] == tiles->splash_anim_2[a] ||
          (tiles->splash_2_triggers[b] + 1) == tiles->splash_anim_2[a])
        covered = true;
    }
    if (!covered)
      one_not_covered = true;

    if (tiles->splash_anim_1[a] != tiles->splash_anim_2[a])
      anims_eq = false;
  }

  if (one_not_covered && !anims_eq)
  {
    if (!dialogue_confirm(msgs_lookup("Splash2Warn")))
      return false;
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static bool write_map_tiles(Writer *const writer, void *const handle,
  char const *const filename)
{
  SaveMapTiles * const savefile_data = handle;
  assert(handle != NULL);

  /* Read displayed animations data into header
  N.B. This has the side effect of confirming the displayed animations for
       use if the dbox is reset (e.g. ADJUST-click 'Cancel') */
  if (!read_anims(savefile_data->super.window_id, &savefile_data->context.hdr))
  {
    return false;
  }

  /* Check animations */
  if (!check_triggers(&savefile_data->context.hdr))
  {
    return false;
  }

  Writer gkwriter;
  bool success = writer_gkey_init_from(&gkwriter, FednetHistoryLog2,
                    tiles_size(&savefile_data->context.hdr), writer);
  if (!success)
  {
    RPT_ERR("NoMem");
    return false;
  }

  Reader reader;
  reader_flex_init(&reader, &savefile_data->sprites);
  SFError err = sprites_to_tiles(&reader, &gkwriter, &savefile_data->context);
  reader_destroy(&reader);

  long int const out_bytes = writer_destroy(&gkwriter);
  DEBUGF("%ld bytes written in write_map_tiles\n", out_bytes);
  if (out_bytes < 0 && err == SFError_OK)
  {
    err = SFError_WriteFail;
  }

  return !handle_error(err, "RAM", filename);
}

/*
 * Wimp message handlers
 */

static int datasave_message(WimpMessage *const message,
  void *const handle)
{
  SaveMapTiles * const savefile_data = handle;

  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataSave);
  assert(handle != NULL);

  DEBUG("Received a message of type %d (ref. %d in reply to %d)",
        message->hdr.action_code, message->hdr.my_ref, message->hdr.your_ref);

  DEBUG("Window handle is %d", message->data.data_save.destination_window);
  DEBUG("File type is &%X", message->data.data_save.file_type);

  /* Don't claim messages that are replies (dealt with by Loader2 module) or
     for windows other than the save box associated with the client handle. */
  if (message->hdr.your_ref != 0 ||
      message->data.data_save.destination_window != savefile_data->wimp_handle)
  {
    return 0; /* pass message on */
  }

  if (message->data.data_save.file_type == FileType_CSV)
  {
    /* Load the contents of the file cited in the message */
    ON_ERR_RPT(loader3_receive_data(message, csv_loaded, load_failed, handle));
  }
  else
  {
    RPT_ERR("NotCSV");
  }

  return 1; /* claim message */
}

static int dataload_message(WimpMessage *const message,
  void *const handle)
{
  SaveMapTiles * const savefile_data = handle;

  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  assert(handle != NULL);

  DEBUG("Received a message of type %d (ref. %d in reply to %d)",
        message->hdr.action_code, message->hdr.my_ref, message->hdr.your_ref);

  DEBUG("Window handle is %d", message->data.data_load.destination_window);
  DEBUG("File type is &%X", message->data.data_load.file_type);

  /* Don't claim messages that are replies (dealt with by Loader2 module) or
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

static int numberrange_value_changed(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* NumberRange clicked on underlying window */
  const NumberRangeValueChangedEvent * const nrvce =
    (NumberRangeValueChangedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (id_block->self_component < ComponentId_S2TriggerA_NumRange ||
      id_block->self_component > ComponentId_S2TriggerA_NumRange + 3)
  {
    return 0; /* bugger off if not 2nd splash triggers */
  }
  else
  {
    /* Synchronise the second displayed number of this trigger pair */
    ON_ERR_RPT(numberrange_set_value(
      0,
      id_block->self_id,
      ComponentId_S2TriggerB_NumRange +
        (id_block->self_component - ComponentId_S2TriggerA_NumRange),
      nrvce->new_value + 1));

    return 1; /* claim event */
  }
}

/* ----------------------------------------------------------------------- */

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
    SaveMapTiles * const savefile_data = handle;
    (void)write_anims(id_block->self_id, &savefile_data->context.hdr);
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
  assert(event_code == SaveAs_SaveToFile);
  NOT_USED(event_code);
  tbox_save_file(event, id_block, handle, write_map_tiles);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  assert(event_code == SaveAs_FillBuffer);
  NOT_USED(event_code);
  assert(event != NULL);
  assert(handle != NULL);

  SaveMapTiles * const savefile_data = handle;
  tbox_send_data(event, id_block, handle, &savefile_data->tiles_data,
    write_map_tiles);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void destroy_savebox(SFTSaveBox *const savebox)
{
  SaveMapTiles * const savefile_data = CONTAINER_OF(savebox, SaveMapTiles, super);

  assert(savefile_data != NULL);
  SFTSaveBox_finalise(savebox);

  if (savefile_data->sprites)
  {
    flex_free(&savefile_data->sprites);
  }

  if (savefile_data->tiles_data)
  {
    flex_free(&savefile_data->tiles_data);
  }

  /* Deregister Wimp message handlers to load animation text files */
  ON_ERR_RPT(event_deregister_message_handler(Wimp_MDataSave,
                                              datasave_message,
                                              savefile_data));

  ON_ERR_RPT(event_deregister_message_handler(Wimp_MDataLoad,
                                              dataload_message,
                                              savefile_data));

  loader3_cancel_receives(savefile_data);

  /* Notify the creator of this dialogue box that it was deleted */
  if (savefile_data->deleted_cb)
    savefile_data->deleted_cb(savebox);

  free(savefile_data);
}

/* -----------------------------------------------------------------------
 *                         Public functions
 */

_Optional SFTSaveBox *SaveMapTiles_create(char const *const save_path, int const x,
  bool const data_saved, flex_ptr sprites,
  MapTileSpritesContext const *const context, _Optional SFTSaveBoxDeletedFn *const deleted_cb)
{
  assert(save_path != NULL);

  /* Initialise status block */
  _Optional SaveMapTiles * const savefile_data = malloc(sizeof(*savefile_data));
  if (savefile_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *savefile_data = (SaveMapTiles){
    .context = *context,
    .deleted_cb = deleted_cb,
  };

  if (SFTSaveBox_initialise(&savefile_data->super,
                            save_path,
                            data_saved,
                            FileType_SFMapGfx,
                            "SprToTex",
                            "SprTexList",
                            x,
                            destroy_savebox))
  {
    /* Register Wimp message handlers to load animation text files */
    if (!E(event_register_message_handler(Wimp_MDataSave,
                                          datasave_message,
                                          &*savefile_data)))
    {
      if (!E(event_register_message_handler(Wimp_MDataLoad,
                                            dataload_message,
                                            &*savefile_data)))
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
                 worst_comp_size(tiles_size(&savefile_data->context.hdr)))))
          {
            break;
          }

          /* Register other event handlers for SaveAs object */
          if (E(event_register_toolbox_handler(savefile_data->super.saveas_id,
                                               SaveAs_SaveToFile,
                                               save_to_file, &*savefile_data)))
          {
            break;
          }

          if (E(event_register_toolbox_handler(savefile_data->super.saveas_id,
                                               SaveAs_FillBuffer,
                                               fill_buffer, &*savefile_data)))
          {
            break;
          }

          /* Register event handlers for underlying Window object */
          if (E(event_register_toolbox_handler(savefile_data->super.window_id,
                                               ActionButton_Selected,
                                               actionbutton_selected,
                                               &*savefile_data)))
          {
            break;
          }

          if (E(event_register_toolbox_handler(savefile_data->super.window_id,
                                               NumberRange_ValueChanged,
                                               numberrange_value_changed,
                                               &*savefile_data)))
          {
            break;
          }

          /* Set up extra gadgets in the underlying Window object */
          const int last_tile = savefile_data->context.hdr.last_tile_num;
          if (E(numberrange_set_value(0, savefile_data->super.window_id,
                                      ComponentId_LastTile_NumRange, last_tile)))
          {
            break;
          }

          if (!set_limits(savefile_data->super.window_id, last_tile))
          {
            break;
          }

          if (!write_anims(savefile_data->super.window_id,
                           &savefile_data->context.hdr))
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
                                               &*savefile_data);
      }
      (void)event_deregister_message_handler(Wimp_MDataSave,
                                             datasave_message,
                                             &*savefile_data);
    }
    SFTSaveBox_finalise(&savefile_data->super);
  }
  free(savefile_data);
  return NULL;
}
