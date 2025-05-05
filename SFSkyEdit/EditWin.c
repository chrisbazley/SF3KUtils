/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky editing window
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
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <assert.h>
#include <stdint.h>

/* RISC OS library files */
#include "kernel.h"
#include "swis.h"
#include "wimp.h"
#include "toolbox.h"
#include "event.h"
#include "wimplib.h"
#include "window.h"
#include "gadgets.h"

/* My library files */
#include "LinkedList.h"
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "ViewsMenu.h"
#include "Pal256.h"
#include "scheduler.h"
#include "PathTail.h"
#include "DateStamp.h"
#include "Debug.h"
#include "Saver2.h"
#include "Drag.h"
#include "WimpExtra.h"
#include "StrExtra.h"
#include "StackViews.h"
#include "Entity2.h"
#include "DeIconise.h"
#include "UserData.h"
#include "StringBuff.h"
#include "Hourglass.h"
#include "Writer.h"
#include "Reader.h"
#include "EventExtra.h"

/* Local headers */
#include "Utils.h"
#include "SFSFileInfo.h"
#include "SFSSavebox.h"
#include "DCS_dialogue.h"
#include "Menus.h"
#include "Preview.h"
#include "Editor.h"
#include "Insert.h"
#include "Interpolate.h"
#include "EditWin.h"
#include "SFSInit.h"
#include "OurEvents.h"
#include "SkyIO.h"
#include "Picker.h"
#include "Goto.h"
#include "Layout.h"

#define VIEWS_SUFFIX " %d"
#define UNSAVED_SUFFIX " *" /* Appended to a window title to indicate that
                               file has unsaved changes */

#define PER_VIEW_SELECT (1)

/* Special value for SWI Wimp_DragBox */
#undef CancelDrag /* definition in "wimplib.h" is wrong! */
#define CancelDrag ((WimpDragBox *)-1)

/* Window component IDs */
enum
{
  ComponentId_CompOffset_NumRange = 0x00,
  ComponentId_StarsAlt_NumRange   = 0x01
};

/* Constant numeric values */
enum
{
  MouseButtonModifier_Drag   = 16,
  MouseButtonModifier_Single = 256,
  UntitledMaxLen             = 64,
  IntKeyNum_Shift            = 0,
  IntKeyNum_Ctrl             = 1,
  ScrollBorder               = 64,
  ToolbarHeight              = 140,
  DragUpdateFrequency        = 10, /* in centiseconds */
  DragUpdatePriority         = SchedulerPriority_Max, /* for scheduler */
  ScrollStepSize             = 32,
  ScrollToCaretStepSize      = 3,
  WimpAutoScrollMinVersion   = 400,
  PathElements               = 1,  /* For title of preview window */
};

struct SkyFile
{
  UserData list_node;
  EditSky edit_sky;
#if !PER_VIEW_SELECT
  Editor editor;
#endif
  PreviewData *preview_data; /* preview window, or NULL if none */
  OSDateAndTime file_date; /* 000000CC DDDDDDDD */
  bool changed_since_save;
  LinkedList views;
  int num_views;
};

struct EditWin
{
  LinkedListItem node;
  SkyFile *file;
#if PER_VIEW_SELECT
  Editor editor;
#endif
  Editor ghost;
  ObjectId window_id; /* Main editing window */
  ObjectId toolbar_id; /* Internal top left toolbar */
  int wimp_handle; /* Wimp handle of main editing window */
  int toolbar_wimp_handle; /* Wimp handle of internal toolbar */

  bool on_menu:1;
  bool has_input_focus:1;
  bool parent_pending:1; /* Open parent directory after file has been
                            saved? */
  bool destroy_pending:1; /* Destroy editing window after file has
                             been saved? */
  bool drop_pending:1;
  bool can_paste:1;
};

bool trap_caret = true;

static enum
{
  DragType_None,
  DragType_Rubber,
  DragType_Data
}
drag_type = DragType_None;
static EditWin *drag_view = NULL;
static EditWin const *auto_scroll_view = NULL;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

typedef bool EditWinCallbackFn(EditWin *edit_win, void *arg);

static EditWin *for_each_view(SkyFile *const file,
  EditWinCallbackFn *const fn, void *const arg)
{
  assert(file != NULL);
  LinkedListItem *next;
  for (LinkedListItem *node = linkedlist_get_head(&file->views);
       node != NULL;
       node = next)
  {
    next = linkedlist_get_next(node);
    EditWin *const edit_win = CONTAINER_OF(node, EditWin, node);
    if (fn(edit_win, arg))
    {
      return edit_win;
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------- */

static inline Editor *get_editor(EditWin *const edit_win)
{
  assert(edit_win != NULL);
#if PER_VIEW_SELECT
  return &edit_win->editor;
#else
  assert(edit_win->file != NULL);
  return &edit_win->file->editor;
#endif
}

/* ----------------------------------------------------------------------- */

static bool key_pressed(int const key_num)
{
  enum
  {
    OSByteScanKeys             = 129,
    OSByteScanKeysNoLimit      = 0xff,
    OSByteScanKeysSingle       = 0xff,
    OSByteR1ResultMask         = 0xff,
  };
  int const key_held = _kernel_osbyte(OSByteScanKeys,
    key_num ^ OSByteScanKeysSingle, OSByteScanKeysNoLimit);

  if (key_held == _kernel_ERROR)
  {
    ON_ERR_RPT(_kernel_last_oserror());
    return false;
  }

  return (key_held & OSByteR1ResultMask) != 0;
}

/* ----------------------------------------------------------------------- */

static bool sky_cancel_io_cb(EditWin *const edit_win, void *const arg)
{
  NOT_USED(arg);
  IO_cancel(edit_win);
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static void sky_cancel_io(SkyFile *const file)
{
  for_each_view(file, sky_cancel_io_cb, NULL);
}

/* ----------------------------------------------------------------------- */

static void fetch_caret(EditWin *const edit_win,
  const WimpOpenWindowBlock *const win_info)
{
  assert(edit_win != NULL);
  assert(!editor_has_selection(get_editor(edit_win)));
  assert(win_info != NULL);
  DEBUGF("Will force caret within visible area %d,%d of view %p\n",
        win_info->visible_area.ymin, win_info->visible_area.ymax,
        (void *)edit_win);

  /* Ensure vertical scroll offset is reasonable */
  int const y_scroll = win_info->yscroll;
  DEBUGF("Vertical scroll offset is %d\n", y_scroll);

  /* Calculate top of window's work area in screen coordinates */
  int const top_scry = win_info->visible_area.ymax - y_scroll;
  DEBUGF("Top of work area is %d (in screen)\n", top_scry);

  /* Calculate bottom of caret in work area coordinates */
  BBox caret_bbox;
  layout_get_caret_bbox(editor_get_caret_pos(get_editor(edit_win)), &caret_bbox);
  DEBUGF("Caret is y=%d..%d (in work area)\n",
         caret_bbox.ymin, caret_bbox.ymax);

  BBox bands_bbox;
  layout_get_bands_bbox(0, 1, &bands_bbox);
  int const scroll_step = bands_bbox.ymax - bands_bbox.ymin;
  DEBUGF("One band is y=%d..%d (in work area)\n",
         bands_bbox.ymin, bands_bbox.ymax);

  int new_caret_pos;

  if (y_scroll < ToolbarHeight + (1<<y_eigen) + caret_bbox.ymax)
  {
    int const real_vis_ymax = win_info->visible_area.ymax - ToolbarHeight -
                              (1<<y_eigen);

    assert(real_vis_ymax >= win_info->visible_area.ymin);
    DEBUGF("Move caret below top of visible area y=%d\n", real_vis_ymax);

    new_caret_pos = layout_decode_y_coord(
      real_vis_ymax - top_scry - scroll_step);
  }
  else
  {
    int const vis_height = win_info->visible_area.ymax -
                           win_info->visible_area.ymin;

    if (y_scroll <= caret_bbox.ymin + vis_height)
    {
      return;
    }

    DEBUGF("Move caret above bottom of visible area\n");
    new_caret_pos = layout_decode_y_coord(
      win_info->visible_area.ymin - top_scry + scroll_step);
  }

  if (editor_set_caret_pos(get_editor(edit_win), new_caret_pos))
  {
    sky_cancel_io(edit_win->file);
  }
}

/* ----------------------------------------------------------------------- */

static void update_menus(EditWin *const edit_win)
{
  /* Update menus to reflect whether or not we have a selection */
  if (showing_as_descendant(EditMenu_sharedid, edit_win->window_id))
  {
    EditMenu_update(edit_win);
  }

  if (showing_as_descendant(EffectMenu_sharedid, edit_win->window_id))
  {
    EffectMenu_update(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

static void resize_selection(EditWin *const edit_win, int const new_y)
{
  WimpGetWindowStateBlock window;

  assert(edit_win != NULL);
  window.window_handle = edit_win->wimp_handle;
  if (E(wimp_get_window_state(&window)))
    return;

  int const top_scry = window.visible_area.ymax - window.yscroll;
  int pointer_row = layout_decode_y_coord(new_y - top_scry);
  if (pointer_row < 0)
  {
    pointer_row = 0;
  }
  else if (pointer_row > NColourBands)
  {
    pointer_row = NColourBands;
  }

  DEBUGF("Moving end of selection to position %d\n", pointer_row);
  if (editor_set_selection_end(get_editor(edit_win), pointer_row))
  {
    sky_cancel_io(edit_win->file);
    update_menus(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

static SchedulerTime drag_selection(void *const handle,
  SchedulerTime const new_time, const volatile bool *const time_up)
{
  /* Handles drag-selection */
  EditWin *const edit_win = handle;

  assert(edit_win != NULL);
  NOT_USED(time_up);

  if (drag_type == DragType_Rubber)
  {
    WimpGetPointerInfoBlock pointer;

    if (!E(wimp_get_pointer_info(&pointer)))
    {
      resize_selection(edit_win, pointer.y);
    }
  }

  return new_time + DragUpdateFrequency;
}

/* ----------------------------------------------------------------------- */

static int decode_pointer_pos(EditWin *const edit_win,
  const WimpGetWindowStateBlock *const window_state, int const y,
  bool *const within_sel)
{
  assert(edit_win != NULL);
  assert(window_state != NULL);
  assert(edit_win->wimp_handle == window_state->window_handle);

  /* Calculate the top of the window's work area, in screen coordinates */
  int const top_scry = window_state->visible_area.ymax - window_state->yscroll;
  DEBUGF("Top of window's work area is y=%d\n", top_scry);

  /* Calculate the index of the nearest colour band which has its centre
     above the mouse pointer coordinates */
  int pos = layout_decode_y_coord(y - top_scry);
  if (pos > NColourBands)
  {
    pos = NColourBands; /* y coordinate is off the top */
  }
  else if (pos < 0)
  {
    pos = 0; /* y coordinate is off the bottom */
  }

  DEBUGF("Nearest higher colour band is %d\n", pos);

  /* Does the caller wants to know whether the pointer is over the selection? */
  if (within_sel != NULL)
  {
    if (!editor_has_selection(get_editor(edit_win)))
    {
      DEBUGF("No selection\n");
      *within_sel = false;
    }
    else
    {
      /* Calculate the upper and lower bounds of the selection in screen
         coordinates */
      int sel_low, sel_high;
      editor_get_selection_range(get_editor(edit_win), &sel_low, &sel_high);

      BBox selection_bbox;
      layout_get_selection_bbox(sel_low, sel_high, &selection_bbox);

      int const selstart_scry = top_scry + selection_bbox.ymin;
      int const selend_scry = top_scry + selection_bbox.ymax;
      DEBUGF("Selection is from y=%d to %d\n", selstart_scry, selend_scry);

      *within_sel = (y >= selstart_scry && y < selend_scry);
    }
    DEBUGF("Pointer %s within selection\n", *within_sel ? "is" : "isn't");
  }
  return pos;
}

/* ----------------------------------------------------------------------- */

static void abort_drag(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  if (edit_win == drag_view)
  {
    switch (drag_type)
    {
      case DragType_Rubber:
        /* The selection is being dragged to resize it */
        scheduler_deregister(drag_selection, edit_win);
        EditWin_stop_auto_scroll(edit_win);
        ON_ERR_RPT(wimp_drag_box(CancelDrag));
        break;

      case DragType_Data:
        /* Selected colours are being dragged */
        ON_ERR_RPT(drag_abort());
        break;

      case DragType_None:
        /* Nothing to do */
        break;

      default:
        assert("Unknown drag type" == NULL);
        break;
    }
    drag_type = DragType_None;
    drag_view = NULL;
  }
}

/* ----------------------------------------------------------------------- */

static void scroll_to_caret(EditWin *const edit_win)
{
  /* Scroll window if necessary to make caret visible */
  assert(edit_win != NULL);
  int sel_low, sel_high;
  editor_get_selection_range(get_editor(edit_win), &sel_low, &sel_high);

  DEBUGF("Will scroll editor %p to reveal selection at %d,%d\n", (void *)edit_win,
        sel_low, sel_high);

  sky_cancel_io(edit_win->file);
  update_menus(edit_win);
  abort_drag(edit_win);

  WimpGetWindowStateBlock window_state;
  window_state.window_handle = edit_win->wimp_handle;
  if (E(wimp_get_window_state(&window_state)))
  {
    return;
  }

  BBox caret_bbox;
  if (editor_has_selection(get_editor(edit_win)))
  {
    layout_get_selection_bbox(sel_low, sel_high, &caret_bbox);
  }
  else
  {
    layout_get_caret_bbox(editor_get_caret_pos(get_editor(edit_win)), &caret_bbox);
  }

  DEBUGF("Selection is y=%d..%d (in work area)\n",
    caret_bbox.ymin, caret_bbox.ymax);

  int const vis_height = window_state.visible_area.ymax -
                         window_state.visible_area.ymin;
  assert(vis_height >= 0);
  assert(vis_height <= layout_get_height());
  int const real_vis_height = vis_height - ToolbarHeight - (1 << y_eigen);
  assert(real_vis_height >= 0);

  int const old_scroll = window_state.yscroll;

  BBox bands_bbox;
  layout_get_bands_bbox(0, 1, &bands_bbox);
  int const scroll_step = ScrollToCaretStepSize *
                          (bands_bbox.ymax - bands_bbox.ymin);

  int const caret_height = caret_bbox.ymax - caret_bbox.ymin;
  assert(caret_height >= 0);

  if (window_state.yscroll < ToolbarHeight + (1<<y_eigen) + caret_bbox.ymax)
  {
    window_state.yscroll = ToolbarHeight + (1<<y_eigen) + caret_bbox.ymax;

    /* Scroll in bigger steps if there is room */
    if (real_vis_height >= caret_height + scroll_step)
    {
      window_state.yscroll += scroll_step;
    }
  }
  else if (window_state.yscroll > caret_bbox.ymin + vis_height)
  {
    window_state.yscroll = caret_bbox.ymin + vis_height;

    /* Scroll in bigger steps if there is room */
    if (real_vis_height >= caret_height + scroll_step)
    {
      window_state.yscroll -= scroll_step;
    }
  }

  if (old_scroll != window_state.yscroll) {
    ON_ERR_RPT(toolbox_show_object(0,
                          edit_win->window_id,
                          Toolbox_ShowObject_FullSpec,
                          &window_state.visible_area,
                          NULL_ObjectId,
                          NULL_ComponentId));
  }
}

/* ----------------------------------------------------------------------- */

static bool set_title_cb(EditWin *const edit_win, void *const arg)
{
  assert(edit_win != NULL);
  assert(arg != NULL);
  char * const title = arg;

  if (E(window_set_title(0, edit_win->window_id, title)))
  {
    return true;
  }

  if (edit_win->on_menu)
  {
    return E(ViewsMenu_setname(edit_win->window_id, title, NULL));
  }

  if (E(ViewsMenu_add(edit_win->window_id, title, "" /* obsolete */ )))
  {
    return true;
  }

  edit_win->on_menu = true;
  return false;
}

/* ----------------------------------------------------------------------- */

static bool set_title(SkyFile *const file)
{
  assert(file != NULL);

  char const * const path =
    userdata_get_file_name_length(&file->list_node) == 0 ?
      msgs_lookup("Untitled") : userdata_get_file_name(&file->list_node);

  int const view_count = file->num_views;
  char view_count_str[sizeof(VIEWS_SUFFIX)+16] = "";
  if (view_count > 1)
  {
    sprintf(view_count_str, VIEWS_SUFFIX, view_count);
  }

  StringBuffer title_buffer;
  stringbuffer_init(&title_buffer);
  bool success = false;

  if (!stringbuffer_append_all(&title_buffer, path) ||
      (file->changed_since_save &&
       !stringbuffer_append_all(&title_buffer, UNSAVED_SUFFIX)) ||
      !stringbuffer_append_all(&title_buffer, view_count_str))
  {
    RPT_ERR("NoMem");
  }
  else
  {
    char * const title = stringbuffer_get_pointer(&title_buffer);
    if (for_each_view(file, set_title_cb, title) == NULL)
    {
      success = true;
    }
  }
  stringbuffer_destroy(&title_buffer);
  return success;
}

/* ----------------------------------------------------------------------- */

static void has_changed(SkyFile *const file)
{
  assert(file != NULL);

  /* Mark file as having been changed since last save, unless it already
     has unsaved changes */
  if (!file->changed_since_save)
  {
    DEBUGF("Marking file %p as changed\n", (void *)file);
    file->changed_since_save = true;
    (void)set_title(file);
  }

  /* Re-render sky preview (if any) */
  if (file->preview_data != NULL)
  {
    Preview_update(file->preview_data);
  }
}

/* ----------------------------------------------------------------------- */

static bool handle_edit(SkyFile *const file, EditResult const r)
{
  switch (r)
  {
    case EditResult_Changed:
      has_changed(file);
      break;
    case EditResult_NoMem:
      RPT_ERR("NoMem");
      break;
    case EditResult_Unchanged:
      break;
    default:
      assert("Bad result code" == NULL);
      break;
  }
  return r != EditResult_NoMem;
}

/* ----------------------------------------------------------------------- */

static void show_preview(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  SkyFile *const file = edit_win->file;
  assert(file != NULL);

  /* Create a preview window for this editing window, if none exists */
  if (file->preview_data == NULL)
  {
    char untitled[UntitledMaxLen];
    char const *leaf_name;

    if (userdata_get_file_name_length(&file->list_node) == 0)
    {
      /* We must use an intermediate buffer because we are combining the
         results of two message look-ups */
      STRCPY_SAFE(untitled, msgs_lookup("Untitled"));
      leaf_name = untitled;
    }
    else
    {
      leaf_name = pathtail(
        userdata_get_file_name(&file->list_node), PathElements);
    }

    file->preview_data = Preview_create(file, leaf_name);
    if (file->preview_data == NULL)
    {
      return;
    }
  }

  Preview_show(file->preview_data, edit_win->window_id);
}

/* ----------------------------------------------------------------------- */

static bool redraw_bbox_cb(EditWin *const edit_win, void *const arg)
{
  BBox *const bbox = arg;

  assert(edit_win != NULL);
  assert(bbox != NULL);
  assert(bbox->xmin <= bbox->xmax);
  assert(bbox->ymin <= bbox->ymax);

  ON_ERR_RPT(window_force_redraw(0, edit_win->window_id, bbox));

  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static void redraw_bbox(Editor *const editor, BBox *const bbox)
{
  assert(editor != NULL);
#if PER_VIEW_SELECT
  redraw_bbox_cb(CONTAINER_OF(editor, EditWin, editor), bbox);
#else
  SkyFile *const file = CONTAINER_OF(editor, SkyFile, editor);
  (void)for_each_view(file, redraw_bbox_cb, bbox);
#endif
}

/* ----------------------------------------------------------------------- */

static void redraw_caret(Editor *const editor, int const pos)
{
  DEBUGF("Force redraw of caret position %d for editor %p\n", pos,
    (void *)editor);

  assert(pos >= 0);
#if PER_VIEW_SELECT
  if (CONTAINER_OF(editor, EditWin, editor)->has_input_focus)
#endif
  {
    BBox caret_bbox;
    layout_get_caret_bbox(pos, &caret_bbox);
    DEBUGF("y=%d..%d (in work area)\n", caret_bbox.ymin, caret_bbox.ymax);
    redraw_bbox(editor, &caret_bbox);
  }
}

/* ----------------------------------------------------------------------- */

static void redraw_select(Editor *const editor, int const start,
  int const end)
{
  DEBUGF("Force redraw of selection %d to %d for editor %p\n", start, end,
    (void *)editor);
  assert(start >= 0);
  assert(start <= end);
  if (end != start)
  {
    BBox redraw_box;
    layout_get_bands_bbox(start, end, &redraw_box);
    redraw_bbox(editor, &redraw_box);
  }
}

/* ----------------------------------------------------------------------- */

static void redraw_current_select(EditWin *const edit_win)
{
  /* Force redraw of caret position or selection area
     in the window that has lost the input focus */
  Editor *const editor = get_editor(edit_win);
  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);

  if (sel_low == sel_high)
  {
    redraw_caret(editor, sel_low);
  }
  else
  {
    redraw_select(editor, sel_low, sel_high);
  }
}

/* ----------------------------------------------------------------------- */

static void redraw_select_cb(Editor *const editor, int const old_low,
  int const old_high, int const new_low, int const new_high)
{
  /* Force an area of the editing window to be redrawn which covers both
     the previous selection and the new selection. */
  assert(old_low >= 0);
  assert(old_low <= old_high);
  assert(old_high <= NColourBands);
  assert(new_low >= 0);
  assert(new_low <= new_high);
  assert(new_high <= NColourBands);
  assert(old_low != new_low || old_high != new_high);

  if (old_low == old_high)
  {
    /* Undraw caret in old position */
    redraw_caret(editor, old_low);

    if (new_low == new_high)
    {
      /* Draw caret in new position */
      redraw_caret(editor, new_low);
    }
    else
    {
      /* Draw new selection */
      redraw_select(editor, new_low, new_high);
    }
  }
  else if (new_low == new_high)
  {
    /* Undraw old selection */
    redraw_select(editor, old_low, old_high);

    /* Draw caret in new position */
    redraw_caret(editor, new_low);
  }
  else if (new_high <= old_low || old_high <= new_low)
  {
    DEBUGF("No overlap between old and new selection\n");

    /* Undraw old selection */
    redraw_select(editor, old_low, old_high);

    /* Draw new selection */
    redraw_select(editor, new_low, new_high);
  }
  else
  {
    /* This part is specific to the on-screen representation of the
       selection, which is why this function isn't in the editor. */
    DEBUGF("Calculating shift in high positions\n");

    if (old_low < new_low)
    {
      /* Undraw old selection */
      redraw_select(editor, old_low, new_low);
    }
    else if (old_low > new_low)
    {
      /* Draw new selection */
      redraw_select(editor, new_low, old_low);
    }

    if (old_high < new_high)
    {
      /* Draw new selection */
      redraw_select(editor, old_high, new_high);
    }
    else if (old_high > new_high)
    {
      /* Undraw old selection */
      redraw_select(editor, new_high, old_high);
    }
  }
}

/* ----------------------------------------------------------------------- */

static void redraw_ghost(Editor *const editor, int const pos)
{
  DEBUGF("Force redraw of ghost caret position %d for editor %p\n", pos,
    (void *)editor);

  assert(pos >= 0);
  BBox caret_bbox;
  layout_get_caret_bbox(pos, &caret_bbox);
  DEBUGF("y=%d..%d (in work area)\n", caret_bbox.ymin, caret_bbox.ymax);
  redraw_bbox_cb(CONTAINER_OF(editor, EditWin, ghost), &caret_bbox);
}

/* ----------------------------------------------------------------------- */

static void ghost_changed_cb(Editor *const editor, int const old_low,
  int const old_high, int const new_low, int const new_high)
{
  assert(old_low >= 0);
  assert(old_low <= old_high);
  assert(old_high <= NColourBands);
  assert(new_low >= 0);
  assert(new_low <= new_high);
  assert(new_high <= NColourBands);
  assert(old_low != new_low || old_high != new_high);

  EditWin *const edit_win = CONTAINER_OF(editor, EditWin, ghost);
  if (edit_win->drop_pending)
  {
    if (old_low == old_high)
    {
      redraw_ghost(editor, old_low);
    }

    if (new_low == new_high)
    {
      redraw_ghost(editor, new_low);
    }
  }
}

/* ----------------------------------------------------------------------- */

static bool set_star_height(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  Sky const *const sky = editor_get_sky(get_editor(edit_win));
  int const height = sky_get_stars_height(sky);

  DEBUGF("Set star height %d in view %p\n", height, (void *)edit_win);
  return !E(numberrange_set_value(0, edit_win->toolbar_id,
              ComponentId_StarsAlt_NumRange, height));
}

/* ----------------------------------------------------------------------- */

static bool set_star_height_cb(EditWin *const edit_win, void *const arg)
{
  assert(edit_win != NULL);
  NOT_USED(arg);
  /* Stop iteration on error */
  return !set_star_height(edit_win);
}

/* ----------------------------------------------------------------------- */

static bool set_render_offset(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  Sky const *const sky = editor_get_sky(get_editor(edit_win));
  int const render_offset = sky_get_render_offset(sky);

  DEBUGF("Set render offset %d in view %p\n", render_offset, (void *)edit_win);
  return !E(numberrange_set_value(0, edit_win->toolbar_id,
              ComponentId_CompOffset_NumRange, render_offset));
}

/* ----------------------------------------------------------------------- */

static bool set_render_offset_cb(EditWin *const edit_win, void *const arg)
{
  assert(edit_win != NULL);
  NOT_USED(arg);
  /* Stop iteration on error */
  return !set_render_offset(edit_win);
}

/* ----------------------------------------------------------------------- */

static void redraw_bands(EditSky *const edit_sky, int const start,
  int const end)
{
  DEBUGF("Force redraw of colour bands %d to %d for file %p\n", start, end,
    (void *)edit_sky);
  assert(edit_sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  if (end == start)
  {
    return;
  }

  BBox redraw_box;
  layout_get_bands_bbox(start, end, &redraw_box);

  SkyFile *const file = CONTAINER_OF(edit_sky, SkyFile, edit_sky);
  (void)for_each_view(file, redraw_bbox_cb, &redraw_box);
}

/* ----------------------------------------------------------------------- */

static void redraw_render_offset(EditSky *const edit_sky)
{
  DEBUGF("Force redraw of render offset for file %p\n", (void *)edit_sky);
  assert(edit_sky != NULL);

  SkyFile *const file = CONTAINER_OF(edit_sky, SkyFile, edit_sky);
  (void)for_each_view(file, set_render_offset_cb, NULL);
}

/* ----------------------------------------------------------------------- */

static void redraw_stars_height(EditSky *const edit_sky)
{
  DEBUGF("Force redraw of min stars height for file %p\n", (void *)edit_sky);
  assert(edit_sky != NULL);

  SkyFile *const file = CONTAINER_OF(edit_sky, SkyFile, edit_sky);
  (void)for_each_view(file, set_star_height_cb, NULL);
}

/* ----------------------------------------------------------------------- */

static void caret_lost(void *const client_handle)
{
  EditWin *const edit_win = client_handle;

  DEBUGF("Notified that input focus lost from view %p\n", (void *)edit_win);
  assert(edit_win != NULL);

  if (edit_win->has_input_focus)
  {
    redraw_current_select(edit_win);
    edit_win->has_input_focus = false;
  }
}

/* ----------------------------------------------------------------------- */

static void claim_caret(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  if (!edit_win->has_input_focus &&
      !E(entity2_claim(Wimp_MClaimEntity_CaretOrSelection, NULL, NULL, NULL,
                       caret_lost, edit_win)))
  {
    edit_win->has_input_focus = true;
    redraw_current_select(edit_win);
  }
}

/* ======================== Wimp event handlers ========================== */

static int open_window(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (trap_caret && !editor_has_selection(get_editor(edit_win)))
    fetch_caret(edit_win, &event->open_window_request);

  ON_ERR_RPT(toolbox_show_object(0,
                                 id_block->self_id,
                                 Toolbox_ShowObject_FullSpec,
                                 &event->open_window_request.visible_area,
                                 NULL_ObjectId,
                                 NULL_ComponentId));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int close_window(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  WimpGetPointerInfoBlock ptr;

  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  /* Check for ADJUST-click on close icon */
  if (!E(wimp_get_pointer_info(&ptr)))
  {
    EditWin * const edit_win = handle;
    bool show_parent = false, close_window = true;

    if (TEST_BITS(ptr.button_state, Wimp_MouseButtonAdjust))
    {
      if (key_pressed(IntKeyNum_Shift))
      {
        /* ADJUST click with shift: Open parent but don't close window */
        close_window = false;
      }
      show_parent = true;
    }

    if (close_window && edit_win->file->changed_since_save &&
        edit_win->file->num_views == 1)
    {
      /* Ask them whether to save or discard changes */
      DCS_query_unsaved(edit_win->window_id, show_parent);
    }
    else
    {
      if (show_parent)
        EditWin_show_parent_dir(edit_win);

      if (close_window)
        EditWin_destroy(edit_win);
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

#ifdef USE_WIMP_CARET_EVENTS
static int lose_caret(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Keep track of whether this view has the input focus */
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  EditWin *const edit_win = handle;
  assert(edit_win != NULL);

  if (edit_win->has_input_focus)
  {
    entity2_release(Wimp_MClaimEntity_CaretOrSelection);
  }

  return 1; /* claim event */
}
#endif

/* ----------------------------------------------------------------------- */

static int gain_caret(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Keep track of whether this view has the input focus */
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  claim_caret(handle);
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int scroll_request(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Handle scroll request events */
  WimpScrollRequestEvent * const wsre = (WimpScrollRequestEvent *)event;
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  DEBUGF("Scroll request for window %d: x change %d, y change %d\n",
        wsre->open.window_handle, wsre->xscroll, wsre->yscroll);

  DEBUGF("Current scroll offsets: %d,%d\n", wsre->open.xscroll,
        wsre->open.yscroll);

  int const vis_height = wsre->open.visible_area.ymax -
                         wsre->open.visible_area.ymin;
  assert(vis_height >= 0);
  assert(vis_height <= layout_get_height());
  int const real_vis_height = vis_height - ToolbarHeight - (1 << y_eigen);
  assert(real_vis_height >= 0);

  int new_y_scroll = wsre->open.yscroll;

  switch (wsre->yscroll)
  {
    case WimpScrollRequest_PageLeftDown:
      new_y_scroll -= real_vis_height;
      new_y_scroll = HIGHEST(new_y_scroll, -layout_get_height() + vis_height);
      break;

    case WimpScrollRequest_LeftDown:
      new_y_scroll -= ScrollStepSize;
      new_y_scroll = HIGHEST(new_y_scroll, -layout_get_height() + vis_height);
      break;

    case WimpScrollRequest_RightUp:
      new_y_scroll += ScrollStepSize;
      new_y_scroll = LOWEST(new_y_scroll, 0);
      break;

    case WimpScrollRequest_PageRightUp:
      new_y_scroll += real_vis_height;
      new_y_scroll = LOWEST(new_y_scroll, 0);
      break;
  }
  DEBUGF("Adjusted y scroll offset: %d\n", new_y_scroll);

#ifdef SUPPORT_X_SCROLL
  int const vis_width = wsre->open.visible_area.xmax -
                        wsre->open.visible_area.xmin;
  assert(vis_width >= 0);
  assert(vis_width <= layout_get_width());

  int new_x_scroll = wsre->open.xscroll;

  switch (wsre->xscroll)
  {
    case WimpScrollRequest_PageLeftDown:
      new_x_scroll -= vis_width;
      new_x_scroll = HIGHEST(new_x_scroll, 0);
      break;

    case WimpScrollRequest_LeftDown:
      new_x_scroll -= ScrollStepSize;
      new_x_scroll = HIGHEST(new_x_scroll, 0);
      break;

    case WimpScrollRequest_RightUp:
      new_x_scroll += ScrollStepSize;
      new_x_scroll = LOWEST(new_x_scroll, layout_get_width() - vis_width);
      break;

    case WimpScrollRequest_PageRightUp:
      new_x_scroll += vis_width;
      assert(vis_width <= layout_get_width());
      new_x_scroll = LOWEST(new_x_scroll, layout_get_width() - vis_width);
      break;
  }
  DEBUGF("Adjusted x scroll offset: %d\n", new_x_scroll);
#endif


#ifdef SUPPORT_X_SCROLL
  if (new_y_scroll != wsre->open.yscroll ||
      new_x_scroll != wsre->open.xscroll)
#else
  if (new_y_scroll != wsre->open.yscroll)
#endif
  {
    wsre->open.yscroll = new_y_scroll;
#ifdef SUPPORT_X_SCROLL
    wsre->open.xscroll = new_x_scroll;
#endif
    if (!E(toolbox_show_object(0,
                               id_block->self_id,
                               Toolbox_ShowObject_FullSpec,
                               &wsre->open.visible_area,
                               id_block->parent_id,
                               id_block->parent_component)) &&

        trap_caret && !editor_has_selection(get_editor(edit_win)))
    {
      fetch_caret(edit_win, &wsre->open);
    }
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void simulate_scroll(EditWin *const edit_win,
  IdBlock *const id_block, int const yscroll)
{
  /* Vertically scroll our editing window */
  WimpScrollRequestEvent scroll_request_event;

  assert(edit_win != NULL);
  assert(id_block != NULL);

  scroll_request_event.open.window_handle = edit_win->wimp_handle;
  if (!E(wimp_get_window_state(
    (WimpGetWindowStateBlock *)&scroll_request_event)))
  {
#ifdef SUPPORT_X_SCROLL
    scroll_request_event.xscroll = 0;
#endif
    scroll_request_event.yscroll = yscroll;

    scroll_request(Wimp_EScrollRequest,
                   (WimpPollBlock *)&scroll_request_event,
                   id_block,
                   edit_win);
  }
}

/* ----------------------------------------------------------------------- */

static int redraw_window(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Custom redraw for editing window */
  EditWin *const edit_win = handle;
  const WimpRedrawWindowRequestEvent * const wrwre =
    (WimpRedrawWindowRequestEvent *)event;
  WimpRedrawWindowBlock block;
  int more;
  const _kernel_oserror *e = NULL;

  NOT_USED(event_code);
  assert(event != NULL);
  NOT_USED(id_block);
  assert(handle != NULL);
  DEBUGF("Request to redraw window handle 0x%x\n", wrwre->window_handle);

  block.window_handle = wrwre->window_handle;
  for (e = wimp_redraw_window(&block, &more);
       e == NULL && more;
       e = wimp_get_rectangle(&block, &more))
  {
    DEBUGF("Redraw rectangle is %d,%d,%d,%d\n",
           block.redraw_area.xmin, block.redraw_area.ymin,
           block.redraw_area.xmax, block.redraw_area.ymax);

    /* Calculate origin of work area in screen coordinates */
    int const top_scry = block.visible_area.ymax - block.yscroll;
    int const left_scrx = block.visible_area.xmin - block.xscroll;

    /* Convert redraw rectangle from screen to work area coordinates */
    block.redraw_area.xmin -= left_scrx;
    block.redraw_area.xmax -= left_scrx;
    block.redraw_area.ymin -= top_scry;
    block.redraw_area.ymax -= top_scry;

    layout_redraw_bbox(left_scrx, top_scry, &block.redraw_area,
                       get_editor(edit_win),
                       edit_win->drop_pending ? &edit_win->ghost : NULL,
                       palette, edit_win->has_input_focus);
  }

  ON_ERR_RPT(e);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int user_drag(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  const WimpUserDragBoxEvent * const wudbe = (WimpUserDragBoxEvent *)event;
  NOT_USED(event_code);
  assert(event != NULL);
  NOT_USED(id_block);
  NOT_USED(handle);

  /* Was the user dragging a rubber-band selection box? */
  if (drag_view == NULL || drag_type != DragType_Rubber)
    return 0; /* No - do not claim event */

  EditWin *const edit_win = drag_view;
  DEBUGF("User has finished dragging a selection box %d,%d,%d,%d\n",
        wudbe->bbox.xmin, wudbe->bbox.ymin,
        wudbe->bbox.xmax, wudbe->bbox.ymax);

  abort_drag(edit_win);
  resize_selection(edit_win, wudbe->bbox.ymin);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int mouse_click(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* In order that the pseudo-transient dbox mechanism can work
     we pass mouse click events on rather than claiming them */
  const WimpMouseClickEvent * const wmce = (WimpMouseClickEvent *)event;
  EditWin *const edit_win = handle;
  WimpGetWindowStateBlock window_state;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  DEBUGF("Mouse click at x=%d y=%d, buttons are %d\n", wmce->mouse_x,
        wmce->mouse_y, wmce->buttons);

  if (wmce->buttons == Wimp_MouseButtonMenu)
    return 0; /* event not handled */

  /* Get the current state of the editing window that was clicked on */
  window_state.window_handle = edit_win->wimp_handle;
  if (E(wimp_get_window_state(&window_state)))
    return 0; /* do not claim event */

  /* Find the index of the colour band nearest to the mouse click coordinates,
     and whether or not the click was within a selection. */
  bool within_select;
  int const click_pos = decode_pointer_pos(edit_win, &window_state,
    wmce->mouse_y, &within_select);

  switch (wmce->buttons)
  {
    case Wimp_MouseButtonSelect * MouseButtonModifier_Single:
      /* Is the mouse click within a selection? */
      if (within_select)
      {
        /* Mouse click was within a selection. Check whether the Ctrl key is
           currently held down (overrides inaction pending start of drag). */
        if (key_pressed(IntKeyNum_Ctrl))
        {
          DEBUGF("Ctrl key overrides click on selection\n");
          EditWin_set_caret_pos(edit_win, click_pos);
        }
      }
      else
      {
        /* No - set caret position */
        EditWin_set_caret_pos(edit_win, click_pos);
      }
      EditWin_give_focus(edit_win);
      break;

    case Wimp_MouseButtonAdjust * MouseButtonModifier_Single:
      /* Create a new selection between caret and mouse pointer position, or
         move nearest endpoint of selection to pointer position */
      if (editor_set_selection_nearest(get_editor(edit_win), click_pos))
      {
        sky_cancel_io(edit_win->file);
        update_menus(edit_win);
      }
      EditWin_give_focus(edit_win);
      break;

    case Wimp_MouseButtonSelect * MouseButtonModifier_Drag:
    case Wimp_MouseButtonAdjust * MouseButtonModifier_Drag:
      if (wmce->buttons != Wimp_MouseButtonAdjust * MouseButtonModifier_Drag &&
          within_select)
      {
        /* Drag selected colour bands */

        /* Translate pointer position to work area coordinates */
        int const x_origin = window_state.visible_area.xmin -
                             window_state.xscroll;
        int const y_origin = window_state.visible_area.ymax -
                             window_state.yscroll;
        DEBUGF("Work area origin in screen coordinates is %d,%d\n",
              x_origin, y_origin);

        int sel_low, sel_high;
        editor_get_selection_range(get_editor(edit_win), &sel_low, &sel_high);
        BBox selected_bbox;
        layout_get_selection_bbox(sel_low, sel_high, &selected_bbox);

        sky_cancel_io(edit_win->file);
        if (!IO_start_drag(edit_win,
                           wmce->mouse_x - x_origin,
                           wmce->mouse_y - y_origin,
                           &selected_bbox))
          break;

        drag_type = DragType_Data;
      }
      else
      {
        /* Start new selection or move near end of existing selection */
        WimpDragBox drag_box;
        unsigned int autoscroll_flags;

        if (E(scheduler_register_delay(drag_selection,
                                       edit_win,
                                       DragUpdateFrequency,
                                       DragUpdatePriority)))
          break;

        /* Start auto-scrolling immediately */
        EditWin_start_auto_scroll(edit_win,
                                  &window_state.visible_area,
                                  0,
                                  &autoscroll_flags);

        drag_box.drag_type = Wimp_DragBox_DragPoint;

        if (TEST_BITS(autoscroll_flags, Wimp_AutoScroll_Horizontal))
        {
          /* Allow drag outside the window to speed up auto-scrolling */
          drag_box.parent_box.xmin = SHRT_MIN;
          drag_box.parent_box.xmax = SHRT_MAX;
        }
        else
        {
          /* All of window's work area is already visible */
          drag_box.parent_box.xmin = window_state.visible_area.xmin;
          drag_box.parent_box.xmax = window_state.visible_area.xmax -
                                     (1<<x_eigen);
        }
        if (TEST_BITS(autoscroll_flags, Wimp_AutoScroll_Vertical))
        {
          /* Allow drag outside the window to speed up auto-scrolling */
          drag_box.parent_box.ymin = SHRT_MIN;
          drag_box.parent_box.ymax = SHRT_MAX;
        }
        else
        {
          /* All of window's work area is already visible */
          drag_box.parent_box.ymin = window_state.visible_area.ymin;
          drag_box.parent_box.ymax = window_state.visible_area.ymax -
                                     ToolbarHeight - (2<<y_eigen);
        }
        if (E(wimp_drag_box(&drag_box)))
        {
          EditWin_stop_auto_scroll(edit_win);
          scheduler_deregister(drag_selection, edit_win);
          break;
        }

        drag_type = DragType_Rubber;
      }

      drag_view = edit_win;
      break;

    case Wimp_MouseButtonSelect: /* SELECT double-click */
      if (within_select)
      {
        /* Open a dialogue box to change the colour of the selected bands */
        if (!E(Pal256_set_colour(Picker_sharedid,
          editor_get_selected_colour(get_editor(edit_win)))))
        {
          ON_ERR_RPT(toolbox_show_object(Toolbox_ShowObject_AsMenu,
                                         Picker_sharedid,
                                         Toolbox_ShowObject_AtPointer,
                                         NULL,
                                         id_block->self_id,
                                         NULL_ComponentId));
        }
      }
      break;

    default:
      /* Not interested in this button combination */
      break;
  }

  return 0; /* pass event on */
}

/* ----------------------------------------------------------------------- */

static inline bool register_wimp_handlers(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  static const struct
  {
    int event_code;
    WimpEventHandler *handler;
  }
  wimp_handlers[] =
  {
    {
      Wimp_ERedrawWindow,
      redraw_window
    },
    {
      Wimp_EOpenWindow,
      open_window
    },
    {
      Wimp_ECloseWindow,
      close_window
    },
    {
      Wimp_EMouseClick,
      mouse_click
    },
#ifdef USE_WIMP_CARET_EVENTS
    {
      Wimp_ELoseCaret,
      lose_caret
    },
#endif
    {
      Wimp_EGainCaret,
      gain_caret
    },
    {
      Wimp_EScrollRequest,
      scroll_request
    }
  };

  /* Register Wimp event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(wimp_handlers); i++)
  {
    if (E(event_register_wimp_handler(edit_win->window_id,
      wimp_handlers[i].event_code, wimp_handlers[i].handler, edit_win)))
    {
      return false;
    }
  }

  return true;
}

/* ======================== Toolbox event handlers ======================= */

static bool create_view(SkyFile *file);

static int misc_tb_event(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  EditWin *const edit_win = handle;

  NOT_USED(event);
  assert(id_block != NULL);
  assert(edit_win != NULL);

  DEBUGF("Misc Toolbox event 0x%x for object 0x%x with ancestor 0x%x\n",
         event_code, (unsigned)id_block->self_id,
         (unsigned)id_block->ancestor_id);

  /* Careful - handler is called for unclaimed toolbox events on any object */
  if (id_block->self_id != edit_win->window_id &&
      id_block->ancestor_id != edit_win->window_id)
  {
    return 0; /* event not for us - pass it on */
  }


  /* Handle hotkey/menu selection events */
  switch (event_code)
  {
    /* ------------------------------------------- */
    /*           General file operations           */

    case EventCode_FileInfo:
      show_object_relative(Toolbox_ShowObject_AsMenu, fileinfo_sharedid,
        edit_win->window_id, id_block->self_id, id_block->self_component);
      break;

    case EventCode_CloseWindow:
      abort_drag(edit_win);

      if (edit_win->file->changed_since_save &&
          edit_win->file->num_views == 1) /* Wait for response */
      {
        DCS_query_unsaved(edit_win->window_id, false);
      }
      else
      {
        EditWin_destroy(edit_win);
      }
      break;

    case EventCode_NewView:
      (void)create_view(edit_win->file);
      break;

    case EventCode_SaveFile:
      /* Open savebox */
      edit_win->destroy_pending = false;
      edit_win->parent_pending = false;
      show_object_relative(Toolbox_ShowObject_AsMenu, savebox_sharedid,
        edit_win->window_id, id_block->self_id, id_block->self_component);
      break;

    case EventCode_QuickSave:
      /* Save file immediately to current path, if any */
      EditWin_do_save(edit_win, false, false);
      break;

    case EventCode_Undo:
      if (!EditWin_can_undo(edit_win))
      {
        putchar('\a'); /* nothing to undo */
        break;
      }
      if (editor_undo(get_editor(edit_win)))
      {
        has_changed(edit_win->file);
      }
      scroll_to_caret(edit_win);
      break;

    case EventCode_Redo:
      if (!EditWin_can_redo(edit_win))
      {
        putchar('\a'); /* nothing to redo */
        break;
      }
      if (editor_redo(get_editor(edit_win), palette))
      {
        has_changed(edit_win->file);
      }
      scroll_to_caret(edit_win);
      break;

    case EventCode_SelectAll:
      if (editor_select_all(get_editor(edit_win)))
      {
        abort_drag(edit_win);
        sky_cancel_io(edit_win->file);
        update_menus(edit_win);
      }
      break;

    case EventCode_ClearSelection:
      if (editor_clear_selection(get_editor(edit_win)))
      {
        scroll_to_caret(edit_win);
      }
      break;

    case EventCode_Preview:
      show_preview(edit_win);
      break;

    case EventCode_CaretUp:
    {
#ifdef SCROLL_KEYS
      simulate_scroll(edit_win, id_block, WimpScrollRequest_RightUp);
#else
      int sel_high;
      editor_get_selection_range(get_editor(edit_win), NULL, &sel_high);
      EditWin_set_caret_pos(edit_win,
                            sel_high < NColourBands ?
                              sel_high + 1 : NColourBands);
#endif
      break;
    }
    case EventCode_CaretDown:
    {
#ifdef SCROLL_KEYS
      simulate_scroll(edit_win, id_block, WimpScrollRequest_LeftDown);
#else
      int sel_low;
      editor_get_selection_range(get_editor(edit_win), &sel_low, NULL);
      EditWin_set_caret_pos(edit_win, sel_low > 0 ? sel_low - 1 : 0);
#endif
      break;
    }
    case EventCode_PageUp:
      simulate_scroll(edit_win, id_block, WimpScrollRequest_PageRightUp);
      break;

    case EventCode_PageDown:
      simulate_scroll(edit_win, id_block, WimpScrollRequest_PageLeftDown);
      break;

    case EventCode_CaretToEnd:
      EditWin_set_caret_pos(edit_win, NColourBands);
      break;

    case EventCode_CaretToStart:
      EditWin_set_caret_pos(edit_win, 0);
      break;

    case EventCode_Goto:
      show_object_relative(Toolbox_ShowObject_AsMenu, Goto_sharedid,
        edit_win->window_id, id_block->self_id, id_block->self_component);
      break;

    /* ------------------------------------------- */
    /*           Operations on selection           */

    case EventCode_Smooth:
      abort_drag(edit_win);
      (void)handle_edit(edit_win->file, editor_smooth(get_editor(edit_win), palette));
      break;

    case EventCode_SetColour:
      abort_drag(edit_win);
      if (editor_has_selection(get_editor(edit_win)))
      {
        if (E(Pal256_set_colour(Picker_sharedid,
              editor_get_selected_colour(get_editor(edit_win)))))
          break;

        show_object_relative(Toolbox_ShowObject_AsMenu, Picker_sharedid,
          edit_win->window_id, id_block->self_id, id_block->self_component);
      }
      else
      {
        putchar('\a'); /* no colour bands selected */
      }
      break;

    case EventCode_Copy:
      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no selection to copy */
      }
      else
      {
        if (!IO_copy(edit_win))
          break;

        /* Update menu to reflect that we can now paste data */
        update_menus(edit_win);
      }
      break;

    case EventCode_Cut:
      abort_drag(edit_win);
      sky_cancel_io(edit_win->file);

      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no selection to cut */
      }
      else
      {
        if (IO_copy(edit_win))
        {
          EditWin_delete_colours(edit_win); /* also updates menus */
        }
      }
      break;

    case EventCode_Delete:
      abort_drag(edit_win);
      sky_cancel_io(edit_win->file);

      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no selection to delete */
      }
      else
      {
        EditWin_delete_colours(edit_win);
      }
      break;

    case EventCode_Interpolate:
      abort_drag(edit_win);

      if (editor_has_selection(get_editor(edit_win)))
      {
        ON_ERR_RPT(wimp_create_menu(CloseMenu, 0, 0));

        show_object_relative(0, Interpolate_sharedid, edit_win->window_id,
          id_block->self_id, id_block->self_component);
      }
      else
      {
        putchar('\a'); /* selection not big enough to interpolate start/end */
      }
      break;

    /* ------------------------------------------- */
    /*           Operations at caret               */

    case EventCode_Paste:
    {
      abort_drag(edit_win);
      IO_paste(edit_win);
      break;
    }
    case EventCode_Insert:
    {
      ON_ERR_RPT(wimp_create_menu(CloseMenu, 0, 0));

      show_object_relative(0, Insert_sharedid, edit_win->window_id,
        id_block->self_id, id_block->self_component);
      break;
    }
    /* ------------------------------------------- */

    case EventCode_AbortDrag: /* self explanatory */
      abort_drag(edit_win);
      break;

    default:
      DEBUGF("Unknown misc event\n");
      return 0; /* not interested */
  }

  DEBUGF("Claiming misc event\n");
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int value_changed(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const NumberRangeValueChangedEvent * const nrvc =
    (NumberRangeValueChangedEvent *)event;
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  switch (id_block->self_component)
  {
     case ComponentId_CompOffset_NumRange:
       /* User has changed the minimum sky height */
       SkyFile_set_render_offset(edit_win->file, nrvc->new_value);
       break;

     case ComponentId_StarsAlt_NumRange:
       /* User has changed the threshold for plotting stars */
       SkyFile_set_star_height(edit_win->file, nrvc->new_value);
       break;

     default:
       return 0; /* unknown gadget (event not handled) */
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static inline bool init_tool_bar(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  /* Get the Object Id of the toolbar used to display header values */
  if (E(window_get_tool_bars(Window_InternalTopLeftToolbar,
                             edit_win->window_id, NULL,
                             &edit_win->toolbar_id, NULL, NULL)))
    return false;

  /* Get the Wimp handle of the toolbar, to make handling
     of DataLoad and DataSave messages more efficient */
  if (E(window_get_wimp_handle(0, edit_win->toolbar_id,
                               &edit_win->toolbar_wimp_handle)))
    return false;

  if (!set_star_height(edit_win) || !set_render_offset(edit_win))
    return false;

  return !E(event_register_toolbox_handler(edit_win->toolbar_id,
            NumberRange_ValueChanged, value_changed, edit_win));
}

/* ----------------------------------------------------------------------- */

static EditWin *add_view(SkyFile *const file)
{
  assert(file != NULL);
  EditWin *const edit_win = malloc(sizeof(*edit_win));
  if (edit_win == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *edit_win = (EditWin){
    .file = file,
    .window_id = NULL_ObjectId,
    .toolbar_id = NULL_ObjectId,
    .wimp_handle = WimpWindow_Top,
    .toolbar_wimp_handle = WimpWindow_Top,
    .has_input_focus = false,
    .parent_pending = false,
    .destroy_pending = false,
    .drop_pending = false,
    .on_menu = false,
  };

  linkedlist_insert(&file->views, NULL, &edit_win->node);
  ++file->num_views;

#if PER_VIEW_SELECT
  editor_init(&edit_win->editor, &file->edit_sky, redraw_select_cb);
#endif
  editor_init(&edit_win->ghost, &file->edit_sky, ghost_changed_cb);

  return edit_win;
}

/* ----------------------------------------------------------------------- */

static void remove_view(EditWin *const edit_win)
{
  assert(edit_win != NULL);

#if PER_VIEW_SELECT
  editor_destroy(&edit_win->editor);
#endif
  editor_destroy(&edit_win->ghost);

  SkyFile *const file = edit_win->file;
  assert(file != NULL);
  assert(file->num_views > 0);
  --file->num_views;
  linkedlist_remove(&file->views, &edit_win->node);

  free(edit_win);
}

/* ----------------------------------------------------------------------- */

static void destroy_view(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  DEBUGF("Destroying view %p (object 0x%x)\n", (void *)edit_win, edit_win->window_id);

  /* Release the caret/selection */
  if (edit_win->has_input_focus)
  {
    entity2_release(Wimp_MClaimEntity_CaretOrSelection);
  }

  /* Stop any drag that may be in progress */
  abort_drag(edit_win);

  /* Destroy main Window object */
  ON_ERR_RPT(remove_event_handlers_delete(edit_win->window_id));

  /* Hide any dialogue boxes that were shown as children of the deleted
     Window object */
  hide_shared_if_child(edit_win->window_id, Interpolate_sharedid);
  hide_shared_if_child(edit_win->window_id, Insert_sharedid);

  /* Hide any transient dialogue boxes that may have been shown as
     children of the deleted Window object. If such objects are shown
     repeatedly then the Toolbox can forget they are showing and
     refuse to hide them. */
  ON_ERR_RPT(wimp_create_menu(CloseMenu,0,0));

  if (edit_win->on_menu)
  {
    ON_ERR_RPT(ViewsMenu_remove(edit_win->window_id));
  }

  /* Finalise the I/O subsystem for this view */
  IO_view_deleted(edit_win);

  /* Deregister event handlers for toolbar */
  ON_ERR_RPT(event_deregister_toolbox_handlers_for_object(
               edit_win->toolbar_id));

  /* Deregister the handler for custom Toolbox events
     (generated by key shortcuts and menu entries) */
  ON_ERR_RPT(event_deregister_toolbox_handler(-1, -1, misc_tb_event, edit_win));

  remove_view(edit_win);
}

/* ----------------------------------------------------------------------- */

static bool destroy_view_cb(EditWin *const edit_win, void *const arg)
{
  NOT_USED(arg);
  destroy_view(edit_win);
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static bool create_view(SkyFile *const file)
{
  assert(file != NULL);

  /* Grab memory for view status */
  EditWin *const edit_win = add_view(file);
  if (edit_win == NULL)
  {
    return false;
  }

  if (!E(toolbox_create_object(0, "EditWin", &edit_win->window_id)))
  {
    DEBUGF("Created window 0x%x\n", edit_win->window_id);

    /* Initialise the I/O subsystem for this view */
    if (IO_view_created(edit_win))
    {
      /* Register the handler for custom Toolbox events
         (generated by key shortcuts and menu entries) */
      if (!E(event_register_toolbox_handler(-1, -1, misc_tb_event, edit_win)))
      {
        if (init_tool_bar(edit_win))
        {
          do
          {
            /* Associate a pointer to the view data with the Window
               object */
            if (E(toolbox_set_client_handle(0, edit_win->window_id, edit_win)))
              break;

            /* Get the Wimp handle of the main window */
            if (E(window_get_wimp_handle(0, edit_win->window_id,
                  &edit_win->wimp_handle)))
              break;

            if (!register_wimp_handlers(edit_win))
            {
              break; /* may have partially succeeded */
            }

            /* Show the main editing window in the default position for
               the next (toolbar will be shown automatically) */
            if (E(StackViews_open(edit_win->window_id, NULL_ObjectId,
                  NULL_ComponentId)))
            {
              break;
            }

            /* Give input focus to the main window */
            if (E(wimp_set_caret_position(edit_win->wimp_handle,
                 -1, 0, 0, -1, -1)))
            {
              break;
            }

            bool const success = set_title(file);
            if (!success)
            {
              destroy_view(edit_win);

              /* Restore any pre-existing windows */
              (void)set_title(file);
            }
            return success;
          }
          while (0);

          ON_ERR_RPT(event_deregister_toolbox_handlers_for_object(
                       edit_win->toolbar_id));
        }
        (void)event_deregister_toolbox_handler(-1, -1, misc_tb_event, edit_win);
      }
      IO_view_deleted(edit_win);
    }
    (void)remove_event_handlers_delete(edit_win->window_id);
  }

  remove_view(edit_win);
  return false;
}

/* ----------------------------------------------------------------------- */

static bool userdata_is_safe(struct UserData *const item)
{
  SkyFile const * const file = CONTAINER_OF(item, SkyFile, list_node);
  assert(file != NULL);
  return !file->changed_since_save;
}

/* ----------------------------------------------------------------------- */

static void destroy_userdata(struct UserData *const item)
{
  SkyFile * const file = CONTAINER_OF(item, SkyFile, list_node);
  assert(file != NULL);
  SkyFile_destroy(file);
}

/* ----------------------------------------------------------------------- */

static inline bool init_date_stamp(SkyFile *const file,
  char const *const load_path)
{
  assert(file != NULL);

  if (load_path != NULL)
  {
    /* Get datestamp of file */
    if (E(get_date_stamp(load_path, &file->file_date)))
    {
      return false;
    }
  }
  else
  {
    /* Get current time & date */
    if (E(get_current_time(&file->file_date)))
    {
      return false;
    }
  }

  return true;
}

/* ----------------------------------------------------------------------- */

static void copy_from(EditWin *const dest_data, EditWin *const src_data)
{
  assert(dest_data != NULL);
  assert(src_data != NULL);

  Editor *const dest = get_editor(dest_data);

  if (handle_edit(dest_data->file,
    editor_copy(dest_data->drop_pending ? &dest_data->ghost : dest,
      get_editor(src_data))))
  {
    EditWin_confirm_insert_pos(dest_data);
  }
}

/* ----------------------------------------------------------------------- */

static void move_from(EditWin *const dest_data, EditWin *const src_data)
{
  assert(dest_data != NULL);
  assert(src_data != NULL);

  Editor *const dest = get_editor(dest_data);

  if (dest_data->file == src_data->file)
  {
    if (!handle_edit(dest_data->file,
      editor_move(dest_data->drop_pending ? &dest_data->ghost : dest,
        get_editor(src_data))))
    {
      return;
    }
  }
  else
  {
    if (!handle_edit(dest_data->file, editor_copy(
      dest_data->drop_pending ? &dest_data->ghost : dest,
      get_editor(src_data))))
    {
      return;
    }

    if (!handle_edit(src_data->file,
      editor_delete_colours(get_editor(src_data))))
    {
      (void)editor_undo(get_editor(dest_data));
      update_menus(dest_data);
      return;
    }

    update_menus(src_data);
  }

  EditWin_confirm_insert_pos(dest_data);
}

/* ----------------------------------------------------------------------- */

typedef struct
{
  int window_handle;
  EditWin *edit_win;
} FindWindowData;

/* ----------------------------------------------------------------------- */

static bool view_owns_handle_cb(EditWin *const edit_win, void *const arg)
{
  FindWindowData * const find_win = arg;
  assert(find_win != NULL);
  assert(find_win->edit_win == NULL);

  if (EditWin_owns_wimp_handle(edit_win, find_win->window_handle))
  {
    /* Stop iteration when view owns window */
    DEBUGF("Returning view data %p\n", (void *)edit_win);
    find_win->edit_win = edit_win;
    return true;
  }

  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static bool show_view_cb(EditWin *const edit_win, void *const arg)
{
  NOT_USED(arg);
  assert(edit_win != NULL);

  /* Bring window to the front of the stack (and deiconise, if needed) */
  ON_ERR_RPT(DeIconise_show_object(0,
                                   edit_win->window_id,
                                   Toolbox_ShowObject_Default,
                                   NULL,
                                   NULL_ObjectId,
                                   NULL_ComponentId));
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static bool sky_owns_handle_cb(UserData *const item, void *const arg)
{
  SkyFile * const file = CONTAINER_OF(item, SkyFile, list_node);

  /* Stop iteration when view owns window */
  return for_each_view(file, view_owns_handle_cb, arg) != NULL;
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

SkyFile *SkyFile_find_by_file_name(char const *const load_path)
{
  UserData *const item = userdata_find_by_file_name(load_path);
  return item ? CONTAINER_OF(item, SkyFile, list_node) : NULL;
}

/* ----------------------------------------------------------------------- */

SkyFile *SkyFile_create(Reader *const reader, char const *const load_path,
  bool const is_safe)
{
  SkyFile *const file = malloc(sizeof(*file));
  if (file == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *file = (SkyFile){
    .preview_data = NULL,
    .changed_since_save = !is_safe,
    .num_views = 0,
  };

  linkedlist_init(&file->views);

  SkyState state = edit_sky_init(&file->edit_sky, reader, redraw_bands,
    redraw_render_offset, redraw_stars_height);

  bool success = IO_report_read(state);

  if (success)
  {
#if !PER_VIEW_SELECT
    editor_init(&file->editor, &file->edit_sky, select_changed);
#endif

    success = userdata_add_to_list(&file->list_node, userdata_is_safe,
      destroy_userdata, STRING_OR_NULL(is_safe ? load_path : NULL));

    if (!success)
    {
      RPT_ERR("NoMem");
    }
    else
    {
      success = init_date_stamp(file, is_safe ? load_path : NULL);
      if (!success)
      {
        userdata_remove_from_list(&file->list_node);
      }
    }

    if (!success)
    {
      edit_sky_destroy(&file->edit_sky);
    }
  }

  if (success)
  {
    success = create_view(file);
    if (!success)
    {
      SkyFile_destroy(file);
    }
  }
  else
  {
    free(file);
  }

  return success ? file : NULL;
}

/* ----------------------------------------------------------------------- */

void SkyFile_destroy(SkyFile *const file)
{
  if (file)
  {
    Preview_destroy(file->preview_data);
    (void)for_each_view(file, destroy_view_cb, NULL);
#if !PER_VIEW_SELECT
    editor_destroy(&file->editor);
#endif
    edit_sky_destroy(&file->edit_sky);
    userdata_remove_from_list(&file->list_node);
    free(file);
  }
}

/* ----------------------------------------------------------------------- */

void SkyFile_set_star_height(SkyFile *const file, int const height)
{
  assert(file != NULL);
  handle_edit(file, edit_sky_set_stars_height(&file->edit_sky, height));
}

/* ----------------------------------------------------------------------- */

void SkyFile_set_render_offset(SkyFile *const file, int const height)
{
  assert(file != NULL);
  handle_edit(file, edit_sky_set_render_offset(&file->edit_sky, height));
}

/* ----------------------------------------------------------------------- */

void SkyFile_add_render_offset(SkyFile *const file, int const offset)
{
  assert(file != NULL);
  handle_edit(file, edit_sky_add_render_offset(&file->edit_sky, offset));
}

/* ----------------------------------------------------------------------- */

void SkyFile_export(SkyFile *const file, Writer *const writer)
{
  assert(file != NULL);

  hourglass_on();
  sky_write_file(edit_sky_get_sky(&file->edit_sky), writer);
  hourglass_off();
}

/* ----------------------------------------------------------------------- */

EditWin *SkyFile_get_win(SkyFile *const file)
{
  assert(file != NULL);
  LinkedListItem *const node = linkedlist_get_head(&file->views);
  assert(node != NULL);
  return CONTAINER_OF(node, EditWin, node);
}

/* ----------------------------------------------------------------------- */

void SkyFile_show(SkyFile *const file)
{
  (void)for_each_view(file, show_view_cb, NULL);
}

/* ----------------------------------------------------------------------- */

void EditWin_initialise(void)
{
  EF(event_register_wimp_handler(-1, Wimp_EUserDrag, user_drag, NULL));
}

/* ----------------------------------------------------------------------- */

void EditWin_destroy(EditWin *const edit_win)
{
  if (edit_win == NULL)
  {
    return;
  }

  SkyFile *const file = edit_win->file;
  if (file->num_views > 1)
  {
    destroy_view(edit_win);
    (void)set_title(file);
  }
  else
  {
    SkyFile_destroy(file);
  }
}

/* ----------------------------------------------------------------------- */

SkyFile *EditWin_get_sky(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  assert(edit_win->file != NULL);
  return edit_win->file;
}

/* ----------------------------------------------------------------------- */

void EditWin_give_focus(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  DEBUGF("Claiming input focus for view %p\n", (void *)edit_win);

  /* We must not attempt to put the caret in a hidden window
     because the window manager will return an error */
  unsigned int state;
  if (E(toolbox_get_object_state(0, edit_win->window_id, &state)) ||
      !TEST_BITS(state, Toolbox_GetObjectState_Showing))
  {
    return;
  }

  /* Give the editing window the input focus */
  ON_ERR_RPT(wimp_set_caret_position(edit_win->wimp_handle, -1, 0, 0, -1, -1));
}

/* ----------------------------------------------------------------------- */

void EditWin_show_parent_dir(EditWin const *const edit_win)
{
  /* Opens the parent directory of a file that is being edited */
  assert(edit_win != NULL);
  char const * const path = userdata_get_file_name(&edit_win->file->list_node);
  char const * const last_dot = strrchr(path, '.');
  if (last_dot == NULL)
  {
    return;
  }

  const size_t path_len = last_dot - path;
  StringBuffer command_buffer;

  stringbuffer_init(&command_buffer);
  if (!stringbuffer_append(&command_buffer, "Filer_OpenDir ", SIZE_MAX) ||
      !stringbuffer_append(&command_buffer, path, path_len))
  {
    RPT_ERR("NoMem");
  }
  else
  {
    if (_kernel_oscli(stringbuffer_get_pointer(&command_buffer)) ==
        _kernel_ERROR)
      ON_ERR_RPT(_kernel_last_oserror());
  }
  stringbuffer_destroy(&command_buffer);
}

/* ----------------------------------------------------------------------- */

void EditWin_file_saved(EditWin *const edit_win, char *save_path)
{
  assert(edit_win != NULL);
  SkyFile *const file = edit_win->file;
  file->changed_since_save = false; /* mark as unchanged */

  if (save_path == NULL)
  {
    /* Data was saved under its existing file name */
    save_path = userdata_get_file_name(&file->list_node);
  }
  else
  {
    /* Record new file name under which the data was saved */
    if (!userdata_set_file_name(&file->list_node, save_path))
    {
      RPT_ERR("NoMem");
      return;
    }
  }

  /* Get date stamp of file */
  ON_ERR_RPT(get_date_stamp(save_path, &file->file_date));

  /* Set title of editing window */
  set_title(file);

  /* Set title of preview window */
  if (file->preview_data != NULL)
  {
    Preview_set_title(file->preview_data,
                      pathtail(save_path, PathElements));
  }

  if (edit_win->parent_pending)
  {
    edit_win->parent_pending = false;
    EditWin_show_parent_dir(edit_win); /* open parent directory of file */
  }

  if (edit_win->destroy_pending)
  {
    EditWin_destroy(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_set_caret_pos(EditWin *const edit_win, int new_pos)
{
  assert(edit_win != NULL);
  DEBUGF("Set caret to %d within view %p\n", new_pos, (void *)edit_win);

  if (editor_set_caret_pos(get_editor(edit_win), new_pos))
  {
    scroll_to_caret(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_delete_colours(EditWin *const edit_win)
{
  DEBUGF("Removing selection from view %p\n", (void *)edit_win);
  assert(edit_win != NULL);

  if (handle_edit(edit_win->file, editor_delete_colours(get_editor(edit_win))))
  {
    update_menus(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

bool EditWin_insert_array(EditWin *const edit_win, int const number,
  int const *const src)
{
  assert(edit_win != NULL);

  Editor *const editor = get_editor(edit_win);
  bool is_valid = true;
  if (!handle_edit(edit_win->file, editor_insert_array(editor, number, src,
    &is_valid)))
  {
    return false;
  }

  if (!is_valid)
  {
    WARN("BadColNum");
  }

  update_menus(edit_win);
  return is_valid;
}

/* ----------------------------------------------------------------------- */

void EditWin_colour_selected(EditWin *const edit_win, int const colour)
{
  assert(edit_win != NULL);
  (void)handle_edit(edit_win->file, editor_set_plain(get_editor(edit_win), colour));
}

/* ----------------------------------------------------------------------- */

void EditWin_insert_plain(EditWin *const edit_win, int const number,
  int const col)
{
  assert(edit_win != NULL);

  if (handle_edit(edit_win->file,
    editor_insert_plain(get_editor(edit_win), number, col)))
  {
    scroll_to_caret(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_interpolate(EditWin *const edit_win,
  int const start_col, int const end_col)
{
  assert(edit_win != NULL);
  (void)handle_edit(edit_win->file, editor_interpolate(
    get_editor(edit_win), palette, start_col, end_col));
}

/* ----------------------------------------------------------------------- */

void EditWin_insert_gradient(EditWin *const edit_win,
  int const number, int const start_col, int const end_col,
  bool const inc_start, bool const inc_end)
{
  assert(edit_win != NULL);

  if (handle_edit(edit_win->file,
    editor_insert_gradient(get_editor(edit_win), palette, number, start_col,
      end_col, inc_start, inc_end)))
  {
    scroll_to_caret(edit_win);
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_drop_handler(EditWin *const dest_view,
  EditWin *const source_view, bool const shift_held)
{
  /* Drag terminated in one of our editing windows, therefore we can
     bypass the remainder of the message protocol */
  DEBUGF("Drag destination is view %p\n", (void *)dest_view);

  if (dest_view != source_view)
  {
    if (shift_held)
    {
      move_from(dest_view, source_view);
    }
    else
    {
      copy_from(dest_view, source_view);
    }
  }
  else if (shift_held)
  {
    copy_from(dest_view, source_view);
  }
  else
  {
    move_from(dest_view, source_view);
  }
}

/* ----------------------------------------------------------------------- */

int EditWin_get_colour(EditWin *const edit_win, int const pos)
{
  assert(edit_win != NULL);
  Sky *const sky = editor_get_sky(get_editor(edit_win));
  return sky_get_colour(sky, pos);
}

/* ----------------------------------------------------------------------- */

int EditWin_get_array(EditWin *const edit_win, int *const dst,
  int const dst_size)
{
  assert(edit_win != NULL);
  return editor_get_array(get_editor(edit_win), dst, dst_size);
}

/* ----------------------------------------------------------------------- */

void EditWin_insert_sky(EditWin *const edit_win, Sky const *const src)
{
  assert(edit_win != NULL);
  Editor *const editor = get_editor(edit_win);
  if (!handle_edit(edit_win->file, editor_insert_sky(editor, src)))
  {
    return;
  }
  update_menus(edit_win);
}

/* ----------------------------------------------------------------------- */

bool EditWin_has_unsaved(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return edit_win->file->changed_since_save;
}

/* ----------------------------------------------------------------------- */

void EditWin_get_selection(EditWin *const edit_win,
                           int *const start, int *const end)
{
  assert(edit_win != NULL);
  editor_get_selection_range(get_editor(edit_win), start, end);
}

/* ----------------------------------------------------------------------- */

int *EditWin_get_stamp(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return (int *)&edit_win->file->file_date;
}

/* ----------------------------------------------------------------------- */

char *EditWin_get_file_path(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  char * const file_name = userdata_get_file_name(&edit_win->file->list_node);
  return *file_name == '\0' ? NULL : file_name;
}

/* ----------------------------------------------------------------------- */

void EditWin_do_save(EditWin *const edit_win, bool const destroy,
                     bool const parent)
{
  assert(edit_win != NULL);
  edit_win->destroy_pending = destroy;
  edit_win->parent_pending = parent;

  char const * const path = userdata_get_file_name(&edit_win->file->list_node);
  if (strchr(path, '.') == NULL)
  {
    /* Must open savebox first */
    show_object_relative(Toolbox_ShowObject_AsMenu, savebox_sharedid,
      edit_win->window_id, edit_win->window_id, NULL_ComponentId);
  }
  else if (IO_export_sky_file(edit_win, path, EditWin_export))
  {
    EditWin_file_saved(edit_win, NULL /* use existing file path */);
  }
}

/* ----------------------------------------------------------------------- */

bool EditWin_owns_wimp_handle(EditWin const *const edit_win,
  int const wimp_handle)
{
  assert(edit_win != NULL);
  DEBUGF("View %p has window handles %d and %d\n", (void *)edit_win,
        edit_win->wimp_handle, edit_win->toolbar_wimp_handle);

  return wimp_handle == edit_win->wimp_handle ||
         wimp_handle == edit_win->toolbar_wimp_handle;
}

/* ----------------------------------------------------------------------- */

int EditWin_get_wimp_handle(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return edit_win->wimp_handle;
}

/* ----------------------------------------------------------------------- */

EditWin *EditWin_from_wimp_handle(int const window_handle)
{
  /* Search our list of editing windows for the drag destination */
  DEBUGF("Searching for a view with window handle %d\n", window_handle);
  FindWindowData find_win = {
    .window_handle = window_handle,
    .edit_win = NULL,
  };
  (void)userdata_for_each(sky_owns_handle_cb, &find_win);
  if (find_win.edit_win == NULL)
  {
    DEBUGF("Unrecognised window handle\n");
  }
  return find_win.edit_win;
}

/* ----------------------------------------------------------------------- */

void EditWin_remove_insert_pos(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  if (edit_win->drop_pending)
  {
    int const insert_pos = editor_get_caret_pos(&edit_win->ghost);
    DEBUGF("Hiding ghost caret at %d\n", insert_pos);
    redraw_ghost(&edit_win->ghost, insert_pos);
    edit_win->drop_pending = false;
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_set_insert_pos(EditWin *const edit_win,
  const WimpGetWindowStateBlock *const window_state, int const y)
{
  assert(edit_win != NULL);
  assert(window_state != NULL);

  /* Calculate the new ghost caret position */
  bool within_sel;
  int const new_ghost_pos = decode_pointer_pos(edit_win, window_state, y,
    &within_sel);

  /* Display no ghost caret inside a selected area */
  if (within_sel)
  {
    EditWin_remove_insert_pos(edit_win);
  }
  else
  {
    editor_set_caret_pos(&edit_win->ghost, new_ghost_pos);
    if (!edit_win->drop_pending)
    {
      edit_win->drop_pending = true;
      redraw_ghost(&edit_win->ghost, new_ghost_pos);
    }
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_confirm_insert_pos(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  if (edit_win->drop_pending)
  {
    int sel_low, sel_high;
    editor_get_selection_range(&edit_win->ghost, &sel_low, &sel_high);
    DEBUGF("Confirming ghost selection %d,%d\n", sel_low, sel_high);

    (void)editor_set_caret_pos(get_editor(edit_win), sel_low);
    (void)editor_set_selection_end(get_editor(edit_win), sel_high);
  }

  update_menus(edit_win);
  EditWin_give_focus(edit_win);
}

/* ----------------------------------------------------------------------- */

void EditWin_start_auto_scroll(EditWin const *const edit_win, BBox const *visible_area, int pause_time, unsigned int *flags_out)
{
  unsigned int flags = 0;

  assert(edit_win != NULL);
  assert(visible_area != NULL);

  /* Older versions of the window manager don't support auto-scrolling */
  if (wimp_version >= WimpAutoScrollMinVersion)
  {
    BBox work_area;

    /* Compare the window's visible area with its work area extent to
       decide whether scrolling in either direction is possible */
    if (E(window_get_extent(0, edit_win->window_id, &work_area)))
      return;

    /* If we can scroll in either direction then enable auto-scrolling */
    if (work_area.xmax - work_area.xmin >
        visible_area->xmax - visible_area->xmin)
    {
      flags |= Wimp_AutoScroll_Horizontal; /* allow horizontal scrolling */
    }

    if (work_area.ymax - work_area.ymin >
        visible_area->ymax - visible_area->ymin)
    {
      flags |= Wimp_AutoScroll_Vertical; /* allow vertical scrolling */
    }
    if (flags != 0)
    {
      WimpAutoScrollBlock auto_scroll = {
        .window_handle = edit_win->wimp_handle,
        .pause_zones.xmin = ScrollBorder,
        .pause_zones.ymin = ScrollBorder,
        .pause_zones.xmax = ScrollBorder,
        .pause_zones.ymax = ScrollBorder + ToolbarHeight + (1<<y_eigen),
        .pause_time = pause_time,
        .state_change_handler = 1, /* default pointer shapes */
        .workspace = NULL,
      };

      if (!E(wimp_auto_scroll(flags, &auto_scroll, NULL)))
      {
        DEBUGF("Enabled auto-scroll for window &%x with flags %u\n",
              auto_scroll.window_handle, flags);

        auto_scroll_view = edit_win;
      }
    }
  }

  if (flags_out != NULL)
    *flags_out = flags;
}

/* ----------------------------------------------------------------------- */

void EditWin_stop_auto_scroll(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  if (auto_scroll_view == edit_win)
  {
    DEBUGF("Stopping auto-scrolling of view %p\n", (void *)edit_win);
    auto_scroll_view = NULL;
    ON_ERR_RPT(wimp_auto_scroll(0, NULL, NULL));
  }
  else
  {
    DEBUGF("Can't stop auto-scrolling of view %p (usurped by %p?)\n",
           (void *)edit_win, (void *)auto_scroll_view);
  }
}

/* ----------------------------------------------------------------------- */

bool EditWin_export(EditWin *const edit_win, Writer *const writer)
{
  assert(edit_win != NULL);
  SkyFile_export(edit_win->file, writer);
  return true;
}

/* ----------------------------------------------------------------------- */

bool EditWin_export_sel(EditWin *const edit_win, Writer *const writer)
{
  assert(edit_win != NULL);

  /* Create a temporary sky file */
  EditSky edit_sky;
  (void)edit_sky_init(&edit_sky, NULL, NULL, NULL, NULL);

  Editor tmp;
  editor_init(&tmp, &edit_sky, NULL);

  /* Copy the selected colour bands to the temporary file */
  bool success = true;
  if (editor_copy(&tmp, get_editor(edit_win)) == EditResult_NoMem)
  {
    RPT_ERR("NoMem");
    success = false;
  }
  else
  {
    /* Save the temporary file */
    hourglass_on();
    sky_write_file(editor_get_sky(&tmp), writer);
    hourglass_off();
  }

  editor_destroy(&tmp);
  edit_sky_destroy(&edit_sky);
  return success;
}

/* ----------------------------------------------------------------------- */

bool EditWin_can_undo(EditWin *const edit_win)
{
  return editor_can_undo(get_editor(edit_win));
}

/* ----------------------------------------------------------------------- */

bool EditWin_can_redo(EditWin *const edit_win)
{
  return editor_can_redo(get_editor(edit_win));
}

/* ----------------------------------------------------------------------- */

bool EditWin_can_paste(EditWin *const edit_win)
{
  /* Prevent paste if none selected or caret at end of file */
  int sel_start, sel_end;
  EditWin_get_selection(edit_win, &sel_start, &sel_end);
  bool const no_room = (sel_start == sel_end) && (sel_start >= SFSky_Height / 2);
  bool const can_paste = (!no_room && edit_win->can_paste);
  DEBUGF("%s paste\n", can_paste ? "Can" : "Can't");
  return can_paste;
}

/* ----------------------------------------------------------------------- */

void EditWin_set_paste_enabled(EditWin *const edit_win, bool const can_paste)
{
  assert(edit_win != NULL);
  DEBUGF("%s paste\n", can_paste ? "Enable" : "Disable");
  edit_win->can_paste = can_paste;
}
