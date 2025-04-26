/*
 *  SFColours - Star Fighter 3000 colours editor
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
#include <stddef.h>
#include <stdbool.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "window.h"
#include "wimplib.h"
#include "wimp.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Debug.h"
#include "DeIconise.h"
#include "WimpExtra.h"

/* Local headers */
#include "Utils.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Constant numeric values */
enum
{
  ShowRelativeXOffset  = 64, /* Open dialogue boxes slightly to the right of the
                                main editing window */
  ShowRelativeYOffset  = -64, /* Open dialogue boxes slightly below the top of
                                 the main editing window */
};

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void show_object_relative(unsigned int flags, ObjectId showobj, ObjectId relativeto, ObjectId parent, ComponentId parent_component)
{
  WimpGetWindowStateBlock winstate;
  WindowShowObjectBlock showblock;

  DEBUGF("Showing object 0x%x relative to 0x%x, with parent 0x%x/0x%x\n",
         showobj, relativeto, parent, parent_component);

  ON_ERR_RPT_RTN(window_get_wimp_handle(0, relativeto, &winstate.window_handle));
  ON_ERR_RPT_RTN(wimp_get_window_state(&winstate));

  showblock.visible_area.xmin = winstate.visible_area.xmin +
                                ShowRelativeXOffset;

  showblock.visible_area.ymin = winstate.visible_area.ymax +
                                ShowRelativeYOffset;

  ON_ERR_RPT(DeIconise_show_object(flags,
                                   showobj,
                                   Toolbox_ShowObject_TopLeft,
                                   &showblock,
                                   parent,
                                   parent_component));
}

/* ----------------------------------------------------------------------- */

bool showing_as_descendant(ObjectId self_id, ObjectId ancestor_id)
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

void scr_to_work_area_coords(int window_handle, int *x, int *y)
{
  /* Translate screen coordinates to window work area */
  WimpGetWindowStateBlock window_state =
  {
    window_handle,
    {0,0,0,0},
    0,
    0,
    WimpWindow_Top,
    0
  };

  DEBUGF("Screen coordinates are %d,%d\n", x == NULL ? 0 : *x, y == NULL ? 0 : *y);
  if (!E(wimp_get_window_state(&window_state)))
  {
    if (x != NULL)
      *x -= window_state.visible_area.xmin - window_state.xscroll;

    if (y != NULL)
      *y -= window_state.visible_area.ymax - window_state.yscroll;

    DEBUGF("Work area coordinates are %d,%d\n", x == NULL ? 0 : *x,
          y == NULL ? 0 : *y);
  }
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

  WimpMessage reply;
  reply.hdr.your_ref = message->hdr.my_ref;
  reply.hdr.action_code = Wimp_MDragClaim;

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
