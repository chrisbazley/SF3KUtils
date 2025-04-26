/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Input/output for sky editing window
 *  Copyright (C) 2006 Christopher Bazley
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
#include <limits.h>
#include "stdlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include "stdio.h"
#include <string.h>

/* RISC OS library files */
#include "kernel.h"
#include "event.h"
#include "wimp.h"
#include "wimplib.h"
#include "swis.h"

/* My library files */
#include "FileUtils.h"
#include "SFFormats.h"
#include "Macros.h"
#include "Saver2.h"
#include "Drag.h"
#include "Entity2.h"
#include "Err.h"
#include "Debug.h"
#include "WimpExtra.h"
#include "msgtrans.h"
#include "Loader3.h"
#include "SprFormats.h"
#include "SpriteArea.h"
#include "ScreenSize.h"
#include "OSVDU.h"
#include "OSFile.h"
#include "PalEntry.h"
#include "ClrTrans.h"
#include "DragAnObj.h"
#include "LinkedList.h"
#include "ReaderGKey.h"
#include "ReaderRaw.h"
#include "WriterGKey.h"
#include "WriterGKC.h"
#include "WriterRaw.h"
#include "WriterNull.h"
#include "Hourglass.h"
#include "CSV.h"
#include "FOpenCount.h"

/* Local headers */
#include "Sky.h"
#include "Export.h"
#include "SkyIO.h"
#include "EditWin.h"
#include "Menus.h"
#include "Utils.h"
#include "SFSInit.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Special value for SWI Wimp_DragBox */
#undef CancelDrag /* definition in "wimplib.h" is wrong! */
#define CancelDrag ((WimpDragBox *)-1)

/* Constant numeric values */
enum
{
  ThumbnailHeight = 68, /* in external graphics units */
  ThumbnailWidth = 68, /* in external graphics units */
  ThumbnailBorderColour = 0xaaaaaa, /* BbGgRr format */
  FixedPointOne = 1 << 24,
  WimpIcon_WorkArea = -1, /* Pseudo icon handle (window's work area) */
  WimpAutoScrollDefaultPause = -1, /* Use configured pause length */
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
  ContinueButton = 1,
  CancelButton = 2,
  DisableButton = 3,
  MinWimpVersion = 321, /* Oldest version of the window manager which
                           supports the extensions to Wimp_ReportError */
  MaxDAOVarValueLen = 15,
};

/* The following structures are used to hold data associated with an
   attempt to import or export colour bands (clipboard paste or drag
   and drop) */

static bool draganobject = false;

/* The following lists of RISC OS file types are in our order of preference
   Note that the first type on the 'export' list is always used if the other
   application expresses no preference. */

static int const import_file_types[] =
{
  FileType_CSV,
  FileType_SFSkyCol,
  FileType_Null
};

static int const export_file_types[] =
{
  FileType_CSV,
  FileType_Sprite,
  FileType_Text,
  FileType_Null
};

static int clipboard[NColourBands];
static int clipboard_size;
static _Optional EditWin *drag_claim_win;
static BBox selected_bbox;
static int drag_start_x, drag_start_y;
static int dragclaim_msg_ref;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static void read_fail(char const *const src_name)
{
  assert(src_name != NULL);
  err_report(DUMMY_ERRNO, msgs_lookup_subn("ReadFail", 1, src_name));
}

/* ----------------------------------------------------------------------- */

static void write_fail(char const *const dst_name)
{
  assert(dst_name != NULL);
  err_report(DUMMY_ERRNO, msgs_lookup_subn("WriteFail", 1, dst_name));
}

/* ----------------------------------------------------------------------- */

static int read_csv(int values[], size_t const max, Reader *const reader)
{
  assert(!reader_ferror(reader));
  assert(values != NULL);

  char str[256];
  size_t const nchars = reader_fread(str, 1, sizeof(str)-1, reader);
  str[nchars] = '\0';

  _Optional char *endp;
  size_t const nvals = csv_parse_string(str, &endp, values, CSVOutputType_Int,
    max);

  if (endp == NULL && nchars == sizeof(str)-1)
  {
    /* We filled the buffer but didn't find the end of the record */
    WARN("BufOFlo");
    return -1;
  }

  return LOWEST(nvals, max);
}

/* ----------------------------------------------------------------------- */

static bool import_csv(EditWin *const edit_win, Reader *const reader,
  char const *const src_name)
{
  /* Insert colour bands in CSV format at the current caret position */
  assert(edit_win != NULL);
  assert(!reader_ferror(reader));
  assert(src_name != NULL);

  DEBUGF("About to import CSV %s into view %p\n", src_name, (void *)edit_win);

  int csv_values[NColourBands];
  int n = read_csv(csv_values, ARRAY_SIZE(csv_values), reader);

  if (n < 0)
  {
    return false;
  }

  if (reader_ferror(reader))
  {
    read_fail(src_name);
    return false;
  }

  EditWin_give_focus(edit_win);
  return EditWin_insert_array(edit_win, n, csv_values);
}

/* ----------------------------------------------------------------------- */

bool load_sky(Reader *const reader, char const * const path,
  bool const is_safe)
{
  assert(path != NULL);
  assert(!reader_ferror(reader));

  /* Decompress the input stream */
  Reader gkreader;
  bool success = reader_gkey_init_from(&gkreader, FednetHistoryLog2, reader);
  if (!success)
  {
    RPT_ERR("NoMem");
    return false;
  }

  if (!SkyFile_create(&gkreader, path, is_safe))
  {
    success = false;
  }
  else if (reader_ferror(&gkreader))
  {
    read_fail(path);
    success = false;
  }
  reader_destroy(&gkreader);
  return success;
}

/* ----------------------------------------------------------------------- */

static bool load_csv(Reader *const reader, char const *const filename)
{
  assert(reader != NULL);
  assert(!reader_ferror(reader));
  assert(filename != NULL);

  bool success = false;
  _Optional SkyFile *const file = SkyFile_create(NULL, NULL, false);
  if (file)
  {
    EditWin *const edit_win = SkyFile_get_win(&*file);
    success = import_csv(edit_win, reader, filename);
    if (!success)
    {
      SkyFile_destroy(file);
    }
  }

  return success;
}

/* ----------------------------------------------------------------------- */

static void probe_complete(int const file_type, void *const client_handle)
{
  NOT_USED(client_handle);
  DEBUGF("Clipboard data is available as file type &%x\n", file_type);
  EditWin_set_paste_enabled(client_handle, in_file_types(file_type, import_file_types));
}

/* ----------------------------------------------------------------------- */

static void probe_failed(_Optional const _kernel_oserror *const e,
  void *const client_handle)
{
  NOT_USED(client_handle);
  NOT_USED(e);
  EditWin_set_paste_enabled(client_handle, false);
}

/* ----------------------------------------------------------------------- */

static bool import_skyfile(EditWin *const edit_win, Reader *const reader,
  char const *const src_name)
{
  DEBUGF("About to import sky %s into view %p\n", src_name, (void *)edit_win);
  assert(edit_win != NULL);
  assert(!reader_ferror(reader));
  assert(src_name != NULL);

  Reader gkreader;
  if (!reader_gkey_init_from(&gkreader, FednetHistoryLog2, reader))
  {
    RPT_ERR("NoMem");
    return false;
  }

  hourglass_on();
  Sky sky;
  SkyState const state = sky_read_file(&sky, &gkreader);
  hourglass_off();

  bool success = IO_report_read(state);
  if (success && reader_ferror(&gkreader))
  {
    read_fail(src_name);
    success = false;
  }
  else if (success)
  {
    /* Copy as many colours from the imported sky file as overlap the
       destination sky file */
    EditWin_give_focus(edit_win);
    EditWin_insert_sky(edit_win, &sky);
  }

  reader_destroy(&gkreader);

  return success;
}

/* ----------------------------------------------------------------------- */

static bool export_skyfile(EditWin *const edit_win, char const * const path,
  Writer *const writer, IOExportSkyFn *const fn)
{
  assert(edit_win != NULL);
  assert(path != NULL);
  assert(fn);

  /* Find the decompressed size upfront to avoid backward-seeking in
     the output stream (which may not be possible). */
  Writer null;
  writer_null_init(&null);
  bool success = fn(edit_win, &null);
  long int const decomp_size = writer_destroy(&null);
  if (success)
  {
    assert(decomp_size >= 0);
    assert(decomp_size <= INT32_MAX);
    DEBUGF("Decompressed size is %ld\n", decomp_size);

    /* Compress the output stream */
    Writer gkwriter;
    success = writer_gkey_init_from(&gkwriter, FednetHistoryLog2,
      (int32_t)decomp_size, writer);
    if (!success)
    {
      RPT_ERR("NoMem");
    }
    else
    {
      success = fn(edit_win, &gkwriter);
      if (writer_destroy(&gkwriter) < 0 && success)
      {
        write_fail(path);
        success = false;
      }
    }
  }

  return success;
}

/* ----------------------------------------------------------------------- */

static bool drag_or_paste_read(Reader *const reader, int const estimated_size,
  int const file_type, char const *const filename, void *const client_handle)
{
  /* This function is called to deliver clipboard contents or dragged data */
  EditWin *const edit_win = client_handle;
  assert(edit_win != NULL);
  NOT_USED(estimated_size);
  assert(filename != NULL);
  bool success = false;

  DEBUGF("Received data of type &%X\n", file_type);

  switch (file_type)
  {
  case FileType_CSV:
    success = import_csv(edit_win, reader, filename);
    break;

  case FileType_SFSkyCol:
    success = import_skyfile(edit_win, reader, filename);
    break;

  default:
    /* Cannot import data of this file type */
    RPT_ERR("BadFileType");
    break;
  }

  return success;
}

/* ----------------------------------------------------------------------- */

static void drag_or_paste_failed(_Optional const _kernel_oserror *const e,
  void *const client_handle)
{
  NOT_USED(client_handle);
  ON_ERR_RPT(e);
}

/* ----------------------------------------------------------------------- */

static int estimate_size(int const file_type, int const ncols)
{
  int size = 0;
  switch (file_type)
  {
    case FileType_CSV:
    case FileType_Text:
      size = estimate_CSV_file(ncols);
      break;

    case FileType_Sprite:
      size = estimate_sprite_file(ncols);
      break;

    default:
      assert("Bad file type " == NULL);
      break;
  }
  return size;
}

/* ----------------------------------------------------------------------- */

static int estimate_cb(int const file_type, void *const client_handle)
{
  NOT_USED(client_handle);
  return estimate_size(file_type, clipboard_size);
}

/* ----------------------------------------------------------------------- */

static bool cb_write(Writer *const writer, int const file_type,
  char const *const filename, void *const client_handle)
{
  /* This function is called to get the current clipboard contents, e.g.
     to paste them into a document. */
  NOT_USED(filename);
  NOT_USED(client_handle);

  if (clipboard_size == 0)
  {
    assert("Clipboard is empty" == NULL);
    return false;
  }

  switch (file_type)
  {
    case FileType_CSV:
    case FileType_Text:
      write_CSV_file(clipboard, clipboard_size, writer);
      break;

    case FileType_Sprite:
      write_sprite_file(clipboard, clipboard_size, writer);
      break;

    default:
      assert("Bad file type " == NULL);
      return false;
  }

  /* Library should detect any error and use the default message */
  return true;
}

/* ----------------------------------------------------------------------- */

static void cb_lost(void *const client_handle)
{
  /* This function is called to free any data held on the clipboard, for
     example if another application claims the global clipboard. */
  NOT_USED(client_handle);
  clipboard_size = 0;
}

/* ----------------------------------------------------------------------- */

static void relinquish_drag(void)
{
  if (drag_claim_win != NULL)
  {
    EditWin *const to_release = &*drag_claim_win;
    DEBUGF("View %p relinquishing drag\n", (void *)drag_claim_win);

    /* Undraw the ghost caret, if any */
    EditWin_remove_insert_pos(to_release);
    EditWin_stop_auto_scroll(to_release);
    drag_claim_win = NULL;
    dragclaim_msg_ref = 0;
  }
}

/* ======================== Wimp message handlers ======================== */

static int dragging_msg_handler(WimpMessage *const message, void *const handle)
{
  assert(message);
  assert(message->hdr.action_code == Wimp_MDragging);
  EditWin *const edit_win = handle;
  const WimpDraggingMessage *const dragging =
    (WimpDraggingMessage *)&message->data;

  DEBUGF("Received a Dragging message for icon %d in window &%x\n"
        " (coordinates %d,%d)\n", dragging->icon_handle,
        dragging->window_handle, dragging->x, dragging->y);

  DEBUGF("Bounding box of data is %d,%d,%d,%d\n", dragging->bbox.xmin,
        dragging->bbox.ymin, dragging->bbox.xmax, dragging->bbox.ymax);

  assert(edit_win != NULL);

  IO_dragging_msg(dragging);

  /* Check whether the pointer is within our window (excluding borders) */
  if (dragging->window_handle != EditWin_get_wimp_handle(edit_win) ||
      dragging->icon_handle < WimpIcon_WorkArea)
  {
    return 0; /* No - do not claim message */
  }

  /* The sender can set a flag to prevent us from claiming the drag again
     (i.e. force us to relinquish it if we had claimed it) */
  if (TEST_BITS(dragging->flags, Wimp_MDragging_DoNotClaimMessage))
  {
    DEBUGF("Forbidden from claiming this drag\n");
    if (drag_claim_win == edit_win)
    {
      relinquish_drag();
    }
  }
  else if (common_file_type(import_file_types, dragging->file_types) !=
    FileType_Null)
  {
    DEBUGF("We can handle one of the file types offered\n");

    WimpGetWindowStateBlock window_state;
    window_state.window_handle = EditWin_get_wimp_handle(edit_win);

    if (E(wimp_get_window_state(&window_state)) ||
        !claim_drag(message, import_file_types, &dragclaim_msg_ref))
    {
      if (drag_claim_win == edit_win)
      {
        relinquish_drag();
      }
    }
    else
    {
      if (drag_claim_win != edit_win)
      {
        EditWin_start_auto_scroll(edit_win,
                                  &window_state.visible_area,
                                  WimpAutoScrollDefaultPause,
                                  NULL);
      }

      /* Update the ghost caret position so that it follows the mouse
         pointer whilst this editing window is claiming the drag */
      EditWin_set_insert_pos(edit_win, &window_state, dragging->y);

      DEBUGF("Drag claimed by view %p\n", (void *)edit_win);
      drag_claim_win = edit_win;
    }
  }
  else
  {
    DEBUGF("We don't like any of their export file types\n");
    if (drag_claim_win == edit_win)
    {
      relinquish_drag();
    }
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int datasave_msg_handler(WimpMessage *const message, void *const handle)
{
  /* This handler should receive DataSave messages before CBLibrary's Loader
     component. We need to intercept replies to a DragClaim message. */
  EditWin *const edit_win = handle;

  assert(edit_win != NULL);
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataSave);

  DEBUGF("View %p evaluating a DataSave message (ref. %d in reply to %d)\n",
        (void *)edit_win, message->hdr.my_ref, message->hdr.your_ref);

  if (!EditWin_owns_wimp_handle(edit_win,
                                message->data.data_save.destination_window))
  {
    DEBUGF("Destination is not in view %p\n", (void *)edit_win);
    return 0; /* message is not intended for this editing window */
  }

  if (message->hdr.your_ref != 0)
  {
    if (dragclaim_msg_ref != message->hdr.your_ref)
    {
      return 0; /* could be a reply to a DataRequest message */
    }

    /* It's a reply to our drag claim message from a task about to send dragged
       data, so set the caret position in preparation for inserting data. */
    EditWin_confirm_insert_pos(edit_win);
    relinquish_drag();
  }

  if (!in_file_types(message->data.data_save.file_type, import_file_types))
  {
    RPT_ERR("BadFileType");
    return 1;
  }

  ON_ERR_RPT(loader3_receive_data(message, drag_or_paste_read,
            drag_or_paste_failed, edit_win));

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int datasave_fallback_handler(WimpMessage *const message,
  void *const handle)
{
  /* A fallback handler in case the window cited in the DataSave message does
     not belong to any of our views. In such cases, none will claim the
     message (leaving the drag claimant with auto-scrolling enabled). */
  int claim = 0;

  assert(message);
  assert(message->hdr.action_code == Wimp_MDataSave);
  NOT_USED(handle);
  DEBUGF("Fallback handler got a DataSave message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref != 0 &&
      dragclaim_msg_ref == message->hdr.your_ref)
  {
    relinquish_drag();
    claim = 1;
  }
  return claim;
}

/* ----------------------------------------------------------------------- */

static int data_open_msg(WimpMessage *const message, void *const handle)
{
  assert(message != NULL);
  assert(message->hdr.action_code == Wimp_MDataOpen);
  NOT_USED(handle);

  if (message->data.data_open.file_type != FileType_SFSkyCol)
  {
    return 0; /* message not handled */
  }

  /* Attempt to load the file, if it is a recognised type */
  IO_load_file(message->data.data_open.file_type,
               message->data.data_open.path_name);

  /* Claim the file whether successful or not */
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

static int dataload_msg_handler(WimpMessage *const message, void *const handle)
{
  EditWin *const edit_win = handle;

  assert(message);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  DEBUGF("Received a DataLoad message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  if (message->hdr.your_ref)
  {
    DEBUGF("View %p ignoring a reply\n", (void *)edit_win);
    return 0; /* message is a reply (should be dealt with by Loader3 module) */
  }

  assert(edit_win != NULL);

  if (!EditWin_owns_wimp_handle(edit_win,
                                message->data.data_load.destination_window))
  {
    DEBUGF("Destination is not in view %p\n", (void *)edit_win);
    return 0; /* message is not intended for this editing window */
  }

  if (!in_file_types(message->data.data_load.file_type, import_file_types))
  {
    RPT_ERR("BadFileType");
    return 1;
  }

  bool success = loader3_load_file(message->data.data_load.leaf_name,
      message->data.data_load.file_type,
      drag_or_paste_read, drag_or_paste_failed, edit_win);

  if (success)
  {
    /* Acknowledge that the file was loaded successfully
     (just a courtesy message, we don't expect a reply) */
    message->hdr.your_ref = message->hdr.my_ref;
    message->hdr.action_code = Wimp_MDataLoadAck;

    if (!E(wimp_send_message(Wimp_EUserMessage,
                message, message->hdr.sender, 0, NULL)))
    {
      DEBUGF("Sent DataLoadAck message (ref. %d)\n", message->hdr.my_ref);
    }
  }

  return 1; /* claim message */
}

static const struct
{
  int                 msg_no;
  WimpMessageHandler *handler;
}
message_handlers[] =
{
  {
    Wimp_MDragging,
    dragging_msg_handler
  },
  {
    Wimp_MDataSave,
    datasave_msg_handler
  },
  {
    Wimp_MDataLoad,
    dataload_msg_handler
  }
};

/* ===================== CBLibrary client functions ====================== */

/* Function called back to render the selected colours for DragAnObject to use
   whilst updating the screen during a drag operation. Must not call shared C
   library functions that may require access to the library's static data
   (not even via assert or DEBUG macros). See DragAnObj.h for details. */
#pragma no_check_stack

static void DAO_render(int const cptr, int const ncols)
{
  /* Draw light grey border rectangle */
  if (_swix(ColourTrans_SetGCOL, _IN(0)|_INR(3,4),
            (unsigned)ThumbnailBorderColour << PaletteEntry_RedShift,
            0,
            GCOLAction_OpaqueBG + GCOLAction_Overwrite) != NULL)
    return; /* error! */

  if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_MoveAbs, 0, 0) != NULL)
    return; /* error! */

  if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_PlotFGAbs,
            ThumbnailWidth - 1,
            0) != NULL)
    return; /* error! */

  if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_PlotFGAbs,
            ThumbnailWidth - 1,
            ThumbnailHeight - 1) != NULL)
    return; /* error! */

  if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_PlotFGAbs,
            0,
            ThumbnailHeight - 1) != NULL)
    return; /* error! */

  if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_PlotFGAbs, 0, 0) != NULL)
    return; /* error! */

  int const *const colours = (int *)(uintptr_t)cptr;
  int const x_pix = 1 << x_eigen;
  int const y_pix = 1 << y_eigen;
  int const row_height = ((ThumbnailHeight - 2 * y_pix) * FixedPointOne) / ncols;
  int bottom_y = y_pix * FixedPointOne;

  for (int index = 0; index < ncols; ++index)
  {
    if (_swix(ColourTrans_SetGCOL, _IN(0)|_INR(3,4),
              palette[colours[index]],
              ColourTrans_SetGCOL_UseECF,
              GCOLAction_OpaqueBG + GCOLAction_Overwrite) != NULL)
      return;

    if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_MoveAbs,
              x_pix,
              (bottom_y + FixedPointOne/2) / FixedPointOne) != NULL)
      return;

    bottom_y += row_height;

    if (_swix(OS_Plot, _INR(0,2), PlotOp_RectangleFill + PlotOp_PlotFGAbs,
              ThumbnailWidth - 2 * x_pix,
              (bottom_y + FixedPointOne/2) / FixedPointOne - y_pix) != NULL)
      return;
  }
}
#pragma -s

/* ----------------------------------------------------------------------- */

static _Optional const _kernel_oserror *drag_box(const DragBoxOp action,
  bool solid_drags, int const mouse_x, int const mouse_y,
  void *const client_handle)
{
  EditWin *const edit_win = client_handle;
  static bool using_dao = false;

  assert(edit_win != NULL);

  /* If the DragAnObject module is not available then revert to using
     a dashed outline to represent the dragged data */
  if (!draganobject)
    solid_drags = false;

  if (action == DragBoxOp_Cancel)
  {
    if (using_dao)
    {
      ON_ERR_RTN_E(drag_an_object_stop());
    }
    else
    {
      DEBUGF("Calling Wimp_DragBox to cancel drag\n");
      ON_ERR_RTN_E(wimp_drag_box(CancelDrag));
    }
  }
  else
  {
    WimpDragBox drag_box;
#ifndef FULL_SIZE_DRAG
    if (solid_drags)
    {
      drag_box.dragging_box.xmin = mouse_x - ThumbnailWidth / 2;
      drag_box.dragging_box.ymin = mouse_y - ThumbnailHeight / 2;
      drag_box.dragging_box.xmax = drag_box.dragging_box.xmin + ThumbnailWidth;
      drag_box.dragging_box.ymax = drag_box.dragging_box.ymin +
                                   ThumbnailHeight;
    }
    else
#endif
    {
      drag_box.dragging_box.xmin = selected_bbox.xmin - drag_start_x + mouse_x;
      drag_box.dragging_box.ymin = selected_bbox.ymin - drag_start_y + mouse_y;
      drag_box.dragging_box.xmax = selected_bbox.xmax - drag_start_x + mouse_x;
      drag_box.dragging_box.ymax = selected_bbox.ymax - drag_start_y + mouse_y;
    }

    if (solid_drags && action == DragBoxOp_Start)
    {
      int colours[NColourBands];
      int const ncol = EditWin_get_array(edit_win, colours, NColourBands);
      assert(ncol <= NColourBands);

      int const renderer_args[4] =
      {
        (uintptr_t)&colours, ncol
      };

      unsigned int flags = DragAnObject_BBoxPointer | DragAnObject_RenderAPCS;
#ifndef FULL_SIZE_DRAG
      flags |= DragAnObject_HAlign_Centre | DragAnObject_VAlign_Centre;
#endif
      ON_ERR_RTN_E(drag_an_object_start(flags,
                                        (uintptr_t)DAO_render,
                                        renderer_args,
                                        &drag_box.dragging_box,
                                        &(BBox){0}));

      using_dao = true;
    }
    else
    {
      if (using_dao)
      {
        using_dao = false;
        ON_ERR_RTN_E(drag_an_object_stop());
      }

      /* Allow drag anywhere on the screen (complicated because the bounding
         box applies to the drag box rather than the mouse pointer) */
      ON_ERR_RTN_E(get_screen_size(&drag_box.parent_box.xmax,
                                   &drag_box.parent_box.ymax));

      drag_box.parent_box.xmin = - (mouse_x - drag_box.dragging_box.xmin);
      drag_box.parent_box.ymin = - (mouse_y - drag_box.dragging_box.ymin);
      drag_box.parent_box.xmax += drag_box.dragging_box.xmax - mouse_x;
      drag_box.parent_box.ymax += drag_box.dragging_box.ymax - mouse_y;
      if (action == DragBoxOp_Hide)
        drag_box.drag_type = Wimp_DragBox_DragPoint;
      else
        drag_box.drag_type = Wimp_DragBox_DragFixedDash;

      DEBUGF("Calling Wimp_DragBox to start drag of type %d\n",
             drag_box.drag_type);

      ON_ERR_RTN_E(wimp_drag_box(&drag_box));
    }
  }
  return NULL; /* no error */
}

/* ----------------------------------------------------------------------- */

static bool sel_write(Writer *const writer, int const file_type,
  char const *const filename, void *const client_handle)
{
  /* This function is called to send the selected data when one of our drags
     terminates. We could predict the file type but don't bother. */
  NOT_USED(filename);

  EditWin *const edit_win = client_handle;
  assert(edit_win != NULL);

  int raw_values[NColourBands];
  int const ncols = EditWin_get_array(edit_win, raw_values, ARRAY_SIZE(raw_values));

  assert(ncols >= 0);
  assert((unsigned)ncols <= ARRAY_SIZE(raw_values));

  switch (file_type)
  {
    case FileType_Text:
    case FileType_CSV:
      write_CSV_file(raw_values, ncols, writer);
      break;

    case FileType_Sprite:
      write_sprite_file(raw_values, ncols, writer);
      break;

    default:
      assert("Bad file type" == NULL);
      return false;
  }

  /* Caller checks the error indicator of the writer object */
  return true;
}

/* ----------------------------------------------------------------------- */

static void sel_moved(int const file_type, _Optional char const *const file_path,
  int const datasave_ref, void *const client_handle)
{
  EditWin *const edit_win = client_handle;
  assert(edit_win != NULL);
  NOT_USED(datasave_ref);
  NOT_USED(file_type);

  DEBUGF("Selection moved to %s with DataSave message %d\n",
    file_path != NULL ? file_path : "unsafe destination", datasave_ref);

  /* Data dragged to another file should be moved (source deleted) if the Shift
     key was held. Move operations within a file will already have been dealt
     with by drop_handler(). */
  EditWin_delete_colours(edit_win);
}

/* ----------------------------------------------------------------------- */

static void sel_failed(_Optional const _kernel_oserror *const error,
  void *const client_handle)
{
  NOT_USED(client_handle);

  if (error != NULL)
  {
    err_report(error->errnum, msgs_lookup_subn("SaveFail", 1, error->errmess));
  }
}

/* ----------------------------------------------------------------------- */

static bool drop_handler_remote(bool const shift_held, int const window,
  int const icon, int const mouse_x, int const mouse_y, int const file_type,
  int const claimant_task, int const claimant_ref, EditWin *const source_view)
{
  /* Drag terminated in another task's window, therefore we cannot
     bypass the remainder of the message protocol */
  DEBUGF("Drag destination is remote\n");

  int sel_start, sel_end;
  EditWin_get_selection(source_view, &sel_start, &sel_end);
  assert(sel_end >= sel_start);
  int const source_size = sel_end - sel_start;

  WimpMessage msg = {
    .hdr.your_ref = claimant_ref,
    /* action code and message size are filled out automatically */
    .data.data_save = {
      .destination_window = window,
      .destination_icon = icon,
      .destination_x = mouse_x,
      .destination_y = mouse_y,
      .estimated_size = estimate_size(file_type, source_size),
      .file_type = file_type,
    }
  };

  STRCPY_SAFE(msg.data.data_save.leaf_name, msgs_lookup("LeafName"));

  return !E(saver2_send_data(claimant_task, &msg, sel_write,
                             shift_held ? sel_moved : 0,
                             sel_failed, source_view));
}

/* ----------------------------------------------------------------------- */

static bool drop_handler(bool const shift_held, int const window,
  int const icon, int const mouse_x, int const mouse_y, int const file_type,
  int const claimant_task, int const claimant_ref, void *const client_handle)
{
  /* This function is called when a drag has terminated */
  EditWin *const source_view = client_handle;
  _Optional EditWin *dest_view;
  bool saved = true;

  DEBUGF("Notification of drop at %d,%d (icon %d in window %d)\n",
        mouse_x, mouse_y, icon, window);

  assert(source_view != NULL);

  if (EditWin_owns_wimp_handle(source_view, window))
  {
    /* Drag destination is within the same editing window */
    DEBUGF("Drag terminated within source window\n");
    dest_view = source_view;
  }
  else
  {
    dest_view = EditWin_from_wimp_handle(window);
  }

  if (dest_view != NULL)
  {
    EditWin_drop_handler(&*dest_view, source_view, shift_held);

    /* It's more robust to stop the drag now instead of returning false
       and waiting for a final Dragging message. */
    if (drag_claim_win == dest_view)
    {
      relinquish_drag();
    }
  }
  else
  {
    saved = drop_handler_remote(shift_held, window, icon, mouse_x, mouse_y,
                                file_type, claimant_task, claimant_ref,
                                source_view);
  }

  return saved;
}

/* ---------------------------------------------------------------------- */

bool format_warning = true;

static bool report_dither(void)
{
  bool valid = true;

  int button;
  /* msgs_error stores its result in a buffer that is shared system-wide, so
     look up the buttons text first in case the SWI triggers callbacks */
  char const *const buttons = msgs_lookup("DithQuiet");
  const _kernel_oserror *const e = msgs_error(DUMMY_ERRNO, "DithWarn");

  if (wimp_version >= MinWimpVersion)
  {
    /* Nice error box */
    button = wimp_report_error((_kernel_oserror *)e,
                               Wimp_ReportError_OK |
                                 Wimp_ReportError_Cancel |
                                 Wimp_ReportError_UseCategory |
                                 Wimp_ReportError_CatInform,
                               taskname,
                               NULL,
                               NULL,
                               buttons);
  }
  else
  {
    /* Backwards compatibility */
    button = wimp_report_error((_kernel_oserror *)e,
                               Wimp_ReportError_OK |
                                 Wimp_ReportError_Cancel,
                               taskname);
  }

  switch (button)
  {
    case DisableButton:
      /* disable future warnings and continue */
      format_warning = false;
      break;

    case ContinueButton:
      /* just continue */
      break;

    case CancelButton:
      valid = false; /* discard loaded file */
      break;

    default:
      assert("Unknown button in error box" == NULL);
      break;
  }

  return valid;
}

/* ----------------------------------------------------------------------- */

static void load_fail(_Optional CONST _kernel_oserror *const error,
  void *const client_handle)
{
  NOT_USED(client_handle);
  if (error != NULL)
  {
    err_check_rep(msgs_error_subn(error->errnum, "LoadFail", 1, error->errmess));
  }
}

/* ----------------------------------------------------------------------- */

static bool read_file(Reader *const reader, int const estimated_size,
  int const file_type, char const *const filename, void *const client_handle)
{
  NOT_USED(estimated_size);
  bool is_safe = (client_handle != NULL);
  bool success = false;

  switch (file_type)
  {
  case FileType_SFSkyCol:
    /* If the data was loaded from a persistent file then
       the client handle should point to a heap block holding its full path. */
    success = load_sky(reader, filename, is_safe);
    break;

  case FileType_CSV:
    success = load_csv(reader, filename);
    break;

  default:
    assert("Unexpected file type" == NULL);
    break;
  }

  return success;
}

static void init_data_request(EditWin const *const edit_win,
  WimpDataRequestMessage *const data_request)
{
  assert(edit_win != NULL);
  assert(data_request);

  *data_request = (WimpDataRequestMessage)
  {
    EditWin_get_wimp_handle(edit_win), WimpIcon_WorkArea, 0, 0,
    Wimp_MDataRequest_Clipboard,
    { FileType_CSV, FileType_SFSkyCol, FileType_Null }
  };
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void IO_initialise(void)
{
  /* Register a fallback handler for DataSave messages
     (should be called last, since it is registered first) */
  static const struct
  {
    int                 msg_no;
    WimpMessageHandler *handler;
  }
  msg_handlers[] =
  {
    {
      Wimp_MDataSave,
      datasave_fallback_handler,
    },
    {
      Wimp_MDataOpen,
      data_open_msg,
    },
  };

  for (size_t i = 0; i < ARRAY_SIZE(msg_handlers); i++)
  {
    EF(event_register_message_handler(msg_handlers[i].msg_no,
                                      msg_handlers[i].handler,
                                      (void *)NULL));
  }

  /* Check for DragAnObject module */
  EF(_kernel_setenv(APP_NAME"$DAO","1"));
  if (_kernel_oscli("RMEnsure DragAnObject 0 Set "APP_NAME"$DAO 0") ==
      _kernel_ERROR)
  {
    EF(_kernel_last_oserror());
    exit(EXIT_FAILURE);
  }

  char readvar_buffer[MaxDAOVarValueLen + 1];
  EF(_kernel_getenv(APP_NAME"$DAO", readvar_buffer, sizeof(readvar_buffer)));
  draganobject = (strcmp(readvar_buffer, "1") == 0);

  /* Older versions of the C library have a bug where NULL cannot be
     passed to _kernel_setenv to delete a variable */
  _kernel_swi_regs regs = {
    .r = {
      (uintptr_t)APP_NAME"$DAO",
      0, /* no value */
      -1, /* delete variable */
      0, /* first call */
      0, /* string */
    },
  };
  EF(_kernel_swi(OS_SetVarVal, &regs, &regs));
}

/* ----------------------------------------------------------------------- */

void IO_receive(WimpMessage const *const message)
{
  assert(message);
  assert(message->hdr.action_code == Wimp_MDataSave);

  if (in_file_types(message->data.data_save.file_type, import_file_types))
  {
    ON_ERR_RPT(loader3_receive_data(message, read_file, load_fail, (void *)NULL));
  }
  else
  {
    RPT_ERR("BadFileType");
  }
}

/* ----------------------------------------------------------------------- */

void IO_load_file(int const file_type, char const *const load_path)
{
  assert(load_path != NULL);
  DEBUGF("Request to load file '%s' of type &%X\n", load_path, file_type);
  _Optional char *canonical_path = NULL;

  /* Check whether this file type is supported */
  if (!in_file_types(file_type, import_file_types))
  {
    RPT_ERR("BadFileType");
  }
  else if (!E(canonicalise(&canonical_path, NULL, NULL, load_path)) &&
           canonical_path)
  {
    /* Check for whether this file is already being edited */
    _Optional SkyFile *const file = SkyFile_find_by_file_name(&*canonical_path);
    if (file == NULL)
    {
      bool is_safe = true;
      (void)loader3_load_file(&*canonical_path, file_type,
                              read_file, load_fail, &is_safe);
    }
    else
    {
      /* Reopen existing editing window at top of stack */
      DEBUGF("This file is already being edited (%p)\n", (void *)file);
      SkyFile_show(&*file);
    }
    free(canonical_path);
  }
}

/* ----------------------------------------------------------------------- */

bool IO_view_created(EditWin *const edit_win)
{
  bool success = true;
  size_t i;

  assert(edit_win != NULL);

  /* Register handlers for Wimp messages (easier to register for each window
     rather than searching the UserData list for the relevant view) */
  for (i = 0; i < ARRAY_SIZE(message_handlers); i++)
  {
    if (E(event_register_message_handler(message_handlers[i].msg_no,
                                         message_handlers[i].handler,
                                         edit_win)))
    {
      success = false;
      break;
    }
  }

  if (!success)
  {
    /* Deregister any Wimp event handlers that were successfully registered */
    while (i-- > 0)
    {
      event_deregister_message_handler(message_handlers[i].msg_no,
                                       message_handlers[i].handler,
                                       edit_win);
    }
  }

  return success;
}

/* ----------------------------------------------------------------------- */

void IO_cancel(EditWin *const edit_win)
{
  /* Make safe any outstanding selection exports */
  DEBUGF("Making safe any I/O concerning window %p\n", (void *)edit_win);
  loader3_cancel_receives(edit_win);
  entity2_cancel_requests(edit_win);
  saver2_cancel_sends(edit_win);
}

/* ----------------------------------------------------------------------- */

void IO_view_deleted(EditWin *const edit_win)
{
  IO_cancel(edit_win);

  /* Deregister handlers for Wimp messages */
  for (size_t i = 0; i < ARRAY_SIZE(message_handlers); i++)
  {
    ON_ERR_RPT(event_deregister_message_handler(message_handlers[i].msg_no,
                                                message_handlers[i].handler,
                                                edit_win));

  }
}

/* ----------------------------------------------------------------------- */

bool IO_start_drag(EditWin *const edit_win, int const start_x, int const start_y,
                   BBox const *const bbox)
{
  assert(edit_win != NULL);
  assert(bbox != NULL);
  assert(bbox->xmin < bbox->xmax);
  assert(bbox->ymin < bbox->ymax);

  selected_bbox = *bbox;
  drag_start_x = start_x;
  drag_start_y = start_y;

  ON_ERR_RPT(drag_abort());

  return !E(drag_start(export_file_types, NULL, drag_box, drop_handler,
                       edit_win));
}

/* ----------------------------------------------------------------------- */

void IO_paste(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  WimpDataRequestMessage data_request;
  init_data_request(edit_win, &data_request);
  ON_ERR_RPT(entity2_request_data(&data_request, drag_or_paste_read,
                         drag_or_paste_failed, edit_win));
}

/* ----------------------------------------------------------------------- */

bool IO_copy(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  /* Claim the global clipboard
     (a side-effect is to free any clipboard data held by us) */
  if (E(entity2_claim(Wimp_MClaimEntity_Clipboard, export_file_types,
      estimate_cb, cb_write, cb_lost, edit_win)))
  {
    return false; /* failure */
  }

  clipboard_size = EditWin_get_array(edit_win, clipboard,
    ARRAY_SIZE(clipboard));

  assert(clipboard_size >= 1);
  assert((unsigned)clipboard_size <= ARRAY_SIZE(clipboard));

  return true; /* success */
}

/* ----------------------------------------------------------------------- */

void IO_dragging_msg(const WimpDraggingMessage *const dragging)
{
  /* If this Dragging message is not for the window that previously claimed
     the drag then undraw its ghost caret and stop auto-scrolling. */
  assert(dragging != NULL);
  if (drag_claim_win != NULL &&
      (dragging->window_handle != EditWin_get_wimp_handle(&*drag_claim_win) ||
       dragging->icon_handle < WimpIcon_WorkArea))
  {
    relinquish_drag();
  }
}

/* ----------------------------------------------------------------------- */

bool IO_report_read(SkyState state)
{
  switch (state)
  {
    case SkyState_ReadFail:
      state = SkyState_OK; /* caller should check for reader error */
      break;
    case SkyState_BadLen:
      WARN("BadLen");
      break;
    case SkyState_BadRend:
      WARN("BadRend");
      break;
    case SkyState_BadStar:
      WARN("BadStar");
      break;
    case SkyState_BadDither:
      if (report_dither())
      {
        state = SkyState_OK;
      }
      break;
    default:
      assert(state == SkyState_OK);
      break;
  }

  return state == SkyState_OK;
}

/* ----------------------------------------------------------------------- */

bool IO_read_sky(Sky *const sky, Reader *const reader)
{
  assert(!reader_ferror(reader));
  assert(sky != NULL);

  hourglass_on();
  SkyState state = sky_read_file(sky, reader);
  hourglass_off();

  return IO_report_read(state);
}

/* ----------------------------------------------------------------------- */

void IO_update_can_paste(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  WimpDataRequestMessage data_request;
  init_data_request(edit_win, &data_request);
  if (E(entity2_probe_data(&data_request, probe_complete, probe_failed, edit_win)))
  {
    EditWin_set_paste_enabled(edit_win, false);
  }
}

/* ----------------------------------------------------------------------- */

bool IO_export_sky_file(EditWin *const edit_win,
  char const * const path, IOExportSkyFn *const fn)
{
  assert(edit_win != NULL);
  assert(path != NULL);
  assert(fn);

  bool success = false;
  _Optional FILE *const f = fopen_inc(path, "wb");
  if (!f)
  {
    err_report(DUMMY_ERRNO, msgs_lookup_subn("OpenOutFail", 1, path));
  }
  else
  {
    Writer raw;
    writer_raw_init(&raw, &*f);
    success = export_skyfile(edit_win, path, &raw, fn);
    long int const comp_size = writer_destroy(&raw);
    int const err = fclose_dec(&*f);

    if ((err || comp_size < 0) && success)
    {
      write_fail(path);
      success = false;
    }

    if (success)
    {
      success = !E(os_file_set_type(path, FileType_SFSkyCol));
    }

    if (!success)
    {
      remove(path);
    }
  }
  return success;
}

/* ----------------------------------------------------------------------- */

int IO_estimate_sky(EditWin *const edit_win, IOExportSkyFn *const fn)
{
  assert(edit_win != NULL);
  assert(fn);

  /* Experimentally compress the sky, to find out the file size */
  Writer gkcounter;
  long int out_size = 0;
  if (!writer_gkc_init(&gkcounter, FednetHistoryLog2, &out_size))
  {
    RPT_ERR("NoMem");
    out_size = 0;
  }
  else
  {
    hourglass_on();
    bool const success = fn(edit_win, &gkcounter);
    hourglass_off();
    /* writer_destroy returns the uncompressed size, not the compressed size */
    if (writer_destroy(&gkcounter) < 0 || !success)
    {
      out_size = 0;
    }
  }
  assert(out_size >= 0);
  assert(out_size <= INT_MAX);
  return (int)out_size;
}
