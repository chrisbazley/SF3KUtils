/*
 *  FednetCmp - Fednet file compression/decompression
 *  Quit confirm dialogue box
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCPreQuit_h
#define FNCPreQuit_h

#include "toolbox.h"

void PreQuit_initialise(ObjectId id);
bool PreQuit_queryunsaved(int task_handle);

#endif
