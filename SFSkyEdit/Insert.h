/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Insertion dialogue box
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSInsert_h
#define SFSInsert_h

#include "toolbox.h"
#include "Sky.h"
#include "EditWin.h"

extern ObjectId Insert_sharedid;
void Insert_initialise(ObjectId object);
void Insert_colour_selected(EditWin *edit_win,
  ComponentId parent_component, SkyColour colour);

#endif
