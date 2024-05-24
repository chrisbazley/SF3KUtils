/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for directory
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTSaveDir_h
#define SFTSaveDir_h

#include "SFTSaveBox.h"

SFTSaveBox *SaveDir_create(char const *input_path, int x, SFTSaveBoxDeletedFn *deleted_cb);

#endif
