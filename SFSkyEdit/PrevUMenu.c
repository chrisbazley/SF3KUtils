/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Menu attached to preview window
 *  Copyright (C) 2009 Christopher Bazley
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
#include "menu.h"

/* My library files */
#include "err.h"
#include "Macros.h"

/* Local headers */
#include "PrevUMenu.h"
#include "Preview.h"

/* Menu component IDs */
enum
{
  ComponentId_Toolbars = 0x02
};

ObjectId PrevUMenu_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  void *client_handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
    PrevUMenu_set_toolbars(Preview_get_toolbars(client_handle));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void PrevUMenu_initialise(ObjectId id)
{
  /* Register Toolbox event handlers */
  EF(event_register_toolbox_handler(id,
                                    Menu_AboutToBeShown,
                                    about_to_be_shown,
                                    NULL));
  PrevUMenu_sharedid = id;
}

/* ----------------------------------------------------------------------- */

void PrevUMenu_set_toolbars(bool shown)
{
  /* Set item tick according to state of relevant flag */
  ON_ERR_RPT(menu_set_tick(0,
                           PrevUMenu_sharedid,
                           ComponentId_Toolbars,
                           (int)shown));
}
