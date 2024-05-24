/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Quit confirm dbox
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTPreQuit_h
#define SFTPreQuit_h

#include <stdbool.h>
#include "toolbox.h"

void PreQuit_initialise(ObjectId id);
bool PreQuit_queryunsaved(int task_handle);

#endif
