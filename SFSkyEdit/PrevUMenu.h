/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Menu attached to preview window
 *  Copyright (C) 2009 Christopher Bazley
 */

#ifndef PrevUMenu_h
#define PrevUMenu_h

#include <stdbool.h>

#include "toolbox.h"

extern ObjectId PrevUMenu_sharedid;

void PrevUMenu_initialise(ObjectId id);
void PrevUMenu_set_toolbars(bool shown);

#endif
