/*
 * SFToSpr test: graphics conversion routines
 * Copyright (C) 2026 Christopher Bazley
 */

#undef NDEBUG

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "Macros.h"
#include "Debug.h"
#include "ReaderMem.h"
#include "WriterMem.h"

#include "Tests.h"
#include "../SFgfxconv.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif

enum
{
  BufferSize = 90000,
  NumTiles = 3,
  NumPlanets = 2,
  RenderOffset = 73,
  StarsHeight = -41,
  PaintX0 = -12,
  PaintY0 = -31,
  PaintX1 = -27,
  PaintY1 = -6,
};

static uint8_t const splash_anim_1[MapAnimFrameCount] = {0, 1, 2, 1},
                     splash_anim_2[MapAnimFrameCount] = {2, 1, 0, 2},
                     splash_2_triggers[MapAnimTriggerCount] = {3, 9, 27, 81};

static char const SkyCSV[] = "73,-41\n",
                  TilesCSV[] = "0,1,2,1\n2,1,0,2\n3,9,27,81\n",
                  PlanetsCSV[] = "-12,-31\n-27,-6\n";

static long int finish_writer(Writer *const writer)
{
  assert(!writer_ferror(writer));
  long int const len = writer_destroy(writer);
  assert(len >= 0);
  return len;
}

static void check_bytes(void const *const expected, size_t const expected_size,
                        void const *const actual, size_t const actual_size)
{
  assert(expected_size == actual_size);

  Reader expected_reader, actual_reader;
  assert(reader_mem_init(&expected_reader, expected, expected_size));
  assert(reader_mem_init(&actual_reader, actual, actual_size));

  for (size_t i = 0; i < expected_size; ++i)
  {
    assert(reader_fgetc(&expected_reader) == reader_fgetc(&actual_reader));
  }
  assert(reader_fgetc(&expected_reader) == EOF);
  assert(reader_fgetc(&actual_reader) == EOF);
  assert(!reader_ferror(&expected_reader));
  assert(!reader_ferror(&actual_reader));
  reader_destroy(&expected_reader);
  reader_destroy(&actual_reader);
}

static void check_csv_result(char const *const csv, SFError const expected,
                             SFError (*const convert)(Reader *, void *),
                             void *const header)
{
  Reader reader;
  assert(reader_mem_init(&reader, csv, strlen(csv)));
  assert(convert(&reader, header) == expected);
  reader_destroy(&reader);
}

static SFError csv_to_tiles_adapter(Reader *const reader, void *const header)
{
  return csv_to_tiles(reader, header);
}

static SFError csv_to_planets_adapter(Reader *const reader, void *const header)
{
  return csv_to_planets(reader, header);
}

static SFError csv_to_sky_adapter(Reader *const reader, void *const header)
{
  return csv_to_sky(reader, header);
}

static long int make_sprite_area(uint8_t *const buffer, size_t const size,
                                 int32_t const count, int32_t const first,
                                 int32_t const used,
                                 _Optional uint8_t const *const extension,
                                 size_t const extension_size)
{
  Writer writer;
  assert(writer_mem_init(&writer, buffer, size));
  assert(writer_fwrite_int32(count, &writer));
  assert(writer_fwrite_int32(first, &writer));
  assert(writer_fwrite_int32(used, &writer));
  if (extension)
  {
    assert(writer_fwrite(&*extension, extension_size, 1, &writer) == 1);
  }
  return finish_writer(&writer);
}

static long int make_one_tile_sprite(void *const buffer, size_t const size,
                                     char const *const name, int32_t const type,
                                     int32_t const width, int32_t const height,
                                     int32_t const left_bit,
                                     int32_t const right_bit)
{
  enum { HeaderSize = 44, AreaHeaderSize = 16 };
  int32_t const sprite_size = HeaderSize + MapTileBitmapSize;
  char name_buffer[12];
  Writer writer;

  assert(strlen(name) <= sizeof(name_buffer));
  strncpy(name_buffer, name, sizeof(name_buffer));
  assert(writer_mem_init(&writer, buffer, size));
  assert(writer_fwrite_int32(1, &writer));
  assert(writer_fwrite_int32(AreaHeaderSize, &writer));
  assert(writer_fwrite_int32(AreaHeaderSize + sprite_size, &writer));
  assert(writer_fwrite_int32(sprite_size, &writer));
  assert(writer_fwrite(name_buffer, sizeof(name_buffer), 1, &writer) == 1);
  assert(writer_fwrite_int32(width, &writer));
  assert(writer_fwrite_int32(height, &writer));
  assert(writer_fwrite_int32(left_bit, &writer));
  assert(writer_fwrite_int32(right_bit, &writer));
  assert(writer_fwrite_int32(HeaderSize, &writer));
  assert(writer_fwrite_int32(HeaderSize, &writer));
  assert(writer_fwrite_int32(type, &writer));
  for (int i = 0; i < MapTileBitmapSize; ++i)
  {
    assert(writer_fputc(i, &writer) != EOF);
  }
  return finish_writer(&writer);
}

static uint8_t pixel(int const image, int const x, int const y)
{
  return (uint8_t)(17 + image * 53 + x * 7 + y * 11);
}

static long int make_sky(void *const buffer, size_t const size)
{
  Writer writer;
  assert(writer_mem_init(&writer, buffer, size));
  assert(writer_fwrite_int32(RenderOffset, &writer));
  assert(writer_fwrite_int32(StarsHeight, &writer));
  for (int y = 0; y < SkyHeight; ++y)
  {
    for (int x = 0; x < WORD_ALIGN(SkyWidth); ++x)
    {
      assert(writer_fputc(pixel(0, x, y), &writer) != EOF);
    }
  }
  return finish_writer(&writer);
}

static long int make_tiles(void *const buffer, size_t const size)
{
  Writer writer;
  assert(writer_mem_init(&writer, buffer, size));
  assert(writer_fwrite_int32(NumTiles - 1, &writer));
  assert(writer_fwrite(splash_anim_1, sizeof(splash_anim_1), 1, &writer) == 1);
  assert(writer_fwrite(splash_anim_2, sizeof(splash_anim_2), 1, &writer) == 1);
  assert(writer_fwrite(splash_2_triggers, sizeof(splash_2_triggers), 1, &writer) == 1);
  for (int tile = 0; tile < NumTiles; ++tile)
  {
    for (int y = 0; y < MapTileHeight; ++y)
    {
      for (int x = 0; x < WORD_ALIGN(MapTileWidth); ++x)
      {
        assert(writer_fputc(pixel(tile, x, y), &writer) != EOF);
      }
    }
  }
  return finish_writer(&writer);
}

static long int make_planets(void *const buffer, size_t const size)
{
  static PlanetsPaintOffset const coords[NumPlanets] = {
    {PaintX0, PaintY0}, {PaintX1, PaintY1}
  };
  int32_t const header_size = sizeof(int32_t) * 9;

  Writer writer;
  assert(writer_mem_init(&writer, buffer, size));
  assert(writer_fwrite_int32(NumPlanets - 1, &writer));
  for (int planet = 0; planet < NumPlanets; ++planet)
  {
    assert(writer_fwrite_int32(coords[planet].x_offset, &writer));
    assert(writer_fwrite_int32(coords[planet].y_offset, &writer));
  }
  for (int planet = 0; planet < NumPlanets; ++planet)
  {
    int32_t const image_a = header_size +
      (planet * 2 * PlanetBitmapSize);
    assert(writer_fwrite_int32(image_a, &writer));
    assert(writer_fwrite_int32(image_a + PlanetBitmapSize, &writer));
  }

  for (int planet = 0; planet < NumPlanets; ++planet)
  {
    for (int image = 0; image < 2; ++image)
    {
      for (int y = 0; y < PlanetHeight; ++y)
      {
        for (int x = 0; x < WORD_ALIGN(PlanetWidth); ++x)
        {
          bool const margin = image == 0 ? x >= PlanetSprWidth :
                                           x < PlanetMargin;
          uint8_t value = 0;
          if (!margin)
          {
            int const sprite_x = image == 0 ? x : x - PlanetMargin;
            value = pixel(planet, sprite_x, y);
          }
          assert(writer_fputc(value, &writer) != EOF);
        }
      }
    }
  }
  return finish_writer(&writer);
}

typedef SFError ToSpritesFn(Reader *, Writer *);
typedef SFError FromSpritesFn(Reader *, Writer *, ScanSpritesContext const *);
typedef void PrepareContextFn(ScanSpritesContext *);

static SFError from_sky(Reader *const reader, Writer *const writer,
                        ScanSpritesContext const *const context)
{
  return sprites_to_sky(reader, writer, &context->sky);
}

static SFError from_tiles(Reader *const reader, Writer *const writer,
                          ScanSpritesContext const *const context)
{
  return sprites_to_tiles(reader, writer, &context->tiles);
}

static SFError from_planets(Reader *const reader, Writer *const writer,
                            ScanSpritesContext const *const context)
{
  return sprites_to_planets(reader, writer, &context->planets);
}

static void prepare_sky_context(ScanSpritesContext *const context)
{
  assert(!context->sky.got_hdr);
  context->sky.hdr.render_offset = RenderOffset;
  context->sky.hdr.min_stars_height = StarsHeight;
}

static void prepare_tiles_context(ScanSpritesContext *const context)
{
  assert(!context->tiles.got_hdr);
  for (size_t i = 0; i < MapAnimFrameCount; ++i)
  {
    context->tiles.hdr.splash_anim_1[i] = splash_anim_1[i];
    context->tiles.hdr.splash_anim_2[i] = splash_anim_2[i];
  }
  for (int i = 0; i < MapAnimTriggerCount; ++i)
  {
    context->tiles.hdr.splash_2_triggers[i] = splash_2_triggers[i];
  }
}

static void prepare_planets_context(ScanSpritesContext *const context)
{
  assert(!context->planets.got_hdr);
  context->planets.hdr.paint_coords[0] =
    (PlanetsPaintOffset){PaintX0, PaintY0};
  context->planets.hdr.paint_coords[1] =
    (PlanetsPaintOffset){PaintX1, PaintY1};
}

static void check_round_trip(void const *const source, size_t const source_size,
                             ToSpritesFn *const to_sprites,
                             FromSpritesFn *const from_sprites,
                             ScanSpritesContext *const context)
{
  uint8_t sprites[BufferSize], result[BufferSize];
  Reader reader;
  Writer writer;

  assert(reader_mem_init(&reader, source, source_size));
  assert(writer_mem_init(&writer, sprites, sizeof(sprites)));
  assert(to_sprites(&reader, &writer) == SFError_OK);
  long int const sprites_size = finish_writer(&writer);
  reader_destroy(&reader);

  memset(context, 0xa5, sizeof(*context));
  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(scan_sprite_file(&reader, context) == SFError_OK);
  reader_destroy(&reader);

  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(writer_mem_init(&writer, result, sizeof(result)));
  assert(from_sprites(&reader, &writer, context) == SFError_OK);
  long int const result_size = finish_writer(&writer);
  reader_destroy(&reader);

  check_bytes(source, source_size, result, (size_t)result_size);
}

static void check_nonextended_round_trip(
  void const *const source, size_t const source_size,
  ToSpritesFn *const to_sprites, FromSpritesFn *const from_sprites,
  PrepareContextFn *const prepare_context)
{
  uint8_t sprites[BufferSize], result[BufferSize];
  Reader reader;
  Writer writer;
  ScanSpritesContext context;

  assert(reader_mem_init(&reader, source, source_size));
  assert(writer_mem_init(&writer, sprites, sizeof(sprites)));
  assert(to_sprites(&reader, &writer) == SFError_OK);
  long int const sprites_size = finish_writer(&writer);
  reader_destroy(&reader);

  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(scan_sprite_file(&reader, &context) == SFError_OK);
  reader_destroy(&reader);
  prepare_context(&context);

  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(writer_mem_init(&writer, result, sizeof(result)));
  assert(from_sprites(&reader, &writer, &context) == SFError_OK);
  long int const result_size = finish_writer(&writer);
  reader_destroy(&reader);

  check_bytes(source, source_size, result, (size_t)result_size);
}

static void test_sizes(void)
{
  MapTilesHeader tiles = {.last_tile_num = NumTiles - 1};
  PlanetsHeader planets = {.last_image_num = NumPlanets - 1};
  assert(tiles_size(&tiles) == 16 + NumTiles * MapTileBitmapSize);
  assert(planets_size(&planets) == 36 + NumPlanets * 2 * PlanetBitmapSize);
  assert(sky_size() == 8 + SkyBitmapSize);
}

static void test_sky_round_trip(void)
{
  uint8_t source[BufferSize];
  long int const source_size = make_sky(source, sizeof(source));
  ScanSpritesContext context;
  check_round_trip(source, (size_t)source_size, sky_to_sprites_ext,
                   from_sky, &context);
  assert(context.sky.count == 1);
  assert(context.sky.got_hdr);
  assert(context.sky.hdr.render_offset == RenderOffset);
  assert(context.sky.hdr.min_stars_height == StarsHeight);
  assert(count_spr_types(&context) == 1);
}

static void test_tiles_round_trip(void)
{
  uint8_t source[BufferSize];
  long int const source_size = make_tiles(source, sizeof(source));
  ScanSpritesContext context;
  check_round_trip(source, (size_t)source_size, tiles_to_sprites_ext,
                   from_tiles, &context);
  assert(context.tiles.count == NumTiles);
  assert(context.tiles.got_hdr);
  assert(context.tiles.hdr.last_tile_num == NumTiles - 1);
  assert(count_spr_types(&context) == 1);
}

static void test_planets_round_trip(void)
{
  uint8_t source[BufferSize];
  long int const source_size = make_planets(source, sizeof(source));
  ScanSpritesContext context;
  check_round_trip(source, (size_t)source_size, planets_to_sprites_ext,
                   from_planets, &context);
  assert(context.planets.count == NumPlanets);
  assert(context.planets.got_hdr);
  assert(context.planets.hdr.last_image_num == NumPlanets - 1);
  assert(count_spr_types(&context) == 1);
}

static void test_sky_nonextended_round_trip(void)
{
  uint8_t sky_data[BufferSize];
  long int const data_size = make_sky(sky_data, sizeof(sky_data));
  check_nonextended_round_trip(sky_data, (size_t)data_size, sky_to_sprites,
                               from_sky, prepare_sky_context);
}

static void test_tiles_nonextended_round_trip(void)
{
  uint8_t tiles_data[BufferSize];
  long int const data_size = make_tiles(tiles_data, sizeof(tiles_data));
  check_nonextended_round_trip(tiles_data, (size_t)data_size,
                               tiles_to_sprites, from_tiles,
                               prepare_tiles_context);
}

static void test_planets_nonextended_round_trip(void)
{
  uint8_t planets_data[BufferSize];
  long int const data_size = make_planets(planets_data,
                                           sizeof(planets_data));
  check_nonextended_round_trip(planets_data, (size_t)data_size,
                               planets_to_sprites, from_planets,
                               prepare_planets_context);
}

static void test_incremental_conversion(void)
{
  uint8_t tiles_data[BufferSize];
  uint8_t expected_sprites[BufferSize], actual_sprites[BufferSize];
  uint8_t result[BufferSize];
  long int const tiles_data_size = make_tiles(tiles_data, sizeof(tiles_data));
  Reader reader;
  Writer writer;
  TilesToSpritesIter to_sprites_iter;
  ScanSpritesIter scan_iter;
  SpritesToTilesIter from_sprites_iter;
  ScanSpritesContext context;

  assert(reader_mem_init(&reader, tiles_data, (size_t)tiles_data_size));
  assert(writer_mem_init(&writer, expected_sprites, sizeof(expected_sprites)));
  assert(tiles_to_sprites_ext(&reader, &writer) == SFError_OK);
  long int const expected_size = finish_writer(&writer);
  reader_destroy(&reader);

  assert(reader_mem_init(&reader, tiles_data, (size_t)tiles_data_size));
  assert(writer_mem_init(&writer, actual_sprites, sizeof(actual_sprites)));
  assert(tiles_to_sprites_ext_init(&to_sprites_iter, &reader, &writer) ==
         SFError_OK);
  assert(to_sprites_iter.super.pos == 0);
  assert(to_sprites_iter.super.count == NumTiles);
  assert(convert_advance(&to_sprites_iter.super) == SFError_OK);
  assert(to_sprites_iter.super.pos == 1);
  assert(convert_finish(&to_sprites_iter.super) == SFError_OK);
  assert(to_sprites_iter.super.pos == NumTiles);
  long int const actual_size = finish_writer(&writer);
  reader_destroy(&reader);
  check_bytes(expected_sprites, (size_t)expected_size,
              actual_sprites, (size_t)actual_size);

  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, actual_sprites, (size_t)actual_size));
  assert(scan_sprite_file_init(&scan_iter, &reader, &context) == SFError_OK);
  assert(scan_iter.super.pos == 0);
  assert(scan_iter.super.count == NumTiles);
  assert(convert_advance(&scan_iter.super) == SFError_OK);
  assert(scan_iter.super.pos == 1);
  assert(convert_finish(&scan_iter.super) == SFError_OK);
  assert(scan_iter.super.pos == NumTiles);
  assert(context.tiles.count == NumTiles);
  assert(context.tiles.got_hdr);
  reader_destroy(&reader);

  assert(reader_mem_init(&reader, actual_sprites, (size_t)actual_size));
  assert(writer_mem_init(&writer, result, sizeof(result)));
  assert(sprites_to_tiles_init(&from_sprites_iter, &reader, &writer,
                               &context.tiles) == SFError_OK);
  assert(from_sprites_iter.super.pos == 0);
  assert(from_sprites_iter.super.count == NumTiles);
  assert(convert_advance(&from_sprites_iter.super) == SFError_OK);
  assert(from_sprites_iter.super.pos == 1);
  assert(convert_finish(&from_sprites_iter.super) == SFError_OK);
  assert(from_sprites_iter.super.pos == NumTiles);
  long int const result_size = finish_writer(&writer);
  reader_destroy(&reader);
  check_bytes(tiles_data, (size_t)tiles_data_size,
              result, (size_t)result_size);
}

static void test_sky_to_csv(void)
{
  uint8_t sky_data[BufferSize], csv[256];
  Reader reader;
  Writer writer;

  long int const len = make_sky(sky_data, sizeof(sky_data));
  assert(reader_mem_init(&reader, sky_data, (size_t)len));
  assert(writer_mem_init(&writer, csv, sizeof(csv)));
  assert(sky_to_csv(&reader, &writer) == SFError_OK);
  long int const csv_len = finish_writer(&writer);
  assert((size_t)csv_len == strlen(SkyCSV));
  assert(memcmp(csv, SkyCSV, (size_t)csv_len) == 0);
  reader_destroy(&reader);
}

static void test_csv_to_sky(void)
{
  Reader reader;
  SkyHeader sky = {0};
  assert(reader_mem_init(&reader, SkyCSV, strlen(SkyCSV)));
  assert(csv_to_sky(&reader, &sky) == SFError_OK);
  assert(sky.render_offset == RenderOffset);
  assert(sky.min_stars_height == StarsHeight);
  reader_destroy(&reader);
}

static void test_tiles_to_csv(void)
{
  uint8_t tiles_data[BufferSize], csv[256];
  Reader reader;
  Writer writer;

  long int const len = make_tiles(tiles_data, sizeof(tiles_data));
  assert(reader_mem_init(&reader, tiles_data, (size_t)len));
  assert(writer_mem_init(&writer, csv, sizeof(csv)));
  assert(tiles_to_csv(&reader, &writer) == SFError_OK);
  long int const csv_len = finish_writer(&writer);
  assert((size_t)csv_len == strlen(TilesCSV));
  assert(memcmp(csv, TilesCSV, (size_t)csv_len) == 0);
  reader_destroy(&reader);
}

static void test_csv_to_tiles(void)
{
  Reader reader;
  MapTilesHeader tiles = {.last_tile_num = NumTiles - 1};
  assert(reader_mem_init(&reader, TilesCSV, strlen(TilesCSV)));
  assert(csv_to_tiles(&reader, &tiles) == SFError_OK);
  assert(tiles.splash_anim_1[2] == 2);
  assert(tiles.splash_anim_2[0] == 2);
  assert(tiles.splash_2_triggers[3] == 81);
  reader_destroy(&reader);
}

static void test_planets_to_csv(void)
{
  uint8_t planets_data[BufferSize], csv[256];
  Reader reader;
  Writer writer;

  long int const len = make_planets(planets_data, sizeof(planets_data));
  assert(reader_mem_init(&reader, planets_data, (size_t)len));
  assert(writer_mem_init(&writer, csv, sizeof(csv)));
  assert(planets_to_csv(&reader, &writer) == SFError_OK);
  long int const csv_len = finish_writer(&writer);
  assert((size_t)csv_len == strlen(PlanetsCSV));
  assert(memcmp(csv, PlanetsCSV, (size_t)csv_len) == 0);
  reader_destroy(&reader);
}

static void test_csv_to_planets(void)
{
  Reader reader;
  PlanetsHeader planets = {.last_image_num = NumPlanets - 1};
  assert(reader_mem_init(&reader, PlanetsCSV, strlen(PlanetsCSV)));
  assert(csv_to_planets(&reader, &planets) == SFError_OK);
  assert(planets.paint_coords[0].x_offset == PaintX0);
  assert(planets.paint_coords[0].y_offset == PaintY0);
  assert(planets.paint_coords[1].x_offset == PaintX1);
  assert(planets.paint_coords[1].y_offset == PaintY1);
  reader_destroy(&reader);
}

static void test_scan_truncated_sprite_area(void)
{
  uint8_t sprite_data[1] = {0};
  Reader reader;
  ScanSpritesContext context;

  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, sprite_data, 0));
  assert(scan_sprite_file(&reader, &context) == SFError_Trunc);
  reader_destroy(&reader);
}

static void test_convert_truncated_sky(void)
{
  uint8_t sky_data[1] = {0};
  uint8_t sprite_data[32];
  Reader reader;
  Writer writer;

  assert(reader_mem_init(&reader, sky_data, 0));
  assert(writer_mem_init(&writer, sprite_data, sizeof(sprite_data)));
  assert(sky_to_sprites(&reader, &writer) == SFError_Trunc);
  assert(finish_writer(&writer) == 0);
  reader_destroy(&reader);
}

static void test_scan_negative_sprite_count(void)
{
  uint8_t sprite_data[32];
  Reader reader;
  Writer writer;
  ScanSpritesContext context;

  assert(writer_mem_init(&writer, sprite_data, sizeof(sprite_data)));
  assert(writer_fwrite_int32(-1, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  long int const len = finish_writer(&writer);
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, sprite_data, (size_t)len));
  assert(scan_sprite_file(&reader, &context) == SFError_BadNumGFX);
  reader_destroy(&reader);
}

static void check_scan_error(void const *const data, size_t const size,
                             SFError const expected)
{
  Reader reader;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, data, size));
  assert(scan_sprite_file(&reader, &context) == expected);
  reader_destroy(&reader);
}

static void test_bad_sprite_area_offsets(void)
{
  uint8_t data[32];
  long int size = make_sprite_area(data, sizeof(data), 0, 15, 15, NULL, 0);
  check_scan_error(data, (size_t)size, SFError_BadDataOff);

  size = make_sprite_area(data, sizeof(data), 0, 17, 16, NULL, 0);
  check_scan_error(data, (size_t)size, SFError_BadDataOff);
}

static void test_truncated_sprite_area_fields(void)
{
  uint8_t data[32];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  long int size = finish_writer(&writer);
  check_scan_error(data, (size_t)size, SFError_Trunc);

  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(16, &writer));
  size = finish_writer(&writer);
  check_scan_error(data, (size_t)size, SFError_Trunc);
}

static void test_empty_sprite_area(void)
{
  uint8_t data[32];
  long int const size = make_sprite_area(data, sizeof(data), 0, 16, 16,
                                         NULL, 0);
  Reader reader;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, data, (size_t)size));
  assert(scan_sprite_file(&reader, &context) == SFError_OK);
  assert(count_spr_types(&context) == 0);
  assert(!context.bad_sprite);
  reader_destroy(&reader);
}

static void test_unrecognised_sprite(void)
{
  uint8_t data[MapTileBitmapSize + 64];
  long int const size = make_one_tile_sprite(
                                             data, sizeof(data), "not_a_tile", 13, MapTileWidth / 4 - 1,
                                             MapTileHeight - 1, 0, 31);
  Reader reader;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, data, (size_t)size));
  assert(scan_sprite_file(&reader, &context) == SFError_OK);
  assert(context.bad_sprite);
  assert(strcmp(context.bad_name, "not_a_tile") == 0);
  assert(count_spr_types(&context) == 0);
  reader_destroy(&reader);
}

static void test_sparse_tiles(void)
{
  uint8_t sprites[MapTileBitmapSize + 64], tiles[BufferSize];
  long int const sprites_size = make_one_tile_sprite(
                                                     sprites, sizeof(sprites), "tile_1", 13, MapTileWidth / 4 - 1,
                                                     MapTileHeight - 1, 0, 31);
  Reader reader;
  Writer writer;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(scan_sprite_file(&reader, &context) == SFError_OK);
  reader_destroy(&reader);
  assert(context.tiles.count == 1);
  assert(context.tiles.hdr.last_tile_num == 1);

  assert(reader_mem_init(&reader, sprites, (size_t)sprites_size));
  assert(writer_mem_init(&writer, tiles, sizeof(tiles)));
  assert(sprites_to_tiles(&reader, &writer, &context.tiles) == SFError_OK);
  long int const tiles_size = finish_writer(&writer);
  reader_destroy(&reader);
  assert(tiles_size == 16 + 2 * MapTileBitmapSize);

  Reader tiles_reader;
  assert(reader_mem_init(&tiles_reader, tiles, (size_t)tiles_size));
  assert(reader_fseek(&tiles_reader, 16, SEEK_SET) == 0);
  for (int i = 0; i < MapTileBitmapSize; ++i)
  {
    assert(reader_fgetc(&tiles_reader) == 0);
  }
  reader_destroy(&tiles_reader);
}

static void test_tiles_csv_repairs(void)
{
  MapTilesHeader tiles = {.last_tile_num = NumTiles - 1};
  check_csv_result("-1,3,1,2\n9,-2,1,0\n-1,256,2,3\n",
                   SFError_ForceAnim, csv_to_tiles_adapter, &tiles);
  assert(tiles.splash_anim_1[0] == 0);
  assert(tiles.splash_anim_1[1] == NumTiles - 1);
  assert(tiles.splash_anim_2[0] == NumTiles - 1);
  assert(tiles.splash_anim_2[1] == 0);
  assert(tiles.splash_2_triggers[0] == 0);
  assert(tiles.splash_2_triggers[1] == UINT8_MAX);
}

static void test_planets_csv_repairs(void)
{
  PlanetsHeader planets = {.last_image_num = 1};
  check_csv_result("1,-37\n-37,1\n", SFError_ForceOff,
                   csv_to_planets_adapter, &planets);
  assert(planets.paint_coords[0].x_offset == 0);
  assert(planets.paint_coords[0].y_offset == -PlanetHeight);
  assert(planets.paint_coords[1].x_offset == -PlanetWidth);
  assert(planets.paint_coords[1].y_offset == 0);
}

static void test_sky_csv_repairs(void)
{
  SkyHeader sky = {0};
  check_csv_result("-1,2049\n", SFError_ForceSky,
                   csv_to_sky_adapter, &sky);
  assert(sky.render_offset == 0);
  assert(sky.min_stars_height == 2048);
  check_csv_result("2049,-32769\n", SFError_ForceSky,
                   csv_to_sky_adapter, &sky);
  assert(sky.render_offset == 2048);
  assert(sky.min_stars_height == -32768);
}

static void test_tiles_csv_overflow(void)
{
  char csv[256];
  memset(csv, '0', sizeof(csv));
  MapTilesHeader tiles = {.last_tile_num = 0};
  Reader reader;

  assert(reader_mem_init(&reader, csv, sizeof(csv)));
  assert(csv_to_tiles(&reader, &tiles) == SFError_StrOFlo);
  reader_destroy(&reader);
}

static void test_planets_csv_overflow(void)
{
  char csv[256];
  memset(csv, '0', sizeof(csv));
  PlanetsHeader planets = {.last_image_num = 0};
  Reader reader;
  assert(reader_mem_init(&reader, csv, sizeof(csv)));
  assert(csv_to_planets(&reader, &planets) == SFError_StrOFlo);
  reader_destroy(&reader);
}

static void test_sky_csv_overflow(void)
{
  char csv[256];
  memset(csv, '0', sizeof(csv));
  SkyHeader sky = {0};
  Reader reader;
  assert(reader_mem_init(&reader, csv, sizeof(csv)));
  assert(csv_to_sky(&reader, &sky) == SFError_StrOFlo);
  reader_destroy(&reader);
}

static void test_convert_advance_done(void)
{
  ConvertIter iter = {.pos = 0, .count = 0};
  assert(convert_advance(&iter) == SFError_Done);
  assert(convert_finish(&iter) == SFError_OK);
}

static void check_to_sprites_error(void const *const data, size_t const size,
                                   ToSpritesFn *const convert,
                                   SFError const expected)
{
  uint8_t sprites[BufferSize];
  Reader reader;
  Writer writer;
  assert(reader_mem_init(&reader, data, size));
  assert(writer_mem_init(&writer, sprites, sizeof(sprites)));
  assert(convert(&reader, &writer) == expected);
  finish_writer(&writer);
  reader_destroy(&reader);
}

static void test_negative_tile_count(void)
{
  uint8_t data[64];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(-1, &writer));
  long int size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, tiles_to_sprites,
                         SFError_BadNumGFX);
}

static void test_excessive_tile_count(void)
{
  uint8_t data[64];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(MapTileMax + 1, &writer));
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, tiles_to_sprites,
                         SFError_BadNumGFX);
}

static void test_bad_tile_animation(void)
{
  uint8_t data[64];
  Writer writer;
  uint8_t const bad_animation[MapAnimFrameCount] = {0, 1, 3, 0},
                good_animation[MapAnimFrameCount] = {0, 1, 2, 0},
                triggers[MapAnimTriggerCount] = {0};
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(2, &writer));
  assert(writer_fwrite(bad_animation, sizeof(bad_animation), 1, &writer) == 1);
  assert(writer_fwrite(good_animation, sizeof(good_animation), 1, &writer) == 1);
  assert(writer_fwrite(triggers, sizeof(triggers), 1, &writer) == 1);
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, tiles_to_sprites,
                         SFError_BadAnims);
}

static void test_bad_sky_render_offset(void)
{
  uint8_t data[16];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(-1, &writer));
  assert(writer_fwrite_int32(0, &writer));
  long int size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, sky_to_sprites, SFError_BadRend);
}

static void test_bad_sky_stars_height(void)
{
  uint8_t data[16];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(2049, &writer));
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, sky_to_sprites, SFError_BadStar);
}

static void test_truncated_sky_header(void)
{
  uint8_t data[16];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, sky_to_sprites, SFError_Trunc);
}

static void test_excessive_planet_count(void)
{
  uint8_t data[128];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(PlanetMax + 1, &writer));
  long int size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, planets_to_sprites,
                         SFError_BadNumGFX);
}

static void test_bad_planet_paint_offset(void)
{
  uint8_t data[128];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(1, &writer));
  assert(writer_fwrite_int32(0, &writer));
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, planets_to_sprites,
                         SFError_BadPaintOff);
}

static void test_bad_planet_data_offset(void)
{
  uint8_t data[128];
  Writer writer;
  assert(writer_mem_init(&writer, data, sizeof(data)));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(0, &writer));
  assert(writer_fwrite_int32(35, &writer));
  assert(writer_fwrite_int32(35 + PlanetBitmapSize, &writer));
  long int const size = finish_writer(&writer);
  check_to_sprites_error(data, (size_t)size, planets_to_sprites,
                         SFError_BadDataOff);
}

static void test_unknown_sprite_extension(void)
{
  uint8_t extension[32], area[64];
  Writer writer;
  assert(writer_mem_init(&writer, extension, sizeof(extension)));
  assert(writer_fwrite("????", 4, 1, &writer) == 1);
  long int extension_size = finish_writer(&writer);
  long int area_size = make_sprite_area(area, sizeof(area), 0,
                                        16 + (int32_t)extension_size, 16 + (int32_t)extension_size,
                                        extension, (size_t)extension_size);
  check_scan_error(area, (size_t)area_size, SFError_OK);
}

static void test_repair_sky_extension(void)
{
  uint8_t extension[32], area[64];
  Writer writer;
  assert(writer_mem_init(&writer, extension, sizeof(extension)));
  assert(writer_fwrite("HEIG", 4, 1, &writer) == 1);
  assert(writer_fwrite_int32(-1, &writer));
  assert(writer_fwrite_int32(2049, &writer));
  long int const extension_size = finish_writer(&writer);
  long int const area_size = make_sprite_area(area, sizeof(area), 0,
                                              16 + (int32_t)extension_size, 16 + (int32_t)extension_size,
                                              extension, (size_t)extension_size);
  Reader reader;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof(context));
  assert(reader_mem_init(&reader, area, (size_t)area_size));
  assert(scan_sprite_file(&reader, &context) == SFError_OK);
  assert(context.sky.got_hdr);
  assert(context.sky.fixed_render);
  assert(context.sky.fixed_stars);
  assert(context.sky.hdr.render_offset == 0);
  assert(context.sky.hdr.min_stars_height == 2048);
  reader_destroy(&reader);
}

static void test_negative_planet_extension_count(void)
{
  uint8_t extension[32], area[64];
  Writer writer;
  assert(writer_mem_init(&writer, extension, sizeof(extension)));
  assert(writer_fwrite("OFFS", 4, 1, &writer) == 1);
  assert(writer_fwrite_int32(-1, &writer));
  long int const extension_size = finish_writer(&writer);
  long int const area_size = make_sprite_area(area, sizeof(area), 0,
                                              16 + (int32_t)extension_size, 16 + (int32_t)extension_size,
                                              extension, (size_t)extension_size);
  check_scan_error(area, (size_t)area_size, SFError_BadNumGFX);
}

#ifdef FORTIFY
static void check_to_sprites_alloc_failure(ToSpritesFn *const convert)
{
  uint8_t input[1] = {0}, output[1];
  Reader reader;
  Writer writer;
  assert(reader_mem_init(&reader, input, sizeof input));
  assert(writer_mem_init(&writer, output, sizeof output));
  Fortify_SetFailRate(100);
  assert(convert(&reader, &writer) == SFError_NoMem);
  Fortify_SetFailRate(0);
  assert(finish_writer(&writer) == 0);
  reader_destroy(&reader);
}

static void check_from_sprites_alloc_failure(FromSpritesFn *const convert)
{
  uint8_t input[1] = {0}, output[1];
  Reader reader;
  Writer writer;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof context);
  assert(reader_mem_init(&reader, input, sizeof input));
  assert(writer_mem_init(&writer, output, sizeof output));
  Fortify_SetFailRate(100);
  assert(convert(&reader, &writer, &context) == SFError_NoMem);
  Fortify_SetFailRate(0);
  assert(finish_writer(&writer) == 0);
  reader_destroy(&reader);
}

static void test_scan_alloc_failure(void)
{
  uint8_t input[1] = {0};
  Reader reader;
  ScanSpritesContext context;
  memset(&context, 0xa5, sizeof context);
  assert(reader_mem_init(&reader, input, sizeof input));
  Fortify_SetFailRate(100);
  assert(scan_sprite_file(&reader, &context) == SFError_NoMem);
  Fortify_SetFailRate(0);
  reader_destroy(&reader);
}

static void test_tiles_to_sprites_alloc_failure(void)
{
  check_to_sprites_alloc_failure(tiles_to_sprites);
}

static void test_tiles_to_sprites_ext_alloc_failure(void)
{
  check_to_sprites_alloc_failure(tiles_to_sprites_ext);
}

static void test_planets_to_sprites_alloc_failure(void)
{
  check_to_sprites_alloc_failure(planets_to_sprites);
}

static void test_planets_to_sprites_ext_alloc_failure(void)
{
  check_to_sprites_alloc_failure(planets_to_sprites_ext);
}

static void test_sky_to_sprites_alloc_failure(void)
{
  check_to_sprites_alloc_failure(sky_to_sprites);
}

static void test_sky_to_sprites_ext_alloc_failure(void)
{
  check_to_sprites_alloc_failure(sky_to_sprites_ext);
}

static void test_sprites_to_tiles_alloc_failure(void)
{
  check_from_sprites_alloc_failure(from_tiles);
}

static void test_sprites_to_planets_alloc_failure(void)
{
  check_from_sprites_alloc_failure(from_planets);
}

static void test_sprites_to_sky_alloc_failure(void)
{
  check_from_sprites_alloc_failure(from_sky);
}
#endif

void Conv_tests(void)
{
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "File sizes", test_sizes },
    { "Sky extended sprite round trip", test_sky_round_trip },
    { "Map tile extended sprite round trip", test_tiles_round_trip },
    { "Planet extended sprite round trip", test_planets_round_trip },
    { "Sky non-extended sprite round trip", test_sky_nonextended_round_trip },
    { "Map tile non-extended sprite round trip",
      test_tiles_nonextended_round_trip },
    { "Planet non-extended sprite round trip",
      test_planets_nonextended_round_trip },
    { "Incremental map tile conversion", test_incremental_conversion },
    { "Convert sky to CSV", test_sky_to_csv },
    { "Apply CSV to sky header", test_csv_to_sky },
    { "Convert map tiles to CSV", test_tiles_to_csv },
    { "Apply CSV to map tile header", test_csv_to_tiles },
    { "Convert planets to CSV", test_planets_to_csv },
    { "Apply CSV to planet header", test_csv_to_planets },
    { "Scan truncated sprite area", test_scan_truncated_sprite_area },
    { "Convert truncated sky", test_convert_truncated_sky },
    { "Scan negative sprite count", test_scan_negative_sprite_count },
    { "Reject bad sprite area offsets", test_bad_sprite_area_offsets },
    { "Reject truncated sprite area fields", test_truncated_sprite_area_fields },
    { "Scan an empty sprite area", test_empty_sprite_area },
    { "Record an unrecognised sprite", test_unrecognised_sprite },
    { "Reconstruct a missing tile as black", test_sparse_tiles },
    { "Repair out-of-range map tile CSV values", test_tiles_csv_repairs },
    { "Repair out-of-range planet CSV values", test_planets_csv_repairs },
    { "Repair out-of-range sky CSV values", test_sky_csv_repairs },
    { "Reject oversized map tile CSV data", test_tiles_csv_overflow },
    { "Reject oversized planet CSV data", test_planets_csv_overflow },
    { "Reject oversized sky CSV data", test_sky_csv_overflow },
    { "Advance an already-complete iterator", test_convert_advance_done },
    { "Reject a negative map tile count", test_negative_tile_count },
    { "Reject an excessive map tile count", test_excessive_tile_count },
    { "Reject a bad map tile animation", test_bad_tile_animation },
    { "Reject a bad sky render offset", test_bad_sky_render_offset },
    { "Reject a bad sky stars height", test_bad_sky_stars_height },
    { "Reject a truncated sky header", test_truncated_sky_header },
    { "Reject an excessive planet count", test_excessive_planet_count },
    { "Reject a bad planet paint offset", test_bad_planet_paint_offset },
    { "Reject a bad planet data offset", test_bad_planet_data_offset },
    { "Ignore an unknown sprite extension", test_unknown_sprite_extension },
    { "Repair a sky sprite extension", test_repair_sky_extension },
    { "Reject a negative planet extension count",
      test_negative_planet_extension_count },
#ifdef FORTIFY
    { "Fail to allocate a sprite scanner", test_scan_alloc_failure },
    { "Fail to allocate a map tile-to-sprite converter",
      test_tiles_to_sprites_alloc_failure },
    { "Fail to allocate an extended map tile-to-sprite converter",
      test_tiles_to_sprites_ext_alloc_failure },
    { "Fail to allocate a planet-to-sprite converter",
      test_planets_to_sprites_alloc_failure },
    { "Fail to allocate an extended planet-to-sprite converter",
      test_planets_to_sprites_ext_alloc_failure },
    { "Fail to allocate a sky-to-sprite converter",
      test_sky_to_sprites_alloc_failure },
    { "Fail to allocate an extended sky-to-sprite converter",
      test_sky_to_sprites_ext_alloc_failure },
    { "Fail to allocate a sprite-to-map tile converter",
      test_sprites_to_tiles_alloc_failure },
    { "Fail to allocate a sprite-to-planet converter",
      test_sprites_to_planets_alloc_failure },
    { "Fail to allocate a sprite-to-sky converter",
      test_sprites_to_sky_alloc_failure },
#endif
  };

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); ++count)
  {
    DEBUGF("Test %zu/%zu : %s\n", 1 + count, ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);
    Fortify_EnterScope();
    unit_tests[count].test_func();
    Fortify_LeaveScope();
  }
}
