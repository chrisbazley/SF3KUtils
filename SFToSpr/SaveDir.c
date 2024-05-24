/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for directory
 *  Copyright (C) 2000 Christopher Bazley
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
#include <stdbool.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "gadgets.h"

/* My library files */
#include "OSFile.h"
#include "Debug.h"
#include "err.h"
#include "msgtrans.h"
#include "FileUtils.h"
#include "Macros.h"
#include "StrExtra.h"
#include "PathTail.h"
#include "EventExtra.h"

/* Local headers */
#include "Scan.h"
#include "SaveDir.h"
#include "SFTSaveBox.h"
#include "StringBuff.h"

/* Window component IDs */
enum
{
  ComponentId_SF3000ToSprite_Radio = 0x00,
  ComponentId_ExtractImages_Radio  = 0x01,
  ComponentId_ExtractData_Radio    = 0x02,
  ComponentId_SpriteToSF3000_Radio = 0x03
};

typedef struct
{
  SFTSaveBox  super;
  ComponentId reset_direction;
  SFTSaveBoxDeletedFn *deleted_cb;
}
SaveDir;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void destroy_savedir(SFTSaveBox *savebox)
{
  SaveDir * const savedir_data = CONTAINER_OF(savebox, SaveDir, super);

  assert(savedir_data != NULL);
  SFTSaveBox_finalise(savebox);

  /* Notify the creator of this dialogue box that it was deleted */
  if (savedir_data->deleted_cb != NULL)
  {
    savedir_data->deleted_cb(savebox);
  }
  free(savedir_data);
}

/*
 * Toolbox event handlers
 */

static int actionbutton_selected(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)event;
  SaveDir *savedir_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust) &&
      id_block->self_component == (SaveAs_ObjectClass << 4) + 2)
  {
    /* ADJUST click on 'Cancel' button - reset dbox state */
    ON_ERR_RPT(radiobutton_set_state(0, id_block->self_id,
                                     savedir_data->reset_direction, 1));
    return 1; /* claim event */
  }
  else
  {
    return 0; /* not interested */
  }
}

/* ----------------------------------------------------------------------- */

static int save_to_file(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)event;
  SaveDir *savedir_data = handle;
  unsigned int flags = 0;
  char *buf = NULL;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  do
  {
    /* Read conversion operation from radio buttons */
    if (E(radiobutton_get_state(0, savedir_data->super.window_id,
                                ComponentId_SpriteToSF3000_Radio, NULL,
                                &savedir_data->reset_direction)))
      break;

    if (E(canonicalise(&buf, NULL, NULL, sastfe->filename)))
      break;

    /* For the moment we just create the root directory */
    if (stricmp(sastfe->filename, "<Wimp$Scrap>"))
    {
      const _kernel_oserror * const e = os_file_create_dir(
                                            sastfe->filename,
                                            OS_File_CreateDir_DefaultNoOfEntries);
      if (e != NULL)
      {
        err_complain(e->errnum, msgs_lookup_subn("DirFail", 1, e->errmess));
        break;
      }
    }
    else
    {
      RPT_ERR("NoDirtoApp");
      break;
    }

    /* We reckon that we already succeeded if we reach this point */
    flags = SaveAs_SuccessfulSave;

    const bool images = (savedir_data->reset_direction == ComponentId_ExtractImages_Radio ||
                         savedir_data->reset_direction == ComponentId_SF3000ToSprite_Radio);

    const bool data = (savedir_data->reset_direction == ComponentId_ExtractData_Radio ||
                       savedir_data->reset_direction == ComponentId_SF3000ToSprite_Radio);

    Scan_create(stringbuffer_get_pointer(&savedir_data->super.super.file_name),
                buf, images, data);
  }
  while (0);

  free(buf);
  saveas_file_save_completed(flags, id_block->self_id, sastfe->filename);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

SFTSaveBox *SaveDir_create(char const *input_path, int x, SFTSaveBoxDeletedFn *deleted_cb)
{
  assert(input_path != NULL);
  DEBUGF("Creating savedir box for path '%s'\n", input_path);

  SaveDir * const savedir_data = malloc(sizeof(*savedir_data));
  if (savedir_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *savedir_data = (SaveDir){
    .deleted_cb = deleted_cb,
  };

  if (SFTSaveBox_initialise(&savedir_data->super, input_path, true,
                            FileType_Directory, "SaveDir", "DirDialogueList",
                            x, destroy_savedir))
  {
    do
    {
      /* Register Toolbox event handlers for SaveAs object */
      if (E(event_register_toolbox_handler(savedir_data->super.saveas_id,
                                           SaveAs_SaveToFile,
                                           save_to_file,
                                           savedir_data)))
      {
        break;
      }

      /* Record initial state of dialogue box */
      if (E(radiobutton_get_state(0, savedir_data->super.window_id,
                                  ComponentId_SF3000ToSprite_Radio,
                                  NULL, &savedir_data->reset_direction)))
      {
        break;
      }

      /* Register an event handler for the underlying Window object */
      if (E(event_register_toolbox_handler(savedir_data->super.window_id,
                                           ActionButton_Selected,
                                           actionbutton_selected,
                                           savedir_data)))
      {
        break;
      }

      return &savedir_data->super;
    }
    while (0);
    SFTSaveBox_finalise(&savedir_data->super);
  }
  free(savedir_data);
  return NULL;
}
