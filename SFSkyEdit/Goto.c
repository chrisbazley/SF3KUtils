/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Goto dialogue box
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
#include <stddef.h>
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "window.h"
#include "gadgets.h"
#include "event.h"

/* My library files */
#include "err.h"
#include "Macros.h"

/* Local headers */
#include "Goto.h"
#include "EditWin.h"

/* Window component IDs */
enum
{
  ComponentId_ColourBand_NumRange = 0,
  ComponentId_Cancel_ActButton    = 2,
  ComponentId_Go_ActButton        = 3
};

ObjectId Goto_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void reset_dbox(EditWin *const edit_win)
{
  int start;

  /* Ensure that the value displayed reflects the current caret position
     (or low boundary of selection) within the specified editing window */
  assert(edit_win != NULL);
  EditWin_get_selection(edit_win, &start, NULL);
  ON_ERR_RPT(numberrange_set_value(0, Goto_sharedid,
               ComponentId_ColourBand_NumRange, start));
}

/* ----------------------------------------------------------------------- */

static int goto_about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  void *client_handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Ensure that the value initially displayed reflects the caret position
     within the editing window which is an ancestor of this dialogue box */
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    reset_dbox(client_handle);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int goto_actionbutton_selected(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  void *client_handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    EditWin * const edit_win = client_handle;
    int value;

    switch (id_block->self_component)
    {
      case ComponentId_Cancel_ActButton:
        /* Reset the dialogue box so that it reverts to displaying the current
           caret position (in case the dbox is not about to be hidden) */
        reset_dbox(edit_win);
        break;

      case ComponentId_Go_ActButton:
       /* Move the caret to the specified position in the editing window which
          is an ancestor of this dialogue box */
        if (E(numberrange_get_value(0, id_block->self_id,
                ComponentId_ColourBand_NumRange, &value)))
          break;

        EditWin_set_caret_pos(edit_win, value);
        EditWin_give_focus(edit_win);
        break;

      default:
        return 0; /* not interested in this component */
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Goto_initialise(ObjectId id)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      Window_AboutToBeShown,
      goto_about_to_be_shown
    },
    {
      ActionButton_Selected,
      goto_actionbutton_selected
    }
  };

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    /* Client handle is only used by the Window_HasBeenHidden handler */
    EF(event_register_toolbox_handler(id, tbox_handlers[i].event_code,
         tbox_handlers[i].handler, NULL));
  }

  Goto_sharedid = id;
}
