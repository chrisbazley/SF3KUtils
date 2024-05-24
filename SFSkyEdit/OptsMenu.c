/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Options menu
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
#include <assert.h>
#include "stdlib.h"

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "menu.h"

/* My library files */
#include "err.h"
#include "Macros.h"

/* Local headers */
#include "OptsMenu.h"
#include "SkyIO.h"
#include "EditWin.h"

#ifdef FORTIFY
#include "PseudoEvnt.h"
#include "PseudoTbox.h"
#endif

/* Menu component IDs */
enum
{
  ComponentId_DitherWarn = 0x00,
  ComponentId_TrapCaret  = 0x01
};

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  ON_ERR_RPT(menu_set_tick(0, id_block->self_id, ComponentId_DitherWarn,
                           format_warning));

  ON_ERR_RPT(menu_set_tick(0, id_block->self_id, ComponentId_TrapCaret,
                           trap_caret));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int menu_selection(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Handle click on icon bar menu */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  switch (id_block->self_component)
  {
    case ComponentId_DitherWarn:
      format_warning = !format_warning;
      ON_ERR_RPT(menu_set_tick(0, id_block->self_id, ComponentId_DitherWarn,
                               format_warning));
      break;

    case ComponentId_TrapCaret:
      trap_caret = !trap_caret;
      ON_ERR_RPT(menu_set_tick(0, id_block->self_id, ComponentId_TrapCaret,
                               trap_caret));
      break;

    default:
      return 0; /* unknown menu entry */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void OptsMenu_initialise(ObjectId id)
{
  /* Listen for selections */
  EF(event_register_toolbox_handler(id, Menu_Selection, menu_selection, NULL));

  EF(event_register_toolbox_handler(id, Menu_AboutToBeShown, about_to_be_shown,
                                    NULL));
}
