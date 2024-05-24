/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Menu attached to sky window (all levels)
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSMenus_h
#define SFSMenus_h

#include "toolbox.h"
#include "EditWin.h"

extern ObjectId EditMenu_sharedid, EffectMenu_sharedid;

void RootMenu_initialise(ObjectId id);

void EditMenu_update(EditWin *edit_win);
void EffectMenu_update(EditWin *edit_win);

void EditMenu_initialise(ObjectId id);
void EffectMenu_initialise(ObjectId id);

#endif
