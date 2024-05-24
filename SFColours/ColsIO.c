/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Input/output for palette editing window
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
#include "stdlib.h"
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <stddef.h>
#include "stdio.h"
#include <string.h>
#include <limits.h>

/* RISC OS library files */
#include "kernel.h"
#include "event.h"
#include "wimp.h"
#include "wimplib.h"
#include "toolbox.h"
#include "window.h"
#include "swis.h"

/* My library files */
#include "FileUtils.h"
#include "FopenCount.h"
#include "ReaderGKey.h"
#include "ReaderRaw.h"
#include "ReaderNull.h"
#include "WriterGKey.h"
#include "WriterGKC.h"
#include "WriterRaw.h"
#include "WriterNull.h"
#include "Macros.h"
#include "Saver2.h"
#include "Drag.h"
#include "Entity2.h"
#include "err.h"
#include "Debug.h"
#include "WimpExtra.h"
#include "msgtrans.h"
#include "Loader3.h"
#include "CSV.h"
#include "ScreenSize.h"
#include "SFFormats.h"
#include "DragAnObj.h"
#include "PalEntry.h"
#include "OSVDU.h"
#include "ClrTrans.h"
#include "LinkedList.h"
#include "Hourglass.h"

/* Local headers */
#include "SFCInit.h"
#include "ColsIO.h"
#include "ExpColFile.h"
#include "EditWin.h"
#include "Utils.h"
#include "Menus.h"

/* Special value for SWI Wimp_DragBox */
#undef CancelDrag /* definition in "wimplib.h" is wrong! */
#define CancelDrag ((WimpDragBox *)-1)

/* Constant numeric values */
enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
  WimpIcon_WorkArea          = -1, /* Pseudo icon handle (window's work area) */
  WimpAutoScrollDefaultPause = -1, /* Use configured pause length */
  MaxDAOVarValueLen = 15,
};

/* Enable support for Data files imported from the Filer. This may be useful
   during debugging but in actual usage such files are rare because the default
   export format is CSV. */
#define DUMB_DATA_IMPORT 1

/* The following structures are used to hold data associated with an
   attempt to import or export colour bands (clipboard paste or drag
   and drop) */

typedef enum
{
  IOActionCode_PasteClip,
  IOActionCode_Import,
  IOActionCode_Export,
  IOActionCode_Done,
}
IOActionCode;

typedef struct IOActionData
{
  LinkedListItem   list_node;
  IOActionCode     action;
  EditWin         *edit_win;
  IOCoords coords;
}
IOActionData;

static bool draganobject = false;
bool cb_valid = false;

/* The following lists of RISC OS file types are in our order to preference
   Note that the first type on the 'export' list is always used if the other
   application expresses no preference. */
static int const import_file_types[] =
{
  FileType_Data,
  FileType_CSV,
  FileType_Fednet,
  FileType_Null
};

static int const export_file_types[] =
{
  FileType_CSV,
  FileType_Text,
  FileType_Fednet,
  FileType_Data,
  FileType_Null
};

static LinkedList action_data_list;
static ExpColFile clipboard;
static EditWin *drag_claim_view;
static BBox selected_bbox;
static IOCoords drag_pos; /* relative to source window's wk area */
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

  hourglass_on();

  char str[256];
  size_t const nchars = reader_fread(str, 1, sizeof(str)-1, reader);
  str[nchars] = '\0';

  char *endp;
  size_t const nvals = csv_parse_string(str, &endp, values, CSVOutputType_Int,
    max);

  hourglass_off();

  if (endp == NULL && nchars == sizeof(str)-1)
  {
    /* We filled the buffer but didn't find the end of the record */
    WARN("BufOFlo");
    return -1;
  }

  return LOWEST(nvals, max);
}

/* ----------------------------------------------------------------------- */

static bool centre_of_first_sel(EditWin *const edit_win,
  IOCoords *const origin)
{
  DEBUGF("Finding centre of first selected colour in view %p\n",
    (void *)edit_win);
  assert(edit_win != NULL);

  /* Find index of first selected logical colour */
  int const first = EditWin_get_next_selected(edit_win, -1);
  if (first < 0)
  {
    return false; /* failed */
  }
  else
  {
    EditWin_coords_from_index(edit_win, first, &origin->x, &origin->y);
    return true; /* success */
  }
}

/* ----------------------------------------------------------------------- */

static bool import_csv(EditWin *const edit_win, Reader *const reader,
  IOCoords const target, char const *const src_name)
{
  /* Map a sequence of logical colours to the physical colour numbers specified
     in a CSV file */
  assert(edit_win != NULL);
  assert(!reader_ferror(reader));
  assert(src_name != NULL);

  DEBUGF("About to import CSV %s into view %p at %d,%d\n",
        src_name, (void *)edit_win, target.x, target.y);

  int csv_values[EditWin_MaxSize];
  int const n = read_csv(csv_values, ARRAY_SIZE(csv_values), reader);

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
  return EditWin_set_array(edit_win, target.x, target.y, n, csv_values);
}

/* ----------------------------------------------------------------------- */

static bool import_colfile(EditWin *const edit_win, Reader *const reader,
  IOCoords const target, char const *const src_name)
{
  /* Map a sequence of logical colours to the physical colour numbers specified
     in a compressed native file */
  assert(edit_win != NULL);
  assert(!reader_ferror(reader));
  assert(src_name != NULL);

  DEBUGF("About to import colours %s into view %p at %d,%d\n",
        src_name, (void *)edit_win, target.x, target.y);

  Reader gkreader;
  if (!reader_gkey_init_from(&gkreader, FednetHistoryLog2, reader))
  {
    RPT_ERR("NoMem");
    return false;
  }

  ColMap colmap;
  bool success = IO_read_colmap(&colmap, &gkreader);
  if (success && reader_ferror(&gkreader))
  {
    read_fail(src_name);
    success = false;
  }
  else if (success)
  {
    EditWin_give_focus(edit_win);
    EditWin_set_colmap(edit_win, target.x, target.y, &colmap);
  }

  reader_destroy(&gkreader);

  return success;
}

/* ----------------------------------------------------------------------- */

static bool import_expcolfile(EditWin *const edit_win,
  Reader *const reader, IOCoords const target, bool const simple,
  char const *const src_name)
{
  /* Map a sequence of logical colours to the physical colour numbers specified
     in an export colour file */
  assert(edit_win != NULL);
  assert(!reader_ferror(reader));
  assert(src_name != NULL);
  DEBUGF("About to import exported colours %s\n", src_name);

  hourglass_on();
  ExpColFile export_file;
  ExpColFileState const state = ExpColFile_read(&export_file, reader);
  hourglass_off();

  switch (state)
  {
    case ExpColFileState_OK:
      break;
    case ExpColFileState_ReadFail:
      read_fail(src_name);
      break;
    case ExpColFileState_NoMem:
      RPT_ERR("NoMem");
      break;
    default:
      WARN("BadDataFile");
      break;
  }

  if (state != ExpColFileState_OK)
  {
    return false;
  }

  EditWin_give_focus(edit_win);

  /* This is our own file format, which contains colours and positional
     information (relative to drop location) */
#if !CLIPBOARD_HOLD_POS
  if (simple)
  {
    EditWin_set_expcol_flat(edit_win, target.x,
      target.y, &export_file);
  }
  else
#endif /* !CLIPBOARD_HOLD_POS */
  {
    EditWin_set_expcol(edit_win, target.x, target.y, &export_file);
  }

  ExpColFile_destroy(&export_file);
  return true;
}

/* ----------------------------------------------------------------------- */

static void destroy_record(IOActionData *action_data)
{
  /* Destroy record of I/O action and de-link it from the list */
  if (action_data != NULL)
  {
    DEBUGF("Destroying I/O record %p\n", (void *)action_data);
    linkedlist_remove(&action_data_list, &action_data->list_node);
    free(action_data);
  }
}

/* ----------------------------------------------------------------------- */

static IOActionData *create_record(IOActionCode action,
  EditWin *const edit_win)
{
  /* Allocate record for an I/O operation and link it into the list */
  IOActionData *const action_data = malloc(sizeof(*action_data));
  if (action_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *action_data = (IOActionData){
    .action = action,
    .edit_win = edit_win,
  };

  action_data->coords = (IOCoords){0,0};

  linkedlist_insert(&action_data_list, NULL, &action_data->list_node);

  DEBUGF("Created IO record %p (action code %d, view %p)\n", (void *)action_data,
        action_data->action, (void *)action_data->edit_win);

  return action_data;
}

/* ----------------------------------------------------------------------- */

static void probe_complete(int const file_type, void *const client_handle)
{
  NOT_USED(client_handle);
  DEBUGF("Clipboard data is available as file type &%x\n", file_type);
  EditWin_set_paste_enabled(client_handle, in_file_types(file_type, import_file_types));
}

/* ----------------------------------------------------------------------- */

static void probe_failed(const _kernel_oserror *const e,
  void *const client_handle)
{
  NOT_USED(client_handle);
  NOT_USED(e);
  EditWin_set_paste_enabled(client_handle, false);
}

/* ----------------------------------------------------------------------- */

static bool export_colmap(EditWin *const edit_win, char const * const path,
  Writer *const writer)
{
  assert(edit_win != NULL);
  assert(path != NULL);

  /* Find the decompressed size upfront to avoid backward-seeking in
     the output stream (which may not be possible). */
  Writer null;
  writer_null_init(&null);
  bool success = EditWin_export(edit_win, &null);
  long int const decomp_size = writer_destroy(&null);
  assert(decomp_size >= 0);
  assert(decomp_size <= INT32_MAX);

  if (success)
  {
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
      success = EditWin_export(edit_win, &gkwriter);
      if ((writer_destroy(&gkwriter) < 0) && success)
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
  IOActionData *const action_data = client_handle;
  assert(action_data != NULL);
  assert(action_data->action == IOActionCode_PasteClip ||
         action_data->action == IOActionCode_Import);
  NOT_USED(estimated_size);
  bool success = false;

  DEBUGF("Received %s data of type &%X\n", action_data->action ==
        IOActionCode_PasteClip ? "clipboard" : "dragged", file_type);

  /* Prevent cancellation by the import function */
  IOActionCode const action = action_data->action;
  action_data->action = IOActionCode_Done;

  switch (file_type)
  {
    case FileType_Data:
      /* Check that this data is in a known format
         (many different kinds of data may have file type 0xffd) */
      success = import_expcolfile(action_data->edit_win,
                   reader, action_data->coords,
                   action == IOActionCode_PasteClip,
                   filename);
      break;

    case FileType_CSV:
      success = import_csv(action_data->edit_win,
                   reader,
                   action_data->coords,
                   filename);
      break;

    case FileType_Fednet:
      success = import_colfile(action_data->edit_win,
                   reader,
                   action_data->coords,
                   filename);
      break;

    default:
      /* Cannot import data of this file type */
      RPT_ERR("BadFileType");
      break;
  }

  destroy_record(action_data);
  return success;
}

/* ----------------------------------------------------------------------- */

static void drag_or_paste_failed(const _kernel_oserror *const e,
  void *const client_handle)
{
  IOActionData *const action_data = client_handle;
  assert(action_data != NULL);
  assert(action_data->action == IOActionCode_PasteClip ||
         action_data->action == IOActionCode_Import);
  ON_ERR_RPT(e);
  destroy_record(action_data);
}

/* ----------------------------------------------------------------------- */

static int estimate_size(int const file_type, int const num_colours)
{
  int size = 0;
  switch (file_type)
  {
    case FileType_CSV:
    case FileType_Text:
      size = ExpColFile_estimate_CSV(num_colours);
      break;

    case FileType_Data:
      size = ExpColFile_estimate(num_colours);
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
  if (!cb_valid)
  {
    DEBUGF("Clipboard is empty\n");
    return 0;
  }
  return estimate_size(file_type, ExpColFile_get_size(&clipboard));
}

/* ----------------------------------------------------------------------- */

static bool cb_write(Writer *const writer, int const file_type,
  char const *const filename, void *const client_handle)
{
  /* This function is called to get the current clipboard contents, for example
     to paste them into a document. */
  NOT_USED(filename);
  NOT_USED(client_handle);

  if (!cb_valid)
  {
    DEBUGF("Clipboard is empty\n");
    return false;
  }

  switch (file_type)
  {
    case FileType_Data:
      ExpColFile_write(&clipboard, writer);
      break;

    case FileType_CSV:
    case FileType_Text:
      ExpColFile_write_CSV(&clipboard, writer);
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

  if (cb_valid)
  {
    cb_valid = false;
    DEBUGF("Freeing clipboard data\n");
    ExpColFile_destroy(&clipboard);
  }
}

/* ----------------------------------------------------------------------- */

static void relinquish_drag(void)
{
  DEBUGF("View %p relinquishing drag\n", (void *)drag_claim_view);
  assert(drag_claim_view != NULL);

  EditWin_set_hint(drag_claim_view, NULL_ComponentId);
  EditWin_stop_auto_scroll(drag_claim_view);
  drag_claim_view = NULL;
}

/* ----------------------------------------------------------------------- */

static bool recognise_drop(const WimpMessage *const message)
{
  /* Does this message claim to be a reply? */
  DEBUGF("Comparing message ref. %d with DragClaim\n", message->hdr.your_ref);

  if (message->hdr.your_ref)
  {
    /* Is it a reply to our last DragClaim message? */
    if (drag_claim_view == NULL || dragclaim_msg_ref != message->hdr.your_ref)
    {
      /* Unrecognised 'your ref.' (could be a message for the Entity2 module) */
      DEBUGF("Unrecognised reply\n");
      return false; /* unrecognised reply */
    }

    DEBUGF("It is a reply to our last DragClaim message\n");
    relinquish_drag();
  }
  return true; /* recognised reply, or original message */
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
        " (coordinates %d,%d)", dragging->icon_handle, dragging->window_handle,
        dragging->x, dragging->y);
  DEBUGF("Bounding box of data is %d,%d,%d,%d\n", dragging->bbox.xmin,
        dragging->bbox.ymin, dragging->bbox.xmax, dragging->bbox.ymax);

  assert(edit_win != NULL);

  IO_dragging_msg(dragging);

  /* Check whether the pointer is within our window (excluding borders) */
  if (!EditWin_owns_wimp_handle(edit_win, dragging->window_handle) ||
      dragging->icon_handle < WimpIcon_WorkArea)
  {
    return 0; /* No - do not claim message */
  }

  /* The sender can set a flag to prevent us from claiming the drag again
     (i.e. force us to relinquish it if we had claimed it) */
  if (TEST_BITS(dragging->flags, Wimp_MDragging_DoNotClaimMessage))
  {
    DEBUGF("Forbidden from claiming this drag\n");
    if (drag_claim_view == edit_win)
    {
      relinquish_drag();
    }
  }
  else if (common_file_type(import_file_types, dragging->file_types) !=
    FileType_Null)
  {
    DEBUGF("We can handle one of the file types offered\n");

    /* We need to update the hint text manually during a drag, because the
       Wimp treats the mouse pointer as having left all windows */
    ComponentId component_id;
    if (!E(window_wimp_to_toolbox(0, dragging->window_handle,
      dragging->icon_handle, NULL, &component_id)))
    {
      EditWin_set_hint(edit_win, component_id);
    }

    if (!claim_drag(message, import_file_types, &dragclaim_msg_ref))
    {
      if (drag_claim_view == edit_win)
        relinquish_drag();
    }
    else
    {
      DEBUGF("Drag claimed by view %p\n", (void *)drag_claim_view);

      /* Get the visible area of the main editing window */
      WimpGetWindowStateBlock window_state;
      window_state.window_handle = EditWin_get_wimp_handle(edit_win);
      if (!E(wimp_get_window_state(&window_state)))
      {
        if (drag_claim_view != edit_win)
        {
          EditWin_start_auto_scroll(edit_win,
                                       &window_state.visible_area,
                                       WimpAutoScrollDefaultPause,
                                       NULL);
        }
      }
      drag_claim_view = edit_win;
    }
  }
  else
  {
    DEBUGF("We don't like any of their export file types\n");
    if (drag_claim_view == edit_win)
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

  DEBUGF("View %p evaluating a DataSave message (ref. %d in reply to %d)\n",
        (void *)edit_win, message->hdr.my_ref, message->hdr.your_ref);

  if (!EditWin_owns_wimp_handle(edit_win,
                                message->data.data_save.destination_window))
  {
    DEBUGF("Destination is not in view %p\n", (void *)edit_win);
    return 0; /* message is not intended for this editing window */
  }

  if (!recognise_drop(message))
    return 0; /* do not claim message (it is an unrecognised reply) */

  if (!in_file_types(message->data.data_save.file_type, import_file_types))
  {
    RPT_ERR("BadFileType");
    return 1;
  }

  IOActionData *const action_data = create_record(IOActionCode_Import,
    edit_win);
  if (action_data != NULL)
  {
    /* Record the drop coordinates within the window's work area */
    action_data->coords.x = message->data.data_save.destination_x;
    action_data->coords.y = message->data.data_save.destination_y;

    scr_to_work_area_coords(EditWin_get_wimp_handle(edit_win),
                            &action_data->coords.x,
                            &action_data->coords.y);

    if (E(loader3_receive_data(message, drag_or_paste_read,
            drag_or_paste_failed, action_data)))
    {
      destroy_record(action_data);
    }
  }

  return 1; /* claim message */
}

/* ----------------------------------------------------------------------- */

static int datasave_fallback_handler(WimpMessage *const message, void *const handle)
{
  /* A fallback handler in case the window cited in the DataSave message does
     not belong to any of our views. In such cases, none will claim the
     message (leaving the drag claimant with auto-scrolling enabled). */

  NOT_USED(handle);
  DEBUGF("Fallback handler got a DataSave message (ref. %d in reply to %d)\n",
        message->hdr.my_ref, message->hdr.your_ref);

  /* Claim the message unless it is an unrecognised reply */
  return (recognise_drop(message) ? 1 : 0);
}

/* ----------------------------------------------------------------------- */

static int dataload_msg_handler(WimpMessage *const message, void *const handle)
{
  assert(message);
  assert(message->hdr.action_code == Wimp_MDataLoad);
  EditWin *const edit_win = handle;

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

  /* Insert the loaded data into our document
     (method used depends on the type of data) */
  bool success = false;

  IOActionData *const action_data = create_record(IOActionCode_Import,
    edit_win);
  if (action_data != NULL)
  {
    action_data->coords = (IOCoords){
      message->data.data_load.destination_x,
      message->data.data_load.destination_y};

    /* Make drop coordinates relative to window work area */
    scr_to_work_area_coords(EditWin_get_wimp_handle(edit_win),
                            &action_data->coords.x,
                            &action_data->coords.y);

    success = loader3_load_file(message->data.data_load.leaf_name,
      message->data.data_load.file_type,
      drag_or_paste_read, drag_or_paste_failed, action_data);
  }

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

/* ----------------------------------------------------------------------- */

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

#pragma no_check_stack
static void DAO_render(int const cptr, int const pptr, int const sptr, int const ncols)
{
  /* Function to render the selected colours for DragAnObject to use
     whilst updating the screen during a drag operation. Must not call shared C
     library functions that may require access to the library's static data
     (not even via assert or DEBUG macros). See DragAnObj.h for details. */
  const unsigned char *const colours = (const unsigned char *)cptr;
  const IOCoords *const pos = (const IOCoords *)pptr;
  const IOCoords *const size = (const IOCoords *)sptr;

  for (int index = 0; index < ncols; ++index)
  {
    if (_swix(ColourTrans_SetGCOL, _IN(0)|_INR(3,4),
              palette[colours[index]],
              ColourTrans_SetGCOL_UseECF,
              GCOLAction_OpaqueBG + GCOLAction_Overwrite) != NULL)
      return; /* error! */

    if (_swix(OS_Plot, _INR(0,2), PlotOp_SolidInclBoth + PlotOp_MoveAbs,
              pos[index].x, pos[index].y) != NULL)
      return; /* error! */

    if (_swix(OS_Plot, _INR(0,2), PlotOp_RectangleFill + PlotOp_PlotFGRel,
              size->x, size->y) != NULL)
      return; /* error! */
  }
}
#pragma -s

/* ----------------------------------------------------------------------- */

static const _kernel_oserror *drag_box(const DragBoxOp action,
  bool solid_drags, int const mouse_x, int const mouse_y,
  void *const client_handle)
{
  EditWin *const edit_win = client_handle;
  WimpDragBox drag_box;
  static bool using_dao = false;
  IOCoords const mouse_pos = {mouse_x, mouse_y};

  assert(edit_win != NULL);

  /* If the DragAnObject module is not available then revert to using
     a dashed outline to represent the dragged data */
  if (!draganobject)
    solid_drags = false;

  if (action != DragBoxOp_Cancel)
  {
    drag_box.dragging_box.xmin = selected_bbox.xmin - drag_pos.x + mouse_pos.x;
    drag_box.dragging_box.ymin = selected_bbox.ymin - drag_pos.y + mouse_pos.y;
    drag_box.dragging_box.xmax = selected_bbox.xmax - drag_pos.x + mouse_pos.x;
    drag_box.dragging_box.ymax = selected_bbox.ymax - drag_pos.y + mouse_pos.y;
  }

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
    if (solid_drags && action == DragBoxOp_Start)
    {
      size_t ncols = 0;

      unsigned char colours[EditWin_MaxSize];
      IOCoords pos[ARRAY_SIZE(colours)];
      IOCoords size;
      bool got_size = false;
      int const x_pix = 1 << x_eigen;
      int const y_pix = 1 << y_eigen;

      for (int index = EditWin_get_next_selected(edit_win, -1);
           index >= 0;
           index = EditWin_get_next_selected(edit_win, index))
      {
        BBox bbox;

        colours[ncols] = EditWin_get_colour(edit_win, index);

        EditWin_bbox_from_index(edit_win, index, &bbox);

        pos[ncols].x = bbox.xmin - selected_bbox.xmin;
        pos[ncols].y = bbox.ymin - selected_bbox.ymin;
        if (got_size)
        {
          assert(size.x == bbox.xmax - bbox.xmin - x_pix);
          assert(size.y == bbox.ymax - bbox.ymin - y_pix);
        }
        else
        {
          size.x = bbox.xmax - bbox.xmin - x_pix;
          size.y = bbox.ymax - bbox.ymin - y_pix;
          got_size = true;
        }

        ++ncols;
      }

      int const renderer_args[4] =
      {
        (int)colours, (int)pos, (int)&size, (int)ncols
      };
      ON_ERR_RTN_E(drag_an_object_start(
                         DragAnObject_BBoxPointer | DragAnObject_RenderAPCS,
                         (int)DAO_render,
                         renderer_args,
                         &drag_box.dragging_box,
                         NULL));
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

      drag_box.parent_box.xmin = - (mouse_pos.x - drag_box.dragging_box.xmin);
      drag_box.parent_box.ymin = - (mouse_pos.y - drag_box.dragging_box.ymin);
      drag_box.parent_box.xmax += drag_box.dragging_box.xmax - mouse_pos.x;
      drag_box.parent_box.ymax += drag_box.dragging_box.ymax - mouse_pos.y;
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

static void drop_handler_local(IOCoords mouse_pos,
  EditWin *const source_view, EditWin *const dest_view)
{
  /* Drag terminated in one of our editing windows, therefore we can
     bypass the remainder of the message protocol */
  DEBUGF("Drag destination is view %p\n", (void *)dest_view);

  /* FIXME: Do we really need to copy source data to a temporary buffer? */
  ExpColFile export_file;
  if (!EditWin_get_expcol(source_view, drag_pos.x, drag_pos.y, &export_file))
  {
    return;
  }

  /* Make drop coordinates relative to window work area */
  scr_to_work_area_coords(EditWin_get_wimp_handle(dest_view),
    &mouse_pos.x, &mouse_pos.y);

  /* Copy the selected colours to the drop location */
  EditWin_give_focus(dest_view);
  EditWin_set_expcol(dest_view, mouse_pos.x, mouse_pos.y, &export_file);

  ExpColFile_destroy(&export_file);
}

/* ----------------------------------------------------------------------- */

static bool sel_write(Writer *const writer, int const file_type,
  char const *const filename, void *const client_handle)
{
  /* This function is called to send the selected data when one of our drags
     terminates. We could predict the file type but don't bother. */
  NOT_USED(filename);

  IOActionData *const action_data = client_handle;
  assert(action_data != NULL);
  assert(action_data->action == IOActionCode_Export);

  ExpColFile export_file;
  if (!EditWin_get_expcol(action_data->edit_win,
    action_data->coords.x,
    action_data->coords.y, &export_file))
  {
    return false;
  }

  switch (file_type)
  {
    case FileType_Text:
    case FileType_CSV:
      ExpColFile_write_CSV(&export_file, writer);
      break;

    case FileType_Data:
      ExpColFile_write(&export_file, writer);
      break;

    default:
      assert("Bad file type" == NULL);
      break;
  }

  ExpColFile_destroy(&export_file);

  /* Caller checks the error indicator of the writer object */
  return true;
}

/* ----------------------------------------------------------------------- */

static void sel_saved(int const file_type, char const *const file_path,
  int const datasave_ref, void *const client_handle)
{
  IOActionData *const action_data = client_handle;

  assert(action_data != NULL);
  assert(action_data->action == IOActionCode_Export);
  NOT_USED(datasave_ref);
  NOT_USED(file_type);

  DEBUGF("Selection saved to %s with DataSave message %d\n",
        file_path != NULL ? file_path : "unsafe destination", datasave_ref);

  destroy_record(action_data);
}

/* ----------------------------------------------------------------------- */

static void sel_failed(const _kernel_oserror *const error,
  void *const client_handle)
{
  IOActionData *const action_data = client_handle;
  assert(action_data != NULL);
  assert(action_data->action == IOActionCode_Export);

  if (error != NULL)
  {
    err_report(error->errnum, msgs_lookup_subn("SaveFail", 1, error->errmess));
  }

  destroy_record(action_data);
}

/* ----------------------------------------------------------------------- */

static bool drop_handler_remote(int const window,
  int const icon, IOCoords const mouse_pos, int const file_type,
  int const claimant_task, int const claimant_ref,
  EditWin *const source_view)
{
  /* Drag terminated in one of our editing windows, therefore we can
     bypass the remainder of the message protocol */
  DEBUGF("Drag destination is remote\n");

  /* Allocate record for an I/O operation and link it into the list */
  IOActionData * const action_data = create_record(
    IOActionCode_Export, source_view);

  if (action_data == NULL)
  {
    return false;
  }

  action_data->coords = drag_pos;
  int const num_to_copy = EditWin_get_num_selected(source_view, NULL);

  WimpMessage msg = {
    .hdr.your_ref = claimant_ref,
    /* action code and message size are filled out automatically */
    .data.data_save = {
      .destination_window = window,
      .destination_icon = icon,
      .destination_x = mouse_pos.x,
      .destination_y = mouse_pos.y,
      .estimated_size = estimate_size(file_type, num_to_copy),
      .file_type = file_type,
    }
  };

  STRCPY_SAFE(msg.data.data_save.leaf_name, msgs_lookup("LeafName"));

  if (E(saver2_send_data(claimant_task, &msg, sel_write,
                         sel_saved, sel_failed, action_data)))
  {
    destroy_record(action_data);
    return false;
  }

  return true;
}

/* ----------------------------------------------------------------------- */

static bool drop_handler(bool const shift_held, int const window, int const icon,
  int const mouse_x, int const mouse_y, int const file_type, int const claimant_task,
  int const claimant_ref, void *const client_handle)
{
  /* This function is called when a drag has terminated */
  NOT_USED(shift_held);
  EditWin *const source_view = client_handle, *dest_view;
  bool saved = true;
  IOCoords const mouse_pos = {mouse_x, mouse_y};

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
    drop_handler_local(mouse_pos, source_view, dest_view);

    /* It's more robust to stop the drag now instead of returning false
       and waiting for a final Dragging message. */
    if (drag_claim_view == dest_view)
    {
      relinquish_drag();
    }
  }
  else
  {
    saved = drop_handler_remote(window, icon, mouse_pos,
                                file_type, claimant_task, claimant_ref,
                                source_view);
  }

  return saved;
}

/* ----------------------------------------------------------------------- */

static bool load_colmap(Reader *const reader, char const *const path,
  const bool is_safe)
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

  if (!ColMapFile_create(&gkreader, path, is_safe, false /* unused */))
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
  ColMapFile *const file = ColMapFile_create(NULL, NULL, false, false);
  if (file)
  {
    EditWin *const edit_win = ColMapFile_get_win(file);
    IOCoords origin;
    EditWin_coords_from_index(edit_win, 0, &origin.x, &origin.y);
    success = import_csv(edit_win, reader, origin, filename);
    if (!success)
    {
      ColMapFile_destroy(file);
    }
  }

  return success;
}

/* ----------------------------------------------------------------------- */

static bool cancel_cb(LinkedList *const list, LinkedListItem *const item, void *const arg)
{
  IOActionData * const action_data = (IOActionData *)item;

  assert(list == &action_data_list);
  NOT_USED(list);
  assert(action_data != NULL);
  assert(arg != NULL);

  if (action_data->edit_win == arg)
  {
    DEBUGF("This record belongs to the dying view %p\n", arg);

    switch (action_data->action)
    {
      case IOActionCode_PasteClip:
        DEBUGF("Cancelling clipboard paste\n");
        /* Beware, a callback will invalidate our 'action_data' pointer */
        entity2_cancel_requests(action_data);
        break;

      case IOActionCode_Import:
        DEBUGF("Cancelling drag import\n");
        /* Beware, a callback will invalidate our 'action_data' pointer */
        loader3_cancel_receives(action_data);
        break;

      case IOActionCode_Export:
        DEBUGF("Cancelling drag export\n");
        saver2_cancel_sends(action_data);
        break;

      case IOActionCode_Done:
        /* Termination in progress: nothing to do */
        break;

      default:
        assert("Bad I/O action" == NULL);
        break;
    }
  }

  return false; /* continue iteration */
}

/* ----------------------------------------------------------------------- */

static void load_fail(CONST _kernel_oserror *const error,
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
  case FileType_Fednet:
    success = load_colmap(reader, filename, is_safe);
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

/* ----------------------------------------------------------------------- */

static void init_data_request(EditWin const *const edit_win,
  WimpDataRequestMessage *const data_request)
{
  assert(edit_win != NULL);
  assert(data_request);

  *data_request = (WimpDataRequestMessage)
  {
    EditWin_get_wimp_handle(edit_win), WimpIcon_WorkArea, 0, 0,
    Wimp_MDataRequest_Clipboard,
    { FileType_Data, FileType_CSV, FileType_Null }
  };
}


/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

bool IO_report_read(ColMap const *const colmap, ColMapState state)
{
  if (state == ColMapState_OK)
  {
    int const size = colmap_get_size(colmap);
    if (size != sizeof(SFObjectColours) && size != sizeof(SFHillColours))
    {
      state = ColMapState_BadLen;
    }
  }

  switch (state)
  {
    case ColMapState_ReadFail:
      state = ColMapState_OK; /* caller should check for reader error */
      break;
    case ColMapState_BadLen:
      WARN("NotColours");
      break;
    default:
      assert(state == ColMapState_OK);
      break;
  }

  return state == ColMapState_OK;
}

/* ----------------------------------------------------------------------- */

bool IO_read_colmap(ColMap *const colmap, Reader *const reader)
{
  assert(!reader_ferror(reader));
  assert(colmap != NULL);

  hourglass_on();
  ColMapState state = colmap_read_file(colmap, reader);
  hourglass_off();

  return IO_report_read(colmap, state);
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

void IO_initialise(void)
{
  /* Register a fallback handler for DataSave messages
     (should be called last, since it is registered first) */
  EF(event_register_message_handler(Wimp_MDataSave,
                                    datasave_fallback_handler,
                                    NULL));
  linkedlist_init(&action_data_list);


  /* Check for DragAnObject module */
  EF(_kernel_setenv(APP_NAME"$DAO","1"));
  if (_kernel_oscli("RMEnsure DragAnObject 0 Set "APP_NAME"$DAO 0") ==
      _kernel_ERROR)
    err_check_fatal_rep(_kernel_last_oserror());

  char readvar_buffer[MaxDAOVarValueLen + 1];
  EF(_kernel_getenv(APP_NAME"$DAO", readvar_buffer, sizeof(readvar_buffer)));
  draganobject = (strcmp(readvar_buffer, "1") == 0);

  /* Older versions of the C library have a bug where NULL cannot be
     passed to _kernel_setenv to delete a variable */
  _kernel_swi_regs regs = {
    .r = {
      (int)APP_NAME"$DAO",
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
    ON_ERR_RPT(loader3_receive_data(message, read_file, load_fail, NULL));
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
  char *canonical_path = NULL;

  /* Check whether this file type is supported */
  if (!in_file_types(file_type, import_file_types))
  {
    RPT_ERR("BadFileType");
  }
  else if (!E(canonicalise(&canonical_path, NULL, NULL, load_path)))
  {
    /* Check for whether this file is already being edited */
    ColMapFile *const file = ColMapFile_find_by_file_name(canonical_path);
    if (file == NULL)
    {
      bool is_safe = true;
      (void)loader3_load_file(canonical_path, file_type,
                              read_file, load_fail, &is_safe);
    }
    else
    {
      /* Reopen existing editing window at top of stack */
      DEBUGF("This file is already being edited (%p)\n", (void *)file);
      ColMapFile_show(file);
    }
    free(canonical_path);
  }
}

/* ----------------------------------------------------------------------- */

bool IO_view_created(EditWin *const edit_win)
{
  /* Register handler for Wimp caret events */
  bool success = true;
  size_t i;

  assert(edit_win != NULL);

  /* Register handlers for Wimp messages (easier to register for each window
     rather than searching the user data list for the relevant view) */
  for (i = 0; success && i < ARRAY_SIZE(message_handlers); i++)
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
  entity2_cancel_requests(edit_win);
  (void)linkedlist_for_each(&action_data_list, cancel_cb, edit_win);
}

/* ----------------------------------------------------------------------- */

void IO_view_deleted(EditWin *const edit_win)
{
  IO_cancel(edit_win);

  /* Deregister handlers for Wimp messages */
  for (size_t i = 0; i < ARRAY_SIZE(message_handlers); i++)
  {
    ON_ERR_RPT(event_deregister_message_handler(message_handlers[i].msg_no,
                                       message_handlers[i].handler, edit_win));

  }
}

/* ----------------------------------------------------------------------- */

bool IO_start_drag(EditWin *const edit_win, IOCoords const pos,
  const BBox *const bbox)
{
  assert(edit_win != NULL);
  assert(bbox != NULL);
  assert(bbox->xmin < bbox->xmax);
  assert(bbox->ymin < bbox->ymax);

  selected_bbox = *bbox;

  /* Record start of drag (relative to source window's work area) */
  drag_pos = pos;

  ON_ERR_RPT(drag_abort());

  return !E(drag_start(export_file_types, NULL, drag_box, drop_handler, edit_win));
}

/* ----------------------------------------------------------------------- */

void IO_paste(EditWin *const edit_win)
{
  /* Record target coordinates for paste
     (centre of first selected logical colour) */
  IOActionData *const action_data = create_record(
    IOActionCode_PasteClip, edit_win);

  if (action_data != NULL)
  {
    if (centre_of_first_sel(edit_win, &action_data->coords))
    {
      WimpDataRequestMessage data_request;
      init_data_request(edit_win, &data_request);
      if (!E(entity2_request_data(&data_request, drag_or_paste_read,
                                  drag_or_paste_failed, action_data)))
      {
        return;
      }
    }

    destroy_record(action_data);
  }
}

/* ----------------------------------------------------------------------- */

bool IO_copy(EditWin *const edit_win)
{
  /* Copy the selected colours to the global clipboard */
  assert(edit_win != NULL);

  IOCoords origin;
  if (!centre_of_first_sel(edit_win, &origin))
  {
    return false; /* nothing selected */
  }

  /* Claim the global clipboard
     (a side-effect is to free any clipboard data held by us) */
  if (E(entity2_claim(Wimp_MClaimEntity_Clipboard, export_file_types,
         estimate_cb, cb_write, cb_lost, NULL)))
  {
    return false;
  }

  assert(!cb_valid);

  if (!EditWin_get_expcol(edit_win, origin.x, origin.y, &clipboard))
  {
    return false;
  }

  cb_valid = true;
  return true;
}

/* ----------------------------------------------------------------------- */

void IO_dragging_msg(const WimpDraggingMessage *const dragging)
{
  /* If this Dragging message is not for the window that previously claimed
     the drag then stop auto-scrolling. */
  if (drag_claim_view != NULL &&
      (!EditWin_owns_wimp_handle(drag_claim_view, dragging->window_handle) ||
       dragging->icon_handle < WimpIcon_WorkArea))
  {
    relinquish_drag();
  }
}

/* ----------------------------------------------------------------------- */

bool IO_export_colmap_file(EditWin *const edit_win, char const * const path)
{
  assert(edit_win != NULL);
  assert(path != NULL);

  bool success = false;
  FILE *const f = fopen_inc(path, "wb");
  if (!f)
  {
    err_report(DUMMY_ERRNO, msgs_lookup_subn("OpenOutFail", 1, path));
  }
  else
  {
    Writer raw;
    writer_raw_init(&raw, f);
    success = export_colmap(edit_win, path, &raw);
    long int const comp_size = writer_destroy(&raw);
    int const err = fclose_dec(f);

    if ((err || comp_size < 0) && success)
    {
      write_fail(path);
      success = false;
    }

    if (success)
    {
      success = !E(os_file_set_type(path, FileType_Fednet));
    }

    if (!success)
    {
      remove(path);
    }
  }
  return success;
}

/* ----------------------------------------------------------------------- */

int IO_estimate_colmap(EditWin *const edit_win)
{
  assert(edit_win != NULL);

  /* Experimentally compress the colour map, to find out the file size */
  Writer gkcounter;
  long int out_size;
  if (!writer_gkc_init(&gkcounter, FednetHistoryLog2, &out_size))
  {
    RPT_ERR("NoMem");
    out_size = 0;
  }
  else
  {
    hourglass_on();
    bool success = EditWin_export(edit_win, &gkcounter);
    hourglass_off();
    /* writer_destroy returns the uncompressed size, not the compressed size */
    if (writer_destroy(&gkcounter) < 0 && success)
    {
      success = false;
    }
    if (!success)
    {
      out_size = 0;
    }
  }
  assert(out_size >= 0);
  assert(out_size <= INT_MAX);
  return (int)out_size;
}
