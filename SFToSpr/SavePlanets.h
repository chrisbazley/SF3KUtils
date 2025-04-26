/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for SFSkyPic file
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTSavePlanets_h
#define SFTSavePlanets_h

#include <stdbool.h>
#include "flex.h"

#include "SFTSaveBox.h"
#include "SFgfxconv.h"

_Optional SFTSaveBox *SavePlanets_create(char const *save_path, int x, bool data_saved,
  flex_ptr sprites, PlanetSpritesContext const *context,
  _Optional SFTSaveBoxDeletedFn *deleted_cb);

#endif
