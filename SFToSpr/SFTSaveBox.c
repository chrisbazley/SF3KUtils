/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box superclass
 *  Copyright (C) 2017 Christopher Bazley
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

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"
#include "saveas.h"
#include "wimplib.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "ViewsMenu.h"
#include "StrExtra.h"
#include "PathTail.h"
#include "Debug.h"
#include "Deiconise.h"
#include "UserData.h"
#include "EventExtra.h"

/* Local headers */
#include "SFTSaveBox.h"

/* Constant numeric values */
enum
{
  PathElements = 3,
  ShowYMin = 96
};

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int dialogue_completed(const int event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  SFTSaveBox_destroy(handle);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void destroy_item(UserData *item)
{
  SFTSaveBox_destroy((SFTSaveBox *)item);
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

SFTSaveBox *SFTSaveBox_initialise(SFTSaveBox *savebox, char const *input_path,
  bool data_saved, int file_type, char *template_name, const char *menu_token,
  int x, SFTSaveBoxDeletedFn *deleted_cb)
{
  assert(savebox != NULL);
  assert(input_path != NULL);
  assert(template_name != NULL);
  assert(menu_token != NULL);
  assert(deleted_cb != NULL);

  DEBUGF("Initialising savebox %p for %ssaved path '%s' with template '%s'\n",
         (void *)savebox, data_saved ? "" : "un", input_path, template_name);

  savebox->deleted_cb = deleted_cb;

  /* Create Toolbox object */
  ObjectId saveas_id;
  if (!E(toolbox_create_object(0, template_name, &saveas_id)))
  {
    savebox->saveas_id = saveas_id;

    /* Add entry to iconbar menu */
    if (!E(ViewsMenu_add(
           saveas_id,
           msgs_lookup_subn(menu_token, 1, pathtail(input_path, PathElements)),
           "" /* obsolete */)))
    {
      if (!E(event_register_toolbox_handler(saveas_id,
                                            SaveAs_DialogueCompleted,
                                            dialogue_completed,
                                            savebox)))
      {
        if (userdata_add_to_list(&savebox->super, NULL, destroy_item,
            data_saved ? input_path : ""))
        {
          do
          {
            if (E(saveas_get_window_id(0, saveas_id, &savebox->window_id)))
              break;

            if (E(saveas_set_file_name(0, saveas_id, (char *)input_path)))
              break;

            if (E(saveas_set_file_type(0, saveas_id, file_type)))
              break;

            WimpGetWindowStateBlock winstate;

            if (E(window_get_wimp_handle(0, savebox->window_id, &winstate.window_handle)))
              break;

            if (E(wimp_get_window_state(&winstate)))
              break;

            /* Show the dbox horizontally centred on a given x position
               with the bottom at a given y position. */
            WindowShowObjectBlock showblock;

            showblock.visible_area.xmin = x - (winstate.visible_area.xmax -
                                               winstate.visible_area.xmin) / 2;

            showblock.visible_area.ymin = ShowYMin + (winstate.visible_area.ymax -
                                          winstate.visible_area.ymin);

            if (E(DeIconise_show_object(0,
                                        savebox->saveas_id,
                                        Toolbox_ShowObject_TopLeft,
                                        &showblock,
                                        NULL_ObjectId,
                                        NULL_ComponentId)))
              break;

            DEBUGF("Created savebox %p (0x%x)\n", (void *)savebox, savebox->saveas_id);
            return savebox;
          }
          while (0);
          userdata_remove_from_list(&savebox->super);
        }
        else
        {
          RPT_ERR("NoMem");
        }
        (void)event_deregister_toolbox_handler(saveas_id,
                                               SaveAs_DialogueCompleted,
                                               dialogue_completed,
                                               savebox);
      }
      (void)ViewsMenu_remove(saveas_id);
    }
    (void)toolbox_delete_object(0, saveas_id);
  }
  return NULL;
}

/* ----------------------------------------------------------------------- */

void SFTSaveBox_show(const SFTSaveBox *savebox)
{
  assert(savebox != NULL);

  /* Bring window to the front of the stack (and deiconise, if needed) */
  ON_ERR_RPT(DeIconise_show_object(0,
                                   savebox->window_id,
                                   Toolbox_ShowObject_Default,
                                   NULL,
                                   NULL_ObjectId,
                                   NULL_ComponentId));
}

/* ----------------------------------------------------------------------- */

void SFTSaveBox_finalise(SFTSaveBox *savebox)
{
  assert(savebox != NULL);

  DEBUGF("Finalising savebox %p (0x%x)\n", (void *)savebox,
         savebox->saveas_id);

  userdata_remove_from_list(&savebox->super);

  /* Deregister event handlers */
  ON_ERR_RPT(event_deregister_toolbox_handlers_for_object(savebox->window_id));

  /* Remove from iconbar menu */
  ON_ERR_RPT(ViewsMenu_remove(savebox->saveas_id));

  /* Delete the toolbox objects (window is deleted automatically) */
  ON_ERR_RPT(remove_event_handlers_delete(savebox->saveas_id));
}

/* ----------------------------------------------------------------------- */

void SFTSaveBox_destroy(SFTSaveBox *savebox)
{
  if (savebox != NULL)
  {
    assert(savebox->deleted_cb != NULL);
    savebox->deleted_cb(savebox);
  }
}
