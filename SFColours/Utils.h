/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Utility functions
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFCUtils_h
#define SFCUtils_h

#include <stdbool.h>
#include "toolbox.h"

void show_object_relative(unsigned int flags, ObjectId showobj, ObjectId relativeto, ObjectId parent, ComponentId parent_component);

bool showing_as_descendant(ObjectId self_id, ObjectId ancestor_id);

void scr_to_work_area_coords(int window_handle, int *x, int *y);

bool claim_drag(const WimpMessage *message, int const file_types[], int *my_ref);

#endif
