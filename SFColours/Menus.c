/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Menu attached to colours window (all levels)
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
#include <stdbool.h>
#include <limits.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "menu.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "Pal256.h"
#include "Debug.h"
#include "EventExtra.h"

/* Local headers */
#include "Menus.h"
#include "EditWin.h"
#include "Picker.h"
#include "ColsIO.h"

#ifdef FORTIFY
#include "PseudoEvnt.h"
#include "PseudoTbox.h"
#endif

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Menu component IDs */
enum
{
  ComponentId_Edit_ClearSelection = 0x03,
  ComponentId_Edit_SelectAll      = 0x05,
  ComponentId_Edit_Copy           = 0x06,
  ComponentId_Edit_Paste          = 0x07,
  ComponentId_Edit_Undo           = 0x0c,
  ComponentId_Edit_Redo           = 0x0d,
};

enum
{
  ComponentId_Effect_SetColour    = 0x01,
  ComponentId_Effect_Smooth       = 0x04
};

ObjectId EditMenu_sharedid = NULL_ObjectId,
         EffectMenu_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int root_menu_show_handler(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(handle);
  assert(id_block != NULL);
  NOT_USED(event);
  NOT_USED(event_code);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    /* Not all versions of the Toolbox seem to update menus after they have
       been shown. This call can complete asynchronously, which is too late
       if it is only called when the Edit menu is about to be shown. */
    IO_update_can_paste(client_handle);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int effect_submenu_handler(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (id_block->self_component != ComponentId_Effect_SetColour)
    return 0; /* event not handled */

  /* Is a submenu warning for the 'Set colour' menu entry */
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    int const index = EditWin_get_next_selected(edit_win, -1);
    if (index >= 0)
    {
      ON_ERR_RPT(Pal256_set_colour(Picker_sharedid,
        EditWin_get_colour(edit_win, index)));
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int edit_menu_show_handler(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(handle);
  assert(id_block != NULL);
  NOT_USED(event);
  NOT_USED(event_code);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
    EditMenu_update(client_handle);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int effect_menu_show_handler(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
    EffectMenu_update(client_handle);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void RootMenu_initialise(ObjectId id)
{
  EF(event_register_toolbox_handler(id,
                                    Menu_AboutToBeShown,
                                    root_menu_show_handler,
                                    (void *)NULL));
}

/* ----------------------------------------------------------------------- */

void EditMenu_initialise(ObjectId id)
{
  EditMenu_sharedid = id;

  EF(event_register_toolbox_handler(id,
                                    Menu_AboutToBeShown,
                                    edit_menu_show_handler,
                                    (void *)NULL));
}

/* ----------------------------------------------------------------------- */

void EditMenu_update(EditWin *const edit_win)
{
  IO_update_can_paste(edit_win);

  int num_selectable;
  int const num_selected = EditWin_get_num_selected(edit_win, &num_selectable);

  ON_ERR_RPT(menu_set_fade(0,
                           EditMenu_sharedid,
                           ComponentId_Edit_Undo,
                           !EditWin_can_undo(edit_win)));

  ON_ERR_RPT(menu_set_fade(0,
                           EditMenu_sharedid,
                           ComponentId_Edit_Redo,
                           !EditWin_can_redo(edit_win)));

  /* If full selection then prevent select all */
  ON_ERR_RPT(menu_set_fade(0,
                           EditMenu_sharedid,
                           ComponentId_Edit_SelectAll,
                           num_selected >= num_selectable));

  /* If no selection then prevent clear selection or copy */
  ON_ERR_RPT(menu_set_fade(0,
                           EditMenu_sharedid,
                           ComponentId_Edit_ClearSelection,
                           num_selected < 1));

  ON_ERR_RPT(menu_set_fade(0,
                           EditMenu_sharedid,
                           ComponentId_Edit_Copy,
                           num_selected < 1));

  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Paste,
                  !EditWin_can_paste(edit_win)));
}

/* ----------------------------------------------------------------------- */

void EffectMenu_initialise(ObjectId id)
{
  EffectMenu_sharedid = id;
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tb_handlers[] =
  {
    {
      Menu_AboutToBeShown,
      effect_menu_show_handler
    },
    {
      Menu_SubMenu,
      effect_submenu_handler
    }
  };

  for (size_t i = 0; i < ARRAY_SIZE(tb_handlers); i++)
  {
    /* Register toolbox event handlers */
    EF(event_register_toolbox_handler(id,
                                      tb_handlers[i].event_code,
                                      tb_handlers[i].handler,
                                      &EffectMenu_sharedid));
  }
}

/* ----------------------------------------------------------------------- */

void EffectMenu_update(EditWin *const edit_win)
{
  unsigned int const num_selected = EditWin_get_num_selected(edit_win, NULL);

  /* If no selection exists prevent setting actual colour */
  ON_ERR_RPT(menu_set_fade(0,
                           EffectMenu_sharedid,
                           ComponentId_Effect_SetColour,
                           num_selected < 1));

  /* If less than 3 logical colours selected then prevent interpolation */
  ON_ERR_RPT(menu_set_fade(0,
                           EffectMenu_sharedid,
                           ComponentId_Effect_Smooth,
                           num_selected < 3));
}
