/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Graphics conversion routines
 *  Copyright (C) 2000 Christopher Bazley
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public Licence as published by
 *  the Free Software Foundation; either version 2 of the Licence, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ANSI headers */
#include <string.h>
#include "stdlib.h"
#include "stdio.h"
#include <stddef.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>
#include <ctype.h>

/* My library files */
#include "Macros.h"
#include "CSV.h"
#include "Debug.h"
#include "Reader.h"
#include "Writer.h"
#include "SprFormats.h"

/* Local headers */
#include "Utils.h"
#include "SFgfxconv.h"
#include "SFError.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

#define TILE_SPR_NAME "tile_"
#define TILE_SPR_TAG "ANIM"

#define PLANET_SPR_NAME "planet_"
#define PLANET_SPR_TAG "OFFS"

#define SKY_SPR_NAME "sky"
#define SKY_SPR_TAG "HEIG"

/* Constant numeric values */
enum
{
  CSVBufferSize = 256,
  SprAreaHdrSize = sizeof(int32_t) * 4,
  SprHdrSize = sizeof(int32_t) * 11,
  SpriteType = 13, /* Mode number */
  SpriteExtTagLen = 4,
  MapTilesHeaderSize = sizeof(int32_t) + 12,
  MapTileSprSize = SprHdrSize + MapTileBitmapSize,
  MapTileSprExtDataSize = SpriteExtTagLen + 12,
  PlanetHeaderSize = sizeof(int32_t) * 9,
  /* Don't allow files more than double the expected size */
  PlanetFileSizeMax = 2 * (PlanetHeaderSize + (PlanetBitmapSize * 2 * (PlanetMax + 1))),
  PlanetSprSize = SprHdrSize + PlanetSprBitmapSize,
  PlanetSprExtDataHdrSize = SpriteExtTagLen + sizeof(int32_t),
  PlanetSprExtDataOffsetSize = sizeof(int32_t) * 2,
  SkyHeaderSize = sizeof(int32_t) * 2,
  SkySprSize = SprHdrSize + SkyBitmapSize,
  SkySprExtDataSize = SpriteExtTagLen + (sizeof(int32_t) * 2),
  SkySprCount = SkyMax + 1,
  SkyRenderMin = 0,
  SkyRenderMax = 2048,
  SkyStarsMin = -32768,
  SkyStarsMax = 2048,
};

typedef struct
{
  int32_t size;
  char name[SpriteNameSize];
  int32_t width;
  int32_t height;
  int32_t left_bit;
  int32_t right_bit;
  int32_t image;
  int32_t mask;
  int32_t type;
}
SFSpriteHeader;

/* Extension data to be embedded in a sprite area for a map tiles set */
typedef struct
{
  uint32_t tag;
  uint8_t  splash_anim_1[4];
  uint8_t  splash_anim_2[4];
  uint8_t  splash_2_triggers[4];
}
TileSprExtData;

/* Extension data to be embedded in a sprite area for a planet images set */
typedef struct
{
  uint32_t           tag;
  uint32_t           num_offsets;
  PlanetsPaintOffset paint_coords[];
}
PlanetSprExtData;

/* Extension data to be embedded in a sprite area for a sky definition */
typedef struct
{
  uint32_t tag;
  int32_t  render_offset;
  int32_t  min_stars_height;
}
SkySprExtData;

/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static inline bool sprite_type_has_8_bpp(int32_t const sprite_type)
{
  bool has_8_bpp = false;
  DEBUGF("Checking sprite type %" PRId32 "\n", sprite_type);

  unsigned long const type = ((long)sprite_type & SPRITE_INFO_TYPE_MASK) >> SPRITE_INFO_TYPE_SHIFT;

  if (type == SPRITE_TYPE_OLD)
  {
    /* Old sprite format: check whether screen mode has 8 bits per pixel */
    static const char mode_nos[] = {10, 13, 15, 21, 24, 28, 32, 36, 40};

    has_8_bpp = false;
    for (size_t i = 0; i < ARRAY_SIZE(mode_nos); i++)
    {
      if (mode_nos[i] == sprite_type)
      {
        has_8_bpp = true;
        break;
      }
    }
  }
  else
  {
    /* New sprite format: check whether it has 8 bits per pixel */
    has_8_bpp = (type == SPRITE_TYPE_8BPP);
  }
  DEBUGF("  Type %s 8 bpp\n", has_8_bpp ? "is" : "isn't");
  return has_8_bpp;
}

/* ----------------------------------------------------------------------- */

static inline bool sprite_has_dims(SFSpriteHeader const *const sph, int const width,
  int const height)
{
  assert(sph);
  assert(width > 0);
  assert(height > 0);

  return (sph->width == (WORD_ALIGN(width) / 4) - 1 &&
          sph->height == height - 1 &&
          sph->left_bit == 0 &&
          sph->right_bit == SPRITE_RIGHT_BIT(width, 8) &&
          sprite_type_has_8_bpp(sph->type));
}

/* ----------------------------------------------------------------------- */

static void write_sprite_area_hdr(int32_t const sprite_count,
  int32_t const ext_data_size, int32_t const sprite_size, Writer *const writer)
{
  assert(sprite_count >= 0);
  assert(ext_data_size >= 0);
  assert(sprite_size >= SprHdrSize);
  int32_t const first = SprAreaHdrSize + ext_data_size;
  int32_t const used = first + (sprite_count * sprite_size);
  writer_fwrite_int32(sprite_count, writer);
  writer_fwrite_int32(first, writer);
  writer_fwrite_int32(used, writer);
}

/* ----------------------------------------------------------------------- */

static void write_spr_header(int32_t const sprite_size, char const *const name,
  int32_t const w, int32_t const h, Writer *const writer)
{
  char namebuf[12] = {0};

  assert(sprite_size >= 0);
  assert(name);
  /* Note: sprite names of maximum length needn't be terminated. */
  assert(strlen(name) <= sizeof(namebuf));
  assert(w >= 0);
  assert(h >= 0);

  writer_fwrite_int32(sprite_size, writer);
  strncpy(namebuf, name, sizeof(namebuf));
  writer_fwrite(namebuf, sizeof(namebuf), 1, writer);
  writer_fwrite_int32(WORD_ALIGN(w) / 4 - 1, writer);
  writer_fwrite_int32(h - 1, writer);
  writer_fwrite_int32(0, writer); /* left bit */
  writer_fwrite_int32(SPRITE_RIGHT_BIT(w, 8), writer);
  writer_fwrite_int32(SprHdrSize, writer);
  writer_fwrite_int32(SprHdrSize, writer);
  writer_fwrite_int32(SpriteType, writer);
}

/* ----------------------------------------------------------------------- */

static SFError read_fail(Reader *const reader)
{
  return reader_feof(reader) ? SFError_Trunc : SFError_ReadFail;
}

/* ----------------------------------------------------------------------- */

static inline SFError copy_n_flip(Reader *const reader, Writer *const writer,
  uint8_t *const tmp, int const width, int const height)
{
  assert(tmp);
  DEBUGF("Copy and flip %d x %d bitmap\n", width, height);
  int const awidth = WORD_ALIGN(width);
  int const size = height * awidth;

  if (!reader_fread(tmp, size, 1, reader))
  {
    return read_fail(reader);
  }

  /* Append the raw bitmap to the output sprite, one row at a time
     (same pixel format etc).
     Note that the bitmap is flipped vertically during copying. */
  uint8_t *src = tmp + size;
  for (int row = 0; row < height && !writer_ferror(writer); ++row)
  {
    src -= awidth;
    writer_fwrite(src, awidth, 1, writer);
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void init_tiles_hdr(MapTilesHeader *const hdr)
{
  assert(hdr);
  hdr->last_tile_num = -1;
}

/* ----------------------------------------------------------------------- */

static inline bool fix_tiles_anim(MapTilesHeader *const hdr)
{
  bool fixed = false;
  assert(hdr);
  DEBUGF("Last tile is %" PRId32 "\n", hdr->last_tile_num);
  assert(hdr->last_tile_num >= 0);
  assert(hdr->last_tile_num <= MapTileMax);

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_1); ++i)
  {
    if (hdr->splash_anim_1[i] > hdr->last_tile_num)
    {
      DEBUGF("Forcing 1st splash animation frame %zu within bounds (was %d)\n",
             i, hdr->splash_anim_1[i]);

      hdr->splash_anim_1[i] = hdr->last_tile_num;
      fixed = true;
    }
  }

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_2); ++i)
  {
    if (hdr->splash_anim_2[i] > hdr->last_tile_num)
    {
      DEBUGF("Forcing 2nd splash animation frame %zu within bounds (was %d)\n",
             i, hdr->splash_anim_2[i]);

      hdr->splash_anim_2[i] = hdr->last_tile_num;
      fixed = true;
    }
  }

  return fixed;
}

/* ----------------------------------------------------------------------- */

static bool read_tiles_anim(MapTilesHeader *const hdr, Reader *const reader)
{
  assert(hdr);
  return reader_fread(hdr->splash_anim_1, sizeof(hdr->splash_anim_1), 1, reader) &&
         reader_fread(hdr->splash_anim_2, sizeof(hdr->splash_anim_2), 1, reader) &&
         reader_fread(hdr->splash_2_triggers, sizeof(hdr->splash_2_triggers), 1, reader);
}

/* ----------------------------------------------------------------------- */

static void write_tiles_anim(MapTilesHeader const *const hdr, Writer *const writer)
{
  assert(hdr);

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_1); ++i)
  {
    assert(hdr->splash_anim_1[i] <= hdr->last_tile_num);
  }
  writer_fwrite(hdr->splash_anim_1, sizeof(hdr->splash_anim_1), 1, writer);

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_2); ++i)
  {
    assert(hdr->splash_anim_2[i] <= hdr->last_tile_num);
  }
  writer_fwrite(hdr->splash_anim_2, sizeof(hdr->splash_anim_2), 1, writer);

  writer_fwrite(hdr->splash_2_triggers, sizeof(hdr->splash_2_triggers), 1, writer);
}

/* ----------------------------------------------------------------------- */

static SFError read_tiles_hdr(MapTilesHeader *const hdr, Reader *const reader)
{
  assert(hdr);
  if (!reader_fread_int32(&hdr->last_tile_num, reader))
  {
    return read_fail(reader);
  }

  /* Check that the no. of tiles claimed to be in the file is sensible */
  int32_t const last_tile_num = hdr->last_tile_num;
  DEBUG("File contains %" PRId32 " tiles", last_tile_num + 1);
  if ((last_tile_num < 0) || (last_tile_num > MapTileMax))
  {
    return SFError_BadNumGFX;
  }

  if (!read_tiles_anim(hdr, reader))
  {
    return read_fail(reader);
  }

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_1); ++i)
  {
    if (hdr->splash_anim_1[i] > last_tile_num)
    {
      return SFError_BadAnims;
    }
  }

  for (size_t i = 0; i < ARRAY_SIZE(hdr->splash_anim_2); ++i)
  {
    if (hdr->splash_anim_2[i] > last_tile_num)
    {
      return SFError_BadAnims;
    }
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void write_tiles_hdr(MapTilesHeader const *const hdr, Writer *const writer)
{
  assert(hdr);

  assert(hdr->last_tile_num >= 0);
  assert(hdr->last_tile_num <= MapTileMax);
  writer_fwrite_int32(hdr->last_tile_num, writer);

  write_tiles_anim(hdr, writer);
}

/* ----------------------------------------------------------------------- */

static SFError read_tiles_ext(ScanSpritesContext *const context, int32_t const ext_size,
                              Reader *const reader)
{
  assert(ext_size >= 0);
  assert(context);

  if (ext_size == MapTileSprExtDataSize)
  {
    context->tiles.got_hdr = true;

    if (!read_tiles_anim(&context->tiles.hdr, reader))
    {
      return read_fail(reader);
    }

    /* Can't validate data here because the final number of tiles isn't known yet */
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void write_tiles_ext(MapTilesHeader const *const hdr, Writer *const writer)
{
  writer_fwrite(TILE_SPR_TAG, SpriteExtTagLen, 1, writer);
  write_tiles_anim(hdr, writer);
}

/* ----------------------------------------------------------------------- */

static SFError copy_n_flip_tile(Reader *const reader, Writer *const writer,
  uint8_t *const tmp)
{
  return copy_n_flip(reader, writer, tmp, MapTileWidth, MapTileHeight);
}

/* ----------------------------------------------------------------------- */

static inline int parse_tile_sprite_name(char const *const name)
{
  assert(name);
  long int tile_num = -1;

  if (strncmp(name, TILE_SPR_NAME, sizeof(TILE_SPR_NAME)-1) == 0 &&
      isdigit(name[sizeof(TILE_SPR_NAME)-1]))
  {
    char *endp;
    tile_num = strtol(name + sizeof(TILE_SPR_NAME)-1, &endp, 10);

    if (tile_num > MapTileMax || *endp != '\0')
    {
      tile_num = -1;
    }
  }

  assert(tile_num <= INT_MAX);
  return (int)tile_num;
}

/* ----------------------------------------------------------------------- */

static bool tiles_sprite_identifier(long int const fpos,
  char const *const name, ScanSpritesContext *const context)
{
  assert(context);

  int const tile_num = parse_tile_sprite_name(name);

  if (tile_num >= 0)
  {
    assert((unsigned)tile_num < ARRAY_SIZE(context->tiles.offsets));
    assert(!context->tiles.offsets[tile_num]);
    context->tiles.offsets[tile_num] = fpos;

    if (tile_num > context->tiles.hdr.last_tile_num)
    {
      context->tiles.hdr.last_tile_num = tile_num;
    }

    ++context->tiles.count;
    return true;
  }

  return false;
}

/* ----------------------------------------------------------------------- */

static SFError tiles_to_sprites_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= MapTileMax + 1);

  char name[SpriteNameSize] = {0};
  char numstr[16];
  int nout = sprintf(numstr, "%" PRId32, iter->pos);
  assert(nout >= 0); /* no formatting error */
  NOT_USED(nout);
  strncat(name, TILE_SPR_NAME, sizeof(name)-1);
  strncat(name, numstr, sizeof(name) - sizeof(TILE_SPR_NAME));
  DEBUGF("Sprite name is %s\n", name);

  write_spr_header(MapTileSprSize, name, MapTileWidth, MapTileHeight, iter->writer);

  TilesToSpritesIter *const sub = CONTAINER_OF(iter, TilesToSpritesIter, super);
  return copy_n_flip_tile(iter->reader, iter->writer, sub->tmp);
}

/* ----------------------------------------------------------------------- */

static SFError sprites_to_tiles_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= MapTileMax + 1);

  SpritesToTilesIter *const sub = CONTAINER_OF(iter, SpritesToTilesIter, super);
  long int const offset = sub->offsets[iter->pos];
  SFError err = SFError_OK;

  DEBUGF("Sprite for tile %d has offset %ld\n", iter->pos, offset);
  if (offset)
  {
    if (reader_fseek(iter->reader, offset, SEEK_SET))
    {
      return SFError_BadSeek;
    }

    err = copy_n_flip_tile(iter->reader, iter->writer, sub->tmp);
  }
  else
  {
    for (size_t i = 0; i < ARRAY_SIZE(sub->tmp); ++i)
    {
      sub->tmp[i] = 0;
    }
    writer_fwrite(sub->tmp, sizeof(sub->tmp), 1, iter->writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

static inline void init_planets_hdr(PlanetsHeader *const hdr)
{
  assert(hdr);
  hdr->last_image_num = -1;

  int expected_offset = PlanetHeaderSize;
  for (size_t i = 0; i < ARRAY_SIZE(hdr->data_offsets); ++i)
  {
    hdr->data_offsets[i].image_A = expected_offset;
    expected_offset += PlanetBitmapSize;
    hdr->data_offsets[i].image_B = expected_offset;
    expected_offset += PlanetBitmapSize;

    DEBUG("Initialized data offsets for image %zu: %" PRId32 ",%" PRId32, i,
          hdr->data_offsets[i].image_A,
          hdr->data_offsets[i].image_B);
  }
}

/* ----------------------------------------------------------------------- */

static bool fix_planets_coords(PlanetsPaintOffset *const paint_offset)
{
  assert(paint_offset);
  bool fixed = false;

  DEBUGF("Paint offsets are %" PRId32 ",%" PRId32 "\n",
         paint_offset->x_offset, paint_offset->y_offset);

  if (paint_offset->x_offset > 0 ||
      paint_offset->x_offset < -PlanetWidth)
  {
    DEBUG("Forcing X offset within bounds");
    if (paint_offset->x_offset > 0)
    {
      paint_offset->x_offset = 0;
    }
    else
    {
      paint_offset->x_offset = -PlanetWidth;
    }
    fixed = true;
  }

  if (paint_offset->y_offset > 0 ||
      paint_offset->y_offset < -PlanetHeight)
  {
    DEBUG("Forcing Y offset within bounds");
    if (paint_offset->y_offset > 0)
    {
      paint_offset->y_offset = 0;
    }
    else
    {
      paint_offset->y_offset = -PlanetHeight;
    }
    fixed = true;
  }
  return fixed;
}

/* ----------------------------------------------------------------------- */

static bool read_planets_coords(PlanetsHeader *const hdr,
  int32_t const ncoords, Reader *const reader)
{
  assert(hdr);
  assert(ncoords >= 0);
  assert((uint32_t)ncoords <= ARRAY_SIZE(hdr->paint_coords));

  for (int32_t i = 0; i < ncoords; i++)
  {
    assert((uint32_t)i < ARRAY_SIZE(hdr->paint_coords));
    if (!reader_fread_int32(&hdr->paint_coords[i].x_offset, reader) ||
        !reader_fread_int32(&hdr->paint_coords[i].y_offset, reader))
    {
      return false;
    }
  }
  return true;
}

/* ----------------------------------------------------------------------- */

static void write_planets_coords(PlanetsHeader const *const hdr,
  int32_t const ncoords, Writer *const writer)
{
  assert(hdr);
  assert(ncoords >= 0);
  assert((uint32_t)ncoords <= ARRAY_SIZE(hdr->paint_coords));

  for (int32_t i = 0; i < ncoords && !writer_ferror(writer); i++)
  {
    assert(hdr->paint_coords[i].x_offset <= 0);
    assert(hdr->paint_coords[i].x_offset >= -PlanetWidth);
    writer_fwrite_int32(hdr->paint_coords[i].x_offset, writer);

    assert(hdr->paint_coords[i].y_offset <= 0);
    assert(hdr->paint_coords[i].y_offset >= -PlanetHeight);
    writer_fwrite_int32(hdr->paint_coords[i].y_offset, writer);
  }
}

/* ----------------------------------------------------------------------- */

static SFError read_planets_hdr(PlanetsHeader *const hdr, Reader *const reader)
{
  assert(hdr);
  if (!reader_fread_int32(&hdr->last_image_num, reader))
  {
    return read_fail(reader);
  }

  /* Check that the no. of tiles claimed to be in the file is sensible */
  DEBUG("File contains %" PRId32 " images", hdr->last_image_num + 1);
  if (hdr->last_image_num < 0 || hdr->last_image_num > PlanetMax)
  {
    return SFError_BadNumGFX;
  }

  if (!read_planets_coords(hdr, hdr->last_image_num + 1, reader))
  {
    return read_fail(reader);
  }

  for (int32_t i = 0; i <= hdr->last_image_num; i++)
  {
    if (hdr->paint_coords[i].x_offset > 0 ||
        hdr->paint_coords[i].x_offset < -PlanetWidth)
    {
      return SFError_BadPaintOff;
    }

    if (hdr->paint_coords[i].y_offset > 0 ||
        hdr->paint_coords[i].y_offset < -PlanetHeight)
    {
      return SFError_BadPaintOff;
    }
  }

  long int const skip_bytes = (ARRAY_SIZE(hdr->paint_coords) - 1l - hdr->last_image_num) *
                              sizeof(uint32_t) * 2;
  if (reader_fseek(reader, skip_bytes, SEEK_CUR))
  {
    return SFError_BadSeek;
  }

  int32_t min_offset = PlanetHeaderSize;

  for (int32_t i = 0; i <= hdr->last_image_num; i++)
  {
    if (!reader_fread_int32(&hdr->data_offsets[i].image_A, reader) ||
        !reader_fread_int32(&hdr->data_offsets[i].image_B, reader))
    {
      return read_fail(reader);
    }

    /* Check that the bitmap data offsets are sensible */
    DEBUGF("Data offsets for image %" PRId32 " are %" PRId32 ",%" PRId32 "\n", i,
           hdr->data_offsets[i].image_A, hdr->data_offsets[i].image_B);

    if (hdr->data_offsets[i].image_A < min_offset)
    {
      return SFError_BadDataOff;
    }

    if (hdr->data_offsets[i].image_B < PlanetBitmapSize)
    {
      return SFError_BadDataOff;
    }

    if (hdr->data_offsets[i].image_B - PlanetBitmapSize < hdr->data_offsets[i].image_A)
    {
      return SFError_BadDataOff;
    }

    if (hdr->data_offsets[i].image_B > PlanetFileSizeMax - PlanetBitmapSize)
    {
      return SFError_BadDataOff;
    }

    min_offset = hdr->data_offsets[i].image_B + PlanetBitmapSize;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void write_planets_hdr(PlanetsHeader const *const hdr, Writer *const writer)
{
  assert(hdr);

  assert(hdr->last_image_num >= 0);
  assert(hdr->last_image_num <= PlanetMax);
  writer_fwrite_int32(hdr->last_image_num, writer);

  write_planets_coords(hdr, ARRAY_SIZE(hdr->paint_coords), writer);

  for (size_t i = 0; i < ARRAY_SIZE(hdr->data_offsets) && !writer_ferror(writer); i++)
  {
    DEBUG("Writing data offsets for image %zu: %" PRId32 ",%" PRId32, i,
          hdr->data_offsets[i].image_A,
          hdr->data_offsets[i].image_B);

    assert(hdr->data_offsets[i].image_A >= PlanetHeaderSize);
    assert(hdr->data_offsets[i].image_B >= PlanetHeaderSize + PlanetBitmapSize);
    assert(hdr->data_offsets[i].image_A <= hdr->data_offsets[i].image_B - PlanetBitmapSize);
    assert(hdr->data_offsets[i].image_B <= PlanetFileSizeMax - PlanetBitmapSize);

    writer_fwrite_int32(hdr->data_offsets[i].image_A, writer);
    writer_fwrite_int32(hdr->data_offsets[i].image_B, writer);
  }
}

/* ----------------------------------------------------------------------- */

static SFError read_planets_ext(ScanSpritesContext *const context, int32_t const ext_size,
                                Reader *const reader)
{
  assert(ext_size >= 0);
  assert(context);

  if (ext_size >= PlanetSprExtDataHdrSize)
  {
    int32_t ncoords = 0;
    if (!reader_fread_int32(&ncoords, reader))
    {
      return read_fail(reader);
    }

    if (ext_size >= PlanetSprExtDataHdrSize + (ncoords * PlanetSprExtDataOffsetSize))
    {
      context->planets.got_hdr = true;

      if (!read_planets_coords(&context->planets.hdr, ncoords, reader))
      {
        return read_fail(reader);
      }

      if (ncoords < 0 || (uint32_t)ncoords > ARRAY_SIZE(context->planets.hdr.paint_coords))
      {
        return SFError_BadNumGFX;
      }

      for (int32_t i = 0; i < ncoords; i++)
      {
        assert((uint32_t)i < ARRAY_SIZE(context->planets.hdr.paint_coords));
        if (fix_planets_coords(&context->planets.hdr.paint_coords[i]))
        {
          context->planets.fixed_hdr = true;
        }
      }
    }
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void write_planets_ext(PlanetsHeader const *const hdr, Writer *const writer)
{
  assert(hdr);

  writer_fwrite(PLANET_SPR_TAG, SpriteExtTagLen, 1, writer);
  int32_t const ncoords = hdr->last_image_num + 1;
  writer_fwrite_int32(ncoords, writer);
  write_planets_coords(hdr, ncoords, writer);
}

/* ----------------------------------------------------------------------- */

static inline SFError planet_to_sprite(Reader *const reader, Writer *const writer,
                                       PlanetsHeader const *const hdr,
                                       uint8_t *const tmp, int const i)
{
  assert(hdr);
  assert(tmp);
  assert(i >= 0);
  assert(i <= PlanetMax);

  char name[16] = PLANET_SPR_NAME;
  char numstr[16];
  int nout = sprintf(numstr, "%" PRId32, i);
  assert(nout >= 0); /* no formatting error */
  NOT_USED(nout);
  strncat(name, numstr, sizeof(name) - sizeof(PLANET_SPR_NAME));
  DEBUGF("Sprite name is %s\n", name);

  write_spr_header(PlanetSprSize, name, PlanetSprWidth, PlanetHeight, writer);

  assert(hdr->data_offsets[i].image_A >= PlanetHeaderSize);
  assert(hdr->data_offsets[i].image_A <= hdr->data_offsets[i].image_B);
  if (reader_fseek(reader, hdr->data_offsets[i].image_A, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  if (!reader_fread(tmp, PlanetBitmapSize, 1, reader))
  {
    return read_fail(reader);
  }

  assert(hdr->data_offsets[i].image_B >= PlanetHeaderSize + PlanetBitmapSize);
  assert(hdr->data_offsets[i].image_B - PlanetBitmapSize >= hdr->data_offsets[i].image_A);
  assert(hdr->data_offsets[i].image_B <= PlanetFileSizeMax - PlanetBitmapSize);
  if (reader_fseek(reader, hdr->data_offsets[i].image_B, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  /* Copy raw bitmap image to sprite area, one row at a time
     (same pixel format etc) */
  const uint8_t *image_A = tmp;
  for (int row = 0;
       row < PlanetHeight && !writer_ferror(writer);
       row++, image_A += WORD_ALIGN(PlanetWidth))
  {
    /* Check that the two copies of the image bitmap are identical except
       for their alignment, and that two pixel columns on the righthand
       (image A) or lefthand (image B) side are black. */
    DEBUG("Last two pixels of image A on row %d are %d,%d", row,
          image_A[PlanetSprWidth], image_A[PlanetWidth - 1]);

    bool pce = (image_A[PlanetSprWidth] != 0);
    /* Penultimate Column Error */
    if (pce)
    {
      /* The 2nd picture in the 'Alien' file has coloured pixels on the
         righthand side of image A, probably due to human error. */
      static const uint8_t alien_error[] = {1,1,2,2,2,36,2,5,2,2,1};
      const int alien_start = 12; /* 1st row with a non-black pixel */

      if (row >= alien_start &&
          row < alien_start + (int)ARRAY_SIZE(alien_error) &&
          image_A[PlanetSprWidth] == alien_error[row - alien_start])
      {
        DEBUGF("Suppressing Penultimate Column Error on row %d\n", row);
        pce = false;
      }
    }

    if (pce)
    {
      DEBUGF("Penultimate Column Error on row %d\n", row);
      return SFError_BadImages;
    }

    uint8_t image_B[WORD_ALIGN(PlanetWidth)];
    if (!reader_fread(image_B, sizeof(image_B), 1, reader))
    {
      return read_fail(reader);
    }
    DEBUG("First two pixels of image B on row %d are %d,%d", row, image_B[0], image_B[1]);

    if (image_B[0] != 0 || image_B[1] != 0 || image_A[PlanetWidth - 1] != 0 ||
        memcmp(image_A, image_B + PlanetMargin, PlanetSprWidth))
    {
      return SFError_BadImages;
    }

    /* The first copy of the image is left-aligned, so we just
       chop the last two pixels off each row */
    writer_fwrite(tmp + (row * WORD_ALIGN(PlanetSprWidth)),
                  WORD_ALIGN(PlanetSprWidth), 1, writer);
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline SFError sprite_to_planet(Writer *const writer,
                                PlanetsHeader const *const hdr, uint8_t *const tmp,
                                int const i)
{
  assert(hdr);
  assert(tmp);
  assert(i >= 0);
  assert(i <= PlanetMax);
  assert((unsigned)i < ARRAY_SIZE(hdr->data_offsets));

  /* Beware of seeking too far ahead because the compressor will zero-fill instead of failing */
  assert(hdr->data_offsets[i].image_A >= PlanetHeaderSize);
  assert(hdr->data_offsets[i].image_A <= hdr->data_offsets[i].image_B);
  writer_fseek(writer, hdr->data_offsets[i].image_A, SEEK_SET);
  /* Do not use BadSeek, which is reserved for read errors! */

  /* We make two copies of the input sprite; one word-aligned and the
     other half-word aligned. This requires copying one row at a time.
   */
  static uint8_t margin[PlanetMargin] = {0};
  for (int row = 0; row < PlanetHeight && !writer_ferror(writer); row++)
  {
    /* The first copy of the image is word-aligned */
    writer_fwrite(tmp + (row * WORD_ALIGN(PlanetSprWidth)), PlanetSprWidth, 1, writer);
    writer_fwrite(margin, sizeof(margin), 1, writer);
  }

  /* Beware of seeking too far ahead because the compressor will zero-fill instead of failing */
  assert(hdr->data_offsets[i].image_B >= PlanetHeaderSize + PlanetBitmapSize);
  assert(hdr->data_offsets[i].image_B - PlanetBitmapSize >= hdr->data_offsets[i].image_A);
  assert(hdr->data_offsets[i].image_B <= PlanetFileSizeMax - PlanetBitmapSize);
  writer_fseek(writer, hdr->data_offsets[i].image_B, SEEK_SET);

  for (int row = 0; row < PlanetHeight && !writer_ferror(writer); row++)
  {
    /* The second copy of the image is half-word aligned */
    writer_fwrite(margin, sizeof(margin), 1, writer);
    writer_fwrite(tmp + (row * WORD_ALIGN(PlanetSprWidth)), PlanetSprWidth, 1, writer);
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline int parse_planet_sprite_name(char const *const name)
{
  assert(name);
  long int image_num = -1;

  if (strncmp(name, PLANET_SPR_NAME, sizeof(PLANET_SPR_NAME)-1) == 0 &&
      isdigit(name[sizeof(PLANET_SPR_NAME)-1]))
  {
    char *endp;
    image_num = strtol(name + sizeof(PLANET_SPR_NAME)-1, &endp, 10);

    if (image_num > PlanetMax || *endp != '\0')
    {
      image_num = -1;
    }
  }

  assert(image_num <= INT_MAX);
  return (int)image_num;
}

/* ----------------------------------------------------------------------- */

static bool planets_sprite_identifier(long int const fpos,
  char const *const name, ScanSpritesContext *const context)
{
  assert(context);
  int const image_num = parse_planet_sprite_name(name);

  if (image_num >= 0)
  {
    assert((unsigned)image_num < ARRAY_SIZE(context->planets.offsets));
    assert(!context->planets.offsets[image_num]);
    context->planets.offsets[image_num] = fpos;

    if (image_num > context->planets.hdr.last_image_num)
    {
      context->planets.hdr.last_image_num = image_num;
    }
    ++context->planets.count;
    return true;
  }

  return false;
}

/* ----------------------------------------------------------------------- */

static SFError planets_to_sprites_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= PlanetMax + 1);

  PlanetsToSpritesIter *const sub = CONTAINER_OF(iter, PlanetsToSpritesIter, super);
  return planet_to_sprite(iter->reader, iter->writer, &sub->hdr,
                          sub->tmp, iter->pos);
}

/* ----------------------------------------------------------------------- */

static SFError sprites_to_planets_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= PlanetMax + 1);

  SpritesToPlanetsIter *const sub = CONTAINER_OF(iter, SpritesToPlanetsIter, super);
  long int const offset = sub->offsets[iter->pos];
  SFError err = SFError_OK;

  DEBUGF("Sprite for planet %d has offset %ld\n", iter->pos, offset);
  if (offset)
  {
    if (reader_fseek(iter->reader, offset, SEEK_SET))
    {
      return SFError_BadSeek;
    }

    if (!reader_fread(sub->tmp, sizeof(sub->tmp), 1, iter->reader))
    {
      return read_fail(iter->reader);
    }
  }
  else
  {
    for (size_t i = 0; i < ARRAY_SIZE(sub->tmp); ++i)
    {
      sub->tmp[i] = 0;
    }
  }

  err = sprite_to_planet(iter->writer, &sub->hdr,
                         sub->tmp, iter->pos);

  if (err == SFError_OK && iter->pos == iter->count - 1)
  {
    DEBUGF("Final file position is %ld\n", writer_ftell(iter->writer));
    assert(writer_ferror(iter->writer) ||
           writer_ftell(iter->writer) == planets_size(&sub->hdr));
  }

  return err;
}

/* ----------------------------------------------------------------------- */

static bool fix_sky_render(SkyHeader *const hdr)
{
  assert(hdr);
  bool fixed = false;

  if (hdr->render_offset < SkyRenderMin || hdr->render_offset > SkyRenderMax)
  {
    DEBUG("Forcing ground level within bounds (was %d)",
          hdr->render_offset);

    if (hdr->render_offset < SkyRenderMin)
    {
      hdr->render_offset = SkyRenderMin;
    }
    else
    {
      hdr->render_offset = SkyRenderMax;
    }
    fixed = true;
  }

  return fixed;
}

/* ----------------------------------------------------------------------- */

static bool fix_sky_stars(SkyHeader *const hdr)
{
  assert(hdr);
  bool fixed = false;

  if (hdr->min_stars_height < SkyStarsMin || hdr->min_stars_height > SkyStarsMax)
  {
    DEBUG("Forcing stars height within bounds (was %d)",
          hdr->min_stars_height);

    if (hdr->min_stars_height < SkyStarsMin)
    {
      hdr->min_stars_height = SkyStarsMin;
    }
    else
    {
      hdr->min_stars_height = SkyStarsMax;
    }
    fixed = true;
  }

  return fixed;
}

/* ----------------------------------------------------------------------- */

static bool read_sky_offsets(SkyHeader *const hdr, Reader *const reader)
{
  assert(hdr);
  return reader_fread_int32(&hdr->render_offset, reader) &&
         reader_fread_int32(&hdr->min_stars_height, reader);
}

/* ----------------------------------------------------------------------- */

static SFError read_sky_hdr(SkyHeader *const hdr, Reader *const reader)
{
  assert(hdr);
  if (!read_sky_offsets(hdr, reader))
  {
    return read_fail(reader);
  }

  if (hdr->render_offset < SkyRenderMin || hdr->render_offset > SkyRenderMax)
  {
    return SFError_BadRend;
  }

  if (hdr->min_stars_height < SkyStarsMin || hdr->min_stars_height > SkyStarsMax)
  {
    return SFError_BadStar;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static void write_sky_hdr(SkyHeader const *const hdr, Writer *const writer)
{
  assert(hdr);

  assert(hdr->render_offset >= SkyRenderMin);
  assert(hdr->render_offset <= SkyRenderMax);
  writer_fwrite_int32(hdr->render_offset, writer);

  assert(hdr->min_stars_height >= SkyStarsMin);
  assert(hdr->min_stars_height <= SkyStarsMax);
  writer_fwrite_int32(hdr->min_stars_height, writer);
}

/* ----------------------------------------------------------------------- */

static SFError read_sky_ext(ScanSpritesContext *const context, int32_t const ext_size,
                            Reader *const reader)
{
  assert(ext_size >= 0);
  assert(context);

  if (ext_size == SkySprExtDataSize)
  {
    context->sky.got_hdr = true;

    if (!read_sky_offsets(&context->sky.hdr, reader))
    {
      return read_fail(reader);
    }

    if (fix_sky_render(&context->sky.hdr))
    {
      context->sky.fixed_render = true;
    }

    if (fix_sky_stars(&context->sky.hdr))
    {
      context->sky.fixed_stars = true;
    }
  }
  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline void write_sky_ext(SkyHeader const *const hdr, Writer *const writer)
{
  writer_fwrite(SKY_SPR_TAG, SpriteExtTagLen, 1, writer);
  write_sky_hdr(hdr, writer);
}

/* ----------------------------------------------------------------------- */

static SFError copy_n_flip_sky(Reader *const reader, Writer *const writer,
  uint8_t *const tmp)
{
  return copy_n_flip(reader, writer, tmp, SkyWidth, SkyHeight);
}

/* ----------------------------------------------------------------------- */

static bool sky_sprite_identifier(long int const fpos, char const *const name,
  ScanSpritesContext *const context)
{
  assert(name);
  assert(context);

  if (strcmp(name, SKY_SPR_NAME) == 0)
  {
    assert(!context->sky.offset);
    context->sky.offset = fpos;
    context->sky.count = 1;
    return true;
  }

  return false;
}

/* ----------------------------------------------------------------------- */

static SFError sky_to_sprites_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= SkyMax + 1);

  SkyToSpritesIter *const sub = CONTAINER_OF(iter, SkyToSpritesIter, super);

  char name[SpriteNameSize] = {0};
  strncat(name, SKY_SPR_NAME, sizeof(name)-1);
  DEBUGF("Sprite name is %s\n", name);

  write_spr_header(SkySprSize, name, SkyWidth, SkyHeight, iter->writer);
  return copy_n_flip_sky(iter->reader, iter->writer, sub->tmp);
}

/* ----------------------------------------------------------------------- */

static SFError sprites_to_sky_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);
  assert(iter->count <= SkyMax + 1);

  SpritesToSkyIter *const sub = CONTAINER_OF(iter, SpritesToSkyIter, super);
  long int const offset = sub->offset;
  SFError err = SFError_OK;

  DEBUGF("Sprite for sky %d has offset %ld\n", iter->pos, offset);
  assert(offset != 0);
  if (reader_fseek(iter->reader, offset, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  err = copy_n_flip_sky(iter->reader, iter->writer, sub->tmp);

  if (err == SFError_OK)
  {
    DEBUGF("Final file position is %ld\n", writer_ftell(iter->writer));
    assert(writer_ferror(iter->writer) ||
           writer_ftell(iter->writer) == sky_size());
  }

  return err;
}

/* ----------------------------------------------------------------------- */

static inline SFError all_ext_parser(ScanSpritesContext *const context, int32_t const ext_size,
                                     Reader *const reader)
{
  assert(context);
  assert(ext_size >= 0);

  if (ext_size >= SpriteExtTagLen)
  {
    char tag[SpriteExtTagLen];
    if (!reader_fread(tag, sizeof(tag), 1, reader))
    {
      return read_fail(reader);
    }

    static struct
    {
      char tag[SpriteExtTagLen + 1];
      SFError (*ext_parser)(ScanSpritesContext *, int32_t, Reader *);
    }
    const data_types[] =
    {
      {TILE_SPR_TAG, read_tiles_ext},
      {PLANET_SPR_TAG, read_planets_ext},
      {SKY_SPR_TAG, read_sky_ext},
    };

    for (size_t i = 0; i < ARRAY_SIZE(data_types); ++i)
    {
      if (!memcmp(data_types[i].tag, tag, sizeof(tag)))
      {
        return data_types[i].ext_parser(context, ext_size, reader);
      }
    }
  }
  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline SFError all_sprite_identifier(
  SFSpriteHeader const *const sph, long int const fpos,
  ScanSpritesContext *const context)
{
  assert(context);
  assert(sph);
  DEBUGF("Identifying sprite %s\n", sph->name);

  static struct
  {
    int width;
    int height;
    bool (*sprite_identifier)(long int, char const *name, ScanSpritesContext *);
  }
  const data_types[] =
  {
    {MapTileWidth, MapTileHeight, tiles_sprite_identifier},
    {PlanetSprWidth, PlanetHeight, planets_sprite_identifier},
    {SkyWidth, SkyHeight, sky_sprite_identifier},
  };

  for (size_t i = 0; i < ARRAY_SIZE(data_types); ++i)
  {
    if (sprite_has_dims(sph, data_types[i].width, data_types[i].height) &&
        data_types[i].sprite_identifier(fpos, sph->name, context))
    {
      return SFError_OK;
    }
  }

  if (!context->bad_sprite)
  {
    context->bad_sprite = true;
    STRCPY_SAFE(context->bad_name, sph->name);
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static inline SFError scan_sprite(Reader *const reader, ScanSpritesContext *const context)
{
  long int const sp_start = reader_ftell(reader);
  if (sp_start < 0)
  {
    return SFError_BadTell;
  }

  SFSpriteHeader sph = {0};
  if (!reader_fread_int32(&sph.size, reader))
  {
    return read_fail(reader);
  }

  if (!reader_fread(&sph.name, sizeof(sph.name) - 1, 1, reader))
  {
    return read_fail(reader);
  }
  DEBUGF("Sprite '%s' has length of %" PRId32 " bytes\n", sph.name, sph.size);

  if (!reader_fread_int32(&sph.width, reader) ||
      !reader_fread_int32(&sph.height, reader) ||
      !reader_fread_int32(&sph.left_bit, reader) ||
      !reader_fread_int32(&sph.right_bit, reader) ||
      !reader_fread_int32(&sph.image, reader) ||
      !reader_fread_int32(&sph.mask, reader) ||
      !reader_fread_int32(&sph.type, reader))
  {
    return read_fail(reader);
  }

  if ((sph.image < SprHdrSize) || (sph.image > sph.size) ||
      (sph.mask < sph.image) || (sph.mask > sph.size))
  {
    return SFError_BadDataOff;
  }

  if (reader_fseek(reader, sp_start + sph.image, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  SFError const err = all_sprite_identifier(&sph, sp_start + sph.image, context);
  if (err != SFError_OK)
  {
    return err;
  }

  long int const sp_offset = sp_start + sph.size;
  DEBUGF("Seeking next sprite at offset %ld\n", sp_offset);
  if (reader_fseek(reader, sp_offset, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

static SFError scan_sprites_conv(ConvertIter *const iter)
{
  assert(iter);
  assert(iter->pos >= 0);
  assert(iter->pos < iter->count);

  ScanSpritesIter *const sub = CONTAINER_OF(iter, ScanSpritesIter, super);
  Reader *const reader = iter->reader;
  ScanSpritesContext *const context = sub->context;

  SFError err = scan_sprite(reader, context);
  if (err == SFError_OK && iter->pos == (iter->count - 1))
  {
    if (context->tiles.hdr.last_tile_num >= 0 &&
        fix_tiles_anim(&context->tiles.hdr))
    {
      context->tiles.fixed_hdr = true;
    }
  }
  return err;
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

SFError convert_advance(ConvertIter *const iter)
{
  assert(iter);
  if (iter->pos >= iter->count)
  {
    return SFError_Done;
  }
  SFError const err = iter->convert(iter);
  ++iter->pos;

  DEBUG_VERBOSEF("Read position: %ld\nWrite position: %ld\n",
     reader_ftell(iter->reader), iter->writer ? writer_ftell(iter->writer) : 0);

  return err;
}

/* ----------------------------------------------------------------------- */

SFError convert_finish(ConvertIter *const iter)
{
  assert(iter);
  SFError err;
  do {
    err = convert_advance(iter);
#ifdef FORTIFY
    Fortify_CheckAllMemory();
#endif
  } while (err == SFError_OK &&
           (!iter->writer || !writer_ferror(iter->writer)));

  return err == SFError_Done ? SFError_OK : err;
}

/* ----------------------------------------------------------------------- */

SFError scan_sprite_file_init(ScanSpritesIter *const iter, Reader *const reader,
  ScanSpritesContext *const context)
{
  assert(iter);
  assert(context);

  iter->super = (ConvertIter){
    .reader = reader,
    .convert = scan_sprites_conv,
  };
  iter->context = context;

  *context = (ScanSpritesContext){ .tiles = { .count = 0 } };

  init_planets_hdr(&context->planets.hdr);
  init_tiles_hdr(&context->tiles.hdr);

  DEBUGF("Parsing sprite file header\n");
  int32_t first = 0, used = 0;
  if (!reader_fread_int32(&iter->super.count, reader) ||
      !reader_fread_int32(&first, reader) ||
      !reader_fread_int32(&used, reader))
  {
    return read_fail(reader);
  }
  DEBUGF("File contains %" PRId32 " sprites, first at offset %" PRId32 "\n",
         iter->super.count, first);

  if (iter->super.count < 0)
  {
    return SFError_BadNumGFX;
  }

  if (first < SprAreaHdrSize || first > used)
  {
    return SFError_BadDataOff;
  }

  const int32_t ext_size = first - SprAreaHdrSize;
  DEBUGF("Parsing %" PRId32 " bytes of extension data\n", ext_size);
  SFError const err = all_ext_parser(context, ext_size, reader);
  if (err != SFError_OK)
  {
    return err;
  }

  long int sp_start = first - (long)sizeof(int32_t);
  DEBUGF("Seeking first sprite at offset %ld\n", sp_start);
  if (reader_fseek(reader, sp_start, SEEK_SET))
  {
    return SFError_BadSeek;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

SFError scan_sprite_file(Reader *const reader, ScanSpritesContext *const context)
{
  _Optional ScanSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = scan_sprite_file_init(&*iter, reader, context);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

int count_spr_types(ScanSpritesContext const *const context)
{
  assert(context);
  int ntypes = (context->planets.count > 0) ? 1 : 0;
  if (context->tiles.count > 0)
  {
    ++ntypes;
  }
  if (context->sky.count > 0)
  {
    ++ntypes;
  }
  return ntypes;
}

/* ----------------------------------------------------------------------- */

SFError sprites_to_tiles_init(SpritesToTilesIter *const iter,
  Reader *const reader, Writer *const writer, MapTileSpritesContext const *const context)
{
  assert(iter);
  assert(context);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .count = context->hdr.last_tile_num + 1,
    .convert = sprites_to_tiles_conv,
  };
  iter->offsets = context->offsets;
  write_tiles_hdr(&context->hdr, writer);
  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

SFError sprites_to_tiles(Reader *const reader, Writer *const writer,
  MapTileSpritesContext const *const context)
{
  _Optional SpritesToTilesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = sprites_to_tiles_init(&*iter, reader, writer, context);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }

  if (err == SFError_OK)
  {
    DEBUGF("Final file position is %ld\n", writer_ftell(writer));
    assert(writer_ferror(writer) ||
           writer_ftell(writer) == tiles_size(&context->hdr));
  }

  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError csv_to_tiles(Reader *const reader, MapTilesHeader * const hdr)
{
  assert(hdr != NULL);
  DEBUG("Will copy animations from a CSV file anchored to a map"
        " graphics header %p", (void *)hdr);

  char csv_buffer[CSVBufferSize];
  size_t const n = reader_fread(csv_buffer, 1, sizeof(csv_buffer), reader);
  if (n >= sizeof(csv_buffer))
  {
    return SFError_StrOFlo;
  }
  csv_buffer[n] = '\0';
  _Optional char *text_ptr = csv_buffer;

  int array[ARRAY_SIZE(hdr->splash_anim_1)];

  DEBUG("Reading 1st splash animation");
  size_t num_fields = csv_parse_string(&*text_ptr, &text_ptr, array,
                                       CSVOutputType_Int, ARRAY_SIZE(array));
  if (num_fields > ARRAY_SIZE(array))
  {
    num_fields = ARRAY_SIZE(array);
  }

  bool out_of_range = false;
  for (size_t frame = 0; frame < num_fields; frame++)
  {
    if (array[frame] < 0 || array[frame] > hdr->last_tile_num)
    {
      DEBUGF("Forcing 1st splash animation frame %zu within bounds (was %d)\n",
             frame, array[frame]);
      out_of_range = true;
      hdr->splash_anim_1[frame] = array[frame] < 0 ? 0 : hdr->last_tile_num;
    }
    else
    {
      hdr->splash_anim_1[frame] = array[frame];
    }
  } /* next frame */

  if (text_ptr != NULL)
  {
    DEBUG("Reading 2nd splash animation");
    num_fields = csv_parse_string(&*text_ptr, &text_ptr, array,
                                  CSVOutputType_Int, ARRAY_SIZE(array));
    if (num_fields > ARRAY_SIZE(array))
    {
      num_fields = ARRAY_SIZE(array);
    }

    for (size_t frame = 0; frame < num_fields; frame++)
    {
      if (array[frame] < 0 || array[frame] > hdr->last_tile_num)
      {
        DEBUGF("Forcing 2nd splash animation frame %zu within bounds (was %d)\n",
               frame, array[frame]);
        out_of_range = true;
        hdr->splash_anim_2[frame] = array[frame] < 0 ? 0 : hdr->last_tile_num;
      }
      else
      {
        hdr->splash_anim_2[frame] = array[frame];
      }
    } /* next frame */
  } /* endif (text_ptr != NULL) */

  if (text_ptr != NULL)
  {
    DEBUG("Reading 2nd splash triggers");
    num_fields = csv_parse_string(&*text_ptr, &text_ptr, array,
                                  CSVOutputType_Int, ARRAY_SIZE(array));
    if (num_fields > ARRAY_SIZE(array))
      num_fields = ARRAY_SIZE(array);

    for (size_t frame = 0; frame < num_fields; frame++)
    {
      if (array[frame] < 0 || array[frame] > UINT8_MAX)
      {
        out_of_range = true;
        hdr->splash_2_triggers[frame] = (array[frame] < 0 ? 0 : UINT8_MAX);
      }
      else
      {
        hdr->splash_2_triggers[frame] = array[frame];
      }
    } /* next frame */
  } /* endif (text_ptr != NULL) */

  if (out_of_range)
  {
    return SFError_ForceAnim;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

int tiles_size(MapTilesHeader const * const hdr)
{
  assert(hdr);
  int32_t const ntiles = hdr->last_tile_num + 1;
  int32_t const size = MapTilesHeaderSize + (ntiles * MapTileBitmapSize);
  DEBUGF("Expected map tile file size is %" PRId32 " (%" PRId32 " tiles)\n", size, ntiles);
  return size;
}

/* ----------------------------------------------------------------------- */

SFError tiles_to_csv(Reader *const reader, Writer *const writer)
{
  MapTilesHeader hdr = {0};
  SFError err = read_tiles_hdr(&hdr, reader);
  if (err != SFError_OK)
  {
    return err;
  }

  char buf[sizeof("255,255,255,255\n")];
  int n = sprintf(buf, "%d,%d,%d,%d\n",
                  hdr.splash_anim_1[0],
                  hdr.splash_anim_1[1],
                  hdr.splash_anim_1[2],
                  hdr.splash_anim_1[3]);
  assert(n >= 0);
  assert((unsigned)n < sizeof(buf));
  NOT_USED(n);
  writer_fwrite(buf, strlen(buf), 1, writer);

  n = sprintf(buf, "%d,%d,%d,%d\n",
              hdr.splash_anim_2[0],
              hdr.splash_anim_2[1],
              hdr.splash_anim_2[2],
              hdr.splash_anim_2[3]);
  assert(n >= 0);
  assert((unsigned)n < sizeof(buf));
  writer_fwrite(buf, strlen(buf), 1, writer);

  n = sprintf(buf, "%d,%d,%d,%d\n",
              hdr.splash_2_triggers[0],
              hdr.splash_2_triggers[1],
              hdr.splash_2_triggers[2],
              hdr.splash_2_triggers[3]);
  assert(n >= 0);
  assert((unsigned)n < sizeof(buf));
  writer_fwrite(buf, strlen(buf), 1, writer);

  return err;
}

/* ----------------------------------------------------------------------- */

SFError tiles_to_sprites_init(TilesToSpritesIter *const iter,
  Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = tiles_to_sprites_conv,
  };
  MapTilesHeader hdr = {0};
  SFError err = read_tiles_hdr(&hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = hdr.last_tile_num + 1;
    write_sprite_area_hdr(iter->super.count, 0, MapTileSprSize, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError tiles_to_sprites(Reader *const reader, Writer *const writer)
{
  _Optional TilesToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = tiles_to_sprites_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError tiles_to_sprites_ext_init(TilesToSpritesIter *const iter,
  Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = tiles_to_sprites_conv,
  };
  SFError err = SFError_OK;
  MapTilesHeader hdr = {0};
  err = read_tiles_hdr(&hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = hdr.last_tile_num + 1;
    write_sprite_area_hdr(iter->super.count, MapTileSprExtDataSize, MapTileSprSize, writer);
    write_tiles_ext(&hdr, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError tiles_to_sprites_ext(Reader *const reader, Writer *const writer)
{
  _Optional TilesToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = tiles_to_sprites_ext_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError sprites_to_planets_init(SpritesToPlanetsIter *const iter,
  Reader *const reader, Writer *const writer, PlanetSpritesContext const *const context)
{
  assert(iter);
  assert(context);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .count = context->hdr.last_image_num + 1,
    .convert = sprites_to_planets_conv,
  };
  iter->hdr = context->hdr,
  iter->offsets = context->offsets,
  write_planets_hdr(&context->hdr, writer);
  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

SFError sprites_to_planets(Reader *reader, Writer *writer,
  PlanetSpritesContext const *const context)
{
  _Optional SpritesToPlanetsIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = sprites_to_planets_init(&*iter, reader, writer, context);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError csv_to_planets(Reader *const reader, PlanetsHeader * const hdr)
{
  assert(hdr != NULL);
  DEBUG("Will copy image offsets from a CSV file to a "
        "planet images header %p", (void *)hdr);

  char csv_buffer[CSVBufferSize];
  size_t const n = reader_fread(csv_buffer, 1, sizeof(csv_buffer), reader);
  if (n >= sizeof(csv_buffer))
  {
    return SFError_StrOFlo;
  }
  csv_buffer[n] = '\0';
  _Optional char *text_ptr = csv_buffer;

  bool fixed = false;
  for (int32_t planet = 0; planet <= hdr->last_image_num; planet++)
  {
    DEBUG("Reading paint offsets for sky picture %" PRId32, planet);

    int array[2] = {0, 0}; /* for X and Y coordinates */
    size_t num_fields = csv_parse_string(&*text_ptr, &text_ptr, array,
                                         CSVOutputType_Int, ARRAY_SIZE(array));
    if (num_fields > ARRAY_SIZE(array))
      num_fields = ARRAY_SIZE(array);

    if (num_fields > 0)
    {
      hdr->paint_coords[planet].x_offset = array[0];
    }

    if (num_fields > 1)
    {
      hdr->paint_coords[planet].y_offset = array[1];
    }

    if (fix_planets_coords(&hdr->paint_coords[planet]))
    {
      fixed = true;
    }

    if (text_ptr == NULL)
    {
      break; /* end of input string - success */
    }

  } /* next planet */

  if (fixed)
  {
    return SFError_ForceOff;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

int planets_size(PlanetsHeader const * const hdr)
{
  assert(hdr);
  int32_t const nimages = hdr->last_image_num + 1;
  int32_t const size = PlanetHeaderSize + (nimages * PlanetBitmapSize * 2);
  DEBUGF("Expected planets file size is %" PRId32 " (%" PRId32 " images)\n", size, nimages);
  return size;
}

/* ----------------------------------------------------------------------- */

SFError planets_to_csv(Reader *const reader, Writer *const writer)
{
  PlanetsHeader hdr = {0};
  SFError err = read_planets_hdr(&hdr, reader);
  if (err != SFError_OK)
  {
    return err;
  }

  for (int32_t planet = 0; planet <= hdr.last_image_num && !writer_ferror(writer); planet++)
  {
    char buf[sizeof("-2147483648,-2147483648\n")];
    int const n = sprintf(buf, "%d,%d\n",
            hdr.paint_coords[planet].x_offset,
            hdr.paint_coords[planet].y_offset);
    assert(n >= 0);
    assert((unsigned)n < sizeof(buf));
    NOT_USED(n);
    writer_fwrite(buf, strlen(buf), 1, writer);
  }

  return err;
}

/* ----------------------------------------------------------------------- */

SFError planets_to_sprites_init(PlanetsToSpritesIter *const iter,
  Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = planets_to_sprites_conv,
  };
  SFError err = read_planets_hdr(&iter->hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = iter->hdr.last_image_num + 1;
    write_sprite_area_hdr(iter->super.count, 0, PlanetSprSize, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError planets_to_sprites(Reader *const reader, Writer *const writer)
{
  _Optional PlanetsToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = planets_to_sprites_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError planets_to_sprites_ext_init(PlanetsToSpritesIter *const iter,
                                    Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = planets_to_sprites_conv,
  };
  SFError err = read_planets_hdr(&iter->hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = iter->hdr.last_image_num + 1;
    int32_t ext_data_size =
       PlanetSprExtDataHdrSize + (PlanetSprExtDataOffsetSize * iter->super.count);

    write_sprite_area_hdr(iter->super.count, ext_data_size, PlanetSprSize, writer);
    write_planets_ext(&iter->hdr, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError planets_to_sprites_ext(Reader *const reader, Writer *const writer)
{
  _Optional PlanetsToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = planets_to_sprites_ext_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}


/* ----------------------------------------------------------------------- */

SFError sprites_to_sky_init(SpritesToSkyIter *const iter,
  Reader *const reader, Writer *const writer, SkySpritesContext const *const context)
{
  assert(iter);
  assert(context);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .count = SkyMax + 1,
    .convert = sprites_to_sky_conv,
  };
  iter->offset = context->offset,
  write_sky_hdr(&context->hdr, writer);
  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

SFError sprites_to_sky(Reader *const reader, Writer *const writer,
  SkySpritesContext const *const context)
{
  assert(context);
  _Optional SpritesToSkyIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = sprites_to_sky_init(&*iter, reader, writer, context);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError csv_to_sky(Reader *const reader, SkyHeader *const hdr)
{
  assert(hdr != NULL);

  DEBUG("Will copy height values from a CSV file to a sky"
        " colours header %p", (void *)hdr);

  char csv_buffer[CSVBufferSize];
  size_t const n = reader_fread(csv_buffer, 1, sizeof(csv_buffer), reader);
  if (n >= sizeof(csv_buffer))
  {
    return SFError_StrOFlo;
  }
  csv_buffer[n] = '\0';
  _Optional char *text_ptr = csv_buffer;

  int array[2];

  const size_t num_fields = csv_parse_string(&*text_ptr, &text_ptr, array,
                                             CSVOutputType_Int, ARRAY_SIZE(array));
  bool fixed = false;
  if (num_fields > 0)
  {
    hdr->render_offset = array[0];
    if (fix_sky_render(hdr))
    {
      fixed = true;
    }
  }

  if (num_fields > 1)
  {
    hdr->min_stars_height = array[1];
    if (fix_sky_stars(hdr))
    {
      fixed = true;
    }
  }

  if (fixed)
  {
    return SFError_ForceSky;
  }

  return SFError_OK;
}

/* ----------------------------------------------------------------------- */

int sky_size(void)
{
  int const size = SkyHeaderSize + SkyBitmapSize;
  DEBUGF("Expected sky file size is %" PRId32 "\n", size);
  return size;
}

/* ----------------------------------------------------------------------- */

SFError sky_to_csv(Reader *const reader, Writer *const writer)
{
  SkyHeader hdr = {0};
  SFError err = read_sky_hdr(&hdr, reader);
  if (err != SFError_OK)
  {
    return err;
  }

  char buf[sizeof("-2147483648,-2147483648\n")];
  const int n = sprintf(buf, "%d,%d\n",
                        hdr.render_offset,
                        hdr.min_stars_height);
  assert(n >= 0);
  assert((unsigned)n < sizeof(buf));
  NOT_USED(n);
  writer_fwrite(buf, strlen(buf), 1, writer);

  return err;
}

/* ----------------------------------------------------------------------- */

SFError sky_to_sprites_init(SkyToSpritesIter *const iter,
  Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = sky_to_sprites_conv,
  };
  SkyHeader hdr = {0};
  SFError err = read_sky_hdr(&hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = SkySprCount;
    write_sprite_area_hdr(SkySprCount, 0, SkySprSize, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError sky_to_sprites_ext_init(SkyToSpritesIter *const iter,
  Reader *const reader, Writer *const writer)
{
  assert(iter);
  iter->super = (ConvertIter){
    .reader = reader,
    .writer = writer,
    .convert = sky_to_sprites_conv,
  };
  SkyHeader hdr = {0};
  SFError err = read_sky_hdr(&hdr, reader);
  if (err == SFError_OK)
  {
    iter->super.count = SkySprCount;
    write_sprite_area_hdr(SkySprCount, SkySprExtDataSize, SkySprSize, writer);
    write_sky_ext(&hdr, writer);
  }
  return err;
}

/* ----------------------------------------------------------------------- */

SFError sky_to_sprites(Reader *const reader, Writer *const writer)
{
  _Optional SkyToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = sky_to_sprites_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}

/* ----------------------------------------------------------------------- */

SFError sky_to_sprites_ext(Reader *const reader, Writer *const writer)
{
  _Optional SkyToSpritesIter *const iter = malloc(sizeof(*iter));
  if (!iter)
  {
    return SFError_NoMem;
  }

  SFError err = sky_to_sprites_ext_init(&*iter, reader, writer);
  if (err == SFError_OK)
  {
    err = convert_finish(&iter->super);
  }
  free(iter);
  return err;
}
