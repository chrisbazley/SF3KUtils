/*
 *  FednetCmp - Fednet file compression/decompression
 *  Uncompressed file savebox
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
#include "Debug.h"

/* Local headers */
#include "SaveFile.h"
#include "FNCSaveBox.h"
#include "Utils.h"

/* Window component IDs */
enum
{
  ComponentId_FileType_StringSet = 0x01,
  ComponentId_Cancel_ActButton   = 0x82bc02,
  ComponentId_Save_ActButton     = 0x82bc03
};

typedef struct
{
  FNCSaveBox super;
  void *comp_data;
  void *decomp_data;
  FNCSaveBoxDeletedFn *deleted_cb;
}
SaveFile;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void destroy_savefile(FNCSaveBox *const savebox)
{
  SaveFile * const savefile_data = CONTAINER_OF(savebox, SaveFile, super);

  assert(savefile_data != NULL);
  FNCSaveBox_finalise(savebox);

  if (savefile_data->comp_data)
  {
    flex_free(&savefile_data->comp_data);
  }

  if (savefile_data->decomp_data)
  {
    flex_free(&savefile_data->decomp_data);
  }

  /* Notify the creator of this dialogue box that it was deleted */
  if (savefile_data->deleted_cb != NULL)
    savefile_data->deleted_cb(savebox);

  free(savefile_data);
}

/*
 * Toolbox event handlers
 */

static int save_to_file(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)event;
  SaveFile *const savefile_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  tbox_save_file(sastfe, id_block->self_id, &savefile_data->comp_data,
    decomp_from_buf);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)event;
  SaveFile *const savefile_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  tbox_send_data(safbe, id_block->self_id, &savefile_data->decomp_data,
    &savefile_data->comp_data, decomp_from_buf);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

FNCSaveBox *SaveFile_create(char const *const filename,
  bool const data_saved, Reader *const reader, int const estimated_size,
  int const x, FNCSaveBoxDeletedFn *const deleted_cb)
{
  /* Open savebox for data - if success, reanchors flex block. */
  assert(filename != NULL);
  DEBUGF("Creating savefile box for data '%s' of size %d (from %s)\n",
         filename, estimated_size, data_saved ? "file" : "application");

  /* Initialise status block */
  SaveFile * const savefile_data = malloc(sizeof(*savefile_data));
  if (savefile_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }
  *savefile_data = (SaveFile){.deleted_cb = deleted_cb};

  if (FNCSaveBox_initialise(&savefile_data->super,
                            filename,
                            data_saved,
                            FileType_Data,
                            "SaveFile",
                            "DeCompDialogueList",
                            x,
                            destroy_savefile))
  {
    /* Keep a copy of the (compressed) input data */
    if (copy_to_buf(&savefile_data->comp_data, reader,
      estimated_size, filename))
    {
      do
      {
        if (E(event_register_toolbox_handler(
            savefile_data->super.saveas_id, SaveAs_SaveToFile,
            save_to_file, savefile_data)))
          break;

        if (E(event_register_toolbox_handler(
            savefile_data->super.saveas_id, SaveAs_FillBuffer,
            fill_buffer, savefile_data)))
          break;

        if (E(saveas_set_file_size(0, savefile_data->super.saveas_id,
          get_decomp_size(&savefile_data->comp_data))))
        {
          break;
        }

        return &savefile_data->super;
      }
      while (0);
      flex_free(&savefile_data->comp_data);
    }
    FNCSaveBox_finalise(&savefile_data->super);
  }
  free(savefile_data);
  return NULL;
}
