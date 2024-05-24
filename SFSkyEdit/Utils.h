/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Utility functions
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSUtils_h
#define SFSUtils_h

#include <stdbool.h>
#include <stddef.h>

#include "toolbox.h"
#include "wimp.h"
#include "event.h"

#include "PalEntry.h"

WimpEventHandler watch_caret;
ToolboxEventHandler hand_back_caret;

void show_object_relative(unsigned int flags, ObjectId showobj, ObjectId relativeto, ObjectId parent, ComponentId parent_component);

void hide_shared_if_child(ObjectId parent_id, ObjectId shared_id);

bool showing_as_descendant(ObjectId self_id, ObjectId ancestor_id);

void set_button_colour(ObjectId window, ComponentId button, PaletteEntry colour);

bool claim_drag(const WimpMessage *message, int const file_types[], int *my_ref);

/* Calculate the last bit used (0 - 31) in the last word of each row of pixel
   data for a sprite. Assumes no lefthand wastage. */
int sprite_right_bit(int width, int bpp);

#endif
