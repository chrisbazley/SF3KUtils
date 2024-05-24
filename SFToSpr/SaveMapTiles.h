/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box for SFMapGfx file
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTSaveMapTiles_h
#define SFTSaveMapTiles_h

#include <stdbool.h>
#include "flex.h"

#include "SFTSaveBox.h"
#include "SFgfxconv.h"

SFTSaveBox *SaveMapTiles_create(char const *save_path, int x, bool data_saved,
  flex_ptr sprites, MapTileSpritesContext const *context,
  SFTSaveBoxDeletedFn *deleted_cb);

#endif
