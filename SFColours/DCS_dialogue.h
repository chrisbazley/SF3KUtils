/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Discard/Cancel/Save dialogue
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFCDCS_dialogue_h
#define SFCDCS_dialogue_h

#include <stdbool.h>
#include "kernel.h"
#include "toolbox.h"

void DCS_initialise(ObjectId object);
void DCS_query_unsaved(ObjectId view, bool open_parent);

#endif
