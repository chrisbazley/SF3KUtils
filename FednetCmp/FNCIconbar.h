/*
 *  FednetCmp - Fednet file compression/decompression
 *  Iconbar icon
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCIconbar_h
#define FNCIconbar_h

#include <stdbool.h>

#include "toolbox.h"

void Iconbar_initialise(ObjectId id);
bool Iconbar_get_multi_dboxes(void);
void Iconbar_set_multi_dboxes(bool multi);

#endif
