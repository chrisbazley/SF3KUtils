/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky preview savebox
 *  Copyright (C) 2009 Christopher Bazley
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
#include <string.h>
#include <assert.h>

/* RISC OS library files */
#include "kernel.h"
#include "swis.h"
#include "toolbox.h"
#include "saveas.h"
#include "event.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "SprFormats.h"
#include "Hourglass.h"
#include "msgtrans.h"
#include "NoBudge.h"
#include "FilePerc.h"
#include "PseudoFlex.h"
#include "Debug.h"

/* Local headers */
#include "SavePrev.h"
#include "Preview.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Constant numeric values */
enum
{
  PreExpandHeap = 512 /* Number of bytes to pre-allocate before disabling
                         flex budging (and thus heap expansion). */
};

ObjectId SavePrev_sharedid = NULL_ObjectId;

static _Optional char *ss_file_name = NULL;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Dialogue box opening */
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Set the suggested file path */
  if (ss_file_name != NULL)
    ON_ERR_RPT(saveas_set_file_name(0, id_block->self_id, &*ss_file_name));

  /* Set the estimated (actual) file size */
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    /* A sprite file is just a sprite area without the first word */
    flex_ptr const sa = Preview_get_anchor(client_handle);
    int const size = flex_size(sa) - offsetof(SpriteAreaHeader, sprite_count);
    ON_ERR_RPT(saveas_set_file_size(0, id_block->self_id, size));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int save_to_file(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)event;
  _Optional const _kernel_oserror *e = NULL;
  void *client_handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  e = toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle);
  if (e == NULL)
  {
    flex_ptr const sa = Preview_get_anchor(client_handle);

    /* A sprite file is just a sprite area without the first word */
    e = file_perc_save(FilePercOp_Save,
                       sastfe->filename,
                       FileType_Sprite,
                       sa,
                       offsetof(SpriteAreaHeader, sprite_count),
                       flex_size(sa));
  }

  if (e != NULL)
    e = msgs_error_subn(e->errnum, "SaveFail", 1, e->errmess);

  ON_ERR_RPT(e);
  saveas_file_save_completed((e == NULL),
                             id_block->self_id,
                             sastfe->filename);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int fill_buffer(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)event;
  _Optional char *buffer = NULL;
  void *client_handle;
  int chunk_size = 0;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    /* A sprite file is just a sprite area without the first word */
    flex_ptr const sa = Preview_get_anchor(client_handle);
    int const size = flex_size(sa) - offsetof(SpriteAreaHeader, sprite_count);

    /* Calculate number of bytes still to send */
    chunk_size = size - safbe->no_bytes;
    DEBUGF("%d bytes already sent of total %d, %d bytes remain\n",
      safbe->no_bytes, size, chunk_size);

    if (chunk_size < 0)
    {
      /* We have already sent all the data */
      chunk_size = 0;
    }
    else if (chunk_size > safbe->size)
    {
      /* We can't fit all of remaining data in the recipient's buffer
         so just fill it */
      chunk_size = safbe->size;
    }

    /* Calculate address within output buffer to copy data from */
    nobudge_register(PreExpandHeap); /* protect de-reference of flex pointer */
    buffer = (char *)*sa + offsetof(SpriteAreaHeader, sprite_count) +
             safbe->no_bytes;
  }

  /* We're in an impossible situation if an error occurred because the SaveAs
     module has already acknowledged the RAMFetch message. It seems better to
     deliver 0 bytes than leave the other task expectant (e.g. leaking any
     input buffer that it allocated). :-( */
  static char dummy;
  ON_ERR_RPT(saveas_buffer_filled(0, id_block->self_id,
                                  buffer ? &*buffer : &dummy, chunk_size));

  if (buffer != NULL)
    nobudge_deregister();

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void free_file_name(void)
{
  free(ss_file_name);
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void SavePrev_initialise(ObjectId const id)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      SaveAs_AboutToBeShown,
      about_to_be_shown
    },
    {
      SaveAs_SaveToFile,
      save_to_file
    },
    {
      SaveAs_FillBuffer,
      fill_buffer
    }
  };
  int len;

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    EF(event_register_toolbox_handler(id,
                                      tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler,
                                      (void *)NULL));
  }

  SavePrev_sharedid = id;

  /* Read default leafname */
  atexit(free_file_name);
  EF(saveas_get_file_name(0, id, 0, 0, &len));
  ss_file_name = malloc(len);
  if (ss_file_name == NULL)
  {
    err_complain_fatal(DUMMY_ERRNO, msgs_lookup("NoMem"));
  }
  else
  {
    EF(saveas_get_file_name(0, id, &*ss_file_name, len, NULL));
  }
}
