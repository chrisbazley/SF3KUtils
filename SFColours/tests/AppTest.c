/*
 *  SFColours test: Top level
 *  Copyright (C) 2020 Christopher Bazley
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
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <setjmp.h>

/* RISC OS library files */
#include "swis.h"
#include "event.h"
#include "toolbox.h"
#include "saveas.h"
#include "gadgets.h"
#include "dcs.h"
#include "quit.h"
#include "iconbar.h"
#include "wimp.h"

/* My library files */
#include "Macros.h"
#include "Debug.h"
#include "Err.h"
#include "userdata.h"
#include "gkeycomp.h"
#include "gkeydecomp.h"
#include "SFFormats.h"
#include "OSFile.h"
#include "PseudoWimp.h"
#include "PseudoTbox.h"
#include "PseudoEvnt.h"
#include "PseudoExit.h"
#include "FOpenCount.h"
#include "ViewsMenu.h"
#include "msgtrans.h"
#include "FileRWInt.h"
#include "WimpExtra.h"
#include "Pal256.h"
#include "SprFormats.h"

/* Local header files */
#include "Tests.h"
#include "SFCInit.h"
#include "Picker.h"
#include "OurEvents.h"
#include "Utils.h"

#include "Fortify.h"

#define TEST_DATA_DIR "<Wimp$ScrapDir>.SFColoursTests"
#define TEST_DATA_IN TEST_DATA_DIR ".in"
#define TEST_DATA_OUT TEST_DATA_DIR ".out"
#define TEST_LEAFNAME "FatChance"

#define assert_no_error(x) \
do { \
  const _kernel_oserror * const e = x; \
  if (e != NULL) \
  { \
    DEBUGF("Error: 0x%x,%s %s:%d\n", e->errnum, e->errmess, __FILE__, __LINE__); \
    abort(); \
  } \
} \
while(0)

enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
  FortifyAllocationLimit = 2048,
  TestDataSize = 12,
  CompressionBufferSize = 5,
  DestinationX = 900,
  DestinationY = 34,
  Timeout = 30 * CLOCKS_PER_SEC,
  DragMsgInterval = CLOCKS_PER_SEC / 4,
  OS_FSControl_Copy = 26,
  OS_FSControl_Wipe = 27,
  OS_FSControl_Flag_Recurse = 1,
  DraggingBBoxMin = -72000,
  DraggingBBoxMax = 72000,
  MaxNumWindows = 3,
  SelectionStart = 5,
  SelectionEnd = 17,
  DropPosition = 21, /* Take care when changing this because colours
                        retain their position relative to the pointer */
  NonSelectionColour = 5,
  SelectionColour = 64,
  WorkArea = -1, /* Pseudo icon handle (window's work area) */
  DirViewerHandle = 24345, /* window handle of directory viewer for DataOpen message */
  ForeignTaskHandle = 999,
  UDBSize = 34,
  UnsafeDataSize = -1,
  FSControl_CanonicalisePath = 37,
  Iconized = -3,
  FirstComponentId = 0x44,
  MaxSelectionWidth = 460,
  MaxSelectionHeight = 532,
  Magic = 0x3f,
};

typedef enum
{
  DTM_RAM,     /* Receiver sends RAM fetch and falls back to data save ack if ignored;
                  sender replies to either RAM fetch or data save ack */
  DTM_File,    /* Receiver sends data save ack; sender ignores (first) RAM fetch */
  DTM_BadRAM,  /* Receiver ignores RAM transmit; sender ignores (2nd or subsequent) RAM fetch */
  DTM_BadFile, /* Receiver ignores data load; sender doesn't send data load */
  DTM_None     /* Receiver ignores data save; sender doesn't send data save */
}
DataTransferMethod;

static int th;

static void wipe(char const *path_name)
{
  _kernel_swi_regs regs;

  assert(path_name != NULL);

  regs.r[0] = OS_FSControl_Wipe;
  regs.r[1] = (int)path_name;
  regs.r[3] = OS_FSControl_Flag_Recurse;
  _kernel_swi(OS_FSControl, &regs, &regs);
}

static void copy(char const *src, char const *dst)
{
  _kernel_swi_regs regs;

  assert(src != NULL);
  assert(dst != NULL);

  regs.r[0] = OS_FSControl_Copy;
  regs.r[1] = (int)src;
  regs.r[2] = (int)dst;
  regs.r[3] = OS_FSControl_Flag_Recurse;
  assert_no_error(_kernel_swi(OS_FSControl, &regs, &regs));
}

static int make_comp_file(char const *file_name, const void *in_buffer, size_t in_size)
{
  FILE *f;
  size_t n;
  char out_buffer[CompressionBufferSize];
  GKeyComp *comp;
  GKeyParameters params;
  int estimated_size = sizeof(int32_t);
  bool ok;
  GKeyStatus status;

  f = fopen(file_name, "wb");
  assert(f != NULL);

  ok = fwrite_int32le(in_size, f);
  assert(ok);

  comp = gkeycomp_make(FednetHistoryLog2);
  assert(comp != NULL);

  params.in_buffer = in_buffer;
  params.in_size = in_size;
  params.out_buffer = out_buffer;
  params.out_size = sizeof(out_buffer);
  params.prog_cb = NULL;
  params.cb_arg = NULL;

  do
  {
    /* Compress the data from the input buffer to the output buffer */
    status = gkeycomp_compress(comp, &params);

    /* Is the output buffer full or have we finished? */
    if (status == GKeyStatus_Finished ||
        status == GKeyStatus_BufferOverflow ||
        params.out_size == 0)
    {
      /* Empty the output buffer by writing to file */
      const size_t to_write = sizeof(out_buffer) - params.out_size;
      n = fwrite(out_buffer, to_write, 1, f);
      assert(n == 1);
      estimated_size += to_write;

      params.out_buffer = out_buffer;
      params.out_size = sizeof(out_buffer);

      if (status == GKeyStatus_BufferOverflow)
        status = GKeyStatus_OK; /* Buffer overflow has been fixed up */
    }
  }
  while (status == GKeyStatus_OK);

  assert(status == GKeyStatus_Finished);
  gkeycomp_destroy(comp);

  fclose(f);
  assert_no_error(os_file_set_type(file_name, FileType_Fednet));

  return estimated_size;
}

static int make_hill_cols_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index))
{
  SFHillColours cols;

  for (size_t i = 0; i < ARRAY_SIZE(cols); ++i)
    cols[i] = compute_colour(i);

  return make_comp_file(file_name, &cols, sizeof(cols));
}

static int make_object_cols_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index))
{
  SFObjectColours cols;

  for (size_t i = 0; i < ARRAY_SIZE(cols.colour_mappings); ++i)
    cols.colour_mappings[i] = i < ARRAY_SIZE(cols.areas.static_colours) ? i :
                              compute_colour(i - ARRAY_SIZE(cols.areas.static_colours));

  return make_comp_file(file_name, &cols, sizeof(cols));
}

static void assert_file_has_type(char const *file_name, int file_type)
{
  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(file_name, &cat));
  assert(cat.object_type == ObjectType_File);
  DEBUGF("Load address: 0x%x\n", cat.load);
  assert(((cat.load >> 8) & 0xfff) == file_type);
}

static void load_comp_file(char const *file_name, void *out_buffer, size_t out_size)
{
  char in_buffer[CompressionBufferSize];
  GKeyDecomp     *decomp;
  GKeyParameters params;
  long int len;
  bool ok, in_pending = false;
  GKeyStatus status;

  FILE * const f = fopen(file_name, "rb");
  assert(f != NULL);

  ok = fread_int32le(&len, f);
  assert(ok);
  assert(len >= 0);
  assert((size_t)len == out_size);

  decomp = gkeydecomp_make(FednetHistoryLog2);
  assert(decomp != NULL);

  params.in_buffer = in_buffer;
  params.in_size = 0;
  params.out_buffer = out_buffer;
  params.out_size = out_size;
  params.prog_cb = NULL;
  params.cb_arg = NULL;

  do
  {
    /* Is the input buffer empty? */
    if (params.in_size == 0)
    {
      /* Fill the input buffer by reading from file */
      params.in_buffer = in_buffer;
      params.in_size = fread(in_buffer, 1, sizeof(in_buffer), f);
      assert(!ferror(f));
    }

    /* Decompress the data from the input buffer to the output buffer */
    status = gkeydecomp_decompress(decomp, &params);

    /* If the input buffer is empty and it cannot be (re-)filled then
       there is no more input pending. */
    in_pending = params.in_size > 0 || !feof(f);

    if (in_pending && status == GKeyStatus_TruncatedInput)
    {
      /* False alarm before end of input data */
      status = GKeyStatus_OK;
    }
    assert(status == GKeyStatus_OK);
  }
  while (in_pending);

  gkeydecomp_destroy(decomp);

  fclose(f);
}

static void check_hill_cols_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index))
{
  SFHillColours out_buffer;

  load_comp_file(file_name, &out_buffer, sizeof(out_buffer));

  for (size_t i = 0; i < ARRAY_SIZE(out_buffer); ++i)
  {
    unsigned int const colour = compute_colour(i);
    if (out_buffer[i] != colour)
    {
      DEBUGF("Got %u at [%zu], expected %u\n",
             out_buffer[i], i, colour);
      abort();
    }
  }
}

static void check_object_cols_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index))
{
  SFObjectColours out_buffer;

  load_comp_file(file_name, &out_buffer, sizeof(out_buffer));

  for (size_t i = 0; i < ARRAY_SIZE(out_buffer.colour_mappings); ++i)
  {
    unsigned int const colour = i < ARRAY_SIZE(out_buffer.areas.static_colours) ? i:
                                compute_colour(i - ARRAY_SIZE(out_buffer.areas.static_colours));
    if (out_buffer.colour_mappings[i] != colour)
    {
      DEBUGF("Got %u at [%zu], expected %u\n",
             out_buffer.colour_mappings[i], i, colour);
      abort();
    }
  }
}

static unsigned int colour_black(unsigned int index)
{
  NOT_USED(index);
  return 0;
}

static unsigned int colour_identity(unsigned int index)
{
  return index ^ Magic;
}

static unsigned int colour_dropped_csv_on_sel(unsigned int index)
{
  return ((index >= SelectionStart) && (index < SelectionStart + TestDataSize)) ?
    colour_identity(index - SelectionStart) : 0;
}

static unsigned int colour_dropped_cols(unsigned int index)
{
  return (index >= DropPosition) ? colour_identity(index - DropPosition) : 0;
}

static unsigned int colour_dropped_csv(unsigned int index)
{
  return ((index >= DropPosition) && (index < DropPosition + TestDataSize)) ?
    colour_identity(index - DropPosition) : 0;
}

static unsigned int colour_csv(unsigned int index)
{
  return (index < TestDataSize) ? colour_identity(index) : 0;
}

static unsigned int colour_selection(unsigned int index)
{
  return (index < (SelectionEnd - SelectionStart)) ? SelectionColour : 0;
}

static unsigned int colour_edited(unsigned int index)
{
  return ((index >= SelectionStart) && (index < SelectionEnd)) ?
    SelectionColour : NonSelectionColour;
}

static unsigned int colour_edited_dragged(unsigned int index)
{
  return (((index >= SelectionStart) && (index < SelectionEnd)) ||
          ((index >= DropPosition) &&
           (index < SelectionEnd - SelectionStart + DropPosition))) ?
         SelectionColour : NonSelectionColour;
}

static int make_csv_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index))
{
  size_t total = 0;
  FILE * const f = fopen(file_name, "wb");
  assert(f != NULL);

  for (unsigned int i = 0; i < TestDataSize; ++i)
  {
    size_t n = fprintf(f, "%u%s", compute_colour(i), i == (TestDataSize - 1) ? "\n" : ",");
    assert(n >= 1);
    total += n;
  }

  fclose(f);
  assert_no_error(os_file_set_type(file_name, FileType_CSV));

  return total;
}

static int estimate_csv_size(unsigned int (*compute_colour)(unsigned int index), unsigned int ncols)
{
  NOT_USED(compute_colour);
  return ncols * 4;
}

static void check_data_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index), unsigned int ncols)
{
  FILE * const f = fopen(file_name, "r");
  assert(f != NULL);
  BBox coverage = {INT_MAX,INT_MAX,INT_MIN,INT_MIN};

  ExportColoursFile header;

  size_t n = fread(&header, sizeof(header), 1, f);
  assert(n == 1);
  assert(header.tag == ExportColoursFile_Tag);
  assert(header.version == ExportColoursFile_Version);
  assert(header.num_cols == ncols);
  for (uint32_t i = 0; i < header.num_cols ; ++i)
  {
    ExportColoursFileRecord body;
    n = fread(&body, sizeof(body), 1, f);
    assert(n == 1);
    assert(compute_colour(i) == body.colour);
    coverage.xmin = LOWEST(coverage.xmin, body.x_offset);
    coverage.ymin = LOWEST(coverage.ymin, body.y_offset);
    coverage.xmax = HIGHEST(coverage.xmax, body.x_offset);
    coverage.ymax = HIGHEST(coverage.ymax, body.y_offset);
  }
  assert(!fread(&header, 1, 1, f));
  assert(feof(f));
  fclose(f);

  assert(coverage.xmax - coverage.xmin <= MaxSelectionWidth);
  assert(coverage.ymax - coverage.ymin <= MaxSelectionHeight);
}

static void check_csv_file(char const *file_name, unsigned int (*compute_colour)(unsigned int index), unsigned int ncols)
{
  FILE * const f = fopen(file_name, "r");
  assert(f != NULL);

  unsigned int i = 0;
  do
  {
    unsigned int colour = 0;
    char sep = 0;
    int n = fscanf(f, "%u%c", &colour, &sep);
    DEBUGF("%u: Read %d items\n", i, n);
    if (n > 0)
    {
      assert(i < ncols);
      assert(compute_colour(i) == colour);
      if (n > 1)
      {
        assert(n == 2);
        if (i == ncols - 1)
          assert(sep == '\n');
        else
          assert(sep == ',');
      }
      ++i;
    }
  }
  while (!feof(f));

  assert(i == ncols);

  fclose(f);
}

static void check_out_file(int file_type, unsigned int (*compute_colour)(unsigned int index), unsigned int ncols)
{
  switch(file_type)
  {
    case FileType_CSV:
    case FileType_Text:
      check_csv_file(TEST_DATA_OUT, compute_colour, ncols);
      break;

    default:
      assert(file_type == FileType_Data);
      check_data_file(TEST_DATA_OUT, compute_colour, ncols);
      break;
  }
}

static int estimate_data_size(unsigned int ncols)
{
  return (ncols * sizeof(ExportColoursFileRecord)) + sizeof(ExportColoursFile);
}

static int estimate_file_size(int file_type, unsigned int (*compute_colour)(unsigned int index), unsigned int ncols)
{
  int estimated_size;

  switch(file_type)
  {
    case FileType_CSV:
    case FileType_Text:
      estimated_size = estimate_csv_size(compute_colour, ncols);
      break;

    default:
      assert(file_type == FileType_Data);
      estimated_size = estimate_data_size(ncols);
      break;
  }
  return estimated_size;
}

static void init_id_block(IdBlock *block, ObjectId id, ComponentId component)
{
  assert(block != NULL);

  block->self_id = id;
  block->self_component = component;
#undef toolbox_get_parent
#undef toolbox_get_ancestor
  if (id == NULL_ObjectId)
  {
    block->parent_id = block->ancestor_id = NULL_ObjectId;
    block->parent_component = block->ancestor_component = NULL_ComponentId;
  }
  else
  {
    assert_no_error(toolbox_get_parent(0, id, &block->parent_id, &block->parent_component));
    assert_no_error(toolbox_get_ancestor(0, id, &block->ancestor_id, &block->ancestor_component));
  }
}

static bool path_is_in_userdata(char *filename)
{
  UserData *window;
  char buffer[1024];
  _kernel_swi_regs regs;

  regs.r[0] = FSControl_CanonicalisePath;
  regs.r[1] = (int)filename;
  regs.r[2] = (int)buffer;
  regs.r[3] = 0;
  regs.r[4] = 0;
  regs.r[5] = sizeof(buffer);
  assert_no_error(_kernel_swi(OS_FSControl, &regs, &regs));
  assert(regs.r[5] >= 0);

  window = userdata_find_by_file_name(buffer);
  return window != NULL;
}

static bool object_is_on_menu(ObjectId id)
{
  ObjectId it;
  assert(id != NULL_ObjectId);
  for (it = ViewsMenu_getfirst();
       it != NULL_ObjectId;
       it = ViewsMenu_getnext(it))
  {
    if (it == id)
      break;
  }
  return it == id;
}

static int fake_ref;

static void init_savetofile_event(WimpPollBlock *poll_block, unsigned int flags)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)&poll_block->words;

  sastfe->hdr.size = sizeof(*poll_block);
  sastfe->hdr.reference_number = ++fake_ref;
  sastfe->hdr.event_code = SaveAs_SaveToFile;
  sastfe->hdr.flags = flags;
  STRCPY_SAFE(sastfe->filename, TEST_DATA_OUT);
}

static void init_fillbuffer_event(WimpPollBlock *poll_block, unsigned int flags, int size, char *address, int no_bytes)
{
  SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)&poll_block->words;

  safbe->hdr.size = sizeof(*poll_block);
  safbe->hdr.reference_number = ++fake_ref;
  safbe->hdr.event_code = SaveAs_FillBuffer;
  safbe->hdr.flags = flags;
  safbe->size = size;
  safbe->address = address;
  safbe->no_bytes = no_bytes;
}

static void init_savecompleted_event(WimpPollBlock *poll_block, unsigned int flags)
{
  SaveAsSaveCompletedEvent * const sasce = (SaveAsSaveCompletedEvent *)&poll_block->words;

  sasce->hdr.size = sizeof(*poll_block);
  sasce->hdr.reference_number = ++fake_ref;
  sasce->hdr.event_code = SaveAs_SaveCompleted;
  sasce->hdr.flags = flags;
  sasce->wimp_message_no = 0; /* as though no drag took place */
  STRCPY_SAFE(sasce->filename, TEST_DATA_OUT);
}

static void init_dcs_discard_event(WimpPollBlock *poll_block)
{
  DCSDiscardEvent * const dcsde = (DCSDiscardEvent *)&poll_block->words;

  dcsde->hdr.size = sizeof(*poll_block);
  dcsde->hdr.reference_number = ++fake_ref;
  dcsde->hdr.event_code = DCS_Discard;
  dcsde->hdr.flags = 0;
}

static void init_dcs_save_event(WimpPollBlock *poll_block)
{
  DCSDiscardEvent * const dcsde = (DCSDiscardEvent *)&poll_block->words;

  dcsde->hdr.size = sizeof(*poll_block);
  dcsde->hdr.reference_number = ++fake_ref;
  dcsde->hdr.event_code = DCS_Save;
  dcsde->hdr.flags = 0;
}

static void init_dcs_cancel_event(WimpPollBlock *poll_block)
{
  DCSCancelEvent * const dcsce = (DCSCancelEvent *)&poll_block->words;

  dcsce->hdr.size = sizeof(*poll_block);
  dcsce->hdr.reference_number = ++fake_ref;
  dcsce->hdr.event_code = DCS_Cancel;
  dcsce->hdr.flags = 0;
}

static void init_quit_cancel_event(WimpPollBlock *poll_block)
{
  QuitCancelEvent * const qce = (QuitCancelEvent *)&poll_block->words;

  qce->hdr.size = sizeof(*poll_block);
  qce->hdr.reference_number = ++fake_ref;
  qce->hdr.event_code = Quit_Cancel;
  qce->hdr.flags = 0;
}

static void init_quit_quit_event(WimpPollBlock *poll_block)
{
  QuitQuitEvent * const qce = (QuitQuitEvent *)&poll_block->words;

  qce->hdr.size = sizeof(*poll_block);
  qce->hdr.reference_number = ++fake_ref;
  qce->hdr.event_code = Quit_Quit;
  qce->hdr.flags = 0;
}

static void init_custom_event(WimpPollBlock *poll_block, int event_code)
{
  ToolboxEvent * const ice = (ToolboxEvent *)&poll_block->words;

  ice->hdr.size = sizeof(*poll_block);
  ice->hdr.reference_number = ++fake_ref;
  ice->hdr.event_code = event_code;
  ice->hdr.flags = 0;
}

static void init_pal256_event(WimpPollBlock *poll_block, unsigned int colour_number)
{
  Pal256ColourSelectedEvent * const pcse = (Pal256ColourSelectedEvent *)&poll_block->words;
  pcse->hdr.size = sizeof(*poll_block);
  pcse->hdr.reference_number = ++fake_ref;
  pcse->hdr.event_code = Pal256_ColourSelected;
  pcse->hdr.flags = 0;
  pcse->colour_number = colour_number;
}

static int get_wa_origin(ObjectId id, int *x, int *y)
{
  WimpGetWindowStateBlock state;
#undef window_get_wimp_handle
#undef wimp_get_window_state
  assert_no_error(window_get_wimp_handle(0, id, &state.window_handle));
  assert_no_error(wimp_get_window_state(&state));
  if (x != NULL)
    *x = state.visible_area.xmin + state.xscroll;
  if (y != NULL)
    *y = state.visible_area.ymax - state.yscroll;
  return state.window_handle;
}

static void init_mouseclick_event(WimpPollBlock *poll_block, ObjectId id, unsigned int pos, int buttons)
{
  WimpMouseClickEvent * const wmce = &poll_block->mouse_click;
  wmce->window_handle = get_wa_origin(id, &wmce->mouse_x, &wmce->mouse_y);
  BBox bbox;
#undef gadget_get_bbox
  assert_no_error(gadget_get_bbox(0, id, FirstComponentId+pos, &bbox));
  wmce->mouse_x += (bbox.xmin + bbox.xmax) / 2;
  wmce->mouse_y += (bbox.ymin + bbox.ymax) / 2;
  wmce->buttons = buttons;
  wmce->icon_handle = WorkArea;
}

static void init_pointer_info_for_win(WimpGetPointerInfoBlock *pointer_info, ObjectId id, unsigned int pos, int buttons)
{
  pointer_info->window_handle = get_wa_origin(id, &pointer_info->x, &pointer_info->y);
  pointer_info->icon_handle = WorkArea;
  BBox bbox;
  assert_no_error(gadget_get_bbox(0, id, FirstComponentId+pos, &bbox));
  pointer_info->x += (bbox.xmin + bbox.xmax) / 2;
  pointer_info->y += (bbox.ymin + bbox.ymax) / 2;
  pointer_info->button_state = buttons;
}

static void init_pointer_info_for_icon(WimpGetPointerInfoBlock *pointer_info)
{
  pointer_info->x = DestinationX;
  pointer_info->y = DestinationY;
  pointer_info->button_state = 0;
  pointer_info->window_handle = WimpWindow_Iconbar;
  assert_no_error(iconbar_get_icon_handle(0, pseudo_toolbox_find_by_template_name("Iconbar"), &pointer_info->icon_handle));
}

static void init_pointer_info_for_foreign(WimpGetPointerInfoBlock *pointer_info)
{
  pointer_info->x = DestinationX;
  pointer_info->y = DestinationY;
  pointer_info->button_state = 0;
  pointer_info->window_handle = DirViewerHandle;
  pointer_info->icon_handle = 0;
}

static void init_userdrag_event(WimpPollBlock *poll_block, int x, int y)
{
  poll_block->user_drag_box.bbox.xmin = x - UDBSize;
  poll_block->user_drag_box.bbox.xmax = x + UDBSize;
  poll_block->user_drag_box.bbox.ymin = y - UDBSize;
  poll_block->user_drag_box.bbox.ymax = y + UDBSize;
}

static void init_close_window_event(WimpPollBlock *poll_block, ObjectId id)
{
  assert_no_error(window_get_wimp_handle(0, id, &poll_block->close_window_request.window_handle));
}

static int init_ram_fetch_msg(WimpPollBlock *poll_block, char *buffer, int buffer_size, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpRAMFetchMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MRAMFetch;

  poll_block->user_message.data.ram_fetch.buffer = buffer;
  poll_block->user_message.data.ram_fetch.buffer_size = buffer_size;

  return poll_block->user_message.hdr.my_ref;
}

static int init_ram_transmit_msg(WimpPollBlock *poll_block, const WimpMessage *ram_fetch, char const *data, int nbytes)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpRAMTransmitMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = ram_fetch->hdr.my_ref;
  poll_block->user_message.hdr.action_code = Wimp_MRAMTransmit;

  char * const buffer = ram_fetch->data.ram_fetch.buffer;
  assert(nbytes <= ram_fetch->data.ram_fetch.buffer_size);
  for (int i = 0; i < nbytes; ++i)
    buffer[i] = data[i];

  poll_block->user_message.data.ram_transmit.buffer = buffer;
  poll_block->user_message.data.ram_transmit.nbytes = nbytes;

  return poll_block->user_message.hdr.my_ref;
}

static int init_dragging_msg(WimpPollBlock *poll_block, int const file_types[], const WimpGetPointerInfoBlock *pointer_info, unsigned int flags)
{
  WimpDraggingMessage *const dragging =
    (WimpDraggingMessage *)poll_block->user_message.data.bytes;

  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpDraggingMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MDragging;

  dragging->window_handle = pointer_info->window_handle;
  dragging->icon_handle = pointer_info->icon_handle;
  dragging->x = pointer_info->x;
  dragging->y = pointer_info->y;
  dragging->flags = flags;
  dragging->bbox.xmin = dragging->bbox.ymin = DraggingBBoxMin;
  dragging->bbox.xmax = dragging->bbox.ymax = DraggingBBoxMax;

  size_t i;
  for (i = 0; i < ARRAY_SIZE(dragging->file_types); ++i)
  {
    DEBUGF("%zu: %d\n", i, dragging->file_types[i]);
    dragging->file_types[i] = file_types[i];
    if (file_types[i] == FileType_Null)
      break;
  }
  assert(i < ARRAY_SIZE(dragging->file_types));

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_load_msg(WimpPollBlock *poll_block, char *filename, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_load.leaf_name) + WORD_ALIGN(strlen(filename)+1);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataLoad;

  poll_block->user_message.data.data_load.destination_window = pointer_info->window_handle;
  poll_block->user_message.data.data_load.destination_icon = pointer_info->icon_handle;
  poll_block->user_message.data.data_load.destination_x = pointer_info->x;
  poll_block->user_message.data.data_load.destination_y = pointer_info->y;
  poll_block->user_message.data.data_load.estimated_size = estimated_size;
  poll_block->user_message.data.data_load.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_load.leaf_name, filename);

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_load_ack_msg(WimpPollBlock *poll_block, const WimpMessage *data_load)
{
  poll_block->user_message = *data_load;
  poll_block->user_message.hdr.action_code = Wimp_MDataLoadAck;
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.your_ref = data_load->hdr.my_ref;

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_open_msg(WimpPollBlock *poll_block, char *filename, int file_type, const WimpGetPointerInfoBlock *pointer_info)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_open.path_name) + WORD_ALIGN(strlen(filename)+1);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MDataOpen;

  poll_block->user_message.data.data_load.destination_window = pointer_info->window_handle;
  poll_block->user_message.data.data_open.padding1 = pointer_info->icon_handle;
  poll_block->user_message.data.data_open.x = pointer_info->x;
  poll_block->user_message.data.data_open.y = pointer_info->y;
  poll_block->user_message.data.data_open.padding2 = 0;
  poll_block->user_message.data.data_open.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_open.path_name, filename);

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_save_msg(WimpPollBlock *poll_block, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_save.leaf_name) + WORD_ALIGN(strlen(TEST_LEAFNAME)+1);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataSave;

  poll_block->user_message.data.data_save.destination_window = pointer_info->window_handle;
  poll_block->user_message.data.data_save.destination_icon = pointer_info->icon_handle;
  poll_block->user_message.data.data_save.destination_x = pointer_info->x;
  poll_block->user_message.data.data_save.destination_y = pointer_info->y;
  poll_block->user_message.data.data_save.estimated_size = estimated_size;
  poll_block->user_message.data.data_save.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_save.leaf_name, TEST_LEAFNAME);

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_save_ack_msg(WimpPollBlock *poll_block, const WimpMessage *data_save)
{
  poll_block->user_message = *data_save;
  poll_block->user_message.hdr.action_code = Wimp_MDataSaveAck;
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_save_ack.leaf_name) + WORD_ALIGN(strlen(TEST_DATA_OUT)+1);
  poll_block->user_message.hdr.your_ref = data_save->hdr.my_ref;
  strcpy(poll_block->user_message.data.data_save_ack.leaf_name, TEST_DATA_OUT);

  return poll_block->user_message.hdr.my_ref;
}

static int init_drag_claim_msg(WimpPollBlock *poll_block, unsigned int flags, int const file_types[], int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpDragClaimMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDragClaim;

  WimpDragClaimMessage * const dc = (WimpDragClaimMessage *)poll_block->user_message.data.bytes;
  dc->flags = flags;

  size_t i;
  for (i = 0; i < ARRAY_SIZE(dc->file_types); ++i)
  {
    DEBUGF("%zu: %d\n", i, dc->file_types[i]);
    dc->file_types[i] = file_types[i];
    if (file_types[i] == FileType_Null)
      break;
  }
  assert(i < ARRAY_SIZE(dc->file_types));

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_request_msg(WimpPollBlock *poll_block, unsigned int flags, int const file_types[], const WimpGetPointerInfoBlock *pointer_info, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpDataRequestMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataRequest;

  WimpDataRequestMessage * const dr = (WimpDataRequestMessage *)poll_block->user_message.data.bytes;
  dr->destination_window = pointer_info->window_handle;
  dr->destination_icon = pointer_info->icon_handle;
  dr->destination_x = pointer_info->x;
  dr->destination_y = pointer_info->y;
  dr->flags = flags;
  size_t i;
  for (i = 0; i < ARRAY_SIZE(dr->file_types); ++i)
  {
    DEBUGF("%zu: %d\n", i, dr->file_types[i]);
    dr->file_types[i] = file_types[i];
    if (file_types[i] == FileType_Null)
      break;
  }
  assert(i < ARRAY_SIZE(dr->file_types));

  return poll_block->user_message.hdr.my_ref;
}

static int init_claim_entity_msg(WimpPollBlock *poll_block, unsigned int flags)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpClaimEntityMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MClaimEntity;

  WimpClaimEntityMessage * const ce = (WimpClaimEntityMessage *)poll_block->user_message.data.bytes;
  ce->flags = flags;

  return poll_block->user_message.hdr.my_ref;
}

static int init_pre_quit_msg(WimpPollBlock *poll_block, bool desktop_shutdown, bool is_risc_os_3)
{
  poll_block->user_message.hdr.size = sizeof(poll_block->user_message.hdr) + (is_risc_os_3 ? sizeof(poll_block->user_message.data.words[0]) : 0);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("size %d my_ref %d\n", poll_block->user_message.hdr.size, poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MPreQuit;
  if (is_risc_os_3)
    poll_block->user_message.data.words[0] = desktop_shutdown ? 0 : 1;
  else
    assert(desktop_shutdown);

  return poll_block->user_message.hdr.my_ref;
}

static void dispatch_event_internal(int event_code, WimpPollBlock *poll_block,
  bool suppress)
{
  Fortify_CheckAllMemory();

  pseudo_wimp_reset();

  DEBUGF("Test dispatches event %d", event_code);

  switch (event_code)
  {
    case Wimp_EToolboxEvent:
      DEBUGF(" (Toolbox event 0x%x)", ((ToolboxEvent *)poll_block)->hdr.event_code);
      break;

    case Wimp_EUserMessage:
    case Wimp_EUserMessageRecorded:
    case Wimp_EUserMessageAcknowledge:
      DEBUGF(" (action %d)", ((WimpMessage *)poll_block)->hdr.action_code);
      break;

    default:
      break;
  }
  DEBUGF("\n");

  assert_no_error(event_dispatch(event_code, poll_block));

  if (!suppress)
  {
    assert_no_error(pseudo_event_wait_for_idle());
  }

  /* Deliver any outgoing broadcasts back to the sender */
  unsigned int count = pseudo_wimp_get_message_count();
  for (unsigned int i = 0; i < count; ++i)
  {
    int msg_code, handle;
    WimpPollBlock msg_block;
    pseudo_wimp_get_message2(i, &msg_code, &msg_block, &handle, NULL);
    if (handle == 0)
    {
      assert_no_error(event_dispatch(msg_code, &msg_block));
    }
  }

  if (!suppress)
  {
    assert_no_error(pseudo_event_wait_for_idle());
  }

  Fortify_CheckAllMemory();
  DEBUGF("exit %s\n", __func__ );
}

static void dispatch_event(int event_code, WimpPollBlock *poll_block)
{
  dispatch_event_internal(event_code, poll_block, false);
}

static void dispatch_event_suppress(int event_code, WimpPollBlock *poll_block)
{
  dispatch_event_internal(event_code, poll_block, true);
}

static void dispatch_event_with_error_sim(int event_code,
  WimpPollBlock *poll_block, unsigned long limit)
{
  DEBUGF("Test sets allocation limit %lu\n", limit);
  Fortify_SetNumAllocationsLimit(limit);
  dispatch_event(event_code, poll_block);

  Fortify_SetNumAllocationsLimit(ULONG_MAX);
  DEBUGF("exit %s\n", __func__ );
}

static void dispatch_event_suppress_with_error_sim(int event_code,
  WimpPollBlock *poll_block, unsigned long limit)
{
  DEBUGF("Test sets allocation limit %lu\n", limit);
  Fortify_SetNumAllocationsLimit(limit);
  dispatch_event_suppress(event_code, poll_block);

  Fortify_SetNumAllocationsLimit(ULONG_MAX);
  DEBUGF("exit %s\n", __func__ );
}

static void set_colour(ObjectId id, unsigned int colour_number)
{
  WimpPollBlock poll_block;
  ObjectId const picker_id = pseudo_toolbox_find_by_template_name("Picker");

  /* Simulate opening the colour picker box */
  init_custom_event(&poll_block, EventCode_SetColour);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  /* Simulate choosing a colour */
  init_pal256_event(&poll_block, colour_number);
  init_id_block(pseudo_event_get_client_id_block(), picker_id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void mouse_select(ObjectId id, unsigned int start, unsigned int end)
{
  /* Simulate a mouseclick selection */
  WimpPollBlock poll_block;
  int buttons = Wimp_MouseButtonSelect * 256;
  for (unsigned int pos = start; pos < end; ++pos)
  {
    init_mouseclick_event(&poll_block, id, pos, buttons);
    buttons = Wimp_MouseButtonAdjust * 256;
    init_id_block(pseudo_event_get_client_id_block(), id, FirstComponentId+pos);
    dispatch_event(Wimp_EMouseClick, &poll_block);
  }
}

static void mouse_drag(ObjectId id, unsigned int pos)
{
  /* Simulate a mouse drag */
  WimpPollBlock poll_block;
  init_mouseclick_event(&poll_block, id, pos, Wimp_MouseButtonSelect * 16);
  init_id_block(pseudo_event_get_client_id_block(), id, FirstComponentId+pos);
  dispatch_event_suppress(Wimp_EMouseClick, &poll_block);
}

static void mouse_drop(int x, int y)
{
  /* Simulate a mouse drag termination */
  WimpPollBlock poll_block;
  init_userdrag_event(&poll_block, x, y);
  init_id_block(pseudo_event_get_client_id_block(), NULL_ObjectId, NULL_ComponentId);
  dispatch_event(Wimp_EUserDrag, &poll_block);
}

static void abort_drag(ObjectId id)
{
  /* Simulate pressing ESCAPE during a drag */
  WimpPollBlock poll_block;
  init_custom_event(&poll_block, EventCode_AbortDrag);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void select_all(ObjectId id)
{
  /* Simulate selecting all the colour indexes */
  WimpPollBlock poll_block;
  init_custom_event(&poll_block, EventCode_SelectAll);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void deselect_all(ObjectId id)
{
  /* Simulate deselecting all the colour indexes */
  WimpPollBlock poll_block;
  init_custom_event(&poll_block, EventCode_ClearSelection);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void setup_selection(ObjectId id)
{
  select_all(id);
  set_colour(id, NonSelectionColour);
  deselect_all(id);
  mouse_select(id, SelectionStart, SelectionEnd);
  set_colour(id, SelectionColour);
  assert(userdata_count_unsafe() == 1);
}

static bool check_drag_claim_msg(int d_ref, int d_handle, WimpMessage *drag_claim, bool expect_data_type)
{
  /* A drag claim message should have been sent in reply to the drag */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDragClaim))
    {
      assert(handle == d_handle);
      assert(poll_block.user_message.hdr.your_ref == d_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      *drag_claim = poll_block.user_message;

      const WimpDragClaimMessage * const dc = (WimpDragClaimMessage *)poll_block.user_message.data.bytes;
      size_t i, fn = 0, csv = 0, data = 0;

      DEBUGF("Drag claim flags 0x%x\n", dc->flags);
      assert(dc->flags == 0);

      for (i = 0; i < ARRAY_SIZE(dc->file_types); ++i)
      {
        DEBUGF("%zu: %d\n", i, dc->file_types[i]);
        if (dc->file_types[i] == FileType_Fednet)
          fn++;
        else if (dc->file_types[i] == FileType_Data)
          data++;
        else if (dc->file_types[i] == FileType_CSV)
          csv++;
        else if (dc->file_types[i] == FileType_Null)
          break;
        else
          assert("Unexpected file type" == NULL);
      }
      assert(i < ARRAY_SIZE(dc->file_types));
      assert(data == expect_data_type ? 1 : 0);
      assert(fn == 1);
      assert(csv == 1);
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data) + offsetof(WimpDragClaimMessage, file_types) + (sizeof(int) * (i+1)));

      return true;
    }
  }
  return false;
}

static bool check_data_request_msg(WimpMessage *data_request, int window_handle)
{
  /* A data request message should have been broadcast if pasting from clipboard */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageRecorded) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataRequest))
    {
      assert(handle == 0);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.your_ref == 0);
      assert(poll_block.user_message.hdr.my_ref != 0);
      *data_request = poll_block.user_message;

      const WimpDataRequestMessage * const dr = (WimpDataRequestMessage *)poll_block.user_message.data.bytes;
      size_t i, csv = 0, data = 0;

      assert(dr->destination_window == window_handle);
      assert(dr->destination_icon == WorkArea);
      assert(dr->destination_x == 0);
      assert(dr->destination_y == 0);

      DEBUGF("Data request flags 0x%x\n", dr->flags);
      assert(dr->flags == Wimp_MDataRequest_Clipboard);

      for (i = 0; i < ARRAY_SIZE(dr->file_types); ++i)
      {
          DEBUGF("%zu: %d\n", i, dr->file_types[i]);
        if (dr->file_types[i] == FileType_CSV)
          csv++;
        else if (dr->file_types[i] == FileType_Data)
          data++;
        else if (dr->file_types[i] == FileType_Null)
          break;
        else
          assert("Unexpected file type" == NULL);
      }
      assert(i < ARRAY_SIZE(dr->file_types));
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data) + offsetof(WimpDataRequestMessage, file_types) + (sizeof(int) * (i+1)));
      assert(csv == 1);
      assert(data == 1);

      return true;
    }
  }
  return false;
}

static bool check_dragging_msg(int dc_ref, int dc_handle, const WimpGetPointerInfoBlock *pointer_info, WimpMessage *dragging, int *code)
{
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int handle, icon;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, code, &poll_block, &handle, &icon);
    DEBUGF("Event code 0x%x\n", *code);

    if (((*code == Wimp_EUserMessage) || (*code == Wimp_EUserMessageRecorded)) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDragging))
    {
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.your_ref == dc_ref);
      assert(poll_block.user_message.hdr.my_ref != 0);
      *dragging = poll_block.user_message;

      const WimpDraggingMessage *const d = (WimpDraggingMessage *)poll_block.user_message.data.bytes;

      DEBUGF("Dragging flags 0x%x\n", d->flags);

      if (dc_ref == 0)
      {
        assert(handle == d->window_handle);
        assert(icon == d->icon_handle);
      }
      else
      {
        assert(handle == dc_handle);
        assert(icon == 0);
      }

      size_t i, text = 0, csv = 0, fn = 0, data = 0;

      assert(d->bbox.xmax < d->bbox.xmin);

      for (i = 0; i < ARRAY_SIZE(d->file_types); ++i)
      {
        DEBUGF("%zu: %d\n", i, d->file_types[i]);
        if (d->file_types[i] == FileType_Text)
          text++;
        else if (d->file_types[i] == FileType_CSV)
          csv++;
        else if (d->file_types[i] == FileType_Fednet)
          fn++;
        else if (d->file_types[i] == FileType_Data)
          data++;
        else if (d->file_types[i] == FileType_Null)
          break;
      }
      assert(i < ARRAY_SIZE(d->file_types));
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data) + offsetof(WimpDraggingMessage, file_types) + (sizeof(int) * (i+1)));
      assert(text == 1);
      assert(csv == 1);
      assert(fn == 1);
      assert(data == 1);

      assert(pointer_info->window_handle == d->window_handle);
      assert(pointer_info->icon_handle == d->icon_handle);
      assert(pointer_info->x == d->x);
      assert(pointer_info->y == d->y);

      return true;
    }
  }
  return false;
}

static bool check_claim_entity_msg(WimpMessage *claim_entity)
{
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle, icon;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, &icon);

    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MClaimEntity))
    {
      /* Claim entity should always be broadcast */
      assert(handle == 0);
      assert(icon == 0);

      assert(poll_block.user_message.hdr.your_ref == 0);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.hdr.size == offsetof(WimpMessage, data) + sizeof(WimpClaimEntityMessage));
      *claim_entity = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_data_save_msg(int dc_ref, int dc_handle, char const *filename, WimpMessage *data_save, const WimpGetPointerInfoBlock *pointer_info)
{
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle, icon;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, &icon);

    if ((code == Wimp_EUserMessageRecorded) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataSave))
    {
      if (dc_ref == 0)
      {
          assert(handle == poll_block.user_message.data.data_save.destination_window);
          assert(icon == poll_block.user_message.data.data_save.destination_icon);
      }
      else
      {
          assert(handle == dc_handle);
          assert(icon == 0);
      }

      assert(poll_block.user_message.hdr.your_ref == dc_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_save.leaf_name) + WORD_ALIGN(strlen(filename)+1));
      assert(poll_block.user_message.data.data_save.destination_window == pointer_info->window_handle);
      assert(poll_block.user_message.data.data_save.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_save.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_save.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_save.estimated_size > 0);
      assert(!strcmp(poll_block.user_message.data.data_save.leaf_name, filename));
      *data_save = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_data_save_ack_msg(int ds_ref, WimpMessage *data_save_ack, const WimpGetPointerInfoBlock *pointer_info)
{
  /* A datasaveack message should have been sent in reply to the datasave */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    /* There may be an indeterminate delay between us sending DataSaveAck
       and other task responding with a DataLoad message. (Sending
       DataSaveAck as recorded delivery breaks the SaveAs module, for one. */
    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataSaveAck))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == ds_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);

      char const * const filename = "<Wimp$Scrap>";
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_save_ack.leaf_name) + WORD_ALIGN(strlen(filename)+1));
      assert(poll_block.user_message.data.data_save_ack.destination_window == pointer_info->window_handle);
      assert(poll_block.user_message.data.data_save_ack.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_save_ack.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_save_ack.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_save_ack.estimated_size == UnsafeDataSize);
      assert(!strcmp(poll_block.user_message.data.data_save_ack.leaf_name, filename));
      *data_save_ack = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_data_load_msg(int dsa_ref, WimpMessage *data_load, const WimpGetPointerInfoBlock *pointer_info)
{
  /* A dataload message should have been sent in reply to the datasaveack */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageRecorded) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataLoad))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == dsa_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_load.leaf_name) + WORD_ALIGN(strlen(TEST_DATA_OUT)+1));
      assert(poll_block.user_message.data.data_load.destination_window == pointer_info->window_handle);
      assert(poll_block.user_message.data.data_load.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_load.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_load.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_load.estimated_size > 0);
      assert(!strcmp(poll_block.user_message.data.data_load.leaf_name, TEST_DATA_OUT));
      *data_load = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_data_load_ack_msg(int dl_ref, char *filename, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info)
{
  /* A dataloadack message should have been sent in reply to the dataload */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataLoadAck))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == dl_ref);
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_load_ack.leaf_name) + WORD_ALIGN(strlen(filename)+1));
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.data.data_load_ack.destination_window == pointer_info->window_handle);

      assert(poll_block.user_message.data.data_load_ack.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_load_ack.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_load_ack.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_load_ack.estimated_size == estimated_size);
      assert(poll_block.user_message.data.data_load_ack.file_type == file_type);
      assert(!strcmp(poll_block.user_message.data.data_load_ack.leaf_name, filename));
      return true;
    }
  }
  return false;
}

static bool check_ram_fetch_msg(int rt_ref, WimpMessage *ram_fetch)
{
  /* A ramfetch message should have been sent in reply to a datasave or ramtransmit */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageRecorded) &&
        (poll_block.user_message.hdr.action_code == Wimp_MRAMFetch))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == rt_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);

      assert(poll_block.user_message.hdr.size == offsetof(WimpMessage, data.ram_fetch) + sizeof(poll_block.user_message.data.ram_fetch));
      assert(poll_block.user_message.data.ram_fetch.buffer != NULL);
      *ram_fetch = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_ram_transmit_msg(int rf_ref, WimpMessage *ram_transmit, int *code)
{
  /* A RAMTransmit message should have been sent in reply to a RAMFetch */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, code, &poll_block, &handle, NULL);

    /* Whether or not the sender of this message expects a reply depends on whether or not
       it filled the data receiver's buffer. */
    if (((*code == Wimp_EUserMessage) || (*code == Wimp_EUserMessageRecorded)) &&
        (poll_block.user_message.hdr.action_code == Wimp_MRAMTransmit))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == rf_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);

      assert(poll_block.user_message.hdr.size == offsetof(WimpMessage, data.ram_transmit) + sizeof(poll_block.user_message.data.ram_transmit));
      assert(poll_block.user_message.data.ram_transmit.buffer != NULL);
      *ram_transmit = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_pre_quit_ack_msg(int pq_ref, WimpMessage *pre_quit)
{
  /* A pre-quit message should have been acknowledged */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageAcknowledge) &&
        (poll_block.user_message.hdr.action_code == Wimp_MPreQuit))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == pq_ref);
      assert(poll_block.user_message.hdr.sender == pre_quit->hdr.sender);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.hdr.size == pre_quit->hdr.size);

      bool expect_shutdown = false, got_shutdown = false;
      assert(pre_quit->hdr.size >= 0);
      if ((size_t)pre_quit->hdr.size >= sizeof(pre_quit->hdr) + sizeof(pre_quit->data.words[0]))
        expect_shutdown = (pre_quit->data.words[0] == 0);

      assert(poll_block.user_message.hdr.size >= 0);
      if ((size_t)poll_block.user_message.hdr.size == sizeof(poll_block.user_message.hdr) + sizeof(poll_block.user_message.data.words[0]))
        got_shutdown = (poll_block.user_message.data.words[0] == 0);

      assert(expect_shutdown == got_shutdown);
      return true;
    }
  }
  return false;
}

static bool check_key_pressed_msg(int key_code)
{
  /* A Ctrl-Shift-F12 key press should have been sent to the originator
     of the pre-quit message */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if (code == Wimp_EKeyPressed)
    {
      assert(handle == ForeignTaskHandle);
      assert(poll_block.key_pressed.key_code == key_code);

      WimpGetCaretPositionBlock caret;
#undef wimp_get_caret_position
      assert_no_error(wimp_get_caret_position(&caret));

      DEBUGF("Key press %d,%d,%d,%d caret %d,%d,%d,%d\n",
             poll_block.key_pressed.caret.window_handle,
             poll_block.key_pressed.caret.icon_handle,
             poll_block.key_pressed.caret.xoffset,
             poll_block.key_pressed.caret.yoffset,
             caret.window_handle,
             caret.icon_handle,
             caret.xoffset,
             caret.yoffset);

      assert(poll_block.key_pressed.caret.window_handle == caret.window_handle);
      if (poll_block.key_pressed.caret.window_handle != WorkArea)
      {
        assert(poll_block.key_pressed.caret.icon_handle == caret.icon_handle);
      }

      return true;
    }
  }
  return false;
}

static void check_file_save_completed(ObjectId id, const _kernel_oserror *err)
{
  /* saveas_file_save_completed must have been called
     to indicate success or failure */
  unsigned int flags;
  char buffer[256];
  int nbytes;
  ObjectId const quoted_id = pseudo_saveas_get_file_save_completed(
                             &flags, buffer, sizeof(buffer), &nbytes);

  assert(id != NULL_ObjectId);
  assert(nbytes >= 0);
  assert((size_t)nbytes <= sizeof(buffer));
  assert(quoted_id == id);
  assert(!strcmp(buffer, TEST_DATA_OUT));
  if (err == NULL)
    assert(flags == SaveAs_SuccessfulSave);
  else
    assert(flags == 0);
}

static void check_and_dispatch_caret_claim(void)
{
  /* A claim entity message should be sent when the caret is claimed */
  WimpMessage claim_entity;
  if (check_claim_entity_msg(&claim_entity))
  {
    assert(((WimpClaimEntityMessage *)claim_entity.data.words)->flags == Wimp_MClaimEntity_CaretOrSelection);
  }
}

static void close_window(ObjectId id)
{
  WimpPollBlock poll_block;

  init_close_window_event(&poll_block, id);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_ECloseWindow, &poll_block);
}

static void close_and_discard(ObjectId id)
{
  /* Close the editing window */
  close_window(id);

  if (userdata_count_unsafe() > 0)
  {
    /* Choose 'discard' in the Discard/Cancel/Save dialogue */
    WimpPollBlock poll_block;
    init_dcs_discard_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), pseudo_toolbox_find_by_template_name("DCS"), 0x82a801);
    dispatch_event(Wimp_EToolboxEvent, &poll_block);
  }
}

static void double_click(int file_type, bool expect_claim)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  int data_open_ref = 0;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock dir_info;
  init_pointer_info_for_foreign(&dir_info);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    data_open_ref = init_data_open_msg(&poll_block, TEST_DATA_IN, file_type, &dir_info);

    err_suppress_errors();

    Fortify_EnterScope();
    dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit);
    assert(fopen_num() == 0);

    check_and_dispatch_caret_claim();

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
    if (id != NULL_ObjectId)
      close_window(id);

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  assert(expect_claim == check_data_load_ack_msg(data_open_ref, TEST_DATA_IN, 0, file_type, &dir_info));

  /* The receiver must not delete persistent files */
  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_File);
}

static void load_persistent(char *templ, int file_type)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  int data_load_ref = 0;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    data_load_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, UnsafeDataSize, file_type, &drag_dest, 0);

    err_suppress_errors();

    Fortify_EnterScope();
    dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit);

    assert(fopen_num() == 0);

    check_and_dispatch_caret_claim();

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name(templ);
    if (id != NULL_ObjectId)
      close_and_discard(id);

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  check_data_load_ack_msg(data_load_ref, TEST_DATA_IN, UnsafeDataSize, file_type, &drag_dest);

  /* The receiver must not delete persistent files */
  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_File);
}

static void activate_savebox(ObjectId saveas_id, unsigned int flags, DataTransferMethod method)
{
  unsigned long limit;
  const _kernel_oserror *err = NULL;

  /* The savebox should have been shown */
  assert(pseudo_toolbox_object_is_showing(saveas_id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;

    /* Recording the new file path can allocate memory so no enter-scope here */
    init_id_block(pseudo_event_get_client_id_block(), saveas_id, NULL_ComponentId);
    DEBUGF("Test sets allocation limit %lu\n", limit);
    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the savebox */
    switch (method)
    {
      case DTM_RAM:
      case DTM_BadRAM:
      {
        assert(!(flags & SaveAs_DestinationSafe));
        /* Open a temporary file in which to store the received data. */
        FILE * const f = fopen(TEST_DATA_OUT, "wb");
        assert(f != NULL);
        int total_bytes = 0;

        /* Make sure we don't get all of the data on the first call */
        int size = 1;

        do
        {
          /* Testing RAM transfer, so fake a Fill Buffer event such as might be
             generated by the Toolbox upon receipt of a RAM fetch message. */
          char buffer[256];

          init_fillbuffer_event(&poll_block, (flags & SaveAs_SelectionSaved) ? SaveAs_SelectionBeingSaved : 0, size, NULL, total_bytes);
          pseudo_saveas_reset_buffer_filled();
          err_suppress_errors();
          dispatch_event(Wimp_EToolboxEvent, &poll_block);
          err = err_dump_suppressed();

          unsigned int flags;
          int nbytes;
          ObjectId const quoted_id = pseudo_saveas_get_buffer_filled(
                                     &flags, buffer, sizeof(buffer), &nbytes);
          if (quoted_id != NULL_ObjectId)
          {
            total_bytes += nbytes;

            assert(nbytes <= size);
            assert(quoted_id == saveas_id);
            assert(flags == 0);

            const size_t n = fwrite(buffer, nbytes, 1, f);
            assert(n == 1);
            if ((method == DTM_BadRAM) || (nbytes < size))
              break; /* Finished */
          }
          else
          {
            /* If data was not sent then it must be because an error occurred. */
            assert(err != NULL);
            break;
          }

          size = sizeof(buffer);
        }
        while (1);

        fclose(f);
        break;
      }
      case DTM_File:
      case DTM_BadFile:
      {
        /* Testing file transfer, so fake a Save To File event such as might be
           generated by the Toolbox upon receipt of a DataSaveAck message. */
        pseudo_saveas_reset_file_save_completed();
        init_savetofile_event(&poll_block, (flags & SaveAs_SelectionSaved) ? SaveAs_SelectionBeingSaved : 0);
        err_suppress_errors();
        dispatch_event(Wimp_EToolboxEvent, &poll_block);
        err = err_dump_suppressed();
        check_file_save_completed(saveas_id, err);
        break;
      }
      default:
      {
        DEBUGF("Method %d is not supported\n", method);
        break;
      }
    }

    if ((err == NULL) && (method != DTM_BadFile) && (method != DTM_BadRAM))
    {
      /* Simulate the save completed event that the Toolbox would have
         delivered had we not intercepted saveas_file_save_completed. */
      err_suppress_errors();

      init_id_block(pseudo_event_get_client_id_block(), saveas_id, NULL_ComponentId);
      init_savecompleted_event(&poll_block, flags);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      err = err_dump_suppressed();
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;

    /* Saving data may destroy the window object if pending but an error may
       still have been suppressed so stop if the window's state can't be got. */
    ObjectId ancestor_id;
    assert_no_error(toolbox_get_ancestor(0, saveas_id, &ancestor_id, NULL));
#undef toolbox_get_object_state
    if (toolbox_get_object_state(0, ancestor_id, NULL) != NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static void save_file(ObjectId id, unsigned int flags, DataTransferMethod method)
{
  unsigned long limit;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;
    WimpPollBlock poll_block;

    err_suppress_errors();
    Fortify_EnterScope();

    /* Simulate a save */
    init_custom_event(&poll_block, EventCode_SaveFile);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    Fortify_LeaveScope();
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  activate_savebox(pseudo_toolbox_find_by_template_name("SaveFile"), flags, method);
}

static void save_close_and_discard(ObjectId id)
{
  WimpPollBlock poll_block;
  ObjectId const savebox_id = pseudo_toolbox_find_by_template_name("SaveFile");

  /* Open the savebox */
  init_custom_event(&poll_block, EventCode_SaveFile);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  assert(pseudo_toolbox_object_is_showing(savebox_id));

  /* Activate the savebox */
  init_savetofile_event(&poll_block, 0);
  init_id_block(pseudo_event_get_client_id_block(), savebox_id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  /* Simulate the save completed event that the Toolbox would have
     delivered had we not intercepted saveas_file_save_completed. */
  init_savecompleted_event(&poll_block, SaveAs_DestinationSafe);
  init_id_block(pseudo_event_get_client_id_block(), savebox_id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  assert(path_is_in_userdata(TEST_DATA_OUT));

  close_and_discard(id);

  assert_file_has_type(TEST_DATA_OUT, FileType_Fednet);
}

static ObjectId get_created_window(char *templ)
{
  /* An editing window should have been created and shown */
  ObjectId const id = pseudo_toolbox_find_by_template_name(templ);
  assert(object_is_on_menu(id));
  assert(pseudo_toolbox_object_is_showing(id));
  return id;
}

static void test1(void)
{
  /* Load CSV file */
  make_csv_file(TEST_DATA_IN, colour_csv);
  load_persistent("EditColmap", FileType_CSV);

  /* An editing window should have been created */
  ObjectId const id = get_created_window("EditColmap");

  /* The data should have been treated as though it had been dragged in
     because the file doesn't represent a whole set of colours. */
  assert(!path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 1);

  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_csv);
  Fortify_LeaveScope();
}

static void test2(void)
{
  /* Load object colours file */
  make_object_cols_file(TEST_DATA_IN, colour_identity);
  load_persistent("EditColmap", FileType_Fednet);

  /* An editing window should have been created */
  ObjectId const id = get_created_window("EditColmap");

  /* The data should be treated as 'safe' and findable by path. */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_identity);
  Fortify_LeaveScope();
}

static void test3(void)
{
  /* Load directory */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  /* Create directory */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));

  int const data_load_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, UnsafeDataSize, FileType_Directory, &drag_dest, 0);

  check_data_load_ack_msg(data_load_ref, TEST_DATA_IN, UnsafeDataSize,
    FileType_Directory, &drag_dest);

  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_Directory);

  err_suppress_errors();
  dispatch_event(Wimp_EUserMessage, &poll_block);
  err = err_dump_suppressed();

  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("BadFileType")));
  assert(fopen_num() == 0);
}

static void wait(clock_t timeout)
{
  const clock_t start_time = clock();
  clock_t elapsed;

  DEBUGF("Waiting %fs\n", (double)timeout / CLOCKS_PER_SEC);
  _swix(Hourglass_On, 0);
  do
  {
    elapsed = clock() - start_time;
    _swix(Hourglass_Percentage, _IN(0), (elapsed * 100) / timeout);
  }
  while (elapsed < timeout);
  _swix(Hourglass_Off, 0);
}

static void cleanup_stalled(void)
{
  /* Wait for timeout then deliver a null event */
  unsigned long limit;

  wait(Timeout);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;

    err_suppress_errors();
    dispatch_event_with_error_sim(Wimp_ENull, NULL, limit);
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
}

static const _kernel_oserror *send_data_core(int file_type, int estimated_size, const WimpGetPointerInfoBlock *pointer_info, DataTransferMethod method, int your_ref)
{
  const _kernel_oserror *err;
  WimpPollBlock poll_block;
  bool use_file = false;

  DEBUGF("send_data_core file_type=%d estimated_size=%d method=%d\n", file_type, estimated_size, method);
  if (method == DTM_None) return NULL;

  err_suppress_errors();

  /* Try to ensure that at least two RAMFetch messages are sent */
  int our_ref = init_data_save_msg(&poll_block,
    method == DTM_BadRAM ? estimated_size/2 : estimated_size,
    file_type, pointer_info, your_ref);

  dispatch_event(Wimp_EUserMessage, &poll_block);

  err = err_dump_suppressed();

  WimpMessage data_save_ack;
  if (check_data_save_ack_msg(our_ref, &data_save_ack, pointer_info))
  {
    DEBUGF("file_type 0x%x\n", data_save_ack.data.data_save_ack.file_type);
    assert(data_save_ack.data.data_save_ack.file_type == file_type);
    use_file = true;
  }
  else
  {
    WimpMessage ram_fetch;
    if (check_ram_fetch_msg(our_ref, &ram_fetch))
    {
      switch (method)
      {
        case DTM_RAM:
        case DTM_BadRAM:
        {
          /* Allowed to use RAM transfer. */
          char test_data[estimated_size];
          FILE * const f = fopen(TEST_DATA_IN, "rb");
          assert(f != NULL);
          size_t const n = fread(test_data, estimated_size, 1, f);
          assert(n == 1);
          fclose(f);

          int total_bytes = 0;
          do
          {
            /* Copy as much data into the receiver's buffer as will fit */
            int const buffer_size = ram_fetch.data.ram_fetch.buffer_size;
            assert(total_bytes <= estimated_size);
            int const nbytes = LOWEST(buffer_size, estimated_size - total_bytes);
            our_ref = init_ram_transmit_msg(&poll_block, &ram_fetch, test_data + total_bytes, nbytes);
            total_bytes += nbytes;

            err_suppress_errors();
            dispatch_event(Wimp_EUserMessage, &poll_block);
            err = err_dump_suppressed();

            /* Expect another RAMFetch message in reply only if we completely filled
               the receiver's buffer. */
            if (check_ram_fetch_msg(our_ref, &ram_fetch))
            {
              assert(nbytes == buffer_size);

              if (method == DTM_BadRAM)
              {
                /* Instead of sending another RAMTransmit message to complete the protocol,
                   fake the return of the RAMFetch message to the saver. */
                err_suppress_errors();
                poll_block.user_message_acknowledge = ram_fetch;
                dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
                err = err_dump_suppressed();
                break;
              }
            }
            else
            {
              /* An error must have occurred or the buffer was not filled (means EOF).  */
              assert((err != NULL) || (nbytes < buffer_size));
              if (err == NULL)
                assert(userdata_count_unsafe() == 1);
              break;
            }
          }
          while (1);
          break;
        }

        case DTM_File:
        case DTM_BadFile:
        {
          /* Not allowed to use RAM transfer, so fake the return of the RAMFetch
             message to the loader. */
          err_suppress_errors();
          poll_block.user_message_acknowledge = ram_fetch;
          dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
          err = err_dump_suppressed();

          /* Expect the loader to retry with a DataSaveAck in response to
             the original DataSave message. */
          if (check_data_save_ack_msg(our_ref, &data_save_ack, pointer_info))
          {
            assert(data_save_ack.data.data_save_ack.file_type == file_type);
            use_file = true;
          }
          else
          {
            /* No reply to the data save message so an error must have occurred */
            assert(err != NULL);
          }
          break;
        }

        default:
        {
          DEBUGF("Method %d is not supported\n", method);
          break;
        }
      }
    }
    else
    {
      /* No reply to the data save message so an error must have occurred */
      assert(err != NULL);
    }
  }

  if (use_file)
  {
    /* We can reach this point with any method because file transfer is the fallback */
    if (method == DTM_BadFile)
    {
      /* There can be an indefinite period between a DataSaveAck and DataLoad message
         so the loader should give up after a while. */
      cleanup_stalled();
    }
    else
    {
      /* Save the data and then reply with a DataLoad message */
      wipe("<Wimp$Scrap>");
      copy(TEST_DATA_IN, "<Wimp$Scrap>");
      int const dataload_ref = init_data_load_msg(&poll_block, "<Wimp$Scrap>", estimated_size, file_type, pointer_info, data_save_ack.hdr.my_ref);

      err_suppress_errors();
      dispatch_event(Wimp_EUserMessage, &poll_block);
      err = err_dump_suppressed();

      if (check_data_load_ack_msg(dataload_ref, "<Wimp$Scrap>", estimated_size, file_type, pointer_info))
      {
        /* It's the receiver's responsibility to delete the temporary file */
        assert(fopen("<Wimp$Scrap>", "rb") == NULL);

        /* The recipient doesn't know that the data is safe because it
           didn't load a persistent file. */
        assert(!path_is_in_userdata("<Wimp$Scrap>"));
        if (err == NULL)
          assert(userdata_count_unsafe() == 1);
      }
      else
      {
        /* CBLibrary doesn't always report failure to send DataLoadAck */
      }
    }
    /* else do nothing because DataSaveAck messages are not recorded */
  }

  assert(fopen_num() == 0);
  check_and_dispatch_caret_claim();

  return err;
}

static void app_save_to_iconbar(char *templ, int file_type, int estimated_size, DataTransferMethod method, int your_ref)
{
  unsigned long limit;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_EnterScope();

    Fortify_SetNumAllocationsLimit(limit);
    err = send_data_core(file_type, estimated_size, &drag_dest, method, your_ref);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name(templ);
    if (id != NULL_ObjectId)
      close_and_discard(id);

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);
}

static void test4(void)
{
  /* CSV file from app with broken file transfer */
  app_save_to_iconbar("EditColmap", FileType_CSV, TestDataSize, DTM_BadFile, 0);
  assert(userdata_count_unsafe() == 0);
  Fortify_LeaveScope();
}

static void test5(void)
{
  /* Compressed file from app with broken file transfer */
  app_save_to_iconbar("EditColmap", FileType_Fednet, TestDataSize, DTM_BadFile, 0);
  assert(userdata_count_unsafe() == 0);
  Fortify_LeaveScope();
}

static void test6(void)
{
  /* Transfer dir from app */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  init_data_save_msg(&poll_block, 0, FileType_Directory, &drag_dest, 0);

  err_suppress_errors();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  err = err_dump_suppressed();
  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("BadFileType")));
  assert(pseudo_wimp_get_message_count() == 0);
}

static void reset_scroll_state(int window_handle)
{
  WimpAutoScrollBlock auto_scroll;
  auto_scroll.window_handle = window_handle;
  assert_no_error(_swix(Wimp_AutoScroll, _INR(0,1), 0, &auto_scroll));
}

static unsigned int get_scroll_state(int window_handle)
{
  unsigned int scroll_state;
  WimpAutoScrollBlock auto_scroll;
  auto_scroll.window_handle = window_handle;
  assert_no_error(_swix(Wimp_AutoScroll, _INR(0,1)|_OUT(0), Wimp_AutoScroll_ReadFlags, &auto_scroll, &scroll_state));
  DEBUGF("AutoScroll state: 0x%x\n", scroll_state);
  return scroll_state;
}

static const _kernel_oserror *rec_data_core(const WimpMessage *data_save, DataTransferMethod method)
{
  WimpPollBlock poll_block;
  const _kernel_oserror *err = NULL;

  switch (method)
  {
    case DTM_RAM:
    case DTM_BadRAM:
    {
      /* Open a temporary file in which to store the received data. */
      FILE * const f = fopen(TEST_DATA_OUT, "wb");
      assert(f != NULL);
      int your_ref = data_save->hdr.my_ref;

      do
      {
        /* Reply with a RamFetch message */
        WimpMessage ram_transmit;
        char buffer[8] = {0};
        int const ram_fetch_ref = init_ram_fetch_msg(&poll_block, buffer, sizeof(buffer), your_ref);

        err_suppress_errors();
        dispatch_event(Wimp_EUserMessage, &poll_block);
        err = err_dump_suppressed();

        /* A RamTransmit message should have been sent to the destination app. */
        int code;
        if (check_ram_transmit_msg(ram_fetch_ref, &ram_transmit, &code))
        {
          your_ref = ram_transmit.hdr.my_ref;
          assert(ram_transmit.data.ram_transmit.buffer == buffer);
          assert(ram_transmit.data.ram_transmit.nbytes >= 0);
          assert((size_t)ram_transmit.data.ram_transmit.nbytes <= sizeof(buffer));
          const size_t n = fwrite(ram_transmit.data.ram_transmit.buffer, ram_transmit.data.ram_transmit.nbytes, 1, f);
          assert(n == 1);
          if ((size_t)ram_transmit.data.ram_transmit.nbytes < sizeof(buffer))
          {
            assert(method != DTM_BadRAM); /* if this fails then the buffer is too big to test */
            assert(code == Wimp_EUserMessage);
            break;
          }
          assert(code == Wimp_EUserMessageRecorded);

          if (method == DTM_BadRAM)
          {
            /* Instead of sending another RAMFetch message to complete the protocol,
               fake the return of the RAMTransmit message to the saver. */
            err_suppress_errors();
            poll_block.user_message_acknowledge = ram_transmit;
            dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
            err = err_dump_suppressed();
            break;
          }
        }
        else
        {
          /* If the RAMTransmit message was not sent then it must be because an error occurred. */
          assert(err != NULL);
          break;
        }
      }
      while (1);

      fclose(f);
      break;
    }

    case DTM_File:
    case DTM_BadFile:
    {
      /* Reply with a DataSaveAck message */
      int const data_save_ack_ref = init_data_save_ack_msg(&poll_block, data_save);

      err_suppress_errors();
      dispatch_event(Wimp_EUserMessage, &poll_block);
      err = err_dump_suppressed();

      /* A DataLoad message should have been sent to the destination app. */
      WimpMessage data_load;
      const WimpGetPointerInfoBlock pointer_info =
      {
        .window_handle = data_save->data.data_save.destination_window,
        .icon_handle = data_save->data.data_save.destination_icon,
        .x = data_save->data.data_save.destination_x,
        .y = data_save->data.data_save.destination_y
      };

      if (check_data_load_msg(data_save_ack_ref, &data_load, &pointer_info))
      {
        assert(data_load.data.data_load.file_type == data_save->data.data_save.file_type);

        err_suppress_errors();
        if (method == DTM_BadFile)
        {
          /* Instead of sending a DataLoadAck message to complete the protocol,
             fake the return of the DataLoad message to the saver. */
          poll_block.user_message_acknowledge = data_load;
          dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
        }
        else
        {
          /* Reply with a DataLoadAck message */
          init_data_load_ack_msg(&poll_block, &data_load);
          dispatch_event(Wimp_EUserMessage, &poll_block);
        }
        err = err_dump_suppressed();
      }
      else
      {
        /* If the dataload message was not sent then it must be because an error occurred. */
        assert(err != NULL);
      }
      break;
    }

    case DTM_None:
    {
      /* Fake the return of the DataSave message to the saver. */
      err_suppress_errors();
      poll_block.user_message_acknowledge = *data_save;
      dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
      err = err_dump_suppressed();
      break;
    }

    default:
    {
      DEBUGF("Method %d is not supported\n", method);
      break;
    }
  }

  return err;
}

static void test7(void)
{
  /* CSV file from app */
  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);
  app_save_to_iconbar("EditColmap", FileType_CSV, estimated_size, DTM_RAM, 0);
  ObjectId const id = get_created_window("EditColmap");
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_csv);
  Fortify_LeaveScope();
}

static void test8(void)
{
  /* Object colours file from app */
  int const estimated_size = make_object_cols_file(TEST_DATA_IN, colour_identity);
  app_save_to_iconbar("EditColmap", FileType_Fednet, estimated_size, DTM_RAM, 0);
  ObjectId const id = get_created_window("EditColmap");
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_identity);
  Fortify_LeaveScope();
}

static void test9(void)
{
  /* CSV file from app with no RAM transfer */
  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);
  app_save_to_iconbar("EditColmap", FileType_CSV, estimated_size, DTM_File, 0);
  ObjectId const id = get_created_window("EditColmap");
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_csv);
  Fortify_LeaveScope();
}

static void test10(void)
{
  /* CSV file from app with broken RAM transfer */
  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);
  app_save_to_iconbar("EditColmap", FileType_CSV, estimated_size, DTM_BadRAM, 0);
  assert(userdata_count_unsafe() == 0);
  Fortify_LeaveScope();
}

static void load_bad_csv(char const *csv)
{
  WimpPollBlock poll_block;
  int data_load_ref;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  assert(csv != NULL);
  FILE * const f = fopen(TEST_DATA_IN, "wb");
  assert(f != NULL);
  int const put = fputs(csv, f);
  fclose(f);
  assert(put >= 0);

  assert_no_error(os_file_set_type(TEST_DATA_IN, FileType_CSV));

  data_load_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, UnsafeDataSize, FileType_CSV, &drag_dest, 0);

  err_suppress_errors();
  dispatch_event(Wimp_EUserMessage, &poll_block);
  assert(fopen_num() == 0);

  err = err_dump_suppressed();
  if (*csv == '\0')
  {
    assert_no_error(err);
  }
  else
  {
    assert(err != NULL);
    assert(err->errnum == DUMMY_ERRNO);
    assert(!strcmp(err->errmess, msgs_lookup("BadColNum")));
  }

  check_and_dispatch_caret_claim();

  /* The dataload message is acknowledged even if we don't like the contents */
  check_data_load_ack_msg(data_load_ref, TEST_DATA_IN, UnsafeDataSize, FileType_CSV, &drag_dest);

  /* The receiver must not delete persistent files */
  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_File);
}

static void test11(void)
{
  /* Load bad CSV file (value too low) */
  load_bad_csv("-1");
}

static void test12(void)
{
  /* Load bad CSV file (value too high) */
  load_bad_csv("256");
}

static void test13(void)
{
  /* Load empty CSV file */
  FILE * const f = fopen(TEST_DATA_IN, "wb");
  assert(f != NULL);
  fclose(f);

  assert_no_error(os_file_set_type(TEST_DATA_IN, FileType_CSV));

  load_persistent("EditColmap", FileType_CSV);

  /* An editing window should have been created */
  ObjectId const id = get_created_window("EditColmap");

  /* The data should have been treated as though it had been dragged in
     because the file doesn't represent a whole set of colours. */
  assert(!path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_black);

  Fortify_LeaveScope();
}

static const _kernel_oserror *do_drag_in_data_core(int const file_types[], unsigned int ftype_idx, int estimated_size, const WimpGetPointerInfoBlock *pointer_info, DataTransferMethod method, unsigned int flags)
{
  WimpPollBlock poll_block;
  const _kernel_oserror *err;

  /* Before a drag is claimed, auto-scrolling should be disabled */
  assert(!get_scroll_state(pointer_info->window_handle));

  err_suppress_errors();
  int const dragging_ref = init_dragging_msg(&poll_block, file_types, pointer_info, flags);
  dispatch_event(Wimp_EUserMessage, &poll_block);

  err = err_dump_suppressed();

  /* Do not expect file type 'data' to be accepted by the app if the drag is over the iconbar */
  WimpMessage drag_claim;
  if (check_drag_claim_msg(dragging_ref, ForeignTaskHandle, &drag_claim, pointer_info->window_handle != WimpWindow_Iconbar))
  {
    /* Whilst a drag is claimed by a window, auto-scrolling should be enabled
       for that window */
    if (err == NULL)
    {
      if (pointer_info->window_handle == WimpWindow_Iconbar)
      {
        assert(!get_scroll_state(pointer_info->window_handle));
      }
      else
      {
        assert((get_scroll_state(pointer_info->window_handle) & (Wimp_AutoScroll_Vertical|Wimp_AutoScroll_Horizontal)) == Wimp_AutoScroll_Vertical);
      }
    }

    /* Send data to the claimant */
    assert(estimated_size);
    err = send_data_core(file_types[ftype_idx], estimated_size, pointer_info, method, drag_claim.hdr.my_ref);
  }
  else
  {
    assert((err != NULL) || !estimated_size);
  }

  /* When a drag terminates, auto-scrolling should be disabled */
  if (err == NULL)
    assert(!get_scroll_state(pointer_info->window_handle));

  return err;
}

static const _kernel_oserror *paste_internal_core(int const file_types[], unsigned int ftype_idx, int estimated_size, ObjectId id, DataTransferMethod method)
{
  WimpPollBlock poll_block;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock pointer_info;
  init_pointer_info_for_win(&pointer_info, id, 0, 0);

  err_suppress_errors();

  init_custom_event(&poll_block, EventCode_Paste);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  err = err_dump_suppressed();
  if (err == NULL)
  {
    WimpMessage data_request;
    assert(check_data_request_msg(&data_request, pointer_info.window_handle));

    if (file_types != NULL)
    {
      /* Send data to the claimant */
      err = send_data_core(file_types[ftype_idx], estimated_size, &pointer_info, method, data_request.hdr.my_ref);
    }
    else
    {
      /* Instead of sending a DataSave message to continue the protocol,
         fake the return of the data request message. */
      poll_block.user_message_acknowledge = data_request;

      err_suppress_errors();
      dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
      err = err_dump_suppressed();
    }
  }

  return err;
}

static void test14(void)
{
  /* Drag claimable CSV file to icon */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_CSV, FileType_Null };
  const _kernel_oserror *err = NULL;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);

  unsigned long limit = 0;
  do
  {
    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);

    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_RAM, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
    if (id != NULL_ObjectId)
    {
      assert(!path_is_in_userdata(TEST_DATA_IN));
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(get_created_window("EditColmap"));
      check_object_cols_file(TEST_DATA_OUT, colour_csv);
    }
    else
    {
      assert(err != NULL);
    }

    Fortify_LeaveScope();
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test15(void)
{
  /* Drag claimable object colours file to icon */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_Fednet, FileType_Null };
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  int const estimated_size = make_object_cols_file(TEST_DATA_IN, colour_identity);
  unsigned long limit = 0;
  do
  {
    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_File, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
    if (id != NULL_ObjectId)
    {
      assert(!path_is_in_userdata(TEST_DATA_IN));
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(get_created_window("EditColmap"));
      check_object_cols_file(TEST_DATA_OUT, colour_identity);
    }
    else
    {
      assert(err != NULL);
    }

    Fortify_LeaveScope();
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test16(void)
{
  /* Drag claimable unsupported types to icon */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_Null };
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  unsigned long limit = 0;
  do
  {
    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 0, 0, &drag_dest, DTM_RAM, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    Fortify_LeaveScope();
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test17(void)
{
  /* Drag unclaimable CSV file to icon */
  static int const file_types[] = { FileType_CSV, FileType_Null };
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  unsigned long limit = 0;
  do
  {
    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 0, 0, &drag_dest, DTM_RAM, Wimp_MDragging_DoNotClaimMessage);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    Fortify_LeaveScope();
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test18(void)
{
  /* Double-click object colours file  */
  make_object_cols_file(TEST_DATA_IN, colour_identity);
  double_click(FileType_Fednet, false);

  /* No editing window should have been created */
  ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
  assert(id == NULL_ObjectId);

  Fortify_LeaveScope();
}

static void test19(void)
{
  /* Double-click CSV file  */
  make_csv_file(TEST_DATA_IN, colour_csv);
  double_click(FileType_CSV, false);

  /* No editing window should have been created */
  ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
  assert(id == NULL_ObjectId);

  Fortify_LeaveScope();
}

static void test20(void)
{
  /* Create new object colours file */
  WimpPollBlock poll_block;
  const _kernel_oserror *err;
  unsigned long limit = 0;
  do
  {
    err_suppress_errors();
    Fortify_EnterScope();

    init_custom_event(&poll_block, EventCode_CreateObjColours);
    init_id_block(pseudo_event_get_client_id_block(), pseudo_toolbox_find_by_template_name("CreateFile"), 1);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    check_and_dispatch_caret_claim();
    err = err_dump_suppressed();

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
    if (id != NULL_ObjectId)
    {
      assert(object_is_on_menu(id));
      assert(pseudo_toolbox_object_is_showing(id));
      assert(userdata_count_unsafe() == 0);
      close_window(id);
    }
    else
    {
      assert(err != NULL);
    }
    Fortify_LeaveScope();
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static ObjectId create_window(int event_code)
{
  WimpPollBlock poll_block;

  /* Simulate Select button click on iconbar icon to create a window */
  init_custom_event(&poll_block, event_code);
  init_id_block(pseudo_event_get_client_id_block(), pseudo_toolbox_find_by_template_name("CreateFile"), 1);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  check_and_dispatch_caret_claim();

  char *templ;
  if (event_code == EventCode_CreateObjColours)
    templ = "EditColmap";
  else
    templ = "EditHills";
  return get_created_window(templ);
}

static void test21(void)
{
  /* Bring windows to the front */
  ObjectId const iconbar_id = pseudo_toolbox_find_by_template_name("Iconbar");
  for (unsigned int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    WimpPollBlock poll_block;
    const _kernel_oserror *err;
    unsigned long limit;

    Fortify_EnterScope();

    for (unsigned int w = 0; w < nwin; ++w)
      create_window(EventCode_CreateObjColours);

    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      err_suppress_errors();
      Fortify_EnterScope();

      /* Simulate click on iconbar icon to bring windows to front */
      init_custom_event(&poll_block, EventCode_WindowsToFront);
      init_id_block(pseudo_event_get_client_id_block(), iconbar_id, NULL_ComponentId);
      dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

      Fortify_LeaveScope();
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    /* Close the editing windows created earlier */
    for (unsigned int w = 0; w < nwin; ++w)
      close_window(pseudo_toolbox_find_by_template_name("EditColmap"));

    Fortify_LeaveScope();
  }
}

static void test22(void)
{
  /* Quicksave no path */
  WimpPollBlock poll_block;
  unsigned long limit;
  const _kernel_oserror *err;

  ObjectId const id = create_window(EventCode_CreateObjColours);
  assert(userdata_count_unsafe() == 0);
  setup_selection(id);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_EnterScope();

    /* Simulate a quicksave */
    init_custom_event(&poll_block, EventCode_QuickSave);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    Fortify_LeaveScope();
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 1);
  activate_savebox(pseudo_toolbox_find_by_template_name("SaveFile"), SaveAs_DestinationSafe, DTM_File);
  assert(userdata_count_unsafe() == 0);

  assert_file_has_type(TEST_DATA_OUT, FileType_Fednet);
  check_object_cols_file(TEST_DATA_OUT, colour_edited);

  close_window(id);
  Fortify_LeaveScope();
}

static void test23(void)
{
  /* Quicksave with path */
  WimpPollBlock poll_block;
  const _kernel_oserror *err;
  unsigned long limit;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  make_object_cols_file(TEST_DATA_IN, colour_identity);
  init_data_load_msg(&poll_block, TEST_DATA_IN, UnsafeDataSize, FileType_Fednet, &drag_dest, 0);
  dispatch_event(Wimp_EUserMessage, &poll_block);

  ObjectId const id = get_created_window("EditColmap");

  check_and_dispatch_caret_claim();

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    /* Recording the new file path can allocate memory so no enter-scope here */

    /* Simulate a quicksave */
    init_custom_event(&poll_block, EventCode_QuickSave);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* The savebox should have not have been shown. */
  assert(!pseudo_toolbox_object_is_showing(pseudo_toolbox_find_by_template_name("SaveFile")));

  assert_file_has_type(TEST_DATA_IN, FileType_Fednet);
  check_object_cols_file(TEST_DATA_IN, colour_identity);

  close_window(id);
  Fortify_LeaveScope();
}

static void test24(void)
{
  /* Save empty object colours file */
  ObjectId const id = create_window(EventCode_CreateObjColours);

  assert(userdata_count_unsafe() == 0);
  save_file(id, SaveAs_DestinationSafe, DTM_File);
  assert(userdata_count_unsafe() == 0);

  close_window(id);
  Fortify_LeaveScope();

  assert_file_has_type(TEST_DATA_OUT, FileType_Fednet);
  check_object_cols_file(TEST_DATA_OUT, colour_black);
}

static void test26(void)
{
  /* DCS save no path */
  unsigned long limit;
  WimpPollBlock poll_block;
  const _kernel_oserror *err;
  ObjectId const id = create_window(EventCode_CreateObjColours);

  assert(userdata_count_unsafe() == 0);
  setup_selection(id);

  ObjectId const dcs_id = pseudo_toolbox_find_by_template_name("DCS");
  assert(!pseudo_toolbox_object_is_showing(dcs_id));

  close_window(id);

  /* Discard/Cancel/Save dialogue should have been shown.
     Editing window should remain open. */
  assert(pseudo_toolbox_object_is_showing(id));
  assert(pseudo_toolbox_object_is_showing(dcs_id));
  assert(userdata_count_unsafe() == 1);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_EnterScope();

    /* Choose 'save' in the Discard/Cancel/Save dialogue */
    init_dcs_save_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), dcs_id, 0x82a803);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    Fortify_LeaveScope();
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Editing window should remain open. */
  assert(pseudo_toolbox_object_is_showing(id));

  assert(userdata_count_unsafe() == 1);
  activate_savebox(pseudo_toolbox_find_by_template_name("SaveFile"), SaveAs_DestinationSafe, DTM_File);
  Fortify_LeaveScope();

  /* Editing window should have been deleted. */
  assert(userdata_count_unsafe() == 0);

  assert_file_has_type(TEST_DATA_OUT, FileType_Fednet);
  check_object_cols_file(TEST_DATA_OUT, colour_edited);
}

static void test27(void)
{
  /* DCS save with path */
  unsigned long limit;
  WimpPollBlock poll_block;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  make_object_cols_file(TEST_DATA_IN, colour_identity);
  init_data_load_msg(&poll_block, TEST_DATA_IN, UnsafeDataSize, FileType_Fednet, &drag_dest, 0);
  dispatch_event(Wimp_EUserMessage, &poll_block);

  ObjectId const id = get_created_window("EditColmap");

  check_and_dispatch_caret_claim();

  assert(userdata_count_unsafe() == 0);
  setup_selection(id);

  ObjectId const dcs_id = pseudo_toolbox_find_by_template_name("DCS");
  assert(!pseudo_toolbox_object_is_showing(dcs_id));

  close_window(id);

  /* Discard/Cancel/Save dialogue should have been shown.
     Editing window should remain open. */
  assert(pseudo_toolbox_object_is_showing(id));
  assert(pseudo_toolbox_object_is_showing(dcs_id));
  assert(userdata_count_unsafe() == 1);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    /* Recording the new file path can allocate memory so no enter-scope here */

    /* Choose 'save' in the Discard/Cancel/Save dialogue */
    init_dcs_save_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), dcs_id, 0x82a803);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    err = err_dump_suppressed();
    if (err == NULL)
    {
      /* Check that the save was successful. */
      assert(userdata_count_unsafe() == 0);
    }

    /* Releasing the clipboard upon deleting an editing window can cause an
       error to be suppressed but the window is deleted anyway. */
    if (err == NULL || pseudo_toolbox_find_by_template_name("EditColmap") == NULL_ObjectId)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* The savebox should have not have been shown. */
  assert(!pseudo_toolbox_object_is_showing(pseudo_toolbox_find_by_template_name("SaveFile")));

  assert_file_has_type(TEST_DATA_IN, FileType_Fednet);
  check_object_cols_file(TEST_DATA_IN, colour_edited);
}

static void test28(void)
{
  /* DCS cancel */
  unsigned long limit;
  WimpPollBlock poll_block;
  const _kernel_oserror *err;

  ObjectId const id = create_window(EventCode_CreateObjColours);

  assert(userdata_count_unsafe() == 0);
  select_all(id);
  set_colour(id, NonSelectionColour);
  assert(userdata_count_unsafe() == 1);

  ObjectId const dcs_id = pseudo_toolbox_find_by_template_name("DCS");
  assert(!pseudo_toolbox_object_is_showing(dcs_id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    close_window(id);
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Discard/Cancel/Save dialogue should have been shown.
     Editing window should remain open. */
  assert(pseudo_toolbox_object_is_showing(id));
  assert(pseudo_toolbox_object_is_showing(dcs_id));
  assert(userdata_count_unsafe() == 1);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_EnterScope();

    /* Choose 'cancel' in the Discard/Cancel/Save dialogue */
    init_dcs_cancel_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), dcs_id, 0x82a802);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    Fortify_LeaveScope();
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Save dialogue should not have been shown.
     Editing window should remain open. */
  assert(pseudo_toolbox_object_is_showing(id));
  assert(!pseudo_toolbox_object_is_showing(pseudo_toolbox_find_by_template_name("SaveFile")));
  assert(userdata_count_unsafe() == 1);

  /* Finally we must discard the changes anyway */
  close_and_discard(id);
  Fortify_LeaveScope();
}

static void quit_with_cancel_core(bool desktop_shutdown, bool is_risc_os_3)
{
  ObjectId const prequit_id = pseudo_toolbox_find_by_template_name("PreQuit");
  for (unsigned int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    WimpPollBlock poll_block;
    const _kernel_oserror *err;
    unsigned long limit;
    int prequit_ref = 0;

    pseudo_toolbox_reset();
    Fortify_EnterScope();

    for (unsigned int w = 0; w < nwin; ++w)
    {
      ObjectId const id = create_window(EventCode_CreateObjColours);

      assert(userdata_count_unsafe() == w);
      select_all(id);
      set_colour(id, NonSelectionColour);
      assert(userdata_count_unsafe() == w+1);
    }

    assert(!pseudo_toolbox_object_is_showing(prequit_id));

    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      err_suppress_errors();
      Fortify_EnterScope();

      /* Try to quit the application */
      prequit_ref = init_pre_quit_msg(&poll_block, desktop_shutdown, is_risc_os_3);
      dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit /* wait for about-to-be-shown */);

      Fortify_LeaveScope();
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    if (nwin)
    {
      /* Pre-quit dialogue should have been shown
         and the pre-quit message should have been acknowledged. */
      assert(pseudo_toolbox_object_is_showing(prequit_id));
      assert(check_pre_quit_ack_msg(prequit_ref, &poll_block.user_message));

      for (limit = 0; limit < FortifyAllocationLimit; ++limit)
      {
        err_suppress_errors();
        Fortify_EnterScope();

        /* Choose 'cancel' in the Pre-quit dialogue */
        init_quit_cancel_event(&poll_block);
        init_id_block(pseudo_event_get_client_id_block(), prequit_id, 0x82a901);
        dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

        Fortify_LeaveScope();
        err = err_dump_suppressed();
        if (err == NULL)
          break;
      }
      assert(limit != FortifyAllocationLimit);
    }
    else
    {
      /* Pre-quit dialogue should not have been shown
         and the quit message should have been ignored. */
      assert(!pseudo_toolbox_object_is_showing(prequit_id));
      assert(pseudo_wimp_get_message_count() == 0);
    }

    /* Close the editing windows created earlier */
    for (unsigned int w = 0; w < nwin; ++w)
    {
      ObjectId const id = pseudo_toolbox_find_by_template_name("EditColmap");
      assert(pseudo_toolbox_object_is_showing(id));
      assert(userdata_count_unsafe() == nwin - w);
      close_and_discard(id);
    }

    Fortify_LeaveScope();
  }
}

static void test29(void)
{
  /* Quit from task manager with cancel */
  quit_with_cancel_core(false, true /* must be OS 3 to do single task quit */);
}

static void test30(void)
{
  /* Shutdown from task manager with cancel */
  quit_with_cancel_core(true, false);
  quit_with_cancel_core(true, true);
}

static void quit_with_confirm_core(bool desktop_shutdown, bool is_risc_os_3)
{
  ObjectId const prequit_id = pseudo_toolbox_find_by_template_name("PreQuit");
  for (unsigned int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    WimpPollBlock poll_block;
    const _kernel_oserror *err;
    unsigned long limit;
    int prequit_ref = 0;

    pseudo_toolbox_reset();
    Fortify_EnterScope();

    for (unsigned int w = 0; w < nwin; ++w)
    {
      ObjectId const id = create_window(EventCode_CreateObjColours);

      assert(userdata_count_unsafe() == w);
      select_all(id);
      set_colour(id, NonSelectionColour);
      assert(userdata_count_unsafe() == w+1);
    }

    assert(!pseudo_toolbox_object_is_showing(prequit_id));

    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      err_suppress_errors();
      Fortify_EnterScope();

      /* Try to quit the application */
      prequit_ref = init_pre_quit_msg(&poll_block, desktop_shutdown, is_risc_os_3);
      dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit /* wait for about-to-be-shown */);

      Fortify_LeaveScope();
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    if (nwin)
    {
      jmp_buf exit_target;

      /* Pre-quit dialogue should have been shown
         and the pre-quit message should have been acknowledged. */
      assert(pseudo_toolbox_object_is_showing(prequit_id));
      assert(check_pre_quit_ack_msg(prequit_ref, &poll_block.user_message));

      for (limit = 0; limit < FortifyAllocationLimit; ++limit)
      {
        err_suppress_errors();
        Fortify_EnterScope();

        int status = setjmp(exit_target);
        if (status == 0)
        {
          /* Jump target has been set up */
          pseudo_exit_set_target(exit_target);

          /* Choose 'Quit' in the Pre-quit dialogue */
          init_quit_quit_event(&poll_block);
          init_id_block(pseudo_event_get_client_id_block(), prequit_id, 0x82a902);
          dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

          err = err_dump_suppressed();

          /* In the case of desktop shutdown we expect a keypress to restart the
             shutdown to have been sent, instead of exiting. Otherwise the only
             valid reason for not exiting is an error. */
          assert(desktop_shutdown || err != NULL);
        }
        else
        {
          /* The exit function returned via setjmp */
          Fortify_SetNumAllocationsLimit(ULONG_MAX);

          assert(!desktop_shutdown);
          status--; /* 0 has a special meaning */
          assert(status == EXIT_SUCCESS);
          err = err_dump_suppressed();
        }

        Fortify_LeaveScope();
        if (err == NULL)
          break;
      }
      assert(limit != FortifyAllocationLimit);

      if (desktop_shutdown)
        check_key_pressed_msg(0x1FC);
    }
    else
    {
      /* Pre-quit dialogue should not have been shown
         and the quit message should have been ignored. */
      assert(!pseudo_toolbox_object_is_showing(prequit_id));
      assert(pseudo_wimp_get_message_count() == 0);
    }

    /* The editing windows created earlier should have been closed */
    assert(userdata_count_unsafe() == 0);

    Fortify_LeaveScope();
  }
}

static void test31(void)
{
  /* Quit from task manager with confirm */
  quit_with_confirm_core(false, true /* must be OS 3 to do single task quit */);
}

static void test32(void)
{
  /* Shutdown from task manager with confirm */
  quit_with_confirm_core(true, false);
  quit_with_confirm_core(true, true);
}

static void test33(void)
{
  /* Drag claimable CSV file to window */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_CSV, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);

  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);

  unsigned long limit = 0;
  do
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    reset_scroll_state(drag_dest.window_handle);
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);

    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_RAM, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
    {
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(id);
      check_object_cols_file(TEST_DATA_OUT, colour_dropped_csv);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test34(void)
{
  /* Drag claimable CSV file to selection */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_CSV, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, SelectionStart, 0);

  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);

  unsigned long limit = 0;
  do
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    reset_scroll_state(drag_dest.window_handle);
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);
    mouse_select(id, SelectionStart, SelectionEnd);

    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_RAM, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
    {
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(id);
      check_object_cols_file(TEST_DATA_OUT, colour_dropped_csv_on_sel);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test35(void)
{
  /* Drag claimable object colours file to window */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_Fednet, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);

  int const estimated_size = make_object_cols_file(TEST_DATA_IN, colour_identity);

  unsigned long limit = 0;
  do
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    reset_scroll_state(drag_dest.window_handle);
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);

    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest,
                               DTM_File, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
    {
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(id);
      check_object_cols_file(TEST_DATA_OUT, colour_dropped_cols);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test36(void)
{
  /* Drag claimable unsupported types to window */
  static int const file_types[] = { FileType_Sprite, FileType_Obey, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);

  unsigned long limit = 0;
  do
  {
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 0, 0, &drag_dest, DTM_RAM, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 0);
  close_window(id);
}

static void test37(void)
{
  /* Drag unclaimable CSV file to window */
  static int const file_types[] = { FileType_CSV, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);

  unsigned long limit = 0;
  do
  {
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 0, 0, &drag_dest, DTM_RAM, Wimp_MDragging_DoNotClaimMessage);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 0);
  close_window(id);
}

static void test38(void)
{
  /* Paste CSV */
  static int const file_types[] = { FileType_Text, FileType_CSV, FileType_Fednet, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  unsigned long limit;
  const _kernel_oserror *err;

  /* The receiver prefers CSV, so we don't expect to have to send a object colours file */
  int const estimated_size = make_csv_file(TEST_DATA_IN, colour_identity);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);
    mouse_select(id, DropPosition, DropPosition+1);

    Fortify_SetNumAllocationsLimit(limit);
    err = paste_internal_core(file_types, 1, estimated_size, id, DTM_RAM);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_dropped_csv);
}

static void test39(void)
{
  /* Paste object colours */
  static int const file_types[] = { FileType_Fednet, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  unsigned long limit;
  int const estimated_size = make_object_cols_file(TEST_DATA_IN, colour_identity);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);
    mouse_select(id, DropPosition, DropPosition+1);

    Fortify_SetNumAllocationsLimit(limit);
    const _kernel_oserror * const err =
      paste_internal_core(file_types, 0, estimated_size, id, DTM_File);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_dropped_cols);
}

static void test40(void)
{
  /* Paste empty clipboard */
  ObjectId const id = create_window(EventCode_CreateObjColours);
  unsigned long limit;

  mouse_select(id, DropPosition, DropPosition+1);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    const _kernel_oserror * const err =
      paste_internal_core(NULL, 0, 0, id, DTM_RAM);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(err != NULL);
    assert(err->errnum == DUMMY_ERRNO);
    if (!strcmp(err->errmess, msgs_lookup("Entity2NoData")))
      break;
  }
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 0);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_black);
}

static void check_not_sent(int action_code)
{
  unsigned int count = pseudo_wimp_get_message_count();
  while (count-- > 0)
  {
    int code;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, NULL, NULL);
    if (code == Wimp_EUserMessage || code == Wimp_EUserMessageRecorded)
    {
      assert(poll_block.user_message.hdr.action_code != action_code);
    }
  }
}

static const _kernel_oserror *check_aborted_drag(int dc_ref, int dc_handle, WimpGetPointerInfoBlock *pointer_info)
{
  const _kernel_oserror *err = NULL;
  int old_dc_ref;

  do
  {
    WimpMessage dragging;
    int code;
    WimpPollBlock poll_block;

    /* No DataSave message should be sent when a drag is aborted. */
    check_not_sent(Wimp_MDataSave);

    /* Two unclaimable Dragging messages should be sent when a drag is aborted. */
    assert(check_dragging_msg(dc_ref, dc_handle, pointer_info, &dragging, &code));
    assert(code == Wimp_EUserMessageRecorded);

    const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;
    assert(d->flags == (Wimp_MDragging_DataFromSelection | Wimp_MDragging_DoNotClaimMessage));

    /* If the app has previously claimed its own drag then deliver the
       unclaimable Dragging message to ensure that it cleans up. */
    if (dc_handle == th)
    {
      err_suppress_errors();
      poll_block.user_message_recorded = dragging;
      dispatch_event(Wimp_EUserMessageRecorded, &poll_block);
      err = err_dump_suppressed();

      check_not_sent(Wimp_MDragClaim);

      /* When a drag terminates, auto-scrolling should be disabled */
      if (err == NULL)
        assert(!get_scroll_state(pointer_info->window_handle));
    }

    /* Fake the return of the Dragging message to the saver. */
    err_suppress_errors();
    poll_block.user_message_acknowledge = dragging;
    dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
    err = err_dump_suppressed();

    if (err != NULL)
      break;

    /* If the drag was previously claimed then a final message is sent to the
       window/icon at the pointer. */
    old_dc_ref = dc_ref;
    dc_handle = 0;
    dc_ref = 0;
  }
  while (old_dc_ref != 0);

  /* No DataSave message or further Dragging messages should be sent after the
     two unclaimable Dragging messages. */
  check_not_sent(Wimp_MDataSave);
  check_not_sent(Wimp_MDragging);

  return err;
}

static void test41(void)
{
  /* Drag selection then abort unclaimed drag */
  unsigned long limit;
  const _kernel_oserror *err;

  ObjectId const id = create_window(EventCode_CreateObjColours);
  select_all(id);

  /* Get the expected dragging message content. */
  WimpGetPointerInfoBlock pointer_info;
  init_pointer_info_for_win(&pointer_info, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&pointer_info);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_SetNumAllocationsLimit(limit);
    mouse_drag(id, 0);
    assert(userdata_count_unsafe() == 0);

    /* The drag may have started (and therefore need to be aborted)
       even if an error occurred. */
    abort_drag(id);

    err = err_dump_suppressed();
    if (err == NULL)
      err = check_aborted_drag(0, 0, &pointer_info);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  close_window(id);
}

static void test42(void)
{
  /* Drag selection then close window */
  ObjectId const id = create_window(EventCode_CreateObjColours);
  select_all(id);

  mouse_drag(id, 0);

  /* Get the expected dragging message content. */
  WimpGetPointerInfoBlock pointer_info;
  init_pointer_info_for_win(&pointer_info, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&pointer_info);

  close_window(id);

  /* Closing the window should abort the drag. */
  check_aborted_drag(0, 0, &pointer_info);
}

static void test43(void)
{
  /* Drag selection then claim and release drag */
  ObjectId const id = create_window(EventCode_CreateObjColours);
  select_all(id);

  /* Initially there's no DragClaim message for the app to reply to. */
  int dc_ref = 0, dc_handle = 0;
  int const flags[] =
  {
    Wimp_MDragClaim_PtrShapeChanged, /* no action */
    0, /* reset pointer shape */
    0, /* no action */
    Wimp_MDragClaim_RemoveDragBox, /* hide dragbox */
    Wimp_MDragClaim_RemoveDragBox, /* no action */
    0, /* show dragbox */
    0, /* no action */
    Wimp_MDragClaim_PtrShapeChanged|Wimp_MDragClaim_RemoveDragBox /* hide dragbox */
  };
  WimpMessage dragging;
  int code;
  WimpPollBlock poll_block;
  const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;

  WimpGetPointerInfoBlock pointer_info;
  init_pointer_info_for_win(&pointer_info, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&pointer_info);

  mouse_drag(id, 0);

  for (size_t i = 0; i <= ARRAY_SIZE(flags); ++i)
  {
    unsigned long limit;
    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      const _kernel_oserror *err;

      wait(DragMsgInterval);

      /* Simulate a null event to trigger a dragging message. */
      err_suppress_errors();
      dispatch_event_suppress_with_error_sim(Wimp_ENull, NULL, limit);
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    /* Check that a claimable dragging message was sent. */
    assert(check_dragging_msg(dc_ref, dc_handle, &pointer_info, &dragging, &code));
    if (dc_ref)
      assert(code == Wimp_EUserMessageRecorded);
    else
      assert(code == Wimp_EUserMessage);

    assert(d->flags == Wimp_MDragging_DataFromSelection);

    if (i < ARRAY_SIZE(flags))
    {
      /* Claim the drag. */
      int const file_types[] = { FileType_Null };
      dc_ref = init_drag_claim_msg(&poll_block, flags[i], file_types,
                                   dragging.hdr.my_ref);
      dc_handle = ForeignTaskHandle;
      dispatch_event_suppress(Wimp_EUserMessageRecorded, &poll_block);

      /* Drag isn't finished */
      check_not_sent(Wimp_MDataSave);
    }
    else
    {
      break;
    }
  }

  /* Fake the return of the Dragging message to the saver. */
  poll_block.user_message_acknowledge = dragging;
  dispatch_event_suppress(Wimp_EUserMessageAcknowledge, &poll_block);

  /* Drag isn't finished */
  check_not_sent(Wimp_MDataSave);

  /* Check that a claimable dragging message was sent. */
  assert(check_dragging_msg(0, 0, &pointer_info, &dragging, &code));
  assert(code == Wimp_EUserMessage);
  assert(d->flags == Wimp_MDragging_DataFromSelection);

  abort_drag(id);
  check_aborted_drag(0, 0, &pointer_info);

  close_window(id);
}

static void test44(void)
{
  /* Drag selection then abort claimed drag */
  unsigned long limit;
  const _kernel_oserror *err;

  ObjectId const id = create_window(EventCode_CreateObjColours);
  select_all(id);

  /* Get the expected dragging message content. */
  WimpGetPointerInfoBlock pointer_info;
  init_pointer_info_for_win(&pointer_info, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&pointer_info);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    int dc_ref = 0;
    err_suppress_errors();
    Fortify_SetNumAllocationsLimit(limit);

    mouse_drag(id, 0);

    err = err_dump_suppressed();
    if (err == NULL)
    {
      err_suppress_errors();

      /* Simulate a null event to trigger a dragging message. */
      wait(DragMsgInterval);
      dispatch_event_suppress(Wimp_ENull, NULL);

      err = err_dump_suppressed();
    }

    if (err == NULL)
    {
      /* Check that a claimable dragging message was sent. */
      WimpMessage dragging;
      int code;
      const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;

      assert(check_dragging_msg(0, 0, &pointer_info, &dragging, &code));
      assert(code == Wimp_EUserMessage);
      assert(d->flags == Wimp_MDragging_DataFromSelection);

      err_suppress_errors();

      /* Claim the drag. */
      int const file_types[] = { FileType_Null };
      WimpPollBlock poll_block;
      dc_ref = init_drag_claim_msg(&poll_block, 0, file_types, dragging.hdr.my_ref);
      dispatch_event_suppress(Wimp_EUserMessageRecorded, &poll_block);

      err = err_dump_suppressed();
    }

    if (err == NULL)
    {
      err_suppress_errors();

      abort_drag(id);
      err = err_dump_suppressed();
    }

    if (err == NULL)
      err = check_aborted_drag(dc_ref, ForeignTaskHandle, &pointer_info);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  close_window(id);
}

static void test45(void)
{
  /* Drag unclaimed selection to source window */
  unsigned long limit;
  const _kernel_oserror *err;
  ObjectId const id = create_window(EventCode_CreateObjColours);

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&drag_dest);

  assert(userdata_count_unsafe() == 0);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;

    setup_selection(id);

    Fortify_SetNumAllocationsLimit(limit);

    err_suppress_errors();
    mouse_drag(id, SelectionStart);
    err = err_dump_suppressed();

    if (err == NULL)
    {
      err_suppress_errors();
      mouse_drop(drag_dest.x, drag_dest.y);
      err = err_dump_suppressed();
    }

    if (err == NULL)
    {
      WimpMessage dragging;
      int code;

      /* A Dragging message should be sent at the end of a drag. */
      assert(check_dragging_msg(0, 0, &drag_dest, &dragging, &code));
      assert(code == Wimp_EUserMessageRecorded);
      const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;
      assert(d->flags == Wimp_MDragging_DataFromSelection);

      /* Fake the return of the Dragging message to the saver. */
      err_suppress_errors();
      poll_block.user_message_acknowledge = dragging;
      dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
      err = err_dump_suppressed();
    }
    else
    {
      /* Clean up after a failed drag */
      err_suppress_errors();
      abort_drag(id);
      err_dump_suppressed();
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* No datasave message should be sent if a drag terminates within its
       source window. */
    check_not_sent(Wimp_MDataSave);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* The app should have moved the selection from its initial position
     although this test doesn't dispatch a dragging message (because the
     app doesn't draw a ghost caret). */
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_edited_dragged);
}

static void test46(void)
{
  /* Drag claimed selection to source window */
  unsigned long limit;
  const _kernel_oserror *err;
  ObjectId const id = create_window(EventCode_CreateObjColours);

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);
  pseudo_wimp_set_pointer_info(&drag_dest);

  assert(userdata_count_unsafe() == 0);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;

    reset_scroll_state(drag_dest.window_handle);
    setup_selection(id);

    Fortify_SetNumAllocationsLimit(limit);

    err_suppress_errors();
    mouse_drag(id, SelectionStart);
    err = err_dump_suppressed();

    if (err == NULL)
    {
      err_suppress_errors();
      mouse_drop(drag_dest.x, drag_dest.y);
      err = err_dump_suppressed();
    }
    else
    {
      /* Clean up after a failed drag */
      err_suppress_errors();
      abort_drag(id);
      err_dump_suppressed();
    }

    /* No datasave message should be sent before the destination task
       has claimed the drag. */
    check_not_sent(Wimp_MDataSave);

    /* A Dragging message should be sent at the end of a drag. */
    WimpMessage dragging;
    int code;
    if (check_dragging_msg(0, 0, &drag_dest, &dragging, &code))
    {
      assert(code == Wimp_EUserMessageRecorded);
      const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;
      assert(d->flags == Wimp_MDragging_DataFromSelection);

      /* Before a drag is claimed, auto-scrolling should be disabled */
      assert(!get_scroll_state(drag_dest.window_handle));

      /* Dispatch the dragging message to ensure that the ghost caret
         position is set in the source/destination window. */
      err_suppress_errors();
      poll_block.user_message_recorded = dragging;
      dispatch_event(code, &poll_block);
      err = err_dump_suppressed();

      /* No datasave message should be sent before the destination task
         has claimed the drag. */
      check_not_sent(Wimp_MDataSave);

      /* The app should have claimed its own drag. */
      WimpMessage drag_claim;
      if (check_drag_claim_msg(poll_block.user_message.hdr.my_ref, th, &drag_claim, true))
      {
        /* Whilst a drag is claimed, auto-scrolling should be enabled */
        if (err == NULL)
          assert((get_scroll_state(drag_dest.window_handle) & (Wimp_AutoScroll_Vertical|Wimp_AutoScroll_Horizontal)) == Wimp_AutoScroll_Vertical);

        /* Dispatch the dragclaim message to complete the drag. */
        err_suppress_errors();
        poll_block.user_message = drag_claim;
        dispatch_event(Wimp_EUserMessage, &poll_block);
        err = err_dump_suppressed();
      }
      else
      {
        /* If the drag was not claimed then it must be because an error occurred. */
        assert(err != NULL);

        /* Fake the return of the Dragging message to the saver. */
        err_suppress_errors();
        poll_block.user_message_acknowledge = dragging;
        dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
        MERGE_ERR(err, err_dump_suppressed());
      }
    }
    else
    {
      /* If the dragging message was not sent then it must be because an error occurred. */
      assert(err != NULL);
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* The app should have moved the selection from its initial position. */
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_edited_dragged);
}

static void drag_selection_core(int const *file_types, int file_type, DataTransferMethod method)
{
  unsigned long limit;
  const _kernel_oserror *err;
  char leaf_name[256] = "";
  ObjectId const id = create_window(EventCode_CreateObjColours);

  strncat(leaf_name, msgs_lookup("LeafName"), sizeof(leaf_name)-1);

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_foreign(&drag_dest);
  pseudo_wimp_set_pointer_info(&drag_dest);

  assert(userdata_count_unsafe() == 0);

  int const estimated_size = estimate_file_size(file_type, colour_selection, SelectionEnd - SelectionStart);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;

    setup_selection(id);

    Fortify_SetNumAllocationsLimit(limit);

    err_suppress_errors();
    mouse_drag(id, SelectionStart);
    err = err_dump_suppressed();

    if (err == NULL)
    {
      err_suppress_errors();
      mouse_drop(drag_dest.x, drag_dest.y);
      err = err_dump_suppressed();
    }
    else
    {
      /* Clean up after a failed drag */
      err_suppress_errors();
      abort_drag(id);
      err_dump_suppressed();
    }

    /* No datasave message should be sent before the destination task
       has claimed the drag. */
    check_not_sent(Wimp_MDataSave);

    /* A Dragging message should be sent at the end of a drag. */
    WimpMessage dragging;
    int code;
    if (check_dragging_msg(0, 0, &drag_dest, &dragging, &code))
    {
      assert(code == Wimp_EUserMessageRecorded);
      const WimpDraggingMessage *const d = (WimpDraggingMessage *)dragging.data.bytes;
      assert(d->flags == Wimp_MDragging_DataFromSelection);

      err_suppress_errors();

      int dc_ref, dc_handle;
      if (file_types != NULL)
      {
        /* Claim the drag */
        dc_ref = init_drag_claim_msg(&poll_block, 0, file_types, dragging.hdr.my_ref);
        dc_handle = ForeignTaskHandle;
        dispatch_event(Wimp_EUserMessageRecorded, &poll_block);
      }
      else
      {
        /* Fake the return of the Dragging message to the saver. */
        dc_ref = 0;
        dc_handle = 0;
        poll_block.user_message_acknowledge = dragging;
        dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
      }
      err = err_dump_suppressed();

      /* A DataSave message should have been sent to the drag destination. */
      WimpMessage data_save;
      if (check_data_save_msg(dc_ref, dc_handle, leaf_name, &data_save, &drag_dest))
      {
        assert(data_save.data.data_save.file_type == file_type);
        assert(data_save.data.data_save.estimated_size == estimated_size);

        /* Reply with a DataSaveAck message from the drag destination. */
        err = rec_data_core(&data_save, method);
      }
      else
      {
        /* If the datasave message was not sent then it must be because an error occurred. */
        assert(err != NULL);
      }
    }
    else
    {
      /* If the dragging message was not sent then it must be because an error occurred. */
      assert(err != NULL);
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if ((method == DTM_BadFile) || (method == DTM_BadRAM))
    {
      assert(err != NULL);
      if (strstr(err->errmess, msgs_lookup("RecDied")))
        break;
    }

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Dragging out a selection should not make an unsafe file safe
     nor change its file name. */
  assert(!path_is_in_userdata(TEST_DATA_OUT));
  assert(userdata_count_unsafe() == 1);

  if (method == DTM_None)
  {
    /* We do not expect the selection to have been saved */
    assert(fopen(TEST_DATA_OUT, "rb") == NULL);
  }
  else if ((method == DTM_RAM) || (method == DTM_File))
  {
    /* Check that the selection was saved correctly */
    if (method == DTM_File)
      assert_file_has_type(TEST_DATA_OUT, file_type);
    check_out_file(file_type, colour_selection, SelectionEnd - SelectionStart);
  }

  /* Unless the shift key is held, dragging a selection outside the
     source window should not move it. */
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_edited);
}

static void test47(void)
{
  /* Drag unclaimed selection to app */

  /* An unclaimed drag should end by sending the default export filetype */
  drag_selection_core(NULL, FileType_CSV, DTM_RAM);
}

static void test48(void)
{
  /* Drag claimed selection to app with no type */

  /* A drag claimant that specifies no filetype should receive the default export filetype */
  int const file_types[] = { FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_RAM);
}

static void test49(void)
{
  /* Drag claimed selection to app with unsupported types */

  /* A drag claimant that specifies no matching filetype should receive the default export filetype */
  int const file_types[] = { FileType_Squash, FileType_Sprite, FileType_Obey, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_RAM);
}

static void test50(void)
{
  /* Drag claimed selection to app as data */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Data, FileType_CSV, FileType_Squash, FileType_Text, FileType_Null };
  drag_selection_core(file_types, FileType_Data, DTM_RAM);
}

static void test51(void)
{
  /* Drag claimed selection to app as text */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Text, FileType_Sprite, FileType_CSV, FileType_Null };
  drag_selection_core(file_types, FileType_Text, DTM_RAM);
}

static void test52(void)
{
  /* Drag claimed selection to app as CSV */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_RAM);
}

static void paste_external_core(int const *file_types, int file_type, DataTransferMethod method)
{
  unsigned long limit;
  const _kernel_oserror *err;
  ObjectId const id = create_window(EventCode_CreateObjColours);

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_foreign(&drag_dest);
  assert(userdata_count_unsafe() == 0);

  int const estimated_size = estimate_file_size(file_type, colour_selection, SelectionEnd - SelectionStart);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;

    setup_selection(id);

    Fortify_SetNumAllocationsLimit(limit);

    /* Copy the selection to the keyboard */
    err_suppress_errors();
    init_custom_event(&poll_block, EventCode_Copy);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    dispatch_event(Wimp_EToolboxEvent, &poll_block);
    err = err_dump_suppressed();

    /* An claim entity message should be sent when the selection is copied */
    WimpMessage claim_entity;
    if (check_claim_entity_msg(&claim_entity))
    {
      assert(((WimpClaimEntityMessage *)claim_entity.data.words)->flags == Wimp_MClaimEntity_Clipboard);

      /* Paste from the clipboard into another app */
      err_suppress_errors();
      int const dr_ref = init_data_request_msg(&poll_block, Wimp_MDataRequest_Clipboard, file_types, &drag_dest, 0);
      dispatch_event(Wimp_EUserMessageRecorded, &poll_block);
      err = err_dump_suppressed();

      /* A data save message should be sent in reply to a data request. */
      WimpMessage data_save;
      if (check_data_save_msg(dr_ref, ForeignTaskHandle, "EntityData", &data_save, &drag_dest))
      {
        assert(data_save.data.data_save.file_type == file_type);
        assert(data_save.data.data_save.estimated_size == estimated_size);

        /* Reply with a DataSaveAck message from the app in which the clipboard is being pasted. */
        err = rec_data_core(&data_save, method);
      }
      else
      {
        /* If the datasave message was not sent then it must be because an error occurred. */
        assert(err != NULL);
      }
    }
    else
    {
      /* If the claim entity message was not sent then it must be because an error occurred. */
      assert(err != NULL);
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* Force the app to dump the clipboard content */
    init_claim_entity_msg(&poll_block, Wimp_MDataRequest_Clipboard);
    dispatch_event(Wimp_EUserMessage, &poll_block);

    if ((method == DTM_BadFile) || (method == DTM_BadRAM))
    {
      assert(err != NULL);
      if (strstr(err->errmess, msgs_lookup("RecDied")))
        break;
    }

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Pasting from the clipboard should not make an unsafe file safe
     nor change its file name. */
  assert(!path_is_in_userdata(TEST_DATA_OUT));
  assert(userdata_count_unsafe() == 1);

  if (method == DTM_None)
  {
    /* We do not expect the clipboard data to have been saved */
    assert(fopen(TEST_DATA_OUT, "rb") == NULL);
  }
  else if ((method == DTM_RAM) || (method == DTM_File))
  {
    /* Check that the clipboard contents were pasted correctly */
    if (method == DTM_File)
      assert_file_has_type(TEST_DATA_OUT, file_type);
    check_out_file(file_type, colour_selection, SelectionEnd - SelectionStart);
  }

  /* Pasting a selection from the clipboard should not alter the source data. */
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_edited);
}

static void test53(void)
{
  /* Paste to app with no type */
  int const file_types[] = { FileType_Null };

  /* A pasting app with no matching filetype should receive the default export filetype */
  paste_external_core(file_types, FileType_CSV, DTM_RAM);
}

static void test54(void)
{
  /* Paste to app with unsupported types */

  /* A pasting app that specifies no matching filetype should receive the default export filetype */
  int const file_types[] = { FileType_Squash, FileType_Obey, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_RAM);
}

static void test55(void)
{
  /* Paste to app as data */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Data, FileType_CSV, FileType_Squash, FileType_Text, FileType_Null };
  paste_external_core(file_types, FileType_Data, DTM_RAM);
}

static void test56(void)
{
  /* Paste to app as text */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Text, FileType_Sprite, FileType_CSV, FileType_Null };
  paste_external_core(file_types, FileType_Text, DTM_RAM);
}

static void test57(void)
{
  /* Paste to app as CSV */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_RAM);
}

static void test58(void)
{
  /* Drag unclaimed selection to nowhere */

  /* An unclaimed drag should end by sending the default export filetype */
  drag_selection_core(NULL, FileType_CSV, DTM_None);
}

static void test59(void)
{
  /* Drag claimed selection to nowhere */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_CSV, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_None);
}

static void test64(void)
{
  /* Paste to nowhere */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_CSV, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_None);
}

static void test65(void)
{
  /* Drag claimed selection to app as CSV with no RAM transfer */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_File);
}

static void test66(void)
{
  /* Paste to app as CSV with no RAM transfer */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_File);
}

static void test71(void)
{
  /* Save empty object colours file with incomplete file transfer */
  ObjectId const id = create_window(EventCode_CreateObjColours);

  assert(userdata_count_unsafe() == 0);
  save_file(id, SaveAs_DestinationSafe, DTM_BadFile);
  assert(userdata_count_unsafe() == 0);

  close_window(id);
}

static void test73(void)
{
  /* Drag claimed selection to app as CSV with no RAM transfer */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_File);
}

static void test74(void)
{
  /* Drag claimed selection to app as CSV with broken RAM transfer */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_BadRAM);
}

static void test75(void)
{
  /* Drag claimed selection to app as CSV with broken file transfer */

  /* The drag source should use the first of the claimant's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  drag_selection_core(file_types, FileType_CSV, DTM_BadFile);
}

static void test76(void)
{
  /* Paste to app as CSV with no RAM transfer */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_File);
}

static void test77(void)
{
  /* Paste to app as CSV with broken RAM transfer */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_BadRAM);
}

static void test78(void)
{
  /* Paste to app as CSV with broken file transfer */

  /* The clipboard owner should use the first of the receiver's file types that it supports */
  int const file_types[] = { FileType_Obey, FileType_Squash, FileType_CSV, FileType_Text, FileType_Sprite, FileType_Null };
  paste_external_core(file_types, FileType_CSV, DTM_BadFile);
}

static void test79(void)
{
  /* Load hill colours file */
  make_hill_cols_file(TEST_DATA_IN, colour_identity);
  load_persistent("EditHills", FileType_Fednet);

  /* An editing window should have been created */
  ObjectId const id = get_created_window("EditHills");

  /* The data should be treated as 'safe' and findable by path. */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  save_close_and_discard(id);
  check_hill_cols_file(TEST_DATA_OUT, colour_identity);
}

static void test80(void)
{
  /* Hill colours file from app */
  int const estimated_size = make_hill_cols_file(TEST_DATA_IN, colour_identity);
  app_save_to_iconbar("EditHills", FileType_Fednet, estimated_size, DTM_RAM, 0);
  ObjectId const id = get_created_window("EditHills");
  assert(userdata_count_unsafe() == 1);
  save_close_and_discard(id);
  check_hill_cols_file(TEST_DATA_OUT, colour_identity);
}

static void test81(void)
{
  /* Drag claimable hill colours file to icon */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_Fednet, FileType_Null };
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  int const estimated_size = make_hill_cols_file(TEST_DATA_IN, colour_identity);
  unsigned long limit = 0;
  do
  {
    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_File, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditHills");
    if (id != NULL_ObjectId)
    {
      assert(!path_is_in_userdata(TEST_DATA_IN));
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(get_created_window("EditHills"));
      check_hill_cols_file(TEST_DATA_OUT, colour_identity);
    }
    else
    {
      assert(err != NULL);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test82(void)
{
  /* Double-click hill colours file  */
  make_hill_cols_file(TEST_DATA_IN, colour_identity);
  double_click(FileType_Fednet, false);

  /* No editing window should have been created */
  ObjectId const id = pseudo_toolbox_find_by_template_name("EditHills");
  assert(id == NULL_ObjectId);
}

static void test83(void)
{
  /* Create new hill colours file */
  WimpPollBlock poll_block;
  const _kernel_oserror *err;
  unsigned long limit = 0;
  do
  {
    err_suppress_errors();

    init_custom_event(&poll_block, EventCode_CreateHillColours);
    init_id_block(pseudo_event_get_client_id_block(), pseudo_toolbox_find_by_template_name("CreateFile"), 1);
    dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit);

    check_and_dispatch_caret_claim();
    err = err_dump_suppressed();

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("EditHills");
    if (id != NULL_ObjectId)
    {
      assert(object_is_on_menu(id));
      assert(pseudo_toolbox_object_is_showing(id));
      assert(userdata_count_unsafe() == 0);
      close_window(id);
    }
    else
    {
      assert(err != NULL);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test84(void)
{
  /* Save empty hill colours file */
  ObjectId const id = create_window(EventCode_CreateHillColours);

  assert(userdata_count_unsafe() == 0);
  save_file(id, SaveAs_DestinationSafe, DTM_File);
  assert(userdata_count_unsafe() == 0);

  close_window(id);

  assert_file_has_type(TEST_DATA_OUT, FileType_Fednet);
  check_hill_cols_file(TEST_DATA_OUT, colour_black);
}

static void test85(void)
{
  /* Drag claimable hill colours file to window */
  static int const file_types[] = { FileType_Data, FileType_Obey, FileType_Fednet, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateHillColours);
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_win(&drag_dest, id, DropPosition, 0);

  int const estimated_size = make_hill_cols_file(TEST_DATA_IN, colour_identity);

  unsigned long limit = 0;
  do
  {
    /* Irreversible changes can happen before an error occurs,
       so unfortunately we must reset the content every time. */
    reset_scroll_state(drag_dest.window_handle);
    select_all(id);
    set_colour(id, 0);
    deselect_all(id);

    Fortify_SetNumAllocationsLimit(limit);
    err = do_drag_in_data_core(file_types, 2, estimated_size, &drag_dest, DTM_File, Wimp_MDragging_DataFromSelection);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
    {
      assert(userdata_count_unsafe() == 1);
      save_close_and_discard(id);
      check_hill_cols_file(TEST_DATA_OUT, colour_dropped_cols);
    }
  }
  while (err != NULL && ++limit < FortifyAllocationLimit);
  assert(limit != FortifyAllocationLimit);
}

static void test86(void)
{
  /* Save empty hill colours file with incomplete file transfer */
  ObjectId const id = create_window(EventCode_CreateHillColours);

  assert(userdata_count_unsafe() == 0);
  save_file(id, SaveAs_DestinationSafe, DTM_BadFile);
  assert(userdata_count_unsafe() == 0);

  close_window(id);
}

static void test87(void)
{
  /* Paste unsupported types */
  static int const file_types[] = { FileType_Sprite, FileType_Obey, FileType_Null };
  ObjectId const id = create_window(EventCode_CreateObjColours);
  unsigned long limit;

  mouse_select(id, DropPosition, DropPosition+1);

  FILE * const f = fopen(TEST_DATA_IN, "wb");
  assert(f != NULL);
  assert(fputc('#', f) == '#');
  fclose(f);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    const _kernel_oserror * const err =
      paste_internal_core(file_types, 0, 0, id, DTM_File);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(err != NULL);
    assert(err->errnum == DUMMY_ERRNO);
    if (!strcmp(err->errmess, msgs_lookup("BadFileType")))
      break;
  }
  assert(limit != FortifyAllocationLimit);

  assert(userdata_count_unsafe() == 0);
  save_close_and_discard(id);
  check_object_cols_file(TEST_DATA_OUT, colour_black);
}

static void create_view(ObjectId id)
{
  assert(id != NULL_ObjectId);
  WimpPollBlock poll_block;
  init_custom_event(&poll_block, EventCode_NewView);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  check_and_dispatch_caret_claim();
}

static void test88(void)
{
  /* Create new view */
  unsigned long limit;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    ObjectId id = create_window(EventCode_CreateObjColours);

    err_suppress_errors();

    Fortify_SetNumAllocationsLimit(limit);
    create_view(id);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(userdata_count_unsafe() == 0);

    if (err_dump_suppressed() == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    do
    {
      assert(object_is_on_menu(id));
      assert(pseudo_toolbox_object_is_showing(id));
      close_window(id);
      id = pseudo_toolbox_find_by_template_name("EditColmap");
    }
    while (id != NULL_ObjectId);
  }
  assert(limit != FortifyAllocationLimit);

  for (int i = 0; i < 2; ++i)
  {
    close_window(get_created_window("EditColmap"));
  }
}

static void test89(void)
{
  /* Create multiple views */
  for (int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    Fortify_EnterScope();
    pseudo_toolbox_reset();

    DEBUGF("Creating first view\n");
    ObjectId const id = create_window(EventCode_CreateObjColours);
    assert(userdata_count_unsafe() == 0);

    for (int w = 0; w < nwin; ++w)
    {
      DEBUGF("Creating view %d/%d\n", w+1, nwin);
      create_view(id);
      assert(userdata_count_unsafe() == 0);
    }

    for (ObjectId it = ViewsMenu_getfirst();
         it != NULL_ObjectId;
         it = ViewsMenu_getnext(it))
    {
      setup_selection(it);
      assert(userdata_count_unsafe() == 1);
    }

    for (int w = 0; w < nwin; ++w)
    {
      DEBUGF("Closing view %d/%d\n", w+1, nwin);
      close_window(get_created_window("EditColmap"));

      ObjectId const dcs_id = pseudo_toolbox_find_by_template_name("DCS");
      assert(!pseudo_toolbox_object_is_showing(dcs_id));
      assert(userdata_count_unsafe() == 1);
    }

    DEBUGF("Closing last view\n");
    close_and_discard(get_created_window("EditColmap"));
    assert(userdata_count_unsafe() == 0);

    Fortify_LeaveScope();
  }
}

void App_tests(void)
{
  _kernel_swi_regs regs;
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Load CSV file", test1 },
    { "Load object colours file", test2 },
    { "Load directory", test3 },
    { "CSV file from app with broken file transfer", test4 },
    { "Compressed file from app with broken file transfer", test5 },
    { "Transfer dir from app", test6 },
    { "CSV file from app", test7 },
    { "Object colours file from app", test8 },
    { "CSV file from app with no RAM transfer", test9 },
    { "CSV file from app with broken RAM transfer", test10 },
    { "Load bad CSV file (value too low)", test11 },
    { "Load bad CSV file (value too high)", test12 },
    { "Load empty CSV file", test13 },
    { "Drag claimable CSV file to icon", test14 },
    { "Drag claimable object colours file to icon", test15 },
    { "Drag claimable unsupported types to icon", test16 },
    { "Drag unclaimable CSV file to icon", test17 },
    { "Double-click object colours file", test18 },
    { "Double-click CSV file", test19 },
    { "Create new object colours file", test20 },
    { "Bring windows to the front", test21 },
    { "Quicksave no path", test22 },
    { "Quicksave with path", test23 },
    { "Save empty object colours file", test24 },
    { "DCS save no path", test26 },
    { "DCS save with path", test27 },
    { "DCS cancel", test28 },
    { "Quit from task manager with cancel", test29 },
    { "Shutdown from task manager with cancel", test30 },
    { "Quit from task manager with confirm", test31 },
    { "Shutdown from task manager with confirm", test32 },
    { "Drag claimable CSV file to window", test33 },
    { "Drag claimable CSV file to selection", test34 },
    { "Drag claimable object colours file to window", test35 },
    { "Drag claimable unsupported types to window", test36 },
    { "Drag unclaimable CSV file to window", test37 },
    { "Paste CSV", test38 },
    { "Paste object colours", test39 },
    { "Paste empty clipboard", test40 },
    { "Drag selection then abort unclaimed drag", test41 },
    { "Drag selection then close window", test42 },
    { "Drag selection then claim and release drag", test43 },
    { "Drag selection then abort claimed drag", test44 },
    { "Drag unclaimed selection to source window", test45 },
    { "Drag claimed selection to source window", test46 },
    { "Drag unclaimed selection to app", test47 },
    { "Drag claimed selection to app with no type", test48 },
    { "Drag claimed selection to app with unsupported types", test49 },
    { "Drag claimed selection to app as data", test50 },
    { "Drag claimed selection to app as text", test51 },
    { "Drag claimed selection to app as CSV", test52 },
    { "Paste to app with no type", test53 },
    { "Paste to app with unsupported types", test54 },
    { "Paste to app as data", test55 },
    { "Paste to app as text", test56 },
    { "Paste to app as CSV", test57 },
    { "Drag unclaimed selection to nowhere", test58 },
    { "Drag claimed selection to nowhere", test59 },
    { "Paste to nowhere", test64 },
    { "Drag claimed selection to app as CSV with no RAM transfer", test65 },
    { "Paste to app as CSV with no RAM transfer", test66 },
    { "Save empty object colours file with incomplete file transfer", test71 },
    { "Drag claimed selection to app as CSV with no RAM transfer", test73 },
    { "Drag claimed selection to app as CSV with broken RAM transfer", test74 },
    { "Drag claimed selection to app as CSV with broken file transfer", test75 },
    { "Paste to app as CSV with no RAM transfer", test76 },
    { "Paste to app as CSV with broken RAM transfer", test77 },
    { "Paste to app as CSV with broken file transfer", test78 },
    { "Load hill colours file", test79 },
    { "Hill colours file from app", test80 },
    { "Drag claimable hill colours file to icon", test81 },
    { "Double-click hill colours file", test82 },
    { "Create new hill colours file", test83 },
    { "Save empty hill colours file", test84 },
    { "Drag claimable hill colours file to window", test85 },
    { "Save empty hill colours file with incomplete file transfer", test86 },
    { "Paste unsupported types", test87 },
    { "Create new view", test88 },
    { "Create multiple views", test89 },
  };

  initialise();

  /* This isn't ideal but it's better for replies to fake messages to be sent
     to our task rather than to an invalid handle or another task. */
  assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));
  th = regs.r[0];

  assert_no_error(pseudo_event_wait_for_idle());

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); count ++)
  {
    DEBUGF("Test %zu/%zu : %s\n",
           1 + count,
           ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);

    wipe(TEST_DATA_DIR);
    assert_no_error(os_file_create_dir(TEST_DATA_DIR, OS_File_CreateDir_DefaultNoOfEntries));

    Fortify_EnterScope();
    pseudo_toolbox_reset();
    pseudo_wimp_reset();

    unit_tests[count].test_func();

    /* Reclaim any entities that might still be owned by the app. */
    WimpPollBlock poll_block;
    init_claim_entity_msg(&poll_block, Wimp_MDataRequest_Clipboard|Wimp_MClaimEntity_CaretOrSelection);
    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
  }

  wipe(TEST_DATA_DIR);
}
