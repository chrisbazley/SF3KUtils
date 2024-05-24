/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Iconbar icon
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTIconbar_h
#define SFTIconbar_h

#include <stdbool.h>

#include "toolbox.h"

void Iconbar_initialise(ObjectId id);
bool Iconbar_get_multi_dboxes(void);
void Iconbar_set_multi_dboxes(bool multi);

#endif
