/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Insertion dialogue box
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
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Pal256.h"
#include "SFFormats.h"
#include "Debug.h"
#include "GadgetUtil.h"

/* Local headers */
#include "Utils.h"
#include "EditWin.h"
#include "Insert.h"
#include "SFSInit.h"

/* Window component IDs */
enum
{
  ComponentId_NumberOfBands_NumRange = 0x03,
  ComponentId_PlainFill_Radio        = 0x18,
  ComponentId_GradatedFill_Radio     = 0x19,
  ComponentId_PlainFill_Label        = 0x15,
  ComponentId_FillColour_Button      = 0x13,
  ComponentId_FillColour_PopUp       = 0x14,
  ComponentId_EndColour_Label        = 0x1c,
  ComponentId_EndColour_Button       = 0x1a,
  ComponentId_EndColour_PopUp        = 0x1b,
  ComponentId_IncludeEnd_Option      = 0x16,
  ComponentId_StartColour_Label      = 0x0c,
  ComponentId_StartColour_Button     = 0x06,
  ComponentId_StartColour_PopUp      = 0x1d,
  ComponentId_IncludeStart_Option    = 0x17,
  ComponentId_Cancel_ActButton       = 0x00,
  ComponentId_Insert_ActButton       = 0x01
};

/* Constant numeric values */
enum
{
  DefaultStartColour = 255, /* use white when selection touches bottom */
  DefaultEndColour   = 0    /* use black when selection touches top */
};

ObjectId Insert_sharedid = NULL_ObjectId;
static int fill_colour, reset_colour, start_colour, end_colour;
static int number;
static ComponentId radio_sel;
static bool have_caret;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void reset_start_end(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  int select_start, select_end;
  EditWin_get_selection(edit_win, &select_start, &select_end);

  /* Default end colour is colour above cursor */
  end_colour = (select_end >= SFSky_Height / 2 ?
                DefaultEndColour : EditWin_get_colour(edit_win, select_end));

  set_button_colour(Insert_sharedid, ComponentId_EndColour_Button,
                    palette[end_colour]);

  /* Default is not to include the end colour */
  ON_ERR_RPT(optionbutton_set_state(0, Insert_sharedid,
                                    ComponentId_IncludeEnd_Option, 0));

  /* Default start colour is colour below the cursor */
  start_colour = (select_start == 0 ?
                  DefaultStartColour :
                  EditWin_get_colour(edit_win, select_start - 1));

  set_button_colour(Insert_sharedid,
                    ComponentId_StartColour_Button,
                    palette[start_colour]);

  /* Default is to include the start colour if we are at the bottom */
  ON_ERR_RPT(optionbutton_set_state(0, Insert_sharedid,
                                    ComponentId_IncludeStart_Option,
                                    select_start == 0));
}

/* ----------------------------------------------------------------------- */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Set up dialogue window */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    reset_start_end(client_handle);

    /* Default plain fill colour is previous value */
    fill_colour = reset_colour;

    set_button_colour(id_block->self_id, ComponentId_FillColour_Button,
                      palette[fill_colour]);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void update_grad_fill(bool sel)
{
  /* Enable/disable the gradated fill controls */
  static const ComponentId gadget_ids[] =
  {
    ComponentId_EndColour_Label,
    ComponentId_EndColour_Button,
    ComponentId_EndColour_PopUp,
    ComponentId_IncludeEnd_Option,
    ComponentId_StartColour_Label,
    ComponentId_StartColour_Button,
    ComponentId_StartColour_PopUp,
    ComponentId_IncludeStart_Option
  };
  bool fade = !sel;

  for (size_t i = 0; i < ARRAY_SIZE(gadget_ids); i++)
  {
    if (E(set_gadget_faded(Insert_sharedid, gadget_ids[i], fade)))
    {
      break;
    }
  }
}

/* ----------------------------------------------------------------------- */

static void update_plain_fill(bool sel)
{
  /* Enable/disable the plain fill controls */
  static const ComponentId gadget_ids[] =
  {
    ComponentId_PlainFill_Label,
    ComponentId_FillColour_Button,
    ComponentId_FillColour_PopUp
  };
  bool fade = !sel;

  for (size_t i = 0; i < ARRAY_SIZE(gadget_ids); i++)
  {
    if (E(set_gadget_faded(Insert_sharedid, gadget_ids[i], fade)))
    {
      break;
    }
  }
}

/* ----------------------------------------------------------------------- */

static int radiobutton_state_changed(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  const RadioButtonStateChangedEvent * const rbsce =
    (RadioButtonStateChangedEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  switch (id_block->self_component)
  {
    case ComponentId_PlainFill_Radio:
      update_plain_fill(rbsce->state);
      break;

    case ComponentId_GradatedFill_Radio:
      update_grad_fill(rbsce->state);
      break;

    default:
      return 0; /* unknown button */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int popup_about_to_be_shown(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  /* Colour picker about to pop up - set colour */
  const PopUpAboutToBeShownEvent * const puatbse =
    (PopUpAboutToBeShownEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  int colour = 0;
  switch (id_block->self_component)
  {
    case ComponentId_FillColour_PopUp:
      colour = fill_colour;
      break;

    case ComponentId_StartColour_PopUp:
      colour = start_colour;
      break;

    case ComponentId_EndColour_PopUp:
      colour = end_colour;
      break;

    default:
      return 0; /* event not handled */
  }

  ON_ERR_RPT(Pal256_set_colour(puatbse->menu_id, colour));
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

  switch (id_block->self_component)
  {
    case ComponentId_Insert_ActButton:
    {
      void *client_handle;
      if (E(toolbox_get_client_handle(0, id_block->ancestor_id,
              &client_handle)))
        break;

      EditWin * const edit_win = client_handle;

      if (E(numberrange_get_value(0, id_block->self_id,
              ComponentId_NumberOfBands_NumRange, &number)))
        break;

      if (E(radiobutton_get_state(0, id_block->self_id,
             ComponentId_PlainFill_Radio, NULL, &radio_sel)))
        break;

      switch (radio_sel)
      {
        case ComponentId_GradatedFill_Radio:
        {
          int include_start, include_end;

          if (E(optionbutton_get_state(0, id_block->self_id,
                  ComponentId_IncludeStart_Option, &include_start)))
            break;

          if (E(optionbutton_get_state(0, id_block->self_id,
                  ComponentId_IncludeEnd_Option, &include_end)))
            break;

          EditWin_insert_gradient(edit_win, number,
            start_colour, end_colour, include_start != 0, include_end != 0);
          break;
        }
        case ComponentId_PlainFill_Radio:
        {
          reset_colour = fill_colour;
          EditWin_insert_plain(edit_win, number, fill_colour);
          break;
        }
        default:
        {
          return 0; /* unknown operation */
        }
      }

      if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust))
      {
        reset_start_end(edit_win); /* Update dbox */
      }
      break;
    }
    case ComponentId_Cancel_ActButton:
    {
      if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust))
      {
        /* Reset dbox state */
        about_to_be_shown(Window_AboutToBeShown, NULL, id_block, handle);
        ON_ERR_RPT(numberrange_set_value(0, id_block->self_id,
          ComponentId_NumberOfBands_NumRange, number));

        ON_ERR_RPT(radiobutton_set_state(0, id_block->self_id, radio_sel, 1));

        update_grad_fill(radio_sel == ComponentId_GradatedFill_Radio);
        update_plain_fill(radio_sel == ComponentId_PlainFill_Radio);
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

void Insert_initialise(ObjectId object)
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
      about_to_be_shown
    },
    {
      ActionButton_Selected,
      actionbutton_selected
    },
    {
      PopUp_AboutToBeShown,
      popup_about_to_be_shown
    },
    {
      RadioButton_StateChanged,
      radiobutton_state_changed
    },
    {
      Window_HasBeenHidden,
      hand_back_caret
    }
  };

  /* Record ID of dialogue box object */
  Insert_sharedid = object;

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    /* Client handle is only used by the Window_HasBeenHidden handler */
    EF(event_register_toolbox_handler(object, tbox_handlers[i].event_code,
         tbox_handlers[i].handler, &have_caret));
  }

  EF(event_register_wimp_handler(object, -1, watch_caret, &have_caret));

  /* Store initial state of dbox */
  EF(numberrange_get_value(0, object, ComponentId_NumberOfBands_NumRange,
                           &number));

  EF(radiobutton_get_state(0, object, ComponentId_PlainFill_Radio, NULL,
                           &radio_sel));

  reset_colour = 0; /* black */
  have_caret = false;
}
/* ----------------------------------------------------------------------- */

void Insert_colour_selected(EditWin *const edit_win, ComponentId parent_component, int colour)
{
  /* User made selection from 256 colour palette */
  ComponentId button = NULL_ComponentId;

  assert(edit_win != NULL);

  /* Record new colour */
  switch (parent_component)
  {
    case ComponentId_FillColour_PopUp:
      if (fill_colour != colour)
      {
        fill_colour = colour;
        button = ComponentId_FillColour_Button;
      }
      break;

    case ComponentId_StartColour_PopUp:
      if (start_colour != colour)
      {
        int select_start;
        int inc_start;

        start_colour = colour;

        /* Should we select the 'include start colour' option? */
        EditWin_get_selection(edit_win, &select_start, NULL);
        if (select_start == 0)
          inc_start = 1; /* no preceding colour - include start colour */
        else if (start_colour == EditWin_get_colour(edit_win, select_start - 1))
          inc_start = 0; /* preceding colour is same - don't duplicate it */
        else
          inc_start = 1; /* preceding colour differs - include start colour */

        ON_ERR_RPT(optionbutton_set_state(0, Insert_sharedid,
                     ComponentId_IncludeStart_Option, inc_start));

        button = ComponentId_StartColour_Button;
      }
      break;

    case ComponentId_EndColour_PopUp:
      if (end_colour != colour)
      {
        int select_end, fol_colour;

        end_colour = colour;

        /* Should we select the 'include end colour' option? */
        EditWin_get_selection(edit_win, NULL, &select_end);
        if (select_end >= SFSky_Height / 2)
          fol_colour = 0; /* no following colour, so use black */
        else
          fol_colour = EditWin_get_colour(edit_win, select_end);

        ON_ERR_RPT(optionbutton_set_state(0, Insert_sharedid,
                     ComponentId_IncludeEnd_Option, end_colour != fol_colour));

        button = ComponentId_EndColour_Button;
      }
      break;

    default:
      break; /* unknown pop-up gadget */
  }

  if (button != NULL_ComponentId)
  {
    /* Display new colour */
    set_button_colour(Insert_sharedid, button, palette[colour]);
  }
}
