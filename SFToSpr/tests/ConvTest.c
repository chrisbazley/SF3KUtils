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
  assert(reader_mem_init(&reader, sprite_data, (size_t)len));
  assert(scan_sprite_file(&reader, &context) == SFError_BadNumGFX);
  reader_destroy(&reader);
}

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
