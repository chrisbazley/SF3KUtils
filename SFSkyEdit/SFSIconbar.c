/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Iconbar icon
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
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include "stdlib.h"
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "iconbar.h"
#include "wimp.h"
#include "wimplib.h"

/* My library files */
#include "err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "Loader3.h"
#include "SFformats.h" /* get Fednet filetype */
#include "Debug.h"
#include "WimpExtra.h"
#include "FileUtils.h"
#include "UserData.h"
#include "ReaderRaw.h"
#include "FopenCount.h"
#include "FileTypes.h"

/* Local headers */
#include "EditWin.h"
#include "SFSIconbar.h"
#include "Utils.h"
#include "SkyIO.h"

enum
{
  WindowHandle_IconBar = -2, /* Pseudo window handle (icon bar) */
};

static ObjectId Iconbar_id = NULL_ObjectId;
static int dragclaim_msg_ref;
static int const import_types[] =
{
  FileType_SFSkyCol,
  FileType_CSV,
  FileType_Null
};

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int dragging_handler(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDragging);
  NOT_USED(handle);

  const WimpDraggingMessage *const dragging =
    (WimpDraggingMessage *)&message->data;
  DEBUGF("Received a Dragging message for icon %d in window &%x\n",
        dragging->icon_handle, dragging->window_handle);

  IO_dragging_msg(dragging);
  dragclaim_msg_ref = 0; /* reset message reference */

  /* Check whether the pointer is over the icon bar */
  if (dragging->window_handle != WindowHandle_IconBar)
  {
    DEBUGF("Drag is not over the icon bar\n");
    return 0; /* do not claim message */
  }

  /* Check whether the pointer is over our icon */
  int icon_handle;
  if (!E(iconbar_get_icon_handle(0, Iconbar_id, &icon_handle)))
  {
    if (dragging->icon_handle != icon_handle)
    {
      DEBUGF("Drag is not over our icon bar icon\n");
      return 0; /* do not claim message */
    }

    /* The sender can set a flag to prevent us from claiming the drag again
       (i.e. force us to relinquish it if we had claimed it) */
    if (TEST_BITS(dragging->flags, Wimp_MDragging_DoNotClaimMessage))
    {
      DEBUGF("Forbidden from claiming this drag\n");
    }
    else
    {
      /* Can we handle any of the export file types offered? */
      if (common_file_type(import_types, dragging->file_types) != FileType_Null)
      {
        /* Claim the drag for ourselves */
        claim_drag(message, import_types, &dragclaim_msg_ref);
      }
      else
      {
        /* Claim the message, but not the drag */
        DEBUGF("We don't like any of their export file types\n");
      }
    }
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int datasave_message(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataSave);
  NOT_USED(handle);

  DEBUGF("Received a DataSave message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref)
  {
    /* Is this message a reply to our last DragClaim message? */
    if (message->hdr.your_ref != dragclaim_msg_ref)
      return 0; /* no - we must not claim it */

    DEBUGF("It is a reply to our last DragClaim message\n");
    dragclaim_msg_ref = 0; /* yes - reset DragClaim message reference */
  }

  DEBUGF("Window handle is %d\n", message->data.data_save.destination_window);
  if (message->data.data_save.destination_window != WindowHandle_IconBar)
  {
    return 0;
  }

  IO_receive(message);
  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int dataload_message(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  NOT_USED(handle);

  DEBUGF("Received a DataLoad message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref)
  {
    DEBUGF("Icon bar ignoring a reply\n");
    return 0; /* will be dealt with by Loader3 module */
  }

  DEBUGF("Window handle is %d\n", message->data.data_load.destination_window);
  if (message->data.data_load.destination_window != WindowHandle_IconBar)
  {
    return 0;
  }

  IO_load_file(message->data.data_load.file_type,
               message->data.data_load.leaf_name);

  /* Acknowledge that the file was loaded successfully
     (just a courtesy message, we don't expect a reply) */
  message->hdr.your_ref = message->hdr.my_ref;
  message->hdr.action_code = Wimp_MDataLoadAck;

  if (!E(wimp_send_message(Wimp_EUserMessage,
              message, message->hdr.sender, 0, NULL)))
  {
    DEBUGF("Sent DataLoadAck message (ref. %d)\n", message->hdr.my_ref);
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Iconbar_initialise(ObjectId id)
{
  static const struct
  {
    int msg_no;
    WimpMessageHandler *handler;
  }
  msg_handlers[] =
  {
    {
      Wimp_MDataSave,
      datasave_message
    },
    {
      Wimp_MDataLoad,
      dataload_message
    },
    {
      Wimp_MDragging,
      dragging_handler
    },
  };

  Iconbar_id = id;

  /* Register Wimp message handlers to load files dropped on iconbar icon */
  for (size_t i = 0; i < ARRAY_SIZE(msg_handlers); i++)
  {
    EF(event_register_message_handler(msg_handlers[i].msg_no,
                                      msg_handlers[i].handler,
                                      NULL));
  }
}
