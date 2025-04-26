/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for SFSkyCol file
 *  Copyright (C) 2006 Christopher Bazley
 */

#ifndef SFTSaveSky_h
#define SFTSaveSky_h

#include <stdbool.h>
#include "flex.h"

#include "SFTSaveBox.h"
#include "SFgfxconv.h"

_Optional SFTSaveBox *SaveSky_create(char const *save_path, int x, bool data_saved,
  flex_ptr sprites, SkySpritesContext const *context,
  _Optional SFTSaveBoxDeletedFn *deleted_cb);

#endif
