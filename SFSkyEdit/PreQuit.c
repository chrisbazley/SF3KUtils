/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Quit confirm dialogue box
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
#include "stdio.h"
#include <stdbool.h>

/* RISC OS library files */
#include "event.h"
#include "toolbox.h"
#include "quit.h"
#include "wimplib.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "InputFocus.h"
#include "Entity2.h"
#include "Debug.h"
#include "UserData.h"

/* Local headers */
#include "PreQuit.h"

/* Constant numeric values */
enum
{
  WimpKey_CtrlShiftF12 = 0x1FC,
  MaxUnsavedCountLen   = 15
};

static ObjectId dbox_id = NULL_ObjectId;
static int prequit_sender;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void cb_released(void)
{
  DEBUGF("Clipboard released - terminating\n");
  exit(EXIT_SUCCESS);
}

/* ----------------------------------------------------------------------- */

static int quit(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event);
  NOT_USED(event_code);
  NOT_USED(id_block);
  NOT_USED(handle);

  DEBUGF("User chose to quit (and lose unsaved data)\n");

  /* We won't be alive to hear the MenusDeleted msg, so fake it */
  ON_ERR_RPT(InputFocus_restorecaret());

  /* Do as Paint, Edit and Draw do: Discard all data and restart the desktop
     shutdown. When we receive another PreQuit message, we will no longer
     have unsaved data so we won't acknowledge it. */
  userdata_destroy_all();

  if (prequit_sender)
  {
    WimpKeyPressedEvent key_event;

    /* Restart desktop shutdown */
    if (!E(wimp_get_caret_position(&key_event.caret)))
    {
      key_event.key_code = WimpKey_CtrlShiftF12;
      DEBUGF("Sending event (w:%d i:%d x:%d y:%d) to task %d to restart desktop shutdown\n",
            key_event.caret.window_handle, key_event.caret.icon_handle,
            key_event.caret.xoffset, key_event.caret.yoffset, prequit_sender);

      ON_ERR_RPT(wimp_send_message(Wimp_EKeyPressed, &key_event, prequit_sender,
                          0, NULL));
    }
  }
  else
  {
    /* We may own the global clipboard, so offer the associated data to
       any 'holder' application before exiting. */
    ON_ERR_RPT(entity2_dispose_all(cb_released));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void PreQuit_initialise(ObjectId id)
{
  /* Record ID */
  dbox_id = id;

  /* Install handlers */
  EF(event_register_toolbox_handler(id, Quit_Quit, quit, NULL));
  EF(event_register_toolbox_handler(id, Quit_AboutToBeShown, InputFocus_recordcaretpos, NULL));
}

/* ----------------------------------------------------------------------- */

bool PreQuit_queryunsaved(int task_handle)
{
  /* Return true from this function in order to prevent immediate quit */
  unsigned int const unsaved_count = userdata_count_unsafe();

  DEBUGF("%u files have unsaved changes\n", unsaved_count);

  if (unsaved_count > 1)
  {
    /* Many files have unsaved modifications */
    char buffer[MaxUnsavedCountLen + 1];
    sprintf(buffer, "%u", unsaved_count);
    ON_ERR_RPT(quit_set_message(0, dbox_id, msgs_lookup_subn("PlurUNS", 1, buffer)));
  }
  else if (unsaved_count > 0)
  {
    /* A single file has unsaved modifications */
    ON_ERR_RPT(quit_set_message(0, dbox_id, msgs_lookup("SingUNS")));
  }
  else
  {
    /* No files have unsaved modifications */
    return false; /* may quit */
  }

  DEBUGF("Opening quit/cancel dialogue box (for %s)\n", prequit_sender ?
        "shutdown" : "task quit");

  ON_ERR_RPT(toolbox_show_object(Toolbox_ShowObject_AsMenu, dbox_id,
                                 Toolbox_ShowObject_Centre, NULL,
                                 NULL_ObjectId, NULL_ComponentId));
  prequit_sender = task_handle;

  return true; /* cannot quit whilst dialogue box is open */
}
