/*
 *  FednetCmp - Fednet file compression/decompression
 *  Compressed file savebox
 *  Copyright (C) 2014 Christopher Bazley
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
#include <assert.h>
#include "stdlib.h"
#include "stdio.h"
#include <string.h>
#include <stdbool.h>

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
#include "Debug.h"
#include "Reader.h"

/* Local headers */
#include "SaveComp.h"
#include "FNCSaveBox.h"
#include "Utils.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

/* Window component IDs */
enum
{
  ComponentId_FileType_StringSet = 0x01,
  ComponentId_Cancel_ActButton   = 0x82bc02,
  ComponentId_Save_ActButton     = 0x82bc03
};

/* Constant numeric values */
enum
{
  MaxFileTypeNameLen = 31,
};

typedef struct
{
  FNCSaveBox super;
  void *comp_data;
  void *decomp_data;
  char reset_filetype[MaxFileTypeNameLen + 1];
  _Optional FNCSaveBoxDeletedFn *deleted_cb;
}
SaveComp;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void destroy_savecomp(FNCSaveBox *const savebox)
{
  SaveComp * const savecomp_data = CONTAINER_OF(savebox, SaveComp, super);

  assert(savecomp_data != NULL);
  FNCSaveBox_finalise(savebox);

  if (savecomp_data->decomp_data != NULL)
  {
    flex_free(&savecomp_data->decomp_data);
  }

  if (savecomp_data->comp_data != NULL)
  {
    flex_free(&savecomp_data->comp_data);
  }

  /* Notify the creator of this dialogue box that it was deleted */
  if (savecomp_data->deleted_cb)
    savecomp_data->deleted_cb(savebox);

  free(savecomp_data);
}

/*
 * Toolbox event handlers
 */

static int stringset_value_changed(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  /* New file type selected from StringSet */
  const StringSetValueChangedEvent * const ssvce =
    (StringSetValueChangedEvent *)event;
  const SaveComp *const savecomp_data = handle;
  unsigned int hex_type = FileType_Fednet;

  NOT_USED(event_code);
  assert(event != NULL);
  NOT_USED(id_block);
  assert(handle != NULL);

  _Optional const char *const bracket = strchr(ssvce->string, '(');
  if (bracket) {
    sscanf(&*bracket, "(&%x)", &hex_type);
  }
  ON_ERR_RPT(saveas_set_file_type(0, savecomp_data->super.saveas_id, hex_type));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_to_file(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)event;
  SaveComp *const savecomp_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  tbox_save_file(sastfe, id_block->self_id, &savecomp_data->decomp_data, comp_from_buf);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)event;
  SaveComp *const savecomp_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  tbox_send_data(safbe, id_block->self_id, &savecomp_data->comp_data,
    &savecomp_data->decomp_data, comp_from_buf);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  const ActionButtonSelectedEvent * const abse =
    (ActionButtonSelectedEvent *)event;
  SaveComp *const savecomp_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (!TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust))
    return 0; /* not interested */

  switch (id_block->self_component)
  {
    case ComponentId_Cancel_ActButton:
      /* Reset dbox state */
      ON_ERR_RPT(stringset_set_selected(
                 0,
                 id_block->self_id,
                 ComponentId_FileType_StringSet,
                 savecomp_data->reset_filetype));
      {
        unsigned int hex_type = FileType_Fednet;
        _Optional const char *const bracket = strchr(savecomp_data->reset_filetype, '(');
        if (bracket) {
          sscanf(&*bracket, "(&%x)", &hex_type);
        }
        ON_ERR_RPT(saveas_set_file_type(0, savecomp_data->super.saveas_id,
          hex_type));
      }
      break;

    case ComponentId_Save_ActButton:
      /* Record dbox state */
      ON_ERR_RPT(stringset_get_selected(
                 0,
                 id_block->self_id,
                 ComponentId_FileType_StringSet,
                 &savecomp_data->reset_filetype,
                 sizeof(savecomp_data->reset_filetype),
                 NULL));
      break;

    default:
      return 0; /* unknown component */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

_Optional FNCSaveBox *SaveComp_create(char const *const filename, bool const data_saved,
  Reader *const reader, int const estimated_size, int const x,
  _Optional FNCSaveBoxDeletedFn *const deleted_cb)
{
  assert(filename != NULL);
  DEBUGF("Creating savecomp box for data '%s' of size %d (from %s) \n",
         filename, estimated_size, data_saved ? "file" : "application");

  /* Initialise status block */
  _Optional SaveComp * const savecomp_data = malloc(sizeof(*savecomp_data));
  if (savecomp_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }
  *savecomp_data = (SaveComp){.deleted_cb = deleted_cb};

  if (FNCSaveBox_initialise(&savecomp_data->super,
                             filename,
                             data_saved,
                             FileType_Fednet,
                             "SaveFednet",
                             "CompDialogueList",
                             x,
                             destroy_savecomp))
  {
    if (copy_to_buf(&savecomp_data->decomp_data, reader, estimated_size,
      filename))
    {
      do
      {
        if (E(saveas_set_file_size(0, savecomp_data->super.saveas_id,
          get_comp_size(&savecomp_data->decomp_data))))
        {
          break;
        }

        if (E(event_register_toolbox_handler(
              savecomp_data->super.saveas_id, SaveAs_SaveToFile,
              save_to_file, &*savecomp_data)))
          break;

        if (E(event_register_toolbox_handler(
            savecomp_data->super.saveas_id, SaveAs_FillBuffer,
            fill_buffer, &*savecomp_data)))
          break;

        /* Get default output file type from object template */
        if (E(stringset_get_selected(
              0,
              savecomp_data->super.window_id,
              ComponentId_FileType_StringSet,
              &savecomp_data->reset_filetype,
              sizeof(savecomp_data->reset_filetype),
              NULL)))
          break;

          /* Register extra handlers for filetype selection & restoration of
             last filetype used if Cancel button clicked */
        if (E(event_register_toolbox_handler(
              savecomp_data->super.window_id,
              StringSet_ValueChanged,
              stringset_value_changed,
              &*savecomp_data)))
          break;

        if (E(event_register_toolbox_handler(
              savecomp_data->super.window_id,
              ActionButton_Selected,
              actionbutton_selected,
              &*savecomp_data)))
          break;

        return &savecomp_data->super;
      }
      while (0);
      flex_free(&savecomp_data->decomp_data);
    }
    FNCSaveBox_finalise(&savecomp_data->super);
  }
  free(savecomp_data);
  return NULL;
}
