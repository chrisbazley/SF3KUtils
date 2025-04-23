/*
 *  FednetCmp - Fednet file compression/decompression
 *  Unit tests
 *  Copyright (C) 2014 Christopher Bazley
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

/* RISC OS library files */
#include "swis.h"
#include "event.h"
#include "toolbox.h"
#include "saveas.h"
#include "gadgets.h"

/* My library files */
#include "Macros.h"
#include "Debug.h"
#include "FileUtils.h"
#include "Err.h"
#include "userdata.h"
#include "gkeycomp.h"
#include "gkeydecomp.h"
#include "SFFormats.h"
#include "OSFile.h"
#include "PseudoWimp.h"
#include "PseudoTbox.h"
#include "PseudoEvnt.h"
#include "FOpenCount.h"
#include "ViewsMenu.h"
#include "msgtrans.h"
#include "Hourglass.h"
#include "FileRWInt.h"

/* Local header files */
#include "FNCInit.h"
#include "FNCSaveBox.h"

#include "FORTIFY.h"

#define TEST_DATA_DIR "<Wimp$ScrapDir>.FednetCmpTests"
#define TEST_DATA_IN TEST_DATA_DIR ".in"
#define TEST_DATA_OUT TEST_DATA_DIR ".out"
#define BATCH_PATH_SUBDIR ".oops"
#define BATCH_PATH_TAIL BATCH_PATH_SUBDIR ".foobarbaz"
#define BATCH_PATH_TAIL_2 BATCH_PATH_SUBDIR ".ignore"
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
  TestUncompFileType = FileType_Data,
  TestCompressedFileType = FileType_Fednet,
  DestinationIcon = 2,
  DestinationX = 900,
  DestinationY = 34,
  Timeout = 30 * CLOCKS_PER_SEC,
  ComponentId_Scan_Abort_ActButton = 0x01,
  ComponentId_Scan_Pause_ActButton = 0x04,
  ComponentId_SaveDir_Compress_Radio = 0x01,
  ComponentId_SaveDir_Decompress_Radio = 0x02,
  OS_FSControl_Wipe = 27,
  OS_FSControl_Flag_Recurse = 1,
};

static void wipe(char const *path_name)
{
  _kernel_swi_regs regs;

  assert(path_name != NULL);

  regs.r[0] = OS_FSControl_Wipe;
  regs.r[1] = (int)path_name;
  regs.r[3] = OS_FSControl_Flag_Recurse;
  _kernel_swi(OS_FSControl, &regs, &regs);
}

static int make_compressed_file(char const *file_name)
{
  FILE *f;
  size_t n;
  char test_data[TestDataSize], out_buffer[CompressionBufferSize];
  GKeyComp      *comp;
  GKeyParameters params;
  unsigned int i;
  int estimated_size = sizeof(int32_t);
  bool ok;
  GKeyStatus status;

  for (i = 0; i < TestDataSize; ++i)
    test_data[i] = (char)i;

  f = fopen(file_name, "wb");
  assert(f != NULL);

  ok = fwrite_int32le(sizeof(test_data), f);
  assert(ok);

  comp = gkeycomp_make(FednetHistoryLog2);
  assert(comp != NULL);

  params.in_buffer = test_data;
  params.in_size = sizeof(test_data);
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
  assert_no_error(os_file_set_type(file_name, TestCompressedFileType));

  return estimated_size;
}

static void check_compressed_file(char const *file_name)
{
  FILE *f;
  char test_data[TestDataSize], in_buffer[CompressionBufferSize];
  GKeyDecomp     *decomp;
  GKeyParameters params;
  unsigned int i;
  long int len;
  bool ok, in_pending = false;
  GKeyStatus status;
  OS_File_CatalogueInfo cat;

  assert_no_error(os_file_read_cat_no_path(file_name, &cat));
  assert(cat.object_type == ObjectType_File);
  DEBUGF("Load address: 0x%x\n", cat.load);
  assert(((cat.load >> 8) & 0xfff) == TestCompressedFileType);

  f = fopen(file_name, "rb");
  assert(f != NULL);

  ok = fread_int32le(&len, f);
  assert(ok);
  assert(len == TestDataSize);

  decomp = gkeydecomp_make(FednetHistoryLog2);
  assert(decomp != NULL);

  params.in_buffer = in_buffer;
  params.in_size = 0;
  params.out_buffer = test_data;
  params.out_size = sizeof(test_data);
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

  for (i = 0; i < TestDataSize; ++i)
    assert(test_data[i] == (char)i);
}

static int make_uncompressed_file(char const *file_name)
{
  FILE *f;
  size_t n;
  char test_data[TestDataSize];
  unsigned int i;

  for (i = 0; i < TestDataSize; ++i)
    test_data[i] = (char)i;

  f = fopen(file_name, "wb");
  assert(f != NULL);

  n = fwrite(test_data, TestDataSize, 1, f);
  assert(n == 1);

  fclose(f);

  assert_no_error(os_file_set_type(file_name, TestUncompFileType));

  return TestDataSize;
}

static void check_uncompressed_file(char const *file_name)
{
  FILE *f;
  size_t n;
  char test_data[TestDataSize];
  unsigned int i;
  OS_File_CatalogueInfo cat;

  assert_no_error(os_file_read_cat_no_path(file_name, &cat));
  assert(cat.object_type == ObjectType_File);
  DEBUGF("Load address: 0x%x\n", cat.load);
  assert(((cat.load >> 8) & 0xfff) == TestUncompFileType);

  f = fopen(file_name, "rb");
  assert(f != NULL);

  n = fread(test_data, TestDataSize, 1, f);
  assert(n == 1);

  fclose(f);

  for (i = 0; i < TestDataSize; ++i)
    assert(test_data[i] == (char)i);
}

static void init_id_block(IdBlock *block, ObjectId id, ComponentId component)
{
  _kernel_oserror *e;

  assert(block != NULL);

  block->self_id = id;
  block->self_component = component;
  e = toolbox_get_parent(0, id, &block->parent_id, &block->parent_component);
  assert(e == NULL);
  e = toolbox_get_ancestor(0, id, &block->ancestor_id, &block->ancestor_component);
  assert(e == NULL);
}

static bool path_is_in_userdata(char *filename)
{
  UserData *savebox;
  char *path;

  assert_no_error(canonicalise(&path, NULL, NULL, filename));
  savebox = userdata_find_by_file_name(path);
  free(path);
  return savebox != NULL;
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

static void init_savetofile_event(WimpPollBlock *poll_block)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)&poll_block->words;

  sastfe->hdr.size = sizeof(*poll_block);
  sastfe->hdr.reference_number = ++fake_ref;
  sastfe->hdr.event_code = SaveAs_SaveToFile;
  sastfe->hdr.flags = 0;
  STRCPY_SAFE(sastfe->filename, TEST_DATA_OUT);
}

static void init_fillbuffer_event(WimpPollBlock *poll_block)
{
  SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)&poll_block->words;

  safbe->hdr.size = sizeof(*poll_block);
  safbe->hdr.reference_number = ++fake_ref;
  safbe->hdr.event_code = SaveAs_FillBuffer;
  safbe->hdr.flags = 0;
  safbe->size = 100;
  safbe->address = NULL;
  safbe->no_bytes = 0;
}

static void init_actionbutton_event(WimpPollBlock *poll_block)
{
  ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)&poll_block->words;

  abse->hdr.size = sizeof(*poll_block);
  abse->hdr.reference_number = ++fake_ref;
  abse->hdr.event_code = ActionButton_Selected;
  abse->hdr.flags = 0;
}

static void init_dialoguecompleted_event(WimpPollBlock *poll_block)
{
  SaveAsDialogueCompletedEvent * const sadce = (SaveAsDialogueCompletedEvent *)&poll_block->words;

  sadce->hdr.size = sizeof(*poll_block);
  sadce->hdr.reference_number = ++fake_ref;
  sadce->hdr.event_code = SaveAs_DialogueCompleted;
  sadce->hdr.flags = 0;
}

static void dispatch_event(int const event_code, WimpPollBlock *poll_block)
{
  Fortify_CheckAllMemory();

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

  /* Deliver any outgoing broadcasts back to the sender */
  unsigned int count = pseudo_wimp_get_message_count();
  while (count-- > 0)
  {
    int msg_code, handle;
    WimpPollBlock msg_block;
    pseudo_wimp_get_message2(count, &msg_code, &msg_block, &handle, NULL);
    if (handle == 0)
    {
      assert_no_error(event_dispatch(msg_code, &msg_block));
    }
  }

  Fortify_CheckAllMemory();
}

static void dialogue_completed(ObjectId id)
{
  assert(id != NULL_ObjectId);
  WimpPollBlock poll_block;
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static int init_ram_transmit_msg(WimpPollBlock *poll_block, WimpMessage *ram_fetch, int nbytes)
{
  /* Set up fake RAMTransmit message */
  _kernel_swi_regs regs;
  char *test_data;

  /* This isn't ideal but it's better for replies to these fake messages to be sent
     to our task rather than to an invalid handle or another task. */
  assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));

  poll_block->user_message.hdr.size = sizeof(*poll_block);
  poll_block->user_message.hdr.sender = regs.r[0];
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = ram_fetch->hdr.my_ref;
  poll_block->user_message.hdr.action_code = Wimp_MRAMTransmit;

  poll_block->user_message.data.ram_transmit.buffer = ram_fetch->data.ram_fetch.buffer;
  poll_block->user_message.data.ram_transmit.nbytes = nbytes;

  test_data = ram_fetch->data.ram_fetch.buffer;
  for (int i = 0; i < nbytes; ++i)
    test_data[i] = (char)i;

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_load_msg(WimpPollBlock *poll_block, char *filename, int estimated_size, int file_type, int your_ref)
{
  /* Set up fake DataLoad message */
  _kernel_swi_regs regs;

  /* This isn't ideal but it's better for replies to these fake messages to be sent
     to our task rather than to an invalid handle or another task. */
  assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));

  poll_block->user_message.hdr.size = sizeof(*poll_block);
  poll_block->user_message.hdr.sender = regs.r[0];
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataLoad;

  poll_block->user_message.data.data_load.destination_window = -2;
  poll_block->user_message.data.data_load.destination_icon = DestinationIcon;
  poll_block->user_message.data.data_load.destination_x = DestinationX;
  poll_block->user_message.data.data_load.destination_y = DestinationY;
  poll_block->user_message.data.data_load.estimated_size = estimated_size;
  poll_block->user_message.data.data_load.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_load.leaf_name, filename);

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_save_msg(WimpPollBlock *poll_block, int estimated_size, int file_type)
{
  /* Set up fake datasave message */
  _kernel_swi_regs regs;

  /* This isn't ideal but it's better for replies to these fake messages to be sent
     to our task rather than to an invalid handle or another task. */
  assert_no_error(toolbox_get_sys_info(Toolbox_GetSysInfo_TaskHandle, &regs));

  poll_block->user_message.hdr.size = sizeof(*poll_block);
  poll_block->user_message.hdr.sender = regs.r[0];
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MDataSave;

  poll_block->user_message.data.data_save.destination_window = -2;
  poll_block->user_message.data.data_save.destination_icon = DestinationIcon;
  poll_block->user_message.data.data_save.destination_x = DestinationX;
  poll_block->user_message.data.data_save.destination_y = DestinationY;
  poll_block->user_message.data.data_save.estimated_size = estimated_size;
  poll_block->user_message.data.data_save.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_save.leaf_name, TEST_LEAFNAME);

  return poll_block->user_message.hdr.my_ref;
}

static int check_data_load_ack_msg(int my_ref, char *filename, int estimated_size, int file_type)
{
  /* A dataloadack message should have been sent in reply to the dataload */
  unsigned int count = pseudo_wimp_get_message_count();
  int their_ref = 0;

  assert(count >= 1);
  while (count-- > 0 && !their_ref)
  {
    WimpMessage msg;
    pseudo_wimp_get_message(count, &msg);

    if (msg.hdr.your_ref == my_ref)
    {
      _kernel_swi_regs regs;

      assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));

      assert(msg.hdr.size >= 0);
      assert((size_t)msg.hdr.size >= offsetof(WimpMessage, data.data_load_ack.leaf_name) + strlen(filename)+1);
      assert(msg.hdr.sender == regs.r[0]);
      assert(msg.hdr.my_ref != 0);
      assert(msg.hdr.action_code == Wimp_MDataLoadAck);
      assert(msg.data.data_load_ack.destination_window == -2);
      assert(msg.data.data_load_ack.destination_icon == DestinationIcon);
      assert(msg.data.data_load_ack.destination_x == DestinationX);
      assert(msg.data.data_load_ack.destination_y == DestinationY);
      assert(msg.data.data_load_ack.estimated_size == estimated_size);
      assert(msg.data.data_load_ack.file_type == file_type);
      assert(!strcmp(msg.data.data_load_ack.leaf_name, filename));
      their_ref = msg.hdr.my_ref;
    }
  }
  return their_ref;
}

static bool check_data_save_ack_msg(int my_ref, WimpMessage *data_save_ack)
{
  /* A datasaveack message should have been sent in reply to the datasave */
  unsigned int count = pseudo_wimp_get_message_count();

  assert(count >= 1);
  while (count-- > 0)
  {
    WimpMessage msg;
    pseudo_wimp_get_message(count, &msg);

    if (msg.hdr.your_ref == my_ref)
    {
      _kernel_swi_regs regs;

      assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));

      DEBUGF("%d %zu\n", msg.hdr.size, offsetof(WimpMessage, data.data_save_ack.leaf_name) + sizeof(TEST_LEAFNAME));
      assert(msg.hdr.sender == regs.r[0]);
      assert(msg.hdr.my_ref != 0);
      assert(msg.hdr.action_code == Wimp_MDataSaveAck || msg.hdr.action_code == Wimp_MRAMFetch);

      if (msg.hdr.action_code == Wimp_MDataSaveAck)
      {
        assert(msg.hdr.size >= 0);
        assert((size_t)msg.hdr.size >= offsetof(WimpMessage, data.data_save_ack.leaf_name) + strlen(msg.data.data_save_ack.leaf_name)+1);
        assert(msg.data.data_save_ack.destination_window == -2);
        assert(msg.data.data_save_ack.destination_icon == DestinationIcon);
        assert(msg.data.data_save_ack.destination_x == DestinationX);
        assert(msg.data.data_save_ack.destination_y == DestinationY);
        assert(msg.data.data_save_ack.estimated_size == -1);
        assert(!strcmp(msg.data.data_save_ack.leaf_name, "<Wimp$Scrap>"));
        *data_save_ack = msg;
        return true;
      }
    }
  }
  return false;
}

static bool check_ram_fetch_msg(int my_ref, WimpMessage *ram_fetch)
{
  /* A datasaveack message should have been sent in reply to the datasave */
  unsigned int count = pseudo_wimp_get_message_count();

  assert(count >= 1);
  while (count-- > 0)
  {
    WimpMessage msg;

    pseudo_wimp_get_message(count, &msg);

    if (msg.hdr.your_ref == my_ref)
    {
      _kernel_swi_regs regs;

      assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));
      assert(msg.hdr.sender == regs.r[0]);
      assert(msg.hdr.my_ref != 0);
      assert(msg.hdr.action_code == Wimp_MDataSaveAck || msg.hdr.action_code == Wimp_MRAMFetch);

      if (msg.hdr.action_code == Wimp_MRAMFetch)
      {
        assert(msg.hdr.size >= 0);
        assert((size_t)msg.hdr.size >= offsetof(WimpMessage, data.ram_fetch) + sizeof(msg.data.ram_fetch));
        assert(msg.data.ram_fetch.buffer != NULL);
        *ram_fetch = msg;
        return true;
      }
    }
  }
  return false;
}

static void check_file_save_completed(ObjectId id, const _kernel_oserror *err)
{
  /* saveas_get_file_save_completed must have been called
     to indicate success or failure */
  unsigned int flags;
  char buffer[256];
  int nbytes;
  ObjectId const quoted_id = pseudo_saveas_get_file_save_completed(
                             &flags, buffer, sizeof(buffer), &nbytes);
  DEBUGF("quoted_id 0x%x id 0x%x\n", quoted_id, id);
  assert(id != NULL_ObjectId);
  assert(nbytes >= 0);
  assert((size_t)nbytes <= sizeof(buffer));
  assert(quoted_id == id);
  assert(!strcmp(buffer, TEST_DATA_OUT));
  if (flags != SaveAs_SuccessfulSave)
  {
    assert(flags == 0);
    assert(err != NULL);
  }
}

static void check_buffer_filled(ObjectId id)
{
  /* saveas_buffer_filled must have been called */
  unsigned int flags;
  char buffer[256];
  int nbytes;
  ObjectId const quoted_id = pseudo_saveas_get_buffer_filled(
                             &flags, &buffer, sizeof(buffer), &nbytes);
  DEBUGF("quoted_id 0x%x id 0x%x\n", quoted_id, id);
  assert(id != NULL_ObjectId);
  assert(nbytes >= 0);
  assert((size_t)nbytes <= sizeof(buffer));
  assert(quoted_id == id);
  assert(flags == 0);

  for (int i = 0; i < nbytes; ++i)
    assert(buffer[i] == (char)i);
}

static void load_persistent(int estimated_size, int file_type)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  int my_ref = 0;
  OS_File_CatalogueInfo cat;
  const _kernel_oserror *err;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, file_type, 0);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name(
      file_type == TestCompressedFileType ? "SaveFile" : "SaveFednet");

    if (id != NULL_ObjectId)
      dialogue_completed(id);

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, file_type);

  /* The receiver must not delete persistent files */
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_File);
}

static void test1(void)
{
  /* Load uncompressed file */
  ObjectId id;
  int const estimated_size = make_uncompressed_file(TEST_DATA_IN);

  load_persistent(estimated_size, TestUncompFileType);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SaveFednet");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  dialogue_completed(id);

  Fortify_LeaveScope();
}

static void test2(void)
{
  /* Load compressed file */
  ObjectId id;
  int const estimated_size = make_compressed_file(TEST_DATA_IN);

  load_persistent(estimated_size, TestCompressedFileType);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SaveFile");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  dialogue_completed(id);
  Fortify_LeaveScope();
}

static void test3(void)
{
  /* Load directory */
  unsigned long limit;
  ObjectId id;
  WimpPollBlock poll_block;
  int my_ref = 0;

  /* Create directory */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;
    my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, 0);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    id = pseudo_toolbox_find_by_template_name("SaveDir");
    if (id != NULL_ObjectId)
      dialogue_completed(id);

   Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  dialogue_completed(id);
  Fortify_LeaveScope();
}

static void test4(void)
{
  /* Save compressed file */
  unsigned long limit;
  const _kernel_oserror *err;
  int const estimated_size = make_uncompressed_file(TEST_DATA_IN);
  WimpPollBlock poll_block;
  int const my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, TestUncompFileType, 0);
  ObjectId id;

  /* Load uncompressed file */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, TestUncompFileType);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveFednet");
  assert(object_is_on_menu(id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    init_savetofile_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the save dialogue */
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    Fortify_LeaveScope();
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    check_file_save_completed(id, err);
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  check_compressed_file(TEST_DATA_OUT);
  dialogue_completed(id);
}

static void test5(void)
{
  /* Save uncompressed file */
  unsigned long limit;
  const _kernel_oserror *err;
  int const estimated_size = make_compressed_file(TEST_DATA_IN);
  WimpPollBlock poll_block;
  int const my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, TestCompressedFileType, 0);
  ObjectId id;

  /* Load compressed file */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, TestCompressedFileType);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveFile");
  assert(object_is_on_menu(id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    init_savetofile_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the save dialogue */
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    Fortify_LeaveScope();
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    check_file_save_completed(id, err);
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  check_uncompressed_file(TEST_DATA_OUT);
  dialogue_completed(id);
}

static void test6(void)
{
  /* Save directory */
  unsigned long limit;
  const _kernel_oserror *err;
  WimpPollBlock poll_block;
  int const my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, 0);
  ObjectId id;

  /* Create directory */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));

  /* Load directory */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    init_savetofile_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the save dialogue */
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    err = err_dump_suppressed();
    check_file_save_completed(id, err);

    /* A scan dbox should have been created */
    ObjectId const scan_id = pseudo_toolbox_find_by_template_name("Scan");
    if (scan_id != NULL_ObjectId)
    {
      OS_File_CatalogueInfo cat;
      assert(object_is_on_menu(scan_id));
      assert(userdata_count_unsafe() == 1);

      /* An output directory should have been created */
      assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT, &cat));
      assert(cat.object_type == ObjectType_Directory);

      /* Abort the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Abort_ActButton);

      dispatch_event(Wimp_EToolboxEvent, &poll_block);
    }
    else
    {
      /* An error must have prevented creation of the scan */
      assert(err != NULL);
    }

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
    assert(userdata_count_unsafe() == 0);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  dialogue_completed(id);
}

static void batch_test(ComponentId radio)
{
  unsigned long limit;
  WimpPollBlock poll_block;
  int const my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, 0);
  ObjectId id, win_id;

  /* Load directory */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));

  assert_no_error(saveas_get_window_id(0, id, &win_id));
  assert_no_error(radiobutton_set_state(0, win_id, radio, 1));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    ObjectId scan_id;
    unsigned int i;
    OS_File_CatalogueInfo cat;
    const _kernel_oserror *err = NULL;

    Fortify_EnterScope();

    /* Activate the save dialogue */
    init_savetofile_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    check_file_save_completed(id, NULL);

    /* A scan dbox should have been created */
    scan_id = pseudo_toolbox_find_by_template_name("Scan");
    assert(scan_id != NULL_ObjectId);
    assert(object_is_on_menu(scan_id));
    assert(userdata_count_unsafe() == 1);

    /* An output directory should have been created */
    assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT, &cat));
    assert(cat.object_type == ObjectType_Directory);

    Fortify_SetNumAllocationsLimit(limit);

    for (i = 0; i < 2 && err == NULL; ++i)
    {
      err_suppress_errors();

      /* Pause/unpause the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);

      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Pause_ActButton);

      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      err = err_dump_suppressed();
    }

    while (err == NULL && pseudo_toolbox_find_by_template_name("Scan") != NULL_ObjectId)
    {
      /* Deliver null events until the scan dbox completes or an error occurs */
      err_suppress_errors();
      dispatch_event(Wimp_ENull, NULL);
      err = err_dump_suppressed();
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* The scan dbox may have deleted itself on error but always
       should have deleted itself if it completed */
    if (pseudo_toolbox_find_by_template_name("Scan") != NULL_ObjectId)
    {
      assert(err != NULL);

      /* Abort the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Abort_ActButton);

      /* Don't risk assigning err = NULL because something failed
         and we want to retry with a higher allocation limit. */
      dispatch_event(Wimp_EToolboxEvent, &poll_block);
    }

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
    assert(userdata_count_unsafe() == 0);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
  dialogue_completed(id);
}

static void test7(void)
{
  /* Batch compress */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be compressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));
  make_uncompressed_file(TEST_DATA_IN BATCH_PATH_TAIL);
  make_compressed_file(TEST_DATA_IN BATCH_PATH_TAIL_2);

  batch_test(ComponentId_SaveDir_Compress_Radio);

  check_compressed_file(TEST_DATA_OUT BATCH_PATH_TAIL);
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_TAIL_2, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test8(void)
{
  /* Batch decompress */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be decompressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));
  make_compressed_file(TEST_DATA_IN BATCH_PATH_TAIL);
  make_uncompressed_file(TEST_DATA_IN BATCH_PATH_TAIL_2);

  batch_test(ComponentId_SaveDir_Decompress_Radio);

  check_uncompressed_file(TEST_DATA_OUT BATCH_PATH_TAIL);
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_TAIL_2, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test9(void)
{
  /* RAM transmit uncompressed file */
  unsigned long limit;
  const _kernel_oserror *err;
  int const estimated_size = make_compressed_file(TEST_DATA_IN);
  WimpPollBlock poll_block;
  int const my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, TestCompressedFileType, 0);
  ObjectId id;

  /* Load compressed file */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, TestCompressedFileType);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveFile");
  assert(object_is_on_menu(id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    init_fillbuffer_event(&poll_block);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);

    err_suppress_errors();

    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the save dialogue */
    pseudo_saveas_reset_buffer_filled();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    check_buffer_filled(id);
    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
  dialogue_completed(id);
}

static void wait(void)
{
  const clock_t start_time = clock();
  clock_t elapsed;

  DEBUGF("Waiting %fs for stalled load operation(s) to be abandoned\n",
         (double)Timeout / CLOCKS_PER_SEC);
  _swix(Hourglass_On, 0);
  do
  {
    elapsed = clock() - start_time;
    _swix(Hourglass_Percentage, _IN(0), (elapsed * 100) / Timeout);
  }
  while (elapsed < Timeout);
  _swix(Hourglass_Off, 0);
}

static void cleanup_stalled(void)
{
  /* Wait for timeout then deliver a null event to clean up the failed load */
  unsigned long limit;
  const _kernel_oserror *err;

  wait();

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_SetNumAllocationsLimit(limit);

    dispatch_event(Wimp_ENull, NULL);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }

  Fortify_LeaveScope();
}

static void send_data_save(int file_type)
{
  unsigned long limit;
  const _kernel_oserror *err;
  int my_ref = 0;
  WimpMessage data_save_ack;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;
    my_ref = init_data_save_msg(&poll_block, TestDataSize, file_type);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  if (check_data_save_ack_msg(my_ref, &data_save_ack))
  {
    DEBUGF("file_type 0x%x\n", data_save_ack.data.data_save_ack.file_type);
    assert(data_save_ack.data.data_save_ack.file_type == file_type);
  }
  else
  {
    WimpMessage ram_fetch;
    assert(check_ram_fetch_msg(my_ref, &ram_fetch));
    assert(ram_fetch.data.ram_fetch.buffer_size >= TestDataSize);
  }
}

static void data_save_with_timeout(int file_type)
{
  send_data_save(file_type);
  cleanup_stalled();
}

static void test10(void)
{
  /* Uncompressed file from app with timeout */
  data_save_with_timeout(TestUncompFileType);
}

static void test11(void)
{
  /* Compressed file from app with timeout */
  data_save_with_timeout(TestCompressedFileType);
}

static void test12(void)
{
  /* Transfer dir from app */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;

  init_data_save_msg(&poll_block, 0, FileType_Directory);

  err_suppress_errors();

  Fortify_EnterScope();
  pseudo_wimp_reset();

  dispatch_event(Wimp_EUserMessage, &poll_block);

  Fortify_LeaveScope();

  err = err_dump_suppressed();
  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("AppDir")));
  assert(pseudo_wimp_get_message_count() == 0);
}

static void test13(void)
{
  /* Transfer app from app */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;

  init_data_save_msg(&poll_block, 0, FileType_Application);

  err_suppress_errors();
  Fortify_EnterScope();
  pseudo_wimp_reset();

  dispatch_event(Wimp_EUserMessage, &poll_block);

  Fortify_LeaveScope();

  err = err_dump_suppressed();
  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("AppDir")));
  assert(pseudo_wimp_get_message_count() == 0);
}

static void do_data_transfer(int file_type, int (*make_file)(char const *filename), char *template_name, bool allow_ram_transfer)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  int dataload_ref = 0;
  int estimated_size = 0;
  const _kernel_oserror *err;
  UserData *savebox;
  ObjectId id;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    estimated_size = make_file("<Wimp$Scrap>");
    int const datasave_ref = init_data_save_msg(&poll_block, estimated_size, file_type);

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    err_suppress_errors();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    err = err_dump_suppressed();
    if (err == NULL)
    {
      WimpMessage data_save_ack;

      if (check_data_save_ack_msg(datasave_ref, &data_save_ack))
      {
        DEBUGF("file_type 0x%x\n", data_save_ack.data.data_save_ack.file_type);
        assert(data_save_ack.data.data_save_ack.file_type == file_type);
        dataload_ref = init_data_load_msg(&poll_block, "<Wimp$Scrap>", estimated_size, file_type, data_save_ack.hdr.my_ref);
      }
      else
      {
        WimpMessage ram_fetch;

        assert(check_ram_fetch_msg(datasave_ref, &ram_fetch));
        assert(ram_fetch.data.ram_fetch.buffer_size >= estimated_size);

        if (allow_ram_transfer)
        {
          /* Allowed to use RAM transfer, so fake a reply to the RAMFetch message
             with a RAMTransmit message. */
          int const ram_transmit_ref = init_ram_transmit_msg(&poll_block, &ram_fetch, estimated_size);
          err_suppress_errors();

          dispatch_event(Wimp_EUserMessage, &poll_block);

          err = err_dump_suppressed();

          /* We need to send another RAMTransmit message if the first one
             filled the receiver's buffer. */
          if (err == NULL && ram_fetch.data.ram_fetch.buffer_size <= estimated_size)
          {
            assert(check_ram_fetch_msg(ram_transmit_ref, &ram_fetch));
            init_ram_transmit_msg(&poll_block, &ram_fetch, 0);
          }
        }
        else
        {
          /* Not allowed to use RAM transfer, so fake the return of the RAMFetch
             message to the loader. */
          poll_block.user_message_acknowledge = ram_fetch;
          err_suppress_errors();

          dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);

          err = err_dump_suppressed();
          if (err == NULL)
          {
            /* Expect the loader to retry with a DataSaveAck in response to
               the original DataSave message. */
            assert(check_data_save_ack_msg(datasave_ref, &data_save_ack));
            assert(data_save_ack.data.data_save_ack.file_type == file_type);
            dataload_ref = init_data_load_msg(&poll_block, "<Wimp$Scrap>", estimated_size, file_type, data_save_ack.hdr.my_ref);
          }
        }
      }
    }

    if (err == NULL)
    {
      err_suppress_errors();

      dispatch_event(Wimp_EUserMessage, &poll_block);

      err = err_dump_suppressed();
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    if (err == NULL)
      break;

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  if (dataload_ref)
  {
    if (check_data_load_ack_msg(dataload_ref, "<Wimp$Scrap>", estimated_size, file_type))
    {
      /* It's the receiver's responsibility to delete the temporary file */
      OS_File_CatalogueInfo cat;
      assert_no_error(os_file_read_cat_no_path("<Wimp$Scrap>", &cat));
      assert(cat.object_type == ObjectType_NotFound);
    }
  }

  /* A single savebox should have been created */
  assert(!path_is_in_userdata("<Wimp$Scrap>"));
  assert(userdata_count_unsafe() == 0);
  savebox = userdata_find_by_file_name("");
  assert(savebox != NULL);
  id = pseudo_toolbox_find_by_template_name(template_name);
  assert(object_is_on_menu(id));
  dialogue_completed(id);

  Fortify_LeaveScope();
}

static void test14(void)
{
  /* Uncompressed file from app */
  do_data_transfer(TestUncompFileType, make_uncompressed_file, "SaveFednet", true);
}

static void test15(void)
{
  /* Compressed file from app */
  do_data_transfer(TestCompressedFileType, make_compressed_file, "SaveFile", true);
}

static void test16(void)
{
  /* Uncompressed file from app with bounce */
  do_data_transfer(TestUncompFileType, make_uncompressed_file, "SaveFednet", false);
}

static void test17(void)
{
  /* Uncompressed file from app with broken RAM transfer */
  unsigned long limit;

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    WimpPollBlock poll_block;
    int const datasave_ref = init_data_save_msg(&poll_block, TestDataSize, TestUncompFileType);
    const _kernel_oserror *err;

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    err_suppress_errors();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    err = err_dump_suppressed();
    if (err == NULL)
    {
      WimpMessage ram_fetch;
      int ram_transmit_ref;

      assert(check_ram_fetch_msg(datasave_ref, &ram_fetch));

      /* Fake a reply to the RAMFetch message with a RAMTransmit message,
         ensuring that we fill the buffer. */
      ram_transmit_ref = init_ram_transmit_msg(&poll_block, &ram_fetch, ram_fetch.data.ram_fetch.buffer_size);
      err_suppress_errors();

      dispatch_event(Wimp_EUserMessage, &poll_block);

      err = err_dump_suppressed();

      if (err == NULL)
      {
        assert(check_ram_fetch_msg(ram_transmit_ref, &ram_fetch));

        /* Instead of sending another RAMTransmit message to complete the protocol,
           fake the return of the second RAMFetch message to the loader. */
        poll_block.user_message_acknowledge = ram_fetch;
        err_suppress_errors();

        dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
        err = err_dump_suppressed();
      }
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);
    Fortify_LeaveScope();

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static bool fortify_detected = false;

static void fortify_check(void)
{
  Fortify_CheckAllMemory();
  assert(!fortify_detected);
}

static void fortify_output(char const *text)
{
  DEBUGF(text);
  if (strstr(text, "Fortify"))
  {
    assert(!fortify_detected);
  }
  if (strstr(text, "detected"))
  {
    fortify_detected = true;
  }
}

int main(int argc, char *argv[])
{
  NOT_USED(argc);
  NOT_USED(argv);

  DEBUG_SET_OUTPUT(DebugOutput_FlushedFile, "FednetCmpLog");
  Fortify_SetOutputFunc(fortify_output);
  atexit(fortify_check);

  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Load uncompressed file", test1 },
    { "Load compressed file", test2 },
    { "Load directory", test3 },
    { "Save compressed file", test4 },
    { "Save uncompressed file", test5 },
    { "Save directory", test6 },
    { "Batch compress", test7 },
    { "Batch decompress", test8 },
    { "RAM transmit uncompressed file", test9 },
    { "Uncompressed file from app with timeout", test10 },
    { "Compressed file from app with timeout", test11 },
    { "Transfer dir from app", test12 },
    { "Transfer app from app", test13 },
    { "Uncompressed file from app", test14 },
    { "Compressed file from app", test15 },
    { "Uncompressed file from app with bounce", test16 },
    { "Uncompressed file from app with broken RAM transfer", test17 }
  };

  initialise();

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

    unit_tests[count].test_func();

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
  }

  wipe(TEST_DATA_DIR);
  Fortify_OutputStatistics();
  return EXIT_SUCCESS;
}
