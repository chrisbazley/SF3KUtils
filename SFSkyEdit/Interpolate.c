/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Interpolation dialogue box
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
#include "stdlib.h"
#include "stdio.h"
#include <string.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "window.h"
#include "gadgets.h"

/* My library files */
#include "err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Pal256.h"

/* Local headers */
#include "Utils.h"
#include "EditWin.h"
#include "Interpolate.h"
#include "SFSInit.h"

/* Window component IDs */
enum
{
  ComponentId_EndColour_Button      = 0x09,
  ComponentId_EndColour_PopUp       = 0x0a,
  ComponentId_StartColour_Button    = 0x06,
  ComponentId_StartColour_PopUp     = 0x07,
  ComponentId_Cancel_ActButton      = 0x00,
  ComponentId_Interpolate_ActButton = 0x01
};

ObjectId Interpolate_sharedid = NULL_ObjectId;
static int start_col, end_col;
static bool have_caret;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Dialogue box about to open - set up from ancestor */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  void *client_handle;
  if (E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
    return 1;

  EditWin * const edit_win = client_handle;
  int sel_start, sel_end;
  EditWin_get_selection(edit_win, &sel_start, &sel_end);

  start_col = EditWin_get_colour(edit_win, sel_start);

  set_button_colour(id_block->self_id,
                    ComponentId_StartColour_Button,
                    palette[start_col]);

  end_col = EditWin_get_colour(edit_win, sel_end > 0 ? sel_end - 1 : 0);

  set_button_colour(id_block->self_id,
                    ComponentId_EndColour_Button,
                    palette[end_col]);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int popup_about_to_be_shown(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  /* Colour picker about to pop up - set colour */
  const PopUpAboutToBeShownEvent * const puatbs =
    (PopUpAboutToBeShownEvent *)event;
  int colour = 0;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  switch (id_block->self_component)
  {
    case ComponentId_StartColour_PopUp:
      colour = start_col;
      break;

    case ComponentId_EndColour_PopUp:
      colour = end_col;
      break;

    default:
      return 0; /* event not handled */
  }

  ON_ERR_RPT(Pal256_set_colour(puatbs->menu_id, colour));
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  const ActionButtonSelectedEvent * const abse =
    (ActionButtonSelectedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  switch (id_block->self_component)
  {
    case ComponentId_Interpolate_ActButton:
    {
      void *client_handle;
      if (E(toolbox_get_client_handle(0, id_block->ancestor_id,
              &client_handle)))
        break;

      EditWin_interpolate(client_handle, start_col, end_col);
      break;
    }
    case ComponentId_Cancel_ActButton:
    {
      if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust))
      {
        /* Reset dbox state */
        (void)about_to_be_shown(Window_AboutToBeShown, NULL, id_block, NULL);
      }
      break;
    }
    default:
    {
      return 0; /* unknown button */
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Interpolate_initialise(ObjectId object)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      ActionButton_Selected,
      actionbutton_selected
    },
    {
      PopUp_AboutToBeShown,
      popup_about_to_be_shown,
    },
    {
      Window_AboutToBeShown,
      about_to_be_shown,
    },
    {
      Window_HasBeenHidden,
      hand_back_caret,
    }
  };

  /* Record ID of dialogue box object */
  Interpolate_sharedid = object;

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    /* Client handle is only used by the Window_HasBeenHidden handler */
    EF(event_register_toolbox_handler(object,
                                      tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler,
                                      &have_caret));
  }

  EF(event_register_wimp_handler(object, -1, watch_caret, &have_caret));

  have_caret = false;
}
/* ----------------------------------------------------------------------- */

void Interpolate_colour_selected(ComponentId parent_component, int colour)
{
  ComponentId button = NULL_ComponentId;

  /* Record new colour */
  switch (parent_component)
  {
    case ComponentId_StartColour_PopUp:
      start_col = colour;
      button = ComponentId_StartColour_Button;
      break;

    case ComponentId_EndColour_PopUp:
      end_col = colour;
      button = ComponentId_EndColour_Button;
      break;

    default:
      break; /* unknown pop-up gadget */
  }

  if (button != NULL_ComponentId)
  {
    /* Display new colour */
    set_button_colour(Interpolate_sharedid,
                      button,
                      palette[colour]);
  }
}
