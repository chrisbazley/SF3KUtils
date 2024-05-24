/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Menu attached to colours window (all levels)
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFCMenus_h
#define SFCMenus_h

#include "toolbox.h"
#include "EditWin.h"

extern ObjectId EditMenu_sharedid, EffectMenu_sharedid;

void RootMenu_initialise(ObjectId id);

void EditMenu_initialise(ObjectId id);
void EditMenu_update(EditWin *edit_win);

void EffectMenu_initialise(ObjectId id);
void EffectMenu_update(EditWin *edit_win);

#endif
