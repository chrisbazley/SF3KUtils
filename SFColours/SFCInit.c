/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Initialisation
 *  Copyright (C) 2016 Christopher Bazley
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
#include <string.h>
#include <signal.h>

/* RISC OS library files */
#include "kernel.h"
#include "wimp.h"
#include "toolbox.h"
#include "event.h"
#include "wimplib.h"
#include "flex.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Hourglass.h"
#include "Macros.h"
#include "Debug.h"
#include "ViewsMenu.h"
#include "InputFocus.h"
#include "scheduler.h"
#include "Saver2.h"
#include "Drag.h"
#include "Loader3.h"
#include "SFFormats.h"
#include "Entity2.h"
#include "PalEntry.h"
#include "ClrTrans.h"
#include "OSVDU.h"
#include "MessTrans.h"
#include "UserData.h"

/* Local headers */
#include "SFCInit.h"
#include "SFCIconbar.h"
#include "SFCSaveBox.h"
#include "SFCFileInfo.h"
#include "Menus.h"
#include "PreQuit.h"
#include "DCS_dialogue.h"
#include "OurEvents.h"
#include "Picker.h"
#include "ColsIO.h"
#include "Utils.h"

/* Constant numeric values */
enum
{
  KnownWimpVersion  = 310,
  ErrNum_ToSaveDrag = 0x80b633,
  ErrNum_LockedFile = 0x131c3,
  MaxTaskNameLen    = 31,
  MinWimpVersion    = 321, /* Earliest version of window manager to support
                              Wimp_ReportError extensions */
  TimeSlice         = 10, /* Minimum time processing a null event (centiseconds)*/
  GameScreenMode    = 13  /* 320 x 256, 8 bits per pixel, 64 palette entries */
};

typedef struct
{
  char const *template_name;
  void      (*initialise)(ObjectId id);
}
ObjectInitInfo;

PaletteEntry palette[NumColours];
int x_eigen = 2, y_eigen = 2;
int wimp_version;
MessagesFD mfd;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int mode_change_msg(WimpMessage *const message, void *const handle)
{
  /* This handler is called upon desktop screen mode change */
  enum
  {
    VarIndex_XEigFactor,
    VarIndex_YEigFactor,
    VarIndex_LAST
  };

  static const VDUVar mode_vars[VarIndex_LAST + 1] =
  {
    [VarIndex_XEigFactor] = (VDUVar)ModeVar_XEigFactor,
    [VarIndex_YEigFactor] = (VDUVar)ModeVar_YEigFactor,
    [VarIndex_LAST] = VDUVar_EndOfList,
  };

  int var_vals[VarIndex_LAST];

  NOT_USED(handle);
  NOT_USED(message);

  if (!E(os_read_vdu_variables(mode_vars, var_vals)))
  {
    /* Negative eigen factors make sense in theory ( > 1 internal graphics unit
       per external graphics unit) but the result of shifting by a negative
       operand is undefined (cf K&R appendix A7.9) */
    assert(var_vals[VarIndex_XEigFactor] >= 0);
    x_eigen = var_vals[VarIndex_XEigFactor];

    assert(var_vals[VarIndex_YEigFactor] >= 0);
    y_eigen = var_vals[VarIndex_YEigFactor];
  }

  return 0; /* don't claim event */
}

/* ----------------------------------------------------------------------- */

static void cb_released(void)
{
  DEBUGF("Clipboard released - terminating\n");
  userdata_destroy_all();
  exit(EXIT_SUCCESS);
}

/* ----------------------------------------------------------------------- */

static int quit_msg(WimpMessage *const message, void *const handle)
{
  /* Quit application */
  assert(message != NULL);
  NOT_USED(handle);

  DEBUGF("Received Wimp quit message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  /* We may own the global clipboard, so offer the associated data to
     any 'holder' application before exiting. */
  EF(entity2_dispose_all(cb_released));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int misc_tb_event(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Handles non-object-specific client allocated toolbox events */
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  switch (event_code)
  {
    case EventCode_Quit:
      if (!PreQuit_queryunsaved(0))
      {
        /* We may own the global clipboard, so offer the associated data to
           any 'holder' application before exiting. */
        EF(entity2_dispose_all(cb_released));
      }
      break;

    case EventCode_Help:
      /* Show application help file */
      if (_kernel_oscli("Filer_Run <"APP_NAME"$Dir>.!Help") == _kernel_ERROR)
        ON_ERR_RPT(_kernel_last_oserror());
      break;

    case EventCode_CreateObjColours:
      ColMapFile_create(NULL, NULL, true, false);

      /* Hide the creation dbox if appropriate (event will have no
         component id if generated in response to key press) */
      if (id_block->self_component == NULL_ComponentId)
        ON_ERR_RPT(toolbox_hide_object(0, id_block->self_id));

      break;

    case EventCode_CreateHillColours:
      ColMapFile_create(NULL, NULL, true, true);

      /* Hide the creation dbox if appropriate (event will have no
         component id if generated in response to key press) */
      if (id_block->self_component == NULL_ComponentId)
        ON_ERR_RPT(toolbox_hide_object(0, id_block->self_id));

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

static int pre_quit_msg(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  NOT_USED(handle);

  DEBUGF("Received Wimp pre-quit message (ref. %d in reply to %d)\n",
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
    DEBUGF("Acknowledging pre-quit message to forestall death\n");
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

static void Menu_initialise(ObjectId id)
{
  EF(ViewsMenu_parentcreated(id, 0x03));
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
      "DCS",
      DCS_initialise
    },
    {
      "EditMenu",
      EditMenu_initialise
    },
    {
      "EffectMenu",
      EffectMenu_initialise
    },
    {
      "FileInfo",
      FileInfo_initialise
    },
    {
      "Iconbar",
      Iconbar_initialise
    },
    {
      "Menu",
      Menu_initialise
    },
    {
      "Picker",
      Picker_initialise
    },
    {
      "PreQuit",
      PreQuit_initialise
    },
    {
      "RootMenu",
      RootMenu_initialise
    },
    {
      "SaveFile",
      SaveFile_initialise
    },
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

  /* "To save drag..." or "locked file" are not serious errors */
  if (totee->errnum == ErrNum_ToSaveDrag || totee->errnum == ErrNum_LockedFile)
    err_report(totee->errnum, totee->errmess);
  else
    err_complain(totee->errnum, totee->errmess);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void simple_exit(const _kernel_oserror *e)
{
  /* Limited amount we can do with no messages file... */
  assert(e != NULL);
  wimp_report_error((_kernel_oserror *)e, Wimp_ReportError_Cancel, APP_NAME);
  exit(EXIT_FAILURE);
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void initialise(void)
{
  static int wimp_messages[] = {
        Wimp_MDataSave, Wimp_MDataSaveAck, Wimp_MDataLoad, Wimp_MDataLoadAck,
        Wimp_MRAMFetch, Wimp_MRAMTransmit,
        Wimp_MModeChange, Wimp_MPaletteChange, Wimp_MToolsChanged,
        Wimp_MDragging, Wimp_MDragClaim,
        Wimp_MClaimEntity, Wimp_MDataRequest, Wimp_MReleaseEntity,
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
      Wimp_MModeChange,
      mode_change_msg
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
  int toolbox_events = 0, task_handle;

  const _kernel_oserror *e = toolbox_initialise(
    0, KnownWimpVersion, wimp_messages, &toolbox_events, "<"APP_NAME"Res$Dir>",
    &mfd, &id_block, &wimp_version, &task_handle, NULL);

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
                    Wimp_Poll_KeyPressedMask)); /* Dealt with by Toolbox */

  /*
   * register permanent event handlers.
   */

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
  EF(InputFocus_initialise());
  EF(scheduler_initialise(TimeSlice, &mfd, err_check_rep));
  EF(saver2_initialise(task_handle, &mfd));
  EF(entity2_initialise(&mfd, err_check_rep));
  EF(ViewsMenu_create(&mfd, err_check_rep));
  EF(drag_initialise(&mfd, err_check_rep));
  EF(loader3_initialise(&mfd));

  EditWin_initialise();
  IO_initialise();

  /* Read the default palette for game's screen mode */
  ColourTransContext source = {
    .type = ColourTransContextType_Screen,
    .data = {
      .screen = {
        .mode = GameScreenMode,
        .palette = ColourTrans_DefaultPalette,
      },
    },
  };
  EF(colourtrans_read_palette(0, &source, palette, sizeof(palette), NULL));

  /* Read variables for current screen mode */
  mode_change_msg(NULL, NULL);

  hourglass_off();
}
