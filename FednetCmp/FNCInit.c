/*
 *  FednetCmp - Fednet file compression/decompression
 *  Initialisation
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

/* ANSI library files */
#include <assert.h>
#include "stdlib.h"
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <ctype.h>

/* RISC OS library files */
#include "kernel.h"
#include "toolbox.h"
#include "event.h"
#include "flex.h"
#include "wimp.h"
#include "wimplib.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Hourglass.h"
#include "Macros.h"
#include "SFFormats.h"
#include "ViewsMenu.h"
#include "Scheduler.h"
#include "InputFocus.h"
#include "StrExtra.h"
#include "Loader3.h"
#include "FileUtils.h"
#include "Debug.h"
#include "MessTrans.h"
#include "UserData.h"
#include "LoadSaveMT.h"
#include "FedCompMT.h"

/* Local headers */
#include "FNCInit.h"
#include "PreQuit.h"
#include "Scan.h"
#include "FNCIconbar.h"
#include "FNCMenu.h"
#include "OurEvents.h"

/* Constant numeric values */
enum
{
  KnownWimpVersion = 310,
  MaxTaskNameLen   = 31,
  MinWimpVersion   = 321, /* Earliest version of window manager to support
                             Wimp_ReportError extensions */
  TimeSlice        = 10
};
/* TimeSlice is the MINIMUM amount of work we do per null poll (though the
   maximum shouldn't be too much greater, since our SchedulerIdleFunction
   is well behaved). We null poll as often as possible, like a program
   running under the TaskWindow module. The event mask is used (rather
   than Wimp_PollIdle) to avoid receiving unnecessarily null events.  */

typedef struct
{
  char const *template_name;
  void      (*initialise)(ObjectId id);
}
ObjectInitInfo;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int pre_quit_msg(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  NOT_USED(handle);

  DEBUG("Received Wimp pre-quit message (ref. %d in reply to %d)",
        message->hdr.my_ref, message->hdr.your_ref);

  /* Open dbox to query whether to discard unsaved data */
  unsigned int flags = 0;
  if (message->hdr.size >= 0 &&
      (size_t)message->hdr.size >= offsetof(WimpMessage, data.words[1]))
  {
    flags = message->data.words[0];
  }
  if (PreQuit_queryunsaved((flags & 1) ? 0 : message->hdr.sender))
  {
    /* Object to dying by acknowledging this message */
    DEBUG("Acknowledging pre-quit message to forestall death");
    message->hdr.your_ref = message->hdr.my_ref;
    ON_ERR_RPT(wimp_send_message(Wimp_EUserMessageAcknowledge,
                                 message,
                                 message->hdr.sender,
                                 0,
                                 NULL));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int quit_msg(WimpMessage *const message, void *const handle)
{
  NOT_USED(message);
  NOT_USED(handle);

  userdata_destroy_all();
  exit(EXIT_SUCCESS);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int data_open_msg(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataOpen);
  NOT_USED(handle);

  if (message->data.data_open.file_type != FileType_Fednet)
    return 0; /* message not handled */

  /* Claim the broadcast by replying with a DataLoadAck message */
  message->hdr.your_ref = message->hdr.my_ref;
  message->hdr.action_code = Wimp_MDataLoadAck;
  if (!E(wimp_send_message(Wimp_EUserMessage,
                           message,
                           message->hdr.sender,
                           0,
                           NULL)))
  {
    char *canonical_file_path;
    if (!E(canonicalise(&canonical_file_path,
                        NULL,
                        NULL,
                        message->data.data_open.path_name)))
    {
      Scan_create(canonical_file_path, canonical_file_path, false, 0);
      free(canonical_file_path);
    }
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int misc_tb_event(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Handles non-object-specific client allocated toolbox events */
  NOT_USED(event);
  NOT_USED(id_block);
  NOT_USED(handle);

  switch (event_code)
  {
    case EventCode_Quit:
      if (!PreQuit_queryunsaved(0))
        quit_msg(NULL, NULL);
      break;

    case EventCode_Help:
      /* Show application help file */
      if (_kernel_oscli("Filer_Run <"APP_NAME"$Dir>.!Help") == _kernel_ERROR)
        ON_ERR_RPT(_kernel_last_oserror());
      break;

    case EventCode_WindowsToFront:
      ON_ERR_RPT(ViewsMenu_showall());
      break;

    default:
      return 0; /* not interested */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int compare_init_info(const void *const key, const void *const element)
{
  const ObjectInitInfo *const init_info = element;

  assert(element != NULL);
  assert(key != NULL);
  return strcmp(key, init_info->template_name);
}

/* ----------------------------------------------------------------------- */

static int object_auto_created(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Catch auto-created objects and initialise handlers etc. */
  const ToolboxObjectAutoCreatedEvent * const toace =
    (ToolboxObjectAutoCreatedEvent *)event;

  /* This array must be in alphabetical order to allow binary search */
  static const ObjectInitInfo auto_created[] =
  {
    {
      "Iconbar",
      Iconbar_initialise
    },
    {
      "Menu",
      Menu_initialise
    },
    {
      "PreQuit",
      PreQuit_initialise
    }
  };
  const ObjectInitInfo *match;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Find the relevant initialisation function from the name of the template
     used to auto-create the object */
  match = bsearch(toace->template_name,
                  auto_created,
                  ARRAY_SIZE(auto_created),
                  sizeof(auto_created[0]),
                  compare_init_info);
  if (match != NULL)
  {
    assert(strcmp(toace->template_name, match->template_name) == 0);
    DEBUGF("Calling function for object 0x%x created from template '%s'\n",
           id_block->self_id, toace->template_name);

    match->initialise(id_block->self_id);
    return 1; /* claim event */
  }
  else
  {
    DEBUGF("Don't know how to init object 0x%x created from template '%s'!\n",
           id_block->self_id, toace->template_name);
    return 0; /* event not handled */
  }
}

/* ----------------------------------------------------------------------- */

static int toolbox_error(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const ToolboxErrorEvent * const totee = (ToolboxErrorEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  NOT_USED(id_block);
  NOT_USED(handle);

  DEBUG("Toolbox error %x '%s'", totee->errnum, totee->errmess);

  /* "To save drag..." or "locked file" are not serious errors */
  if (totee->errnum == 0x80b633 ||
      totee->errnum == 0x131c3)
  {
    err_report(totee->errnum, totee->errmess);
  }
  else
  {
    err_complain(totee->errnum, totee->errmess);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void simple_exit(const _kernel_oserror *e)
{
  /* Limited amount we can do with no messages file... */
  wimp_report_error((_kernel_oserror *)e, Wimp_ReportError_Cancel, APP_NAME);
  exit(EXIT_FAILURE);
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void initialise(void)
{
  int toolbox_events = 0, wimp_version;

  static int wimp_messages[] = {
        Wimp_MDataOpen,
        Wimp_MDataSave, Wimp_MDataSaveAck, Wimp_MDataLoad, Wimp_MDataLoadAck,
        Wimp_MRAMFetch, Wimp_MRAMTransmit,
        Wimp_MMenusDeleted,
        Wimp_MPreQuit, Wimp_MQuit /* must be last */};

  static const struct
  {
    int                  event_code;
    ToolboxEventHandler *handler;
  }
  tb_handlers[] =
  {
    {
      Toolbox_ObjectAutoCreated,
      object_auto_created
    },
    {
      Toolbox_Error,
      toolbox_error
    },
    {
      -1,
      misc_tb_event
    }
  };
  static const struct
  {
    int                 msg_no;
    WimpMessageHandler *handler;
  }
  msg_handlers[] =
  {
    {
      Wimp_MPreQuit,
      pre_quit_msg
    },
    {
      Wimp_MQuit,
      quit_msg
    },
    {
      Wimp_MDataOpen,
      data_open_msg
    }
  };

  hourglass_on();

  /*
   * Prevent termination on SIGINT (we use the escape key ourselves)
   */
   signal(SIGINT, SIG_IGN);

  /*
   * register ourselves with the Toolbox.
   */
  static IdBlock id_block;
  static MessagesFD mfd;
  const _kernel_oserror *e = toolbox_initialise(0,
                         KnownWimpVersion,
                         wimp_messages,
                         &toolbox_events,
                         "<"APP_NAME"Res$Dir>",
                         &mfd,
                         &id_block,
                         &wimp_version,
                         0,
                         0);
  if (e != NULL)
    simple_exit(e);

  static char taskname[MaxTaskNameLen + 1];
  e = messagetrans_lookup(&mfd,
                          "_TaskName",
                          taskname,
                          sizeof(taskname),
                          NULL,
                          0);
  if (e != NULL)
    simple_exit(e);

  e = err_initialise(taskname, wimp_version >= MinWimpVersion, &mfd);
  if (e != NULL)
    simple_exit(e);

  /*
   * initialise the flex library
   */

  flex_init(taskname, 0, 0); /* (use Wimpslot and default English messages) */
  flex_set_budge(1); /* allow budging of flex when heap extends */

  /*
   * initialise the event library.
   */

  EF(event_initialise(&id_block));
  EF(event_set_mask(Wimp_Poll_NullMask |
                    Wimp_Poll_PointerLeavingWindowMask |
                    Wimp_Poll_PointerEnteringWindowMask |
                    Wimp_Poll_KeyPressedMask | /* Dealt with by Toolbox */
                    Wimp_Poll_LoseCaretMask |
                    Wimp_Poll_GainCaretMask));

  for (size_t i = 0; i < ARRAY_SIZE(tb_handlers); i++)
  {
    EF(event_register_toolbox_handler(-1,
                                      tb_handlers[i].event_code,
                                      tb_handlers[i].handler,
                                      NULL));
  }
  for (size_t i = 0; i < ARRAY_SIZE(msg_handlers); i++)
  {
    EF(event_register_message_handler(msg_handlers[i].msg_no,
                                      msg_handlers[i].handler,
                                      NULL));
  }

  /*
   * initialise the CBLibrary components that we use.
   */
  EF(msgs_initialise(&mfd));
  EF(compress_initialise(&mfd));
  EF(loadsave_initialise(&mfd));
  EF(InputFocus_initialise());
  EF(scheduler_initialise(TimeSlice, &mfd, err_check_rep));
  EF(loader3_initialise(&mfd));
  EF(ViewsMenu_create(&mfd, err_check_rep));
  userdata_init();

  hourglass_off();
}
