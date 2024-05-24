/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Quit confirm dialogue box
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFCPreQuit_h
#define SFCPreQuit_h

#include <stdbool.h>
#include "toolbox.h"

void PreQuit_initialise(ObjectId id);
bool PreQuit_queryunsaved(int task_handle);

#endif
