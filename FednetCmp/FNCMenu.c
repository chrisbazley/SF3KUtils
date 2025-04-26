/*
 *  FednetCmp - Fednet file compression/decompression
 *  Iconbar menu
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
#include <stddef.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "menu.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "ViewsMenu.h"

/* Local headers */
#include "FNCMenu.h"
#include "FNCIconbar.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

/* Menu component IDs */
enum
{
  ComponentId_Windows           = 0x03,
  ComponentId_MultipleSaveBoxes = 0x04
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

  ON_ERR_RPT(menu_set_tick(0,
                           id_block->self_id,
                           ComponentId_MultipleSaveBoxes,
                           Iconbar_get_multi_dboxes()));

  return 0; /* pass event on (to ViewsMenu) */
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

  if (id_block->self_component != ComponentId_MultipleSaveBoxes)
    return 0; /* event not handled */

  /* Update menu entry tick (doesn't do it automatically!) */
  bool const multi_saveboxes = !Iconbar_get_multi_dboxes();

  ON_ERR_RPT(menu_set_tick(0,
                           id_block->self_id,
                           ComponentId_MultipleSaveBoxes,
                           (int)multi_saveboxes));

  Iconbar_set_multi_dboxes(multi_saveboxes);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Menu_initialise(ObjectId id)
{
  /* Listen for selections */
  EF(event_register_toolbox_handler(id,
                                    Menu_Selection,
                                    menu_selection,
                                    0));

  EF(event_register_toolbox_handler(id,
                                    Menu_AboutToBeShown,
                                    about_to_be_shown,
                                    0));

  EF(ViewsMenu_parentcreated(id, ComponentId_Windows));
}
