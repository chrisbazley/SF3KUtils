/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for Sprite file
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
#include <limits.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "gadgets.h"
#include "flex.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "SFFormats.h"
#include "SprFormats.h"
#include "FilePerc.h"
#include "PathTail.h"
#include "EventExtra.h"
#include "Debug.h"
#include "ReaderFlex.h"
#include "WriterNull.h"
#include "Hourglass.h"
#include "ReaderGKey.h"

/* Local headers */
#include "SFgfxconv.h"
#include "SaveSprites.h"
#include "SFTSaveBox.h"
#include "Utils.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

/* Window component IDs */
enum
{
  ComponentId_ImagesData_Radio = 0x00,
  ComponentId_Images_Radio     = 0x01,
  ComponentId_Data_Radio       = 0x02
};

/* Constant numeric values */
enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
};

typedef struct
{
  SFTSaveBox super;
  void      *input_buffer; /* flex_block */
  void      *output_buffer; /* flex block */
  int        reset_radio;
  int        input_file_type;
  _Optional SFTSaveBoxDeletedFn *deleted_cb;
}
SaveSprites;

typedef SFError ConverterFn(Reader *, Writer *);

/* -----------------------------------------------------------------------
 *                          Private functions
 */

static void destroy_savebox(SFTSaveBox *savebox)
{
  SaveSprites * const savefile_data = (SaveSprites *)savebox;

  assert(savefile_data != NULL);
  SFTSaveBox_finalise(savebox);

  /* free memory */
  if (savefile_data->output_buffer != NULL)
    flex_free(&savefile_data->output_buffer);

  flex_free(&savefile_data->input_buffer);

  /* Notify the creator of this dialogue box that it was deleted */
  if (savefile_data->deleted_cb)
    savefile_data->deleted_cb(savebox);

  free(savefile_data);
}

/* ----------------------------------------------------------------------- */

static int get_size(SaveSprites *const savefile_data, ConverterFn *const fn)
{
  long int out_size = 0;

  assert(savefile_data);
  assert(fn);

  Reader reader;
  reader_flex_init(&reader, &savefile_data->input_buffer);

  Reader gkreader;
  bool success = reader_gkey_init_from(&gkreader, FednetHistoryLog2, &reader);
  if (!success)
  {
    RPT_ERR("NoMem");
  }
  else
  {
    Writer writer;
    writer_null_init(&writer);

    hourglass_on();
    SFError err = fn(&gkreader, &writer);
    hourglass_off();

    out_size = writer_destroy(&writer);
    if (out_size < 0 || err != SFError_OK)
    {
      DEBUGF("Unable to get file size: conversion failed\n");
      out_size = 0;
    }

    reader_destroy(&gkreader);
  }

  reader_destroy(&reader);

  assert(out_size >= 0);
  assert(out_size <= INT_MAX);
  DEBUGF("Expected file size is %ld\n", out_size);
  return (int)out_size;
}

/* ----------------------------------------------------------------------- */

static _Optional ConverterFn *pick_converter(SaveSprites *const savefile_data, int const radio_button)
{
  _Optional ConverterFn *fn = (ConverterFn *)NULL;

  assert(savefile_data);
  DEBUGF("Input filetype is 0x%x, output component ID is 0x%x\n",
         savefile_data->input_file_type, radio_button);

  static ConverterFn *const tile_conv[] =
  {
    [ComponentId_Data_Radio] = tiles_to_csv,
    [ComponentId_ImagesData_Radio] = tiles_to_sprites_ext,
    [ComponentId_Images_Radio] = tiles_to_sprites,
  };

  static ConverterFn *const planet_conv[] =
  {
    [ComponentId_Data_Radio] = planets_to_csv,
    [ComponentId_ImagesData_Radio] = planets_to_sprites_ext,
    [ComponentId_Images_Radio] = planets_to_sprites,
  };

  static ConverterFn *const sky_conv[] =
  {
    [ComponentId_Data_Radio] = sky_to_csv,
    [ComponentId_ImagesData_Radio] = sky_to_sprites_ext,
    [ComponentId_Images_Radio] = sky_to_sprites,
  };
  ConverterFn *_Optional const (*table)[ComponentId_Data_Radio + 1] = NULL;

  switch (savefile_data->input_file_type)
  {
    case FileType_SFMapGfx:
      table = &tile_conv;
      break;

    case FileType_SFSkyPic:
      table = &planet_conv;
      break;

    case FileType_SFSkyCol:
      table = &sky_conv;
      break;

    default:
      assert(false);
      break;
  }

  if (table)
  {
    if (radio_button >= 0 && (unsigned)radio_button < ARRAY_SIZE(*table))
    {
      fn = (*table)[radio_button];
    }
  }
  return fn;
}

/* ----------------------------------------------------------------------- */

static bool change_output(SaveSprites *const savefile_data, int const radio_button)
{
  assert(savefile_data != NULL);
  DEBUGF("Changing output mode to %d\n", radio_button);

  /* Update the file type icon displayed in the savebox */
  static int const filetypes[] =
  {
    [ComponentId_Data_Radio] = FileType_CSV,
    [ComponentId_ImagesData_Radio] = FileType_Sprite,
    [ComponentId_Images_Radio] = FileType_Sprite,
  };

  if (radio_button < 0 || (unsigned)radio_button >= ARRAY_SIZE(filetypes))
  {
    return false;
  }

  if (E(saveas_set_file_type(0, savefile_data->super.saveas_id,
                             filetypes[radio_button])))
  {
    return false;
  }

  _Optional ConverterFn *const converter = pick_converter(savefile_data, radio_button);
  if (!converter)
  {
    DEBUGF("No format converter\n");
    return false;
  }

  int const file_size = get_size(savefile_data, &*converter);
  if (E(saveas_set_file_size(0, savefile_data->super.saveas_id, file_size)))
  {
    return false;
  }

  return true;
}

/* ----------------------------------------------------------------------- */

static bool write_sprite_or_csv(Writer *const writer, void *const handle,
  char const *const filename)
{
  SaveSprites * const savefile_data = handle;
  assert(handle != NULL);

  /* Read state of radio buttons in the underlying Window object */
  if (E(radiobutton_get_state(0, savefile_data->super.window_id,
                              ComponentId_ImagesData_Radio, NULL,
                              &savefile_data->reset_radio)))
  {
    return false;
  }

  _Optional ConverterFn *const converter = pick_converter(savefile_data, savefile_data->reset_radio);
  if (!converter)
  {
    DEBUGF("No format converter\n");
    return false;
  }

  Reader reader;
  reader_flex_init(&reader, &savefile_data->input_buffer);

  SFError err = SFError_NoMem;
  Reader gkreader;
  bool success = reader_gkey_init_from(&gkreader, FednetHistoryLog2, &reader);
  if (success)
  {
    hourglass_on();
    err = converter(&gkreader, writer);
    hourglass_off();
    reader_destroy(&gkreader);
  }
  reader_destroy(&reader);

  return !handle_error(err, "RAM", filename);
}

/*
 * Toolbox event handlers
 */

static int save_to_file(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  assert(event_code == SaveAs_SaveToFile);
  NOT_USED(event_code);
  tbox_save_file(event, id_block, handle, write_sprite_or_csv);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  assert(event_code == SaveAs_FillBuffer);
  NOT_USED(event_code);
  assert(event != NULL);
  assert(handle != NULL);

  SaveSprites * const savefile_data = handle;
  tbox_send_data(event, id_block, handle, &savefile_data->output_buffer, write_sprite_or_csv);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_completed(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Save completed */
  SaveSprites * const savefile_data = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  if (savefile_data->output_buffer != NULL)
  {
    flex_free(&savefile_data->output_buffer);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust) &&
      id_block->self_component == (SaveAs_ObjectClass << 4) + 2)
  {
    /* ADJUST click on 'Cancel' button - reset dbox state */
    SaveSprites * const savefile_data = handle;

    if (!E(radiobutton_set_state(0, id_block->self_id,
           savefile_data->reset_radio, 1)))
    {
      (void)change_output(savefile_data, savefile_data->reset_radio);
    }
    return 1; /* claim event */
  }
  else
  {
    return 0; /* not interested */
  }
}

/* ----------------------------------------------------------------------- */

static int radiobutton_state_changed(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const RadioButtonStateChangedEvent * const rbsce =
    (RadioButtonStateChangedEvent *)event;
  SaveSprites * const savefile_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (rbsce->state != 1)
    return 0; /* button de-selection event */

  if (id_block->self_component != ComponentId_ImagesData_Radio &&
      id_block->self_component != ComponentId_Images_Radio &&
      id_block->self_component != ComponentId_Data_Radio)
  {
    return 0; /* unknown radio button */
  }

  (void)change_output(savefile_data, id_block->self_component);

  return 1; /* claim event */
}

/* -----------------------------------------------------------------------
 *                         Public functions
 */

_Optional SFTSaveBox *SaveSprites_create(char const *save_path, int x, bool data_saved, flex_ptr buffer,
  int input_file_type, _Optional SFTSaveBoxDeletedFn *deleted_cb)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      SaveAs_SaveToFile,
      save_to_file
    },
    {
      SaveAs_FillBuffer,
      fill_buffer
    },
    {
      SaveAs_SaveCompleted,
      save_completed
    }
  };

  assert(save_path != NULL);
  DEBUGF("Creating savebox for sprites/CSV, input size is %d\n", flex_size(buffer));

  /* Initialise status block */
  _Optional SaveSprites * const savefile_data = malloc(sizeof(*savefile_data));
  if (savefile_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *savefile_data = (SaveSprites){
    .input_file_type = input_file_type,
    .deleted_cb = deleted_cb,
  };

  if (SFTSaveBox_initialise(&savefile_data->super, save_path, data_saved,
                            FileType_SFMapGfx, "ToSpr", "ToSprList", x,
                            destroy_savebox))
  {
    do
    {
      /* Record initial state of dialogue box */
      if (E(radiobutton_get_state(0, savefile_data->super.window_id,
                                  ComponentId_ImagesData_Radio, NULL,
                                  &savefile_data->reset_radio)))
        break;

      /* Register other event handlers for SaveAs object */
      for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
      {
        if (E(event_register_toolbox_handler(savefile_data->super.saveas_id,
                                             tbox_handlers[i].event_code,
                                             tbox_handlers[i].handler,
                                             &*savefile_data)))
          break;
      }

      /* Register event handlers for the underlying Window object */
      if (E(event_register_toolbox_handler(savefile_data->super.window_id,
                                           ActionButton_Selected,
                                           actionbutton_selected,
                                           &*savefile_data)))
        break;

      if (E(event_register_toolbox_handler(savefile_data->super.window_id,
                                           RadioButton_StateChanged,
                                           radiobutton_state_changed,
                                           &*savefile_data)))
        break;

      if (!flex_reanchor(&savefile_data->input_buffer, buffer))
      {
        assert("flex_reanchor failed!" == NULL);
        break;
      }

      if (!change_output(&*savefile_data, savefile_data->reset_radio))
      {
        if (!flex_reanchor(buffer, &savefile_data->input_buffer))
          assert("flex_reanchor failed!" == NULL);
        break;
      }

      return &savefile_data->super;
    }
    while (0);
    SFTSaveBox_finalise(&savefile_data->super);
  }
  free(savefile_data);
  return NULL;
}
