/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  SkyCols file savebox
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

/* ISO library files */
#include "stdlib.h"
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "FileUtils.h"
#include "Debug.h"
#include "SFFormats.h"

/* Local headers */
#include "EditWin.h"
#include "SFSSaveBox.h"
#include "SkyIO.h"
#include "Utils.h"

ObjectId savebox_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int save_about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Set up dialogue box for ancestor document */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  DEBUGF("About to show savebox 0x%x with ancestor 0x%x\n",
         (unsigned)id_block->self_id, (unsigned)id_block->ancestor_id);

  assert(id_block->ancestor_id != NULL_ObjectId);
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    int sel_start, sel_end;

    /* Default file name is the full path under which this file was last saved
     */
    char *filename = EditWin_get_file_path(edit_win);
    if (filename == NULL)
    {
      /* This file has not been saved before, so invent a suitable leaf name */
      filename = msgs_lookup("LeafName2");
    }
    ON_ERR_RPT(saveas_set_file_name(0, savebox_sharedid, filename));

    EditWin_get_selection(edit_win, &sel_start, &sel_end);
    ON_ERR_RPT(saveas_selection_available(0, savebox_sharedid, sel_start != sel_end));

    ON_ERR_RPT(saveas_set_file_size(0, savebox_sharedid,
      IO_estimate_sky(edit_win, EditWin_export)));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_to_file(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  SaveAsSaveToFileEvent * const sastf = (SaveAsSaveToFileEvent *)event;
  unsigned int flags = 0;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  DEBUGF("Save %sto file %s\n",
        (sastf->hdr.flags & SaveAs_SelectionBeingSaved) ? "selection " : "",
        sastf->filename);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    IOExportSkyFn *fn = EditWin_export;

    if ((sastf->hdr.flags & SaveAs_SelectionBeingSaved) != 0)
    {
      fn = EditWin_export_sel;
    }

    if (IO_export_sky_file(edit_win, sastf->filename, fn))
    {
      flags = SaveAs_SuccessfulSave;
    }
  }

  saveas_file_save_completed(flags, id_block->self_id, sastf->filename);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_completed(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const SaveAsSaveCompletedEvent * const sasc = (SaveAsSaveCompletedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  DEBUGF("Saved %sto %sfile %s\n",
        (sasc->hdr.flags & SaveAs_SelectionSaved) ? "selection " : "",
        (sasc->hdr.flags & SaveAs_DestinationSafe) ? "safe " : "",
        sasc->filename);

  /* We cannot consider the sky as having no more unsaved changes if only
     the current selection was was saved. */
  if (TEST_BITS(sasc->hdr.flags, SaveAs_DestinationSafe) &&
      !TEST_BITS(sasc->hdr.flags, SaveAs_SelectionSaved))
  {
    void *client_handle;
    char *buf = NULL;
    if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)) &&
        !E(canonicalise(&buf, NULL, NULL, sasc->filename)))
    {
      /* Mark data as saved */
      EditWin_file_saved(client_handle, buf);
      free(buf);
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void SaveFile_initialise(ObjectId id)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      SaveAs_AboutToBeShown,
      save_about_to_be_shown
    },
    {
      SaveAs_SaveCompleted,
      save_completed
    },
    {
      SaveAs_SaveToFile,
      save_to_file
    }
  };

  /* Register Toolbox event handlers. */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    EF(event_register_toolbox_handler(id,
                                      tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler,
                                      NULL));
  }
  savebox_sharedid = id;
}
