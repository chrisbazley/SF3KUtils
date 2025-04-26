/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Menu attached to sky window (all levels)
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
#include <stdbool.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "menu.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "Pal256.h"
#include "Debug.h"
#include "SFFormats.h"

/* Local headers */
#include "EditWin.h"
#include "SFSFileInfo.h"
#include "SFSSaveBox.h"
#include "Interpolate.h"
#include "Insert.h"
#include "DCS_dialogue.h"
#include "Menus.h"
#include "SkyIO.h"
#include "Picker.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Menu component IDs */
enum
{
  ComponentId_Edit_ClearSelection = 0x03,
  ComponentId_Edit_Delete         = 0x06,
  ComponentId_Edit_Copy           = 0x07,
  ComponentId_Edit_Cut            = 0x08,
  ComponentId_Edit_Paste          = 0x09,
  ComponentId_Edit_SelectAll      = 0x0a,
  ComponentId_Edit_Insert         = 0x0b,
  ComponentId_Edit_Undo           = 0x0c,
  ComponentId_Edit_Redo           = 0x0d,
};

enum
{
  ComponentId_Effect_SetColour    = 0x01,
  ComponentId_Effect_Interpolate  = 0x05,
  ComponentId_Effect_Smooth       = 0x04
};

enum
{
  SetColourMinSelect   = 1,
  InterpolateMinSelect = 2,
  SmoothMinSelect      = 3
};

ObjectId EditMenu_sharedid = NULL_ObjectId,
         EffectMenu_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int root_menu_about_to_be_shown(int const event_code, ToolboxEvent *const event,
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

static int edit_menu_about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(handle);
  assert(id_block != NULL);
  NOT_USED(event);
  NOT_USED(event_code);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditMenu_update(client_handle);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int effect_menu_submenu(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (id_block->self_component != ComponentId_Effect_SetColour)
  {
    return 0; /* event not handled */
  }

  /* Is a submenu warning for the 'Set colour' menu entry */
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    int sel_start, sel_end;
    EditWin_get_selection(edit_win, &sel_start, &sel_end);
    if (sel_start != sel_end)
    {
      ON_ERR_RPT(Pal256_set_colour(Picker_sharedid,
        EditWin_get_colour(edit_win, sel_start)));
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int effect_menu_about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(handle);
  assert(id_block != NULL);
  NOT_USED(event);
  NOT_USED(event_code);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EffectMenu_update(client_handle);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void RootMenu_initialise(ObjectId id)
{
  /* Register Toolbox event handlers */
  EF(event_register_toolbox_handler(id, Menu_AboutToBeShown,
    root_menu_about_to_be_shown, (void *)NULL));
}

/* ----------------------------------------------------------------------- */

void EditMenu_initialise(ObjectId id)
{
  /* Register Toolbox event handlers */
  EF(event_register_toolbox_handler(id, Menu_AboutToBeShown,
    edit_menu_about_to_be_shown, (void *)NULL));

  EditMenu_sharedid = id;
}

/* ----------------------------------------------------------------------- */

void EditMenu_fade_paste(bool const cb_valid)
{
  /* Fade the 'Paste' entry in the 'Edit' menu if the clipboard data is not
     available in any of the file types that we can import */
  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Paste,
    !cb_valid));
}

/* ----------------------------------------------------------------------- */

void EffectMenu_initialise(ObjectId id)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tb_handlers[] =
  {
    {
      Menu_AboutToBeShown,
      effect_menu_about_to_be_shown
    },
    {
      Menu_SubMenu,
      effect_menu_submenu
    }
  };

  for (size_t i = 0; i < ARRAY_SIZE(tb_handlers); i++)
  {
    /* Register toolbox event handlers */
    EF(event_register_toolbox_handler(id, tb_handlers[i].event_code,
      tb_handlers[i].handler, (void *)NULL));
  }

  EffectMenu_sharedid = id;
}

/* ----------------------------------------------------------------------- */

void EditMenu_update(EditWin *const edit_win)
{
  IO_update_can_paste(edit_win);

  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Undo,
    !EditWin_can_undo(edit_win)));

  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Redo,
    !EditWin_can_redo(edit_win)));

  int sel_start, sel_end;
  EditWin_get_selection(edit_win, &sel_start, &sel_end);

  /* If full selection then prevent select all */
  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_SelectAll,
    sel_end - sel_start >= SFSky_Height / 2));

  /* Prevent operations on selection if none */
  static const ComponentId sel_items[] =
  {
    ComponentId_Edit_ClearSelection,
    ComponentId_Edit_Cut,
    ComponentId_Edit_Copy,
    ComponentId_Edit_Delete
  };
  bool const no_sel = (sel_start == sel_end);

  for (size_t i = 0; i < ARRAY_SIZE(sel_items); i++)
  {
    ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, sel_items[i], no_sel));
  }

  /* Prevent insertion if caret at end of file */
  bool const no_room = (no_sel && sel_start >= SFSky_Height / 2);
  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Insert,
    no_room));

  /* Prevent paste if no clipboard contents or caret at end of file */
  ON_ERR_RPT(menu_set_fade(0, EditMenu_sharedid, ComponentId_Edit_Paste,
    !EditWin_can_paste(edit_win)));
}

/* ----------------------------------------------------------------------- */

void EffectMenu_update(EditWin *const edit_win)
{
  /* Prevent operations if none selected (or not enough bands) */
  static const struct
  {
    ComponentId entry;
    int min_sel;
  }
  items_to_fade[] =
  {
    {
      ComponentId_Effect_SetColour,
      SetColourMinSelect
    },
    {
      ComponentId_Effect_Interpolate,
      InterpolateMinSelect
    },
    {
      ComponentId_Effect_Smooth,
      SmoothMinSelect
    }
  };
  int sel_start, sel_end, sel_len;

  EditWin_get_selection(edit_win, &sel_start, &sel_end);

  assert(sel_end >= sel_start);
  sel_len = sel_end - sel_start;

  for (size_t i = 0; i < ARRAY_SIZE(items_to_fade); i++)
  {
    ON_ERR_RPT(menu_set_fade(0, EffectMenu_sharedid, items_to_fade[i].entry,
      sel_len < items_to_fade[i].min_sel));
  }
}
