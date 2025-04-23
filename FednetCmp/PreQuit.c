/*
 *  FednetCmp - Fednet file compression/decompression
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

#include "stdlib.h"
#include <string.h>
#include "stdio.h"
#include <assert.h>

/* RISC OS library files */
#include "event.h"
#include "toolbox.h"
#include "quit.h"
#include "wimplib.h"
#include "wimp.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Scheduler.h"
#include "InputFocus.h"
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
static int quit_sender, window_handle;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  NOT_USED(handle);

  scheduler_suspend(); /* freeze all directory scans */

  return 0; /* pass event on (to InputFocus_recordcaretpos) */
}

/* ----------------------------------------------------------------------- */

static int quit(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  NOT_USED(handle);

  /* We won't be alive to hear the MenusDeleted msg, so fake it */
  EF(InputFocus_restorecaret());

  userdata_destroy_all();

  if (quit_sender == 0)
  {
    /* Quit application */
    exit(EXIT_SUCCESS);
  }
  else
  {
    /* Restart desktop shutdown. When we receive another PreQuit message,
       we will no longer have unsaved data so we won't acknowledge it. */
    WimpKeyPressedEvent key_event;

    EF(wimp_get_caret_position(&key_event.caret));
    key_event.key_code = WimpKey_CtrlShiftF12;
    EF(wimp_send_message(Wimp_EKeyPressed, &key_event, quit_sender, 0, NULL));

#ifdef QUIT_ON_SHUTDOWN
    /* Quit immediately */
    exit(EXIT_SUCCESS);
#endif
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int menus_deleted(WimpMessage *const message, void *const handle)
{
  /* 'Menu tree' has been closed - is menu block our wimp window? */
  assert(message != NULL);
  NOT_USED(handle);

  if (message->data.words[0] == window_handle)
    scheduler_resume(); /* yes - resume all directory scans */

  return 0; /* pass the event on to other handlers */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void PreQuit_initialise(ObjectId id)
{
  static const struct
  {
    int                  event_code;
    ToolboxEventHandler *handler;
  }
  tb_handlers[] =
  {
    {
      Quit_AboutToBeShown,
      InputFocus_recordcaretpos
    },
    {
      Quit_AboutToBeShown,
      about_to_be_shown
    },
    {
      Quit_Quit,
      quit
    }
  };
  ObjectId window_id;

  dbox_id = id;

  /* Register toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tb_handlers); i++)
  {
    EF(event_register_toolbox_handler(id,
                                      tb_handlers[i].event_code,
                                      tb_handlers[i].handler,
                                      NULL));
  }

  /* Use Wimp_MMenusDeleted rather than Quit_DialogueCompleted to work around
     a bug where the Window_HasBeenHidden-like events aren't delivered for
     Toolbox objects shown with Wimp_CreateMenu semantics, if the version of
     !Help supplied with RISC OS 4 is running. */
  EF(quit_get_window_id(0, id, &window_id));
  EF(window_get_wimp_handle(0, window_id, &window_handle));
  EF(event_register_message_handler(Wimp_MMenusDeleted, menus_deleted, NULL));
}

/* ----------------------------------------------------------------------- */

bool PreQuit_queryunsaved(int task_handle)
{
  /* Return true from this function in order to prevent immediate quit */
  unsigned int const unfinished_count = userdata_count_unsafe();

  DEBUGF("%u scans are still in progress\n", unfinished_count);

  if (unfinished_count > 1)
  {
    char buffer[MaxUnsavedCountLen + 1];
    sprintf(buffer, "%d", unfinished_count);
    ON_ERR_RPT(quit_set_message(0, dbox_id, msgs_lookup_subn("PlurUNS", 1, buffer)));
  }
  else if (unfinished_count > 0)
  {
    ON_ERR_RPT(quit_set_message(0, dbox_id, msgs_lookup("SingUNS")));
  }
  else
  {
    return false; /* may quit */
  }

  ON_ERR_RPT(toolbox_show_object(Toolbox_ShowObject_AsMenu,
                                 dbox_id,
                                 Toolbox_ShowObject_Centre,
                                 NULL,
                                 NULL_ObjectId,
                                 NULL_ComponentId));
  quit_sender = task_handle;

  return true; /* cannot quit whilst dialogue box is open */
}
