/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Graphics conversion routines
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTgfxconv_h
#define SFTgfxconv_h

#include <stdint.h>
#include <stdbool.h>
#include "Reader.h"
#include "Writer.h"
#include "SFError.h"

typedef struct ConvertIter
{
  int32_t pos;
  int32_t count;
  Reader *reader;
  Writer *writer;
  SFError (*convert)(struct ConvertIter *iter);
} ConvertIter;

SFError convert_advance(ConvertIter *iter);
SFError convert_finish(ConvertIter *iter);


enum {
  PlanetWidth  = 36,
  PlanetHeight = 36,
  PlanetMargin = 2,
  PlanetBitmapSize = WORD_ALIGN(PlanetWidth) * PlanetHeight,
  PlanetSprWidth = PlanetWidth - PlanetMargin,
  PlanetSprBitmapSize = WORD_ALIGN(PlanetSprWidth) * PlanetHeight,
  PlanetMax = 1,
};

typedef struct
{
  int32_t x_offset;
  int32_t y_offset;
}
PlanetsPaintOffset;

typedef struct
{
  int32_t image_A;
  int32_t image_B;
}
PlanetsBitmapOffset;

typedef struct
{
  int32_t last_image_num;
  PlanetsPaintOffset  paint_coords[PlanetMax + 1];
  PlanetsBitmapOffset data_offsets[PlanetMax + 1];
}
PlanetsHeader;

typedef struct
{
  long int offsets[PlanetMax + 1];
  int count;
  PlanetsHeader hdr;
  bool got_hdr;
  bool fixed_hdr;
}
PlanetSpritesContext;

typedef struct {
  long int const *offsets;
  PlanetsHeader hdr;
  ConvertIter super;
  uint8_t tmp[PlanetSprBitmapSize];
} SpritesToPlanetsIter;

SFError sprites_to_planets_init(SpritesToPlanetsIter *iter,
  Reader *reader, Writer *writer, PlanetSpritesContext const *context);

SFError sprites_to_planets(Reader *reader, Writer *writer, PlanetSpritesContext const *context);

typedef struct {
  PlanetsHeader hdr;
  ConvertIter super;
  uint8_t tmp[PlanetBitmapSize];
} PlanetsToSpritesIter;

SFError planets_to_sprites_init(PlanetsToSpritesIter *iter,
  Reader *reader, Writer *writer);

SFError planets_to_sprites_ext_init(PlanetsToSpritesIter *iter,
                                    Reader *reader, Writer *writer);

int planets_size(PlanetsHeader const *hdr);
SFError planets_to_sprites_ext(Reader *reader, Writer *writer);
SFError planets_to_sprites(Reader *reader, Writer *writer);
SFError planets_to_csv(Reader *reader, Writer *writer);
SFError csv_to_planets(Reader *reader, PlanetsHeader *hdr);


enum {
  MapTileWidth  = 16,
  MapTileHeight = 16,
  MapTileBitmapSize = WORD_ALIGN(MapTileWidth) * MapTileHeight,
  MapTileMax = 254,
  MapAnimFrameCount = 4,
  MapAnimTriggerCount = 4,
};

typedef struct
{
  int32_t last_tile_num;
  uint8_t splash_anim_1[MapAnimFrameCount];
  uint8_t splash_anim_2[MapAnimFrameCount];
  uint8_t splash_2_triggers[MapAnimTriggerCount];
}
MapTilesHeader;

typedef struct
{
  long int offsets[MapTileMax + 1];
  int count;
  MapTilesHeader hdr;
  bool got_hdr;
  bool fixed_hdr;
}
MapTileSpritesContext;

typedef struct {
  long int const *offsets;
  ConvertIter super;
  uint8_t tmp[MapTileBitmapSize];
} SpritesToTilesIter;

SFError sprites_to_tiles_init(SpritesToTilesIter *iter,
  Reader *reader, Writer *writer, MapTileSpritesContext const *context);

SFError sprites_to_tiles(Reader *reader, Writer *writer, MapTileSpritesContext const *context);

typedef struct {
  ConvertIter super;
  uint8_t tmp[MapTileBitmapSize];
} TilesToSpritesIter;

SFError tiles_to_sprites_init(TilesToSpritesIter *iter,
  Reader *reader, Writer *writer);

SFError tiles_to_sprites_ext_init(TilesToSpritesIter *iter,
  Reader *reader, Writer *writer);

int tiles_size(MapTilesHeader const *hdr);
SFError tiles_to_sprites_ext(Reader *reader, Writer *writer);
SFError tiles_to_sprites(Reader *reader, Writer *writer);
SFError tiles_to_csv(Reader *reader, Writer *writer);
SFError csv_to_tiles(Reader *reader, MapTilesHeader *hdr);


enum {
  SkyWidth  = 4,
  SkyHeight = 126,
  SkyBitmapSize = WORD_ALIGN(SkyWidth) * SkyHeight,
  SkyMax = 0,
};

typedef struct
{
  int32_t render_offset;
  int32_t min_stars_height;
}
SkyHeader;

typedef struct
{
  long int offset;
  int count;
  SkyHeader hdr;
  bool got_hdr;
  bool fixed_stars;
  bool fixed_render;
}
SkySpritesContext;

typedef struct {
  long int offset;
  ConvertIter super;
  uint8_t tmp[SkyBitmapSize];
} SpritesToSkyIter;

SFError sprites_to_sky_init(SpritesToSkyIter *iter,
  Reader *reader, Writer *writer, SkySpritesContext const *context);

SFError sprites_to_sky(Reader *reader, Writer *writer, SkySpritesContext const *context);

typedef struct {
  ConvertIter super;
  uint8_t tmp[SkyBitmapSize];
} SkyToSpritesIter;

SFError sky_to_sprites_init(SkyToSpritesIter *iter,
  Reader *reader, Writer *writer);

SFError sky_to_sprites_ext_init(SkyToSpritesIter *iter,
  Reader *reader, Writer *writer);

int sky_size(void);
SFError sky_to_sprites_ext(Reader *reader, Writer *writer);
SFError sky_to_sprites(Reader *reader, Writer *writer);
SFError sky_to_csv(Reader *reader, Writer *writer);
SFError csv_to_sky(Reader *reader, SkyHeader *hdr);


enum {
  SpriteNameSize = 13, /* including terminator */
};

typedef struct
{
  MapTileSpritesContext tiles;
  PlanetSpritesContext planets;
  SkySpritesContext sky;
  bool bad_sprite;
  char bad_name[SpriteNameSize];
}
ScanSpritesContext;

typedef struct
{
  ConvertIter super;
  ScanSpritesContext *context;
} ScanSpritesIter;

SFError scan_sprite_file_init(ScanSpritesIter *iter, Reader *reader,
  ScanSpritesContext *context);

SFError scan_sprite_file(Reader *reader, ScanSpritesContext *context);
int count_spr_types(ScanSpritesContext const *context);

#endif
