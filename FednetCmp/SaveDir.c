/*
 *  FednetCmp - Fednet file compression/decompression
 *  Save dialogue box for directory
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
#include "stdio.h"
#include <string.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "gadgets.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "FileUtils.h"
#include "StrExtra.h"
#include "GadgetUtil.h"
#include "OSFile.h"
#include "Debug.h"

/* Local headers */
#include "Scan.h"
#include "SaveDir.h"
#include "FNCSaveBox.h"

/* Window component IDs */
enum
{
  ComponentId_Compress_Radio     = 0x01,
  ComponentId_Decompress_Radio   = 0x02,
  ComponentId_FileType_Label     = 0x11,
  ComponentId_FileType_StringSet = 0x12,
  ComponentId_Cancel_ActButton   = 0x82bc02
};

/* Constant numeric values */
enum
{
  MaxFileTypeNameLen = 31
};

typedef struct
{
  FNCSaveBox  super;
  ComponentId reset_direction;
  char        reset_filetype[MaxFileTypeNameLen + 1];
  FNCSaveBoxDeletedFn *deleted_cb;
}
SaveDir;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void destroy_savedir(FNCSaveBox *const savebox)
{
  SaveDir * const savedir_data = CONTAINER_OF(savebox, SaveDir, super);

  assert(savedir_data != NULL);
  FNCSaveBox_finalise(savebox);

  /* Notify the creator of this dialogue box that it was deleted */
  if (savedir_data->deleted_cb != NULL)
    savedir_data->deleted_cb(savebox);

  free(savedir_data);
}

/*
 * Toolbox event handlers
 */

static int radiobutton_state_changed(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  /* Handles greying/ungreying of filetype gadgets */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  const bool fade = (id_block->self_component != ComponentId_Compress_Radio);
  ON_ERR_RPT(set_gadget_faded(id_block->self_id, ComponentId_FileType_StringSet, fade));
  ON_ERR_RPT(set_gadget_faded(id_block->self_id, ComponentId_FileType_Label, fade));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int actionbutton_selected(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)event;
  SaveDir *savedir_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (!TEST_BITS(abse->hdr.flags, ActionButton_Selected_Adjust))
    return 0; /* not interested */

  switch (id_block->self_component)
  {
    case ComponentId_Cancel_ActButton:
      /* Reset dbox state */
      ON_ERR_RPT(radiobutton_set_state(0, id_block->self_id, savedir_data->reset_direction, 1));

      ON_ERR_RPT(stringset_set_selected(0,
                                        id_block->self_id,
                                        ComponentId_FileType_StringSet,
                                        savedir_data->reset_filetype));

      /* Ensure that gadgets are greyed out/ungreyed correctly */
      {
        IdBlock fake_id = *id_block;
        fake_id.self_component = savedir_data->reset_direction;

        (void)radiobutton_state_changed(RadioButton_StateChanged,
                                        NULL,
                                        &fake_id,
                                        handle);
      }
      break;

    default:
      return 0; /* unknown component */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_to_file(int const event_code, ToolboxEvent *const event,
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
    if (E(radiobutton_get_state(0,
                                savedir_data->super.window_id,
                                ComponentId_Compress_Radio,
                                NULL,
                                &savedir_data->reset_direction)))
      break;

    /* Read filetype to give output */
    if (E(stringset_get_selected(0,
                                 savedir_data->super.window_id,
                                 ComponentId_FileType_StringSet,
                                 &savedir_data->reset_filetype,
                                 sizeof(savedir_data->reset_filetype),
                                 NULL)))
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

    unsigned int hex_type;
    sscanf(strchr(savedir_data->reset_filetype, '('), "(&%x)", &hex_type);
    Scan_create(userdata_get_file_name(&savedir_data->super.super),
                buf,
                savedir_data->reset_direction == ComponentId_Compress_Radio,
                hex_type);
  }
  while (0);

  free(buf);
  saveas_file_save_completed(flags, id_block->self_id, sastfe->filename);

  /* Hide the dialogue box if saving was successful. ROOL's version of SaveAs
     doesn't do this automatically. :( */
  if (flags & SaveAs_SuccessfulSave)
  {
    ON_ERR_RPT(toolbox_hide_object(0, id_block->self_id));
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

FNCSaveBox *SaveDir_create(char const *input_path, int const x,
  FNCSaveBoxDeletedFn *const deleted_cb)
{
  assert(input_path != NULL);
  DEBUGF("Creating savedir box for path '%s'\n", input_path);

  SaveDir * const savedir_data = malloc(sizeof(*savedir_data));
  if (savedir_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }
  *savedir_data = (SaveDir){.deleted_cb = deleted_cb};

  if (FNCSaveBox_initialise(&savedir_data->super,
                            input_path,
                            true,
                            FileType_Directory,
                            "SaveDir",
                            "DirDialogueList",
                            x,
                            destroy_savedir))
  {
    do
    {
      /* Register Toolbox event handlers for SaveAs object */
      if (E(event_register_toolbox_handler(savedir_data->super.saveas_id,
                                           SaveAs_SaveToFile,
                                           save_to_file,
                                           savedir_data)))
        break;

      /* Record initial state of dialogue box */
      if (E(radiobutton_get_state(0,
                                  savedir_data->super.window_id,
                                  ComponentId_Compress_Radio,
                                  NULL,
                                  &savedir_data->reset_direction)))
        break;

      if (E(stringset_get_selected(0,
                                   savedir_data->super.window_id,
                                   ComponentId_FileType_StringSet,
                                   &savedir_data->reset_filetype,
                                   sizeof(savedir_data->reset_filetype),
                                   NULL)))
        break;

      /* Register extra handlers for compression/decompression selection &
         restoration of last filetype used if Cancel button clicked */
      if (E(event_register_toolbox_handler(savedir_data->super.window_id,
                                           ActionButton_Selected,
                                           actionbutton_selected,
                                           savedir_data)))
        break;

      if (E(event_register_toolbox_handler(savedir_data->super.window_id,
                                           RadioButton_StateChanged,
                                           radiobutton_state_changed,
                                           savedir_data)))
        break;

      return &savedir_data->super;
    }
    while (0);
    FNCSaveBox_finalise(&savedir_data->super);
  }
  free(savedir_data);
  return NULL;
}
