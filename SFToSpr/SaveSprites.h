/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for Sprite file
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTSaveSprites_h
#define SFTSaveSprites_h

#include <stdbool.h>

#include "flex.h"

#include "SFTSaveBox.h"

_Optional SFTSaveBox *SaveSprites_create(char const *save_path, int x, bool data_saved,
  flex_ptr buffer, int input_file_type, _Optional SFTSaveBoxDeletedFn *deleted_cb);

#endif
