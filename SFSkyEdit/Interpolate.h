/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Interpolation dialogue box
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSInterpolate_h
#define SFSInterpolate_h

#include "toolbox.h"

extern ObjectId Interpolate_sharedid;
void Interpolate_initialise(ObjectId object);
void Interpolate_colour_selected(ComponentId parent_component, int colour);

#endif
