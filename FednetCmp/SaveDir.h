/*
 *  FednetCmp - Fednet file compression/decompression
 *  Save dialogue box for directory
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCSaveDir_h
#define FNCSaveDir_h

#include "FNCSaveBox.h"

FNCSaveBox *SaveDir_create(char const *input_path, int x,
                                  FNCSaveBoxDeletedFn *deleted_cb);

#endif
