/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Insertion dialogue box
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSInsert_h
#define SFSInsert_h

#include "toolbox.h"

extern ObjectId Insert_sharedid;
void Insert_initialise(ObjectId object);
void Insert_colour_selected(EditWin *edit_win,
  ComponentId parent_component, int colour);

#endif
