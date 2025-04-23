/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  File info window
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
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "fileinfo.h"
#include "event.h"

/* My library files */
#include "Err.h"
#include "Macros.h"

/* Local headers */
#include "EditWin.h"
#include "SkyIO.h"
#include "SFSFileInfo.h"

ObjectId fileinfo_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;

    /* Set up contents of file info window */
    ON_ERR_RPT(fileinfo_set_file_size(0, id_block->self_id,
      IO_estimate_sky(edit_win, EditWin_export)));

    ON_ERR_RPT(fileinfo_set_modified(0, id_block->self_id,
      EditWin_has_unsaved(edit_win)));

    ON_ERR_RPT(fileinfo_set_file_name(0, id_block->self_id,
      EditWin_get_file_path(edit_win)));

    ON_ERR_RPT(fileinfo_set_date(0, id_block->self_id,
      EditWin_get_stamp(edit_win)));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void FileInfo_initialise(ObjectId id)
{
  /* Register event handlers */
  EF(event_register_toolbox_handler(id,
                                    FileInfo_AboutToBeShown,
                                    about_to_be_shown,
                                    NULL));

  fileinfo_sharedid = id;
}
