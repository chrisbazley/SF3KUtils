/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colour picker dialogue box
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
#include <stddef.h>
#include <assert.h>

/* RISC OS library files */
#include "toolbox.h"
#include "event.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "Pal256.h"
#include "Debug.h"

/* Local headers */
#include "Picker.h"
#include "SFCInit.h"
#include "EditWin.h"

#ifdef FORTIFY
#include "PseudoEvnt.h"
#include "PseudoTbox.h"
#endif

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


ObjectId Picker_sharedid = NULL_ObjectId;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int selhandler(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const Pal256ColourSelectedEvent * const pcse = (Pal256ColourSelectedEvent *)event;
  void *client_handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  NOT_USED(handle);

  DEBUGF("Received a Pal256_ColourSelected event (object = &%X, ancestor = &%X)\n",
        id_block->self_id, id_block->ancestor_id);

  if (!E(toolbox_get_client_handle(0, id_block->ancestor_id, &client_handle)))
  {
    /* Remap selected logical colours to the actual colour picked */
    EditWin_colour_selected(client_handle, pcse->colour_number);
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Picker_initialise(ObjectId id)
{
  Picker_sharedid = id;

  EF(Pal256_initialise(id, palette, &mfd, err_check_rep));

  EF(event_register_toolbox_handler(id,
                                    Pal256_ColourSelected,
                                    selhandler,
                                    (void *)NULL));
}
