/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Utility functions
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
#include "stdio.h"
#include <string.h>
#include <stdbool.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "window.h"
#include "wimplib.h"
#include "wimp.h"
#include "gadgets.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "Debug.h"
#include "DeIconise.h"
#include "WimpExtra.h"
#include "PalEntry.h"

/* Local headers */
#include "Utils.h"
#include "EditWin.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Constant numeric values */
enum
{
  ValidationMaxLen     = 15,
  ShowRelativeXOffset  = 64, /* Open dialogue boxes slightly to the right of
                                the main editing window */
  ShowRelativeYOffset  = -64, /* Open dialogue boxes slightly below the top of
                                 the main editing window */
  CaretNoWindow        = -1,
  SpriteBitsPerWord = 32,
};

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void show_object_relative(unsigned int const flags, ObjectId const showobj,
  ObjectId const relativeto, ObjectId const parent,
  ComponentId const parent_component)
{

  DEBUGF("Showing object 0x%x relative to 0x%x, with parent 0x%x/0x%x\n",
         showobj, relativeto, parent, parent_component);

  WimpGetWindowStateBlock winstate;
  ON_ERR_RPT_RTN(window_get_wimp_handle(0, relativeto,
    &winstate.window_handle));

  ON_ERR_RPT_RTN(wimp_get_window_state(&winstate));

  WindowShowObjectBlock showblock =
  {
    .visible_area.xmin = winstate.visible_area.xmin + ShowRelativeXOffset,
    .visible_area.ymin = winstate.visible_area.ymax + ShowRelativeYOffset,
  };

  ON_ERR_RPT(DeIconise_show_object(flags, showobj, Toolbox_ShowObject_TopLeft,
                                   &showblock, parent, parent_component));
}

/* ----------------------------------------------------------------------- */

int watch_caret(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  bool *have_caret = handle;

  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  switch (event_code)
  {
    case Wimp_ELoseCaret:
      *have_caret = false;
      break;

    case Wimp_EGainCaret:
      *have_caret = true;
      break;

    default:
      return 0; /* pass event on */
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

int hand_back_caret(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Is our ancestor alive and not hidden? */
  unsigned int anc_state;
  const bool *focus = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (toolbox_get_object_state(0, id_block->ancestor_id, &anc_state) == NULL &&
      TEST_BITS(anc_state, Toolbox_GetObjectState_Showing))
  {
    /* Did we have the input focus? */
    if (*focus == true)
    {
      WimpGetCaretPositionBlock now_pos;

      /* Is there now no input focus anywhere on the desktop? */
      if (!E(wimp_get_caret_position(&now_pos)) &&
          now_pos.window_handle == CaretNoWindow)
      {
        /* No - pass the focus back to ancestor window */
        void *client_handle;
        if (!E(toolbox_get_client_handle(0, id_block->ancestor_id,
               &client_handle)))
        {
          EditWin_give_focus(client_handle);
        }
      }
    }
  }

  return 0; /* pass event on */
}

/* ----------------------------------------------------------------------- */

void hide_shared_if_child(ObjectId const parent_id, ObjectId const shared_id)
{
  ObjectId ancestor;

  ON_ERR_RPT_RTN(toolbox_get_ancestor(0, shared_id, &ancestor, NULL));
  DEBUGF("Ancestor of 0x%x is 0x%x (sought 0x%x)\n",
         shared_id, ancestor, parent_id);

  if (ancestor == parent_id)
    ON_ERR_RPT(DeIconise_hide_object(0, shared_id));
}

/* ----------------------------------------------------------------------- */

bool showing_as_descendant(ObjectId const self_id, ObjectId const ancestor_id)
{
  if (self_id != NULL_ObjectId) /* object doesn't exist? */
  {
    /* Now check what the ancestor of the specified object is */
    ObjectId actual_ancestor_id;
    if (!E(toolbox_get_ancestor(0, self_id, &actual_ancestor_id, NULL)))
    {
      DEBUGF("Ancestor is object 0x%x (looking for 0x%x)\n", actual_ancestor_id,
             ancestor_id);

      /* Is it the ancestor we were looking for? */
      return actual_ancestor_id == ancestor_id;
    }
  }
  else
  {
    DEBUGF("Null object ID\n");
  }

  return false; /* not showing */
}

/* ----------------------------------------------------------------------- */

void set_button_colour(ObjectId const window, ComponentId const button,
  PaletteEntry const colour)
{
  char validation[ValidationMaxLen + 1];
  sprintf(validation, "r2;C/%X", colour >> PaletteEntry_RedShift);
  ON_ERR_RPT(button_set_validation(0, window, button, validation));
}

/* ----------------------------------------------------------------------- */

bool claim_drag(const WimpMessage *const message, int const file_types[],
  int *const my_ref)
{
  /* Claim a drag for ourselves */
  assert(message != NULL);
  assert(file_types != NULL);
  DEBUGF("Replying to message ref %d from task 0x%x with a DragClaim message\n",
        message->hdr.my_ref, message->hdr.sender);

  WimpMessage reply = {
    .hdr = {
      .your_ref = message->hdr.my_ref,
      .action_code = Wimp_MDragClaim,
    },
  };

  WimpDragClaimMessage *const dragclaim = (WimpDragClaimMessage *)&reply.data;
  dragclaim->flags = 0;

  size_t const array_len = copy_file_types(dragclaim->file_types, file_types,
    ARRAY_SIZE(dragclaim->file_types) - 1) + 1;

  reply.hdr.size = WORD_ALIGN(sizeof(reply.hdr) +
    offsetof(WimpDragClaimMessage, file_types) +
    (sizeof(dragclaim->file_types[0]) * array_len));

  bool success = false;

  if (!E(wimp_send_message(Wimp_EUserMessage, &reply, message->hdr.sender,
       0, NULL)))
  {
    success = true;
    DEBUGF("DragClaim message ref. is %d\n", reply.hdr.my_ref);
  }

  if (my_ref != NULL)
  {
    *my_ref = success ? reply.hdr.my_ref : 0;
  }

  return success;
}

/* ----------------------------------------------------------------------- */

int sprite_right_bit(int const width, int const bpp)
{
  assert(width > 0);
  assert(bpp > 0);
  int const spare_bits = (bpp * width) % SpriteBitsPerWord;
  return (spare_bits > 0 ? spare_bits : SpriteBitsPerWord) - 1;
}
