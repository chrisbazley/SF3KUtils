/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Discard/Cancel/Save dialogue box
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
#include <stdbool.h>
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "dcs.h"

/* My library files */
#include "Macros.h"
#include "Err.h"
#include "InputFocus.h"

/* Local headers */
#include "DCS_dialogue.h"
#include "EditWin.h"
#include "Utils.h"

static ObjectId dbox_id = NULL_ObjectId;
static bool dcs_open_parent = false;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int dcs_save(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  void *client_handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (!E(toolbox_get_client_handle(0,
                                  id_block->ancestor_id,
                                  &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    EditWin_do_save(edit_win, true, dcs_open_parent);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int dcs_discard(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  void *client_handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (!E(toolbox_get_client_handle(0,
                                   id_block->ancestor_id,
                                   &client_handle)))
  {
    EditWin * const edit_win = client_handle;

    if (dcs_open_parent) /* (set if ADJUST-click on close icon) */
      EditWin_show_parent_dir(edit_win); /* open parent directory */

    EditWin_destroy(edit_win);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void DCS_initialise(ObjectId id)
{
  /* Install event handlers */
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      DCS_AboutToBeShown,
      InputFocus_recordcaretpos
    },
    {
      DCS_Save,
      dcs_save
    },
    {
      DCS_Discard,
      dcs_discard
    }
  };

  /* Record ID */
  dbox_id = id;

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    EF(event_register_toolbox_handler(id,
                                      tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler,
                                      NULL));
  }
}

/* ----------------------------------------------------------------------- */

void DCS_query_unsaved(ObjectId view, bool open_parent)
{
  show_object_relative(Toolbox_ShowObject_AsMenu,
                       dbox_id,
                       view,
                       view,
                       NULL_ComponentId);
  dcs_open_parent = open_parent;
}
