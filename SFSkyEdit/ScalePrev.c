/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky preview scale dialogue box
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
#include <assert.h>
#include <stddef.h>

/* RISC OS library files */
#include "toolbox.h"
#include "scale.h"
#include "event.h"

/* My library files */
#include "Macros.h"
#include "Err.h"

/* Local headers */
#include "ScalePrev.h"
#include "Preview.h"

ObjectId ScalePrev_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int scale_about_to_be_shown(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(event_code);
  NOT_USED(event);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Ensure that the scale value initially displayed reflects the current scale
     of the sky preview which is an ancestor of this dialogue box */
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    ON_ERR_RPT(scale_set_value(0, id_block->self_id,
                               Preview_get_scale(client_handle)));
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int scale_apply_factor(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const ScaleApplyFactorEvent * const safe = (ScaleApplyFactorEvent *)event;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  /* Apply the selected scale to the sky preview which is an ancestor of this
     dialogue box */
  void *client_handle;
  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    Preview_set_scale(client_handle, safe->factor);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void ScalePrev_initialise(ObjectId id)
{
  static const struct
  {
    int event_code;
    ToolboxEventHandler *handler;
  }
  tbox_handlers[] =
  {
    {
      Scale_AboutToBeShown,
      scale_about_to_be_shown,
    },
    {
      Scale_ApplyFactor,
      scale_apply_factor,
    }
  };

  /* Register Toolbox event handlers */
  for (size_t i = 0; i < ARRAY_SIZE(tbox_handlers); i++)
  {
    EF(event_register_toolbox_handler(id, tbox_handlers[i].event_code,
                                      tbox_handlers[i].handler, NULL));
  }

  ScalePrev_sharedid = id;
}
