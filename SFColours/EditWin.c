/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colours data editing windows
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
#include <stddef.h>
#include "stdio.h"
#include <string.h>
#include <stdbool.h>
#include <limits.h>
#include <stdint.h>
#include <assert.h>

/* RISC OS library files */
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
#include "SFFormats.h"
#include "Pal256.h"
#include "scheduler.h"
#include "DateStamp.h"
#include "Debug.h"
#include "WimpExtra.h"
#include "StrExtra.h"
#include "PalEntry.h"
#include "StackViews.h"
#include "Entity2.h"
#include "OSVDU.h"
#include "Drag.h"
#include "UserData.h"
#include "DeIconise.h"
#include "Hourglass.h"
#include "EventExtra.h"

/* Local headers */
#include "EditWin.h"
#include "ExpColFile.h"
#include "SFCFileInfo.h"
#include "Utils.h"
#include "SFCSaveBox.h"
#include "DCS_dialogue.h"
#include "Menus.h"
#include "SFCInit.h"
#include "ColsIO.h"
#include "Picker.h"
#include "OurEvents.h"
#include "ColMap.h"
#include "Editor.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


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
  ComponentId_First_Button        = 0x44,
  ComponentId_Last_Button         = 0x83,
  ComponentId_Status_DisplayField = 0x00
};

/* Constant numeric values */
enum
{
  DefaultColour = 0,
  MouseButtonModifier_Drag   = 16,
  MouseButtonModifier_Single = 256,
  WindowTitleMaxLen          = 255,
  IntKeyNum_Shift            = 0,
  IntKeyNum_Ctrl             = 1,
  ScrollBorder               = 64,
  ToolbarHeight              = 68,
  ButtonDFGColour            = 0xffffff, /* BbGgRr format, for dark colours */
  ButtonLFGColour            = 0x000000, /* BbGgRr format, for light colours */
  TrackPointerFrequency      = 10, /* in centiseconds */
  TrackPointerPriority       = SchedulerPriority_Min, /* for scheduler */
  ScrollStepSize             = 32,
  Hint_None                  = 0,
  Hint_First                 = 1,
  Hint_Last                  = 12,
  HintTokenMaxLen            = 11,
  WimpAutoScrollMinVersion   = 400,
  MinColourNumber = 0,
  MaxColourNumber = 255,
};

struct ColMapFile
{
  UserData list_node;
  EditColMap edit_colmap;
#if !PER_VIEW_SELECT
  Editor editor;
#endif
  OSDateAndTime file_date; /* 000000CC DDDDDDDD */
  bool changed_since_save;
  bool hillcols;
  LinkedList views;
  int num_views;
  int num_cols;
  int start_editnum;
  BBox *gadget_bboxes;
};

struct EditWin
{
  LinkedListItem node;
  ColMapFile *file;
#if PER_VIEW_SELECT
  Editor editor;
#endif
  ObjectId window_id; /* Toolbox ID of editing window */
  ObjectId status_bar_id; /* Toolbox ID of status bar */
  int   wimp_handle; /* Wimp handle of editing window */
  int   pane_wimp_handle; /* Wimp handle of attached pane */
  int   last_mouseover;

  bool nullpoll:1; /* are we null polling to check for mouseovers */
  bool on_menu:1;
  bool has_input_focus:1; /* reflects ownership of caret/selection entities */
  bool parent_pending:1; /* Open parent directory after file has been
                            saved? */
  bool destroy_pending:1; /* Destroy editing window after file has
                             been saved? */
  bool drop_pending:1;
  bool can_paste:1;
};

static enum
{
  DragType_None,
  DragType_Rubber,
  DragType_Data
}
drag_type = DragType_None;
static bool drag_adjust;
static _Optional EditWin *drag_view = NULL;
static _Optional EditWin const *auto_scroll_view = NULL;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

typedef bool EditWinCallbackFn(EditWin *edit_win, void *arg);

static _Optional EditWin *for_each_view(ColMapFile *const file,
  EditWinCallbackFn *const fn, void *const arg)
{
  assert(file != NULL);
  _Optional LinkedListItem *next;
  for (_Optional LinkedListItem *node = linkedlist_get_head(&file->views);
       node != NULL;
       node = next)
  {
    next = linkedlist_get_next(&*node);
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

static bool file_cancel_io_cb(EditWin *const edit_win, void *const arg)
{
  NOT_USED(arg);
  IO_cancel(edit_win);
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static void file_cancel_io(ColMapFile *const file)
{
  for_each_view(file, file_cancel_io_cb, file);
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

static void selection_changed(EditWin *const edit_win)
{
  file_cancel_io(edit_win->file);
  update_menus(edit_win);
}

/* ----------------------------------------------------------------------- */

static bool get_selected(EditWin *const edit_win, int const index)
{
  assert(edit_win != NULL);
  assert(edit_win->file != NULL);
  assert(index >= 0);
  assert(index < edit_win->file->num_cols);

  return editor_is_selected(get_editor(edit_win),
    edit_win->file->start_editnum + index);
}

/* ----------------------------------------------------------------------- */

static bool set_selected(EditWin *const edit_win, int index,
  bool const select)
{
  assert(edit_win != NULL);
  assert(index >= 0);
  assert(index < edit_win->file->num_cols);

  bool sel = false;
  index += edit_win->file->start_editnum;
  if (select)
  {
    sel = editor_select(get_editor(edit_win), index, index + 1);
  }
  else
  {
    sel = editor_deselect(get_editor(edit_win), index, index + 1);
  }
  return sel;
}

/* ----------------------------------------------------------------------- */

static void make_selection_bbox(EditWin *const edit_win,
  BBox *selected_bbox)
{
  assert(edit_win != NULL);
  assert(selected_bbox != NULL);

  /* Start with a reversed (invalid) bounding box */
  selected_bbox->xmin = selected_bbox->ymin = INT_MAX;
  selected_bbox->xmax = selected_bbox->ymax = INT_MIN;

  int const num_cols = edit_win->file->num_cols;
  for (int col_num = 0; col_num < num_cols; col_num++)
  {
    BBox const *gadget_bbox;

    if (!get_selected(edit_win, col_num))
    {
      continue; /* this colour isn't selected */
    }

    gadget_bbox = &edit_win->file->gadget_bboxes[col_num];
    DEBUGF("Gadget %d's bounding box is %d <= x < %d, %d <= y < %d\n",
          col_num, gadget_bbox->xmin, gadget_bbox->xmax,
          gadget_bbox->ymin, gadget_bbox->ymax);

    /* Expand selection box to cover this gadget */
    if (gadget_bbox->xmax > selected_bbox->xmax)
    {
      selected_bbox->xmax = gadget_bbox->xmax;
    }

    if (gadget_bbox->ymax > selected_bbox->ymax)
    {
      selected_bbox->ymax = gadget_bbox->ymax;
    }

    if (gadget_bbox->xmin < selected_bbox->xmin)
    {
      selected_bbox->xmin = gadget_bbox->xmin;
    }

    if (gadget_bbox->ymin < selected_bbox->ymin)
    {
      selected_bbox->ymin = gadget_bbox->ymin;
    }

  } /* next gadget */

  DEBUGF("Selection covers x:%d,%d, y:%d,%d\n", selected_bbox->xmin,
        selected_bbox->xmax, selected_bbox->ymin, selected_bbox->ymax);
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

static void abort_drag(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  if (edit_win == drag_view && drag_type != DragType_None)
  {
    if (drag_type == DragType_Rubber)
    {
      /* A rubber band box is being dragged */
      EditWin_stop_auto_scroll(edit_win);
      ON_ERR_RPT(wimp_drag_box(CancelDrag));
    }
    else
    {
      /* Selected colours are being dragged */
      assert(drag_type == DragType_Data);
      ON_ERR_RPT(drag_abort());
    }

    drag_type = DragType_None;
    drag_view = NULL;
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
    return true; /* stop */
  }

  if (edit_win->on_menu)
  {
    return E(ViewsMenu_setname(edit_win->window_id, title, NULL));
  }

  if (E(ViewsMenu_add(edit_win->window_id, title, "" /* obsolete */ )))
  {
    return true; /* stop */
  }

  edit_win->on_menu = true;
  return false;
}

/* ----------------------------------------------------------------------- */

static bool set_title(ColMapFile *const file)
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

static void has_changed(ColMapFile *const file)
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

  file_cancel_io(file);
}

/* ----------------------------------------------------------------------- */

static bool handle_edit(EditWin *const edit_win, EditResult const r)
{
  switch (r)
  {
    case EditResult_Changed:
      has_changed(edit_win->file);
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

static void caret_lost(void *const client_handle)
{
  EditWin *const edit_win = client_handle;

  DEBUGF("Notified that input focus lost from view %p\n", client_handle);
  assert(edit_win != NULL);
  edit_win->has_input_focus = false;
}

/* ----------------------------------------------------------------------- */

static void claim_caret(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  if (!edit_win->has_input_focus &&
      !E(entity2_claim(Wimp_MClaimEntity_CaretOrSelection, (int *)NULL,
                       (Entity2EstimateMethod *)NULL, (Saver2WriteMethod *)NULL,
                       caret_lost, edit_win)))
  {
    edit_win->has_input_focus = true;
  }
}

/* ===================== CBLibrary client functions ====================== */

static SchedulerTime idle_track_pointer(void *const handle, SchedulerTime new_time,
  volatile const bool *const time_up)
{
  EditWin *const edit_win = handle;
  int buttons;
  ObjectId window;
  ComponentId component;

  assert(handle != NULL);
  NOT_USED(time_up);

  /* Check whether the pointer is over one of the colours */
  if (!E(window_get_pointer_info(0, NULL, NULL, &buttons, &window, &component)))
  {
    if (window != edit_win->window_id ||
        TEST_BITS(buttons, Window_GetPointerNotToolboxWindow))
    {
      component = NULL_ComponentId;
    }
    EditWin_set_hint(edit_win, component);
  }

  return new_time + TrackPointerFrequency; /* delay period until next call */
}

/* ======================== Wimp event handlers ========================== */

static int user_drag(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  const WimpUserDragBoxEvent * const wudbe = (WimpUserDragBoxEvent *)event;
  WimpGetWindowStateBlock window_state;
  BBox drag_box;

  NOT_USED(id_block);
  assert(event != NULL);
  NOT_USED(event_code);
  NOT_USED(handle);

  /* Was the user dragging a rubber-band selection box? */
  if (drag_view == NULL || drag_type != DragType_Rubber)
    return 0; /* No - do not claim event */

  EditWin *const edit_win = &*drag_view;
  DEBUGF("User has finished dragging a selection box %d,%d,%d,%d\n",
        wudbe->bbox.xmin, wudbe->bbox.ymin,
        wudbe->bbox.xmax, wudbe->bbox.ymax);

  abort_drag(edit_win);

  /* Get definition of window within which we were dragging */
  window_state.window_handle = edit_win->wimp_handle;
  if (!E(wimp_get_window_state(&window_state)))
  {
    DEBUGF("Drag box is x:%d,%d, y:%d,%d\n", wudbe->bbox.xmin, wudbe->bbox.xmax,
          wudbe->bbox.ymin, wudbe->bbox.ymax);

    /* Make drag box coordinates relative to window's work area */
    int const x_origin = window_state.visible_area.xmin - window_state.xscroll;
    int const y_origin = window_state.visible_area.ymax - window_state.yscroll;

    DEBUGF("Work area origin in screen coordinates is %d,%d\n",
          x_origin, y_origin);

    if (wudbe->bbox.xmin < wudbe->bbox.xmax)
    {
      drag_box.xmin = wudbe->bbox.xmin - x_origin;
      drag_box.xmax = wudbe->bbox.xmax - x_origin;
    }
    else
    {
      drag_box.xmin = wudbe->bbox.xmax - x_origin;
      drag_box.xmax = wudbe->bbox.xmin - x_origin;
    }

    if (wudbe->bbox.ymin < wudbe->bbox.ymax)
    {
      drag_box.ymin = wudbe->bbox.ymin - y_origin;
      drag_box.ymax = wudbe->bbox.ymax - y_origin;
    }
    else
    {
      drag_box.ymin = wudbe->bbox.ymax - y_origin;
      drag_box.ymax = wudbe->bbox.ymin - y_origin;
    }
    DEBUGF("Drag box in work area coordinates is %d <= x < %d, %d <= y < %d\n",
          drag_box.xmin, drag_box.xmax, drag_box.ymin, drag_box.ymax);


    /* Discover which gadgets the drag box covers */
    int const num_cols = edit_win->file->num_cols;
    bool sel = false;
    for (int index = 0; index < num_cols; index++)
    {
      BBox *gadget_bbox = &edit_win->file->gadget_bboxes[index];

      DEBUGF("Bounding box %u is %d <= x < %d, %d <= y < %d\n", index,
            gadget_bbox->xmin, gadget_bbox->xmax, gadget_bbox->ymin,
            gadget_bbox->ymax);

      if (drag_box.xmin <= gadget_bbox->xmax &&
          drag_box.xmax > gadget_bbox->xmin &&
          drag_box.ymin < gadget_bbox->ymax &&
          drag_box.ymax >= gadget_bbox->ymin)
      {
        /* Drag box intersects with this gadget */
        if (set_selected(edit_win, index, !drag_adjust ||
            !get_selected(edit_win, index)))
        {
          sel = true;
        }
      }
    }
    if (sel)
    {
      selection_changed(edit_win);
    }
  }

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
        /* Shift-ADJUST: open parent and don't attempt to close window */
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

static int pointer_entering_window(int const event_code,
  WimpPollBlock *const event, IdBlock *const id_block, void *const handle)
{
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  if (!E(scheduler_register_delay(idle_track_pointer, edit_win,
    TrackPointerFrequency, TrackPointerPriority)))
  {
    edit_win->nullpoll = true;
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int pointer_leaving_window(int const event_code,
  WimpPollBlock *const event, IdBlock *const id_block, void *const handle)
{
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  assert(handle != NULL);

  if (edit_win->nullpoll)
  {
    scheduler_deregister(idle_track_pointer, edit_win);
    edit_win->nullpoll = false;
  }

  EditWin_set_hint(edit_win, NULL_ComponentId);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

#ifdef USE_WIMP_CARET_EVENTS
static int lose_caret(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Keep track of whether this view has the input focus */
  EditWin *const edit_win = handle;
  assert(edit_win != NULL);

  if (edit_win->has_input_focus)
    entity2_release(Wimp_MClaimEntity_CaretOrSelection);

  return 1; /* claim event */
}
#endif /* USE_WIMP_CARET_EVENTS */

/* ----------------------------------------------------------------------- */

static int gain_caret(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Keep track of whether this view has the input focus */
  NOT_USED(event_code);
  NOT_USED(event);
  NOT_USED(id_block);
  EditWin *const edit_win = handle;

  claim_caret(edit_win);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int scroll_request(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Handle scroll request events */
  WimpScrollRequestEvent * const wsre = (WimpScrollRequestEvent *)event;
  int visible_height = 0, visible_width = 0;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  DEBUGF("Scroll request for window %d: x change %d, y change %d\n",
        wsre->open.window_handle, wsre->xscroll, wsre->yscroll);

  DEBUGF("Current scroll offsets: %d,%d\n", wsre->open.xscroll,
        wsre->open.yscroll);

  visible_height = (wsre->open.visible_area.ymax - ToolbarHeight -
                   (1<<y_eigen)) - wsre->open.visible_area.ymin;

  switch (wsre->yscroll)
  {
    case WimpScrollRequest_PageLeftDown:
      wsre->open.yscroll -= visible_height;
      break;
    case WimpScrollRequest_LeftDown:
      wsre->open.yscroll -= ScrollStepSize;
      break;
    case WimpScrollRequest_RightUp:
      wsre->open.yscroll += ScrollStepSize;
      break;
    case WimpScrollRequest_PageRightUp:
      wsre->open.yscroll += visible_height;
      break;
  }

  visible_width = wsre->open.visible_area.xmax - wsre->open.visible_area.xmin;

  switch (wsre->xscroll)
  {
    case WimpScrollRequest_PageLeftDown:
      wsre->open.xscroll -= visible_width;
      break;
    case WimpScrollRequest_LeftDown:
      wsre->open.xscroll -= ScrollStepSize;
      break;
    case WimpScrollRequest_RightUp:
      wsre->open.xscroll += ScrollStepSize;
      break;
    case WimpScrollRequest_PageRightUp:
      wsre->open.xscroll += visible_width;
      break;
  }

  DEBUGF("Adjusted scroll offsets: %d,%d\n",
        wsre->open.xscroll, wsre->open.yscroll);

  ON_ERR_RPT(toolbox_show_object(0,
                                 id_block->self_id,
                                 Toolbox_ShowObject_FullSpec,
                                 &wsre->open.visible_area,
                                 id_block->parent_id,
                                 id_block->parent_component));

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int mouse_click(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  const WimpMouseClickEvent * const wmce = (WimpMouseClickEvent *)event;
  EditWin *const edit_win = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);
  DEBUGF("Mouse buttons %d at %d,%d\n", wmce->buttons, wmce->mouse_x, wmce->mouse_y);

  if (wmce->buttons == Wimp_MouseButtonMenu)
    return 0; /* event not handled */

  /* Bypass this for drags, to speed things up */
  if (wmce->buttons == Wimp_MouseButtonSelect * MouseButtonModifier_Single ||
      wmce->buttons == Wimp_MouseButtonAdjust * MouseButtonModifier_Single)
  {
    /* Give the input focus to the window in which the user clicked */
    EditWin_give_focus(edit_win);
  }

  if (id_block->self_component == NULL_ComponentId)
  {
    DEBUGF("Handle clicks and drags on the window background\n");
    switch (wmce->buttons)
    {
      case Wimp_MouseButtonSelect * MouseButtonModifier_Single:
        if (editor_clear_selection(get_editor(edit_win)))
        {
          abort_drag(edit_win);
          selection_changed(edit_win);
        }
        break;

      case Wimp_MouseButtonSelect * MouseButtonModifier_Drag:
      case Wimp_MouseButtonAdjust * MouseButtonModifier_Drag:
      { /* Start rubber-band box to allow user selection */
        WimpDragBox drag_box;
        WimpGetWindowStateBlock window_state =
        {
          edit_win->wimp_handle, {0,0,0,0}, 0, 0, WimpWindow_Top, 0
        };
        unsigned int autoscroll_flags;

        if (E(wimp_get_window_state(&window_state)))
          break;

        /* Start auto-scrolling immediately */
        EditWin_start_auto_scroll(edit_win, &window_state.visible_area, 0, &autoscroll_flags);

        drag_box.wimp_window = edit_win->wimp_handle;
        drag_box.drag_type = Wimp_DragBox_DragRubberDash;
        drag_box.dragging_box.xmin = wmce->mouse_x;
        drag_box.dragging_box.xmax = wmce->mouse_x;
        drag_box.dragging_box.ymin = wmce->mouse_y;
        drag_box.dragging_box.ymax = wmce->mouse_y;

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
          drag_box.parent_box.xmax = window_state.visible_area.xmax;
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

          /* Clip bounding box if we have an internal toolbar */
          if (edit_win->status_bar_id != NULL_ObjectId)
            drag_box.parent_box.ymin += ToolbarHeight + (1<<y_eigen);

          drag_box.parent_box.ymax = window_state.visible_area.ymax;
        }

        if (E(wimp_drag_box2(&drag_box, Wimp_DragBox_FixToWorkArea | Wimp_DragBox_ClipToWindow)))
        {
          (void)EditWin_stop_auto_scroll(edit_win);
          break;
        }
        drag_view = edit_win;
        drag_adjust = (wmce->buttons == Wimp_MouseButtonAdjust * MouseButtonModifier_Drag);
        drag_type = DragType_Rubber; /* record type of drag */
        break;
      }

      default:
        return 0; /* event not handled */
    }
  }
  else if (id_block->self_component >= ComponentId_First_Button ||
           id_block->self_component < ComponentId_First_Button +
           (int)edit_win->file->num_cols)
  {
    DEBUGF("Handle clicks and drags on colours\n");
    switch (wmce->buttons)
    {
      case Wimp_MouseButtonSelect: /* SELECT double-click */
      {
        /* Open dialogue box to edit the colour clicked upon */
        if (!E(Pal256_set_colour(Picker_sharedid,
            EditWin_get_colour(edit_win,
              id_block->self_component - ComponentId_First_Button))))
        {
          ON_ERR_RPT(toolbox_show_object(Toolbox_ShowObject_AsMenu,
            Picker_sharedid, Toolbox_ShowObject_AtPointer, NULL,
            id_block->self_id, id_block->self_component));
        }
        break;
      }

      case Wimp_MouseButtonSelect * MouseButtonModifier_Drag:
      { /* Record pointer position within window's work area at start of drag */
        if (!editor_has_selection(get_editor(edit_win)))
          break; /* Can't drag selection if nothing selected */

        IOCoords start = {wmce->mouse_x, wmce->mouse_y};
        scr_to_work_area_coords(edit_win->wimp_handle, &start.x, &start.y);

        BBox selected_bbox;
        make_selection_bbox(edit_win, &selected_bbox);
        assert(selected_bbox.xmin <= selected_bbox.xmax);
        assert(selected_bbox.ymin <= selected_bbox.ymax);

        file_cancel_io(edit_win->file);
        if (IO_start_drag(edit_win, start, &selected_bbox))
        {
          drag_type = DragType_Data;
          drag_view = edit_win;
        }
        break;
      }

      case Wimp_MouseButtonSelect * MouseButtonModifier_Single:
      {
        /* Select the logical colour clicked upon, and deselect all others
           (unless it was already selected and the Ctrl key was not held) */
        if (get_selected(edit_win,
          id_block->self_component - ComponentId_First_Button))
        {
          /* It is already selected - do nothing unless exclusive selection
             is forced by user holding Ctrl key */
          if (!key_pressed(IntKeyNum_Ctrl))
          {
            break;
          }
        }

        editor_exc_select(get_editor(edit_win),
          edit_win->file->start_editnum +
          (id_block->self_component - ComponentId_First_Button));

        break;
      }

      case Wimp_MouseButtonAdjust * MouseButtonModifier_Single:
      {
        /* Toggle the selection state of the logical colour clicked upon */
        int const index = id_block->self_component - ComponentId_First_Button;
        if (set_selected(edit_win, index, !get_selected(edit_win, index)))
        {
          selection_changed(edit_win);
        }
        break;
      }

      default:
      {
        return 0; /* event not handled */
      }
    }
  }
  else
  {
    return 0; /* event not handled */
  }

  return 1; /* claim event */
}

/* ======================== Toolbox event handlers ======================= */

static bool create_view(ColMapFile *const file);

static int misc_tb_event(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  EditWin *const edit_win = handle;

  NOT_USED(event);
  assert(id_block != NULL);
  assert(handle != NULL);

  /* Careful - handler is called for unclaimed toolbox events on any object */
  if (id_block->self_id != edit_win->window_id &&
      id_block->ancestor_id != edit_win->window_id)
  {
    return 0; /* event not for us - pass it on */
  }

  /* Handle hotkey/menu selection events */
  switch (event_code)
  {
    case EventCode_FileInfo:
      show_object_relative(Toolbox_ShowObject_AsMenu,
                           fileinfo_sharedid,
                           edit_win->window_id,
                           id_block->self_id,
                           id_block->self_component);
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
      show_object_relative(Toolbox_ShowObject_AsMenu,
                           savebox_sharedid,
                           edit_win->window_id,
                           id_block->self_id,
                           id_block->self_component);
      break;

    case EventCode_QuickSave:
      /* Save file immediately to current path, if any */
      EditWin_do_save(edit_win, false, false);
      break;

    case EventCode_Undo:
      abort_drag(edit_win);
      if (!EditWin_can_undo(edit_win))
      {
        putchar('\a'); /* nothing to undo */
        break;
      }
      if (editor_undo(get_editor(edit_win)))
      {
        update_menus(edit_win);
        has_changed(edit_win->file);
      }
      break;

    case EventCode_Redo:
      abort_drag(edit_win);
      if (!EditWin_can_redo(edit_win))
      {
        putchar('\a'); /* nothing to redo */
        break;
      }
      if (editor_redo(get_editor(edit_win)))
      {
        update_menus(edit_win);
        has_changed(edit_win->file);
      }
      break;

    case EventCode_Smooth:
      abort_drag(edit_win);
      (void)handle_edit(edit_win, editor_interpolate(get_editor(edit_win),
        palette));
      break;

    case EventCode_SelectAll:
      if (editor_select(get_editor(edit_win), edit_win->file->start_editnum,
        edit_win->file->start_editnum + edit_win->file->num_cols))
      {
        abort_drag(edit_win);
        selection_changed(edit_win);
      }
      break;

    case EventCode_ClearSelection:
      if (editor_clear_selection(get_editor(edit_win)))
      {
        abort_drag(edit_win);
        selection_changed(edit_win);
      }
      break;

    case EventCode_SetColour:
      abort_drag(edit_win);
      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no colours are selected */
        break;
      }

      /* Display selected colour in palette dialogue box */
      if (!E(Pal256_set_colour(Picker_sharedid,
        editor_get_selected_colour(get_editor(edit_win)))))
      {
        show_object_relative(Toolbox_ShowObject_AsMenu,
                             Picker_sharedid,
                             edit_win->window_id,
                             id_block->self_id,
                             id_block->self_component);
      }
      break;

    case EventCode_AbortDrag:
      /* User has pressed Escape key - abort any drag that is in progress */
      abort_drag(edit_win);
      break;

    case EventCode_Copy:
      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no selection to copy */
      }
      else if (IO_copy(edit_win))
      {
        /* Update menu to reflect that we can now paste data */
        update_menus(edit_win);
      }
      break;

    case EventCode_Paste:
      abort_drag(edit_win);
      if (!editor_has_selection(get_editor(edit_win)))
      {
        putchar('\a'); /* no destination for paste */
      }
      else
      {
        IO_paste(edit_win);
      }
      break;

    default:
      return 0; /* this is heavy, man */
  }

  return 1; /* claim event */
}

/* =========================== Other functions =========================== */

static bool display_selected(EditWin *const edit_win, int const index)
{
  /* Update the button gadget used to display a colour */
  assert(edit_win != NULL);
  assert(index >= 0);
  assert(index < edit_win->file->num_cols);

  bool const select = get_selected(edit_win, index);

  /* Show number and border */
  char number[4];
  if (select)
  {
#ifdef SHOW_INDEX_NOT_COLNUM
    sprintf(number, "%d", edit_win->file->start_editnum + index);
#else
    sprintf(number, "%d", EditWin_get_colour(edit_win, index));
#endif
  }

  ON_ERR_RPT(button_set_value(0,
                              edit_win->window_id,
                              index + ComponentId_First_Button,
                              select ? number : ""));

  ON_ERR_RPT(button_set_flags(0,
                              edit_win->window_id,
                              index + ComponentId_First_Button,
                              WimpIcon_Border,
                              select ? WimpIcon_Border : 0));

  return true;
}

/* ----------------------------------------------------------------------- */

static bool redraw_selected_cb(EditWin *const edit_win, void *const arg)
{
  int *const pos = arg;

  assert(edit_win != NULL);
  assert(pos != NULL);

  if (*pos >= edit_win->file->start_editnum)
  {
    if (!display_selected(edit_win, *pos - edit_win->file->start_editnum))
    {
      return true; /* stop */
    }
  }
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static void redraw_selected(Editor *const editor, int pos)
{
  DEBUGF("Force redraw of entry %d for editor %p\n", pos, (void *)editor);
  assert(editor != NULL);
#if PER_VIEW_SELECT
  (void)redraw_selected_cb(CONTAINER_OF(editor, EditWin, editor), &pos);
#else
  ColMapFile *const file = CONTAINER_OF(editor, ColMapFile, editor);
  (void)for_each_view(file, redraw_selected_cb, pos);
#endif
}

/* ----------------------------------------------------------------------- */

static bool display_colour(EditWin *const edit_win, int const index)
{
  /* Update the button gadget used to display a colour */
  assert(edit_win != NULL);
  assert(index >= 0);
  assert(index < edit_win->file->num_cols);

  int const colour = EditWin_get_colour(edit_win, index);
  int fg_colour;
  char validation[sizeof("C000000/000000")];
#ifdef WIMP_FORE_COLOUR
  /* Foreground colour is Wimp colour 0-15 (vulnerable to silly palette) */
  if (palette_entry_brightness(palette[colour]) > MaxBrightness / 2)
  {
    fg_colour = WimpColour_Black;
  }
  else
  {
    fg_colour = WimpColour_White;
  }

  if (E(button_set_flags(0, edit_win->window_id,
    ComponentId_First_Button + index, WimpIcon_FGColour * 0x0f,
    WimpIcon_FGColour * fg_colour)))
  {
    return false;
  }

  /* Background colour is 24-bit RGB in validation string */
  sprintf(validation, "C/%X", palette[colour] >> PaletteEntry_RedShift);
#else
  if (palette_entry_brightness(palette[colour]) > MaxBrightness / 2)
  {
    fg_colour = ButtonLFGColour;
  }
  else
  {
    fg_colour = ButtonDFGColour;
  }

  /* Both colours are 24-bit RGB in validation string */
  sprintf(validation, "C%X/%X", fg_colour,
          palette[colour] >> PaletteEntry_RedShift);
#endif
  if (E(button_set_validation(0, edit_win->window_id,
    ComponentId_First_Button + index, validation)))
  {
    return false;
  }

#ifndef SHOW_INDEX_NOT_COLNUM
  /* If the colour is selected and it's displayed as part of the selection
     then update it. */
  if (get_selected(edit_win, index))
  {
    char number[16];
    sprintf(number, "%u", colour);
    if (E(button_set_value(0, edit_win->window_id,
      ComponentId_First_Button + index, number)))
    {
      return false;
    }
  }
#endif

  return true;
}

/* ----------------------------------------------------------------------- */

static bool display_all(EditWin *const edit_win)
{
  assert(edit_win != NULL);
  assert(edit_win->file != NULL);
  int const num_cols = edit_win->file->num_cols;

  for (int index = 0; index < num_cols; index++)
  {
    if (!display_colour(edit_win, index))
    {
      return false;
    }
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static bool redraw_entry_cb(EditWin *const edit_win, void *const arg)
{
  int *const pos = arg;

  assert(edit_win != NULL);
  assert(pos != NULL);

  if (*pos >= edit_win->file->start_editnum)
  {
    if (!display_colour(edit_win, *pos - edit_win->file->start_editnum))
    {
      return true; /* stop */
    }
  }
  return false; /* continue */
}

/* ----------------------------------------------------------------------- */

static void redraw_entry(EditColMap *const edit_colmap, int pos)
{
  DEBUGF("Force redraw of entry %d for editor %p\n", pos, (void *)edit_colmap);
  assert(edit_colmap != NULL);
  ColMapFile *const file = CONTAINER_OF(edit_colmap, ColMapFile, edit_colmap);
  (void)for_each_view(file, redraw_entry_cb, &pos);
}

/* ----------------------------------------------------------------------- */

static bool read_bboxes(EditWin * const edit_win)
{
  assert(edit_win != NULL);
  ColMapFile * const file = edit_win->file;

  /* For efficiency we cache all the gadget bounding boxes
     locally the first time that a Window of a given type is
     created */
  static bool hgbb_cached = false, ogbb_cached = false;
  static BBox hill_gadget_bboxes[ ARRAY_SIZE( (*(SFHillColours *)0) ) ];
  static BBox obj_gadget_bboxes[EditWin_MaxSize];

  bool *bboxes_read;
  size_t num_cols;
  if (file->hillcols)
  {
    num_cols = ARRAY_SIZE(hill_gadget_bboxes);
    file->gadget_bboxes = hill_gadget_bboxes;
    bboxes_read = &hgbb_cached;
  }
  else
  {
    num_cols = ARRAY_SIZE(obj_gadget_bboxes);
    file->gadget_bboxes = obj_gadget_bboxes;
    bboxes_read = &ogbb_cached;
  }

  if (*bboxes_read)
  {
    return true;
  }

  ObjectId const window_id = edit_win->window_id;

  for (size_t index = 0; index < num_cols; index++)
  {
    if (E(gadget_get_bbox(0, window_id,
           ComponentId_First_Button + index,
           &file->gadget_bboxes[index])))
    {
      return false;
    }
  }

  *bboxes_read = true;
  return true;
}

/* ----------------------------------------------------------------------- */

static int index_from_coords(EditWin const *const edit_win, int x, int y)
{
  int found = -1;

  DEBUGF("Searching view %p for colour at coordinates %d,%d\n",
        (void *)edit_win, x, y);
  assert(edit_win != NULL);

  int const num_cols = edit_win->file->num_cols;
  int const start_editnum = edit_win->file->start_editnum;

  for (int index = 0; index < num_cols; index++)
  {
    BBox const * const gadget_bbox = &edit_win->file->gadget_bboxes[index];

    DEBUGF("Bounding box %u is %d,%d,%d,%d\n",
          start_editnum + index,
          gadget_bbox->xmin, gadget_bbox->ymin,
          gadget_bbox->xmax, gadget_bbox->ymax);

    /* Is the imported colour within this bounding box? */
    if (x >= gadget_bbox->xmin && x < gadget_bbox->xmax &&
        y >= gadget_bbox->ymin && y < gadget_bbox->ymax)
    {
      found = start_editnum + index;
      break;
    }
  }
  if (found >= 0)
  {
    DEBUGF("Found logical colour %u\n", found);
  }
  else
  {
    DEBUGF("No logical colour found\n");
  }
  return found;
}

/* ----------------------------------------------------------------------- */

static bool register_wimp_handlers(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  static const struct
  {
    int event_code;
    WimpEventHandler *handler;
    bool hillcols;
  }
  wimp_handlers[] =
  {
    {
      Wimp_ECloseWindow,
      close_window,
      true
    },
    {
      Wimp_EMouseClick,
      mouse_click,
      true
    },
#ifdef USE_WIMP_CARET_EVENTS
    {
      Wimp_ELoseCaret,
      lose_caret,
      true
    },
#endif /* USE_WIMP_CARET_EVENTS */
    {
      Wimp_EGainCaret,
      gain_caret,
      true
    },
    {
      Wimp_EScrollRequest,
      scroll_request,
      false
    },
    {
      Wimp_EPointerLeavingWindow,
      pointer_leaving_window,
      false
    },
    {
      Wimp_EPointerEnteringWindow,
      pointer_entering_window,
      false
    }
  };

  bool const hillcols = edit_win->file->hillcols;

  for (size_t i = 0; i < ARRAY_SIZE(wimp_handlers); i++)
  {
    /* Hill colour editing windows don't need so many event
       handlers */
    if (hillcols && !wimp_handlers[i].hillcols)
    {
      continue;
    }

    if (E(event_register_wimp_handler(edit_win->window_id,
      wimp_handlers[i].event_code, wimp_handlers[i].handler, edit_win)))
    {
      return false;
    }
  }

  return true;
}

/* ----------------------------------------------------------------------- */

static _Optional EditWin *add_view(ColMapFile *const file)
{
  assert(file != NULL);
  _Optional EditWin *const edit_win = malloc(sizeof(*edit_win));
  if (edit_win == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *edit_win = (EditWin){
    .file = file,
    .window_id = NULL_ObjectId,
    .status_bar_id = NULL_ObjectId,
    .wimp_handle = WimpWindow_Top,
    .pane_wimp_handle = WimpWindow_Top,
    .has_input_focus = false,
    .parent_pending = false,
    .destroy_pending = false,
    .drop_pending = false,
    .on_menu = false,
    .nullpoll = false,
  };

  linkedlist_insert(&file->views, NULL, &edit_win->node);
  ++file->num_views;

#if PER_VIEW_SELECT
  editor_init(&edit_win->editor, &file->edit_colmap, redraw_selected);
#endif
  return edit_win;
}

/* ----------------------------------------------------------------------- */

static void remove_view(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  ColMapFile *const file = edit_win->file;
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

  /* Stop updating the hint text for this window */
  if (edit_win->nullpoll)
  {
    scheduler_deregister(idle_track_pointer, edit_win);
  }

  /* Release the caret/selection */
  if (edit_win->has_input_focus)
  {
    entity2_release(Wimp_MClaimEntity_CaretOrSelection);
  }

  /* Stop any drag that may be in progress */
  abort_drag(edit_win);

  /* Destroy main Window object */
  ON_ERR_RPT(remove_event_handlers_delete(edit_win->window_id));

  if (edit_win->on_menu)
  {
    ON_ERR_RPT(ViewsMenu_remove(edit_win->window_id));
  }

  /* Finalise the I/O subsystem for this view */
  IO_view_deleted(edit_win);

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

static bool create_view(ColMapFile *const file)
{
  assert(file != NULL);

  /* Grab memory for view status */
  _Optional EditWin *const edit_win = add_view(file);
  if (edit_win == NULL)
  {
    return false;
  }

  if (!E(toolbox_create_object(0, file->hillcols ?
    "EditHills" : "EditColmap", &edit_win->window_id)))
  {
    DEBUGF("Created window 0x%x\n", edit_win->window_id);

    /* Initialise the I/O subsystem for this view */
    if (IO_view_created(&*edit_win))
    {
      /* Register the handler for custom Toolbox events
         (generated by key shortcuts and menu entries) */
      if (!E(event_register_toolbox_handler(-1, -1, misc_tb_event, &*edit_win)))
      {
        do
        {
          /* Associate a pointer to the view data with the Window
             object */
          if (E(toolbox_set_client_handle(0, edit_win->window_id, &*edit_win)))
          {
            break;
          }

          /* Get the Object Id of the status bar (if any) */
          if (E(window_get_tool_bars(
                Window_InternalBottomLeftToolbar, edit_win->window_id,
                &edit_win->status_bar_id, NULL, NULL, NULL)))
          {
            break;
          }

          /* Get the Wimp handle of the main window */
          if (E(window_get_wimp_handle(0, edit_win->window_id,
                &edit_win->wimp_handle)))
          {
            break;
          }

          /* Get the Wimp handle of the status bar, to make handling
             of DataLoad and DataSave messages more efficient */
          if (edit_win->status_bar_id != NULL_ObjectId)
          {
            if (E(window_get_wimp_handle(0,
                   edit_win->status_bar_id,
                   &edit_win->pane_wimp_handle)))
            {
              break;
            }
          }

          if (!read_bboxes(&*edit_win) ||
              !register_wimp_handlers(&*edit_win) ||
              !display_all(&*edit_win))
          {
            break; /* may have partially succeeded */
          }

          /* Show the main editing window in the default position for
             the next (toolbar will be shown automatically) */
          if (E(StackViews_open(edit_win->window_id, NULL_ObjectId, NULL_ComponentId)))
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
            destroy_view(&*edit_win);

            /* Restore any pre-existing windows */
            (void)set_title(file);
          }
          return success;
        }
        while (0);

        (void)event_deregister_toolbox_handler(-1, -1, misc_tb_event, &*edit_win);
      }

      IO_view_deleted(&*edit_win);
    }
    (void)remove_event_handlers_delete(edit_win->window_id);
  }

  remove_view(&*edit_win);
  return false;
}

/* ----------------------------------------------------------------------- */

static bool userdata_is_safe(struct UserData *const item)
{
  ColMapFile const * const file = CONTAINER_OF(item, ColMapFile, list_node);
  assert(file != NULL);
  return !file->changed_since_save;
}

/* ----------------------------------------------------------------------- */

static void destroy_userdata(struct UserData *const item)
{
  ColMapFile * const file = CONTAINER_OF(item, ColMapFile, list_node);
  assert(file != NULL);
  ColMapFile_destroy(file);
}

/* ----------------------------------------------------------------------- */

static inline bool init_date_stamp(ColMapFile *const file,
  _Optional char const *const load_path)
{
  assert(file != NULL);

  if (load_path != NULL)
  {
    /* Get datestamp of file */
    if (E(get_date_stamp(&*load_path, &file->file_date)))
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

typedef struct
{
  int window_handle;
  _Optional EditWin *edit_win;
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

static bool file_owns_handle_cb(UserData *const item, void *const arg)
{
  ColMapFile * const file = CONTAINER_OF(item, ColMapFile, list_node);

  /* Stop iteration when view owns window */
  return for_each_view(file, view_owns_handle_cb, arg) != NULL;
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

_Optional ColMapFile *ColMapFile_find_by_file_name(char const *const load_path)
{
  _Optional UserData *const item = userdata_find_by_file_name(load_path);
  return item ? CONTAINER_OF(item, ColMapFile, list_node) : NULL;
}

/* ----------------------------------------------------------------------- */

_Optional ColMapFile *ColMapFile_create(_Optional Reader *const reader,
  _Optional char const *const load_path, bool const is_safe, bool const hillcols)
{
  _Optional ColMapFile *const file = malloc(sizeof(*file));
  if (file == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *file = (ColMapFile){
    .changed_since_save = !is_safe,
    .num_views = 0,
  };

  linkedlist_init(&file->views);

  int const size = hillcols ? sizeof(SFHillColours) :
                              sizeof(SFObjectColours);
  ColMapState state = edit_colmap_init(&file->edit_colmap, reader, size,
                                       redraw_entry);
  ColMap *const colmap = edit_colmap_get_colmap(&file->edit_colmap);
  bool success = IO_report_read(colmap, state);

  if (success)
  {
    int const real_size = colmap_get_size(colmap);
    if (real_size > EditWin_MaxSize)
    {
      /* Exclude the static colours (non-editable) */
      file->num_cols = EditWin_MaxSize;
      file->hillcols = false;
    }
    else
    {
      file->num_cols = real_size;
      file->hillcols = true;
    }

    file->start_editnum = real_size - file->num_cols;

    /* Populate the initial part of the palette (static colours) */
    for (int pos = 0; pos < file->start_editnum; ++pos)
    {
      colmap_set_colour(colmap, pos, pos);
    }

#if !PER_VIEW_SELECT
    editor_init(&file->editor, &file->edit_colmap, redraw_selected);
#endif

    success = userdata_add_to_list(&file->list_node, userdata_is_safe,
      destroy_userdata, STRING_OR_NULL(is_safe ? load_path : NULL));

    if (!success)
    {
      RPT_ERR("NoMem");
    }
    else
    {
      success = init_date_stamp(&*file, is_safe ? load_path : NULL);
      if (!success)
      {
        userdata_remove_from_list(&file->list_node);
      }
    }

    if (!success)
    {
      edit_colmap_destroy(&file->edit_colmap);
    }
  }

  if (success)
  {
    success = create_view(&*file);
    if (!success)
    {
      ColMapFile_destroy(&*file);
    }
  }
  else
  {
    free(file);
  }

  return success ? &*file : NULL;
}

/* ----------------------------------------------------------------------- */

void ColMapFile_destroy(_Optional ColMapFile *const file)
{
  if (file)
  {
    (void)for_each_view(&*file, destroy_view_cb, &*file);
    edit_colmap_destroy(&file->edit_colmap);
    userdata_remove_from_list(&file->list_node);
    free(file);
  }
}

/* ----------------------------------------------------------------------- */

bool ColMapFile_export(ColMapFile *const file, Writer *const writer)
{
  assert(file != NULL);

  hourglass_on();
  colmap_write_file(edit_colmap_get_colmap(&file->edit_colmap), writer);
  hourglass_off();

  return true; /* caller should check writer for error */
}

/* ----------------------------------------------------------------------- */

bool ColMapFile_import(ColMapFile *const file, Reader *const reader)
{
  assert(file != NULL);
  return IO_read_colmap(edit_colmap_get_colmap(&file->edit_colmap), reader);
}

/* ----------------------------------------------------------------------- */

EditWin *ColMapFile_get_win(ColMapFile *const file)
{
  assert(file != NULL);
  _Optional LinkedListItem *const node = linkedlist_get_head(&file->views);
  assert(node != NULL);
  return CONTAINER_OF(node, EditWin, node);
}

/* ----------------------------------------------------------------------- */

void ColMapFile_show(ColMapFile *const file)
{
  (void)for_each_view(file, show_view_cb, file);
}

/* ----------------------------------------------------------------------- */

void EditWin_initialise(void)
{
  EF(event_register_wimp_handler(-1, Wimp_EUserDrag, user_drag, (void *)NULL));
}

/* ----------------------------------------------------------------------- */

void EditWin_destroy(EditWin *const edit_win)
{
  if (edit_win == NULL)
  {
    return;
  }

  ColMapFile *const file = edit_win->file;
  if (file->num_views > 1)
  {
    destroy_view(edit_win);
    (void)set_title(file);
  }
  else
  {
    ColMapFile_destroy(file);
  }

}

/* ----------------------------------------------------------------------- */

ColMapFile *EditWin_get_colmap(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  assert(edit_win->file != NULL);
  return edit_win->file;
}

/* ----------------------------------------------------------------------- */

int EditWin_get_colour(EditWin const *const edit_win, int const index)
{
  assert(edit_win != NULL);
  assert(index >= 0);
  assert(index < edit_win->file->num_cols);

  ColMap *const colmap = edit_colmap_get_colmap(&edit_win->file->edit_colmap);
  int const colour = colmap_get_colour(colmap,
    edit_win->file->start_editnum + index);

  DEBUGF("Got actual colour %u from logical colour %u in view %p\n",
        colour, edit_win->file->start_editnum + index, (void *)edit_win);

  return colour;
}

/* ----------------------------------------------------------------------- */

void EditWin_colour_selected(EditWin *const edit_win, int const colour)
{
  assert(edit_win != NULL);
  (void)handle_edit(edit_win, editor_set_plain(get_editor(edit_win), colour));
}

/* ----------------------------------------------------------------------- */

void EditWin_file_saved(EditWin *const edit_win, _Optional char *save_path)
{
  assert(edit_win != NULL);
  ColMapFile *const file = edit_win->file;
  file->changed_since_save = false; /* mark as unchanged */

  char *filename;
  if (save_path == NULL)
  {
    /* Data was saved under its existing file name */
    filename = userdata_get_file_name(&file->list_node);
  }
  else
  {
    /* Record new file name under which the data was saved */
    filename = &*save_path;
    if (!userdata_set_file_name(&file->list_node, filename))
    {
      RPT_ERR("NoMem");
      return;
    }
  }

  /* Get date stamp of file */
  ON_ERR_RPT(get_date_stamp(filename, &file->file_date));

  /* Set title of editing window */
  set_title(file);

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

void EditWin_show_parent_dir(EditWin const *const edit_win)
{
  /* Opens the parent directory of a file that is being edited */
  assert(edit_win != NULL);
  char const * const path = userdata_get_file_name(&edit_win->file->list_node);
  char const * const last_dot = strrchr(path, '.');
  if (last_dot != NULL)
  {
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
}

/* ----------------------------------------------------------------------- */

int EditWin_get_next_selected(EditWin *const edit_win, int const index)
{
  assert(edit_win != NULL);
  assert(edit_win->file != NULL);

  int const start_editnum = edit_win->file->start_editnum;
  int sel = editor_get_next_selected(get_editor(edit_win),
                                     start_editnum + index);
  if (sel >= 0)
  {
    assert(sel >= start_editnum);
    sel -= start_editnum;
  }
  return sel;
}

/* ----------------------------------------------------------------------- */

int EditWin_get_num_selected(EditWin *const edit_win,
  _Optional int *const num_selectable)
{
  assert(edit_win != NULL);
  if (num_selectable != NULL)
  {
    *num_selectable = edit_win->file->num_cols;
  }
  int const num_sel = editor_get_num_selected(get_editor(edit_win));
  assert(num_sel >= 0);
  assert(num_sel <= edit_win->file->num_cols);
  return num_sel;
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

void EditWin_set_hint(EditWin *const edit_win,
  ComponentId const component)
{
  /* Does this window have a status bar? */
  if (edit_win->status_bar_id == NULL_ObjectId)
    return; /* no */

  int hint_num;
  if (component < ComponentId_First_Button ||
      component > ComponentId_Last_Button)
  {
    hint_num = Hint_None;
  }
  else
  {
    const SFObjectColours *cols = (SFObjectColours *)0; /* null pointer */
    hint_num = Hint_First + (component - ComponentId_First_Button) /
               ARRAY_SIZE(cols->areas.engine_colours.player_engine);
    if (hint_num > Hint_Last)
      hint_num = Hint_Last; /* Rest are player's ship livery */
  }

  /* Don't keep resetting the same status text (flickers) */
  if (hint_num == edit_win->last_mouseover)
    return; /* same hint as before */

  edit_win->last_mouseover = hint_num;

  /* Set the status bar info text */
  DEBUGF("Updating hint text to %u\n", hint_num);
  char *value;

  if (hint_num < Hint_First || hint_num > Hint_Last)
  {
    value = "";
  }
  else
  {
    char token[HintTokenMaxLen + 1];
    sprintf(token, "hint%u", hint_num);
    value = msgs_lookup(token);
  }

  ON_ERR_RPT(displayfield_set_value(0, edit_win->status_bar_id,
    ComponentId_Status_DisplayField, value));
}

/* ----------------------------------------------------------------------- */

bool EditWin_set_array(EditWin *const edit_win, int const x,
  int const y, int number, int const *const src)
{
  assert(edit_win != NULL);
  assert(number >= 0);
  assert(src != NULL);

  /* Search for the logical colour the imported file was dropped on */
  int const pos = index_from_coords(edit_win, x, y);
  if (pos < 0)
  {
    return false;
  }

  abort_drag(edit_win);

  int const limit = edit_win->file->start_editnum +
                    edit_win->file->num_cols;
  number = LOWEST(number, limit - pos);

  Editor *const editor = get_editor(edit_win);

  editor_clear_selection(editor);
  editor_select(editor, pos, pos + number);
  selection_changed(edit_win);

  bool is_valid = true;
  if (!handle_edit(edit_win, editor_set_array(editor, src, number, &is_valid)))
  {
    return false;
  }

  if (!is_valid)
  {
    WARN("BadColNum");
  }
  return is_valid;
}

/* ----------------------------------------------------------------------- */

void EditWin_set_colmap(EditWin *const edit_win, int const x, int const y,
  ColMap const *const src)
{
  /* Do not import static colours */
  int skip = 0;
  int num_to_import = colmap_get_size(src);

  if (num_to_import == sizeof(SFObjectColours))
  {
    skip = ARRAY_SIZE(((SFObjectColours *)0)->areas.static_colours);
    num_to_import -= skip;
  }

  num_to_import = LOWEST(num_to_import,
           edit_win->file->start_editnum + edit_win->file->num_cols);

  int tmp[EditWin_MaxSize];

  for (int i = 0; i < num_to_import; ++i)
  {
    assert(i < EditWin_MaxSize);
    tmp[i] = colmap_get_colour(src, skip + i);
  }

  (void)EditWin_set_array(edit_win, x, y, num_to_import, tmp);
}

/* ----------------------------------------------------------------------- */

typedef struct {
  unsigned short position;
  unsigned short colour;
} ColourWithPos;

int compare(const void *a, const void *b)
{
  assert(a != NULL);
  assert(b != NULL);
  ColourWithPos const *const c2 = b, *const c1 = a;
  assert(c1->position < ColMap_MaxSize);
  assert(c2->position < ColMap_MaxSize);
  return c1->position - c2->position;
}

void EditWin_set_expcol(EditWin *const edit_win, int const x, int const y,
  ExpColFile const *const file)
{
  abort_drag(edit_win);
  editor_clear_selection(get_editor(edit_win));

  /* Scan through the list of colours in the file */
  int const size = ExpColFile_get_size(file);
  Editor *const editor = get_editor(edit_win);
  ColourWithPos tmp[EditWin_MaxSize];
  int num_to_import = 0;

  for (int record_no = 0; record_no < size; record_no++)
  {
    /* Read a colour number and its position (relative to the target
       coordinates) from the file */
    IOCoords coords;
    int const col_num = ExpColFile_get_colour(file, record_no,
      &coords.x, &coords.y);

    /* Search for a display gadget beneath the imported colour */
    int const pos = index_from_coords(edit_win, coords.x + x, coords.y + y);
    if (pos >= 0)
    {
      tmp[num_to_import++] =
        (ColourWithPos){.position = pos, .colour = col_num};

      editor_select(editor, pos, pos + 1);
    }
  }

  selection_changed(edit_win);

  /* Sort the colour numbers by position */
  qsort(tmp, num_to_import, sizeof(*tmp), compare);
  int tmp2[EditWin_MaxSize];
  for (int i = 0; i < num_to_import; ++i)
  {
    tmp2[i] = tmp[i].colour;
  }

  bool is_valid = true;
  (void)handle_edit(edit_win, editor_set_array(editor, tmp2, num_to_import,
    &is_valid));
}

/* ----------------------------------------------------------------------- */

#if !CLIPBOARD_HOLD_POS
void EditWin_set_expcol_flat(EditWin *const edit_win, int const x,
  int const y, ExpColFile const *const file)
{
  int tmp[EditWin_MaxSize];
  int const num_to_import = LOWEST(ExpColFile_get_size(file),
           edit_win->file->start_editnum + edit_win->file->num_cols);

  for (int i = 0; i < num_to_import; ++i)
  {
    assert(i < EditWin_MaxSize);
    tmp[i] = ExpColFile_get_colour(file, i, NULL, NULL);
  }

  (void)EditWin_set_array(edit_win, x, y, num_to_import, tmp);
}
#endif /* !CLIPBOARD_HOLD_POS */

/* ----------------------------------------------------------------------- */

bool EditWin_get_expcol(EditWin *const edit_win,
  int const x, int const y, ExpColFile *const export_file)
{
  assert(edit_win != NULL);
  assert(export_file != NULL);

  int const num_to_copy = editor_get_num_selected(get_editor(edit_win));

  /* Pre-allocate memory to hold the data to be saved */
  if (!ExpColFile_init(export_file, num_to_copy))
  {
    RPT_ERR("NoMem");
    return false;
  }

  /* Copy selected colours into the export file, or all colours */
  int s = 0;
  for (int c = EditWin_get_next_selected(edit_win, -1);
       c >= 0;
       c = EditWin_get_next_selected(edit_win, c))
  {
    /* Record colour number and position relative to centre point */
    int x_offset, y_offset;
    EditWin_coords_from_index(edit_win, c, &x_offset, &y_offset);

    x_offset -= x;
    y_offset -= y;

    int const colour = EditWin_get_colour(edit_win, c);
    (void)ExpColFile_set_colour(export_file, s++,
      x_offset, y_offset, colour);
  }

  return true;
}

/* ----------------------------------------------------------------------- */

bool EditWin_has_unsaved(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return edit_win->file->changed_since_save;
}

/* ----------------------------------------------------------------------- */

int *EditWin_get_stamp(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return (int *)&edit_win->file->file_date;
}

/* ----------------------------------------------------------------------- */

_Optional char *EditWin_get_file_path(EditWin const *const edit_win)
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

  char const * const path = userdata_get_file_name(
                              &edit_win->file->list_node);

  if (strchr(path, '.') == NULL)
  {
    /* Must open savebox first */
    show_object_relative(Toolbox_ShowObject_AsMenu, savebox_sharedid,
      edit_win->window_id, edit_win->window_id, NULL_ComponentId);
  }
  else if (IO_export_colmap_file(edit_win, path))
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
        edit_win->wimp_handle, edit_win->pane_wimp_handle);

  return wimp_handle == edit_win->wimp_handle ||
         wimp_handle == edit_win->pane_wimp_handle;
}

/* ----------------------------------------------------------------------- */

int EditWin_get_wimp_handle(EditWin const *const edit_win)
{
  assert(edit_win != NULL);
  return edit_win->wimp_handle;
}

/* ----------------------------------------------------------------------- */

_Optional EditWin *EditWin_from_wimp_handle(int const window_handle)
{
  /* Search our list of editing windows for the drag destination */
  DEBUGF("Searching for a view with window handle %d\n", window_handle);
  FindWindowData find_win = {
    .window_handle = window_handle,
    .edit_win = NULL,
  };
  (void)userdata_for_each(file_owns_handle_cb, &find_win);
  if (find_win.edit_win == NULL)
  {
    DEBUGF("Unrecognised window handle\n");
  }
  return find_win.edit_win;
}

/* ----------------------------------------------------------------------- */

void EditWin_start_auto_scroll(EditWin const *const edit_win,
  BBox const *visible_area, int const pause_time,
  _Optional unsigned int *const flags_out)
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
        .pause_zones.ymax = ScrollBorder,
        .pause_time = pause_time,
        .state_change_handler = 1, /* default pointer shapes */
      };

      /* Increase bottom pause zone if we have an internal toolbar */
      if (edit_win->status_bar_id != NULL_ObjectId)
        auto_scroll.pause_zones.ymin += ToolbarHeight + (1<<y_eigen);

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
    ON_ERR_RPT(wimp_auto_scroll(0, &(WimpAutoScrollBlock){0}, NULL));
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
  return ColMapFile_export(edit_win->file, writer);
}

/* ----------------------------------------------------------------------- */

void EditWin_coords_from_index(EditWin const *const edit_win, int index, int *x, int *y)
{
  DEBUGF("Getting coordinates of logical colour %u in view %p\n", index, (void *)edit_win);
  assert(edit_win != NULL);
  assert(index < edit_win->file->num_cols);

  /* Calculate centre coordinates of the specified logical colour
     (within the window's work area) */
  if (x != NULL) /* paranoia */
  {
    *x = edit_win->file->gadget_bboxes[index].xmin +
         (edit_win->file->gadget_bboxes[index].xmax -
          edit_win->file->gadget_bboxes[index].xmin) / 2;
    DEBUGF("Centre x is %d\n", *x);
  }
  if (y != NULL) /* paranoia */
  {
    *y = edit_win->file->gadget_bboxes[index].ymin +
         (edit_win->file->gadget_bboxes[index].ymax -
          edit_win->file->gadget_bboxes[index].ymin) / 2;
    DEBUGF("Centre y is %d\n", *y);
  }
}

/* ----------------------------------------------------------------------- */

void EditWin_bbox_from_index(EditWin const *const edit_win, int index, BBox *bbox)
{
  DEBUGF("Getting bbox of logical colour %u in view %p\n", index, (void *)edit_win);
  assert(edit_win != NULL);
  assert(index < edit_win->file->num_cols);
  assert(bbox != NULL);
  *bbox = edit_win->file->gadget_bboxes[index];
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
  bool const can_paste = (editor_get_num_selected(get_editor(edit_win)) >= 1 &&
                          edit_win->can_paste);
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
