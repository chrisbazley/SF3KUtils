/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Unit tests
 *  Copyright (C) 2017 Christopher Bazley
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

/* ANSI library files */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <time.h>
#include <setjmp.h>
#include <stdint.h>

/* RISC OS library files */
#include "swis.h"
#include "event.h"
#include "toolbox.h"
#include "saveas.h"
#include "gadgets.h"
#include "quit.h"

/* My library files */
#include "Macros.h"
#include "Debug.h"
#include "FileUtils.h"
#include "Err.h"
#include "userdata.h"
#include "gkeycomp.h"
#include "gkeydecomp.h"
#include "SFFormats.h"
#include "OSFile.h"
#include "PseudoWimp.h"
#include "PseudoTbox.h"
#include "PseudoEvnt.h"
#include "PseudoExit.h"
#include "FOpenCount.h"
#include "ViewsMenu.h"
#include "msgtrans.h"
#include "Hourglass.h"
#include "FileRWInt.h"

/* Local header files */
#include "SFTInit.h"

#include "Fortify.h"

#define TEST_DATA_DIR "<Wimp$ScrapDir>.SFtoSprTests"
#define TEST_DATA_IN TEST_DATA_DIR ".in"
#define TEST_DATA_OUT TEST_DATA_DIR ".out"
#define BATCH_PATH_SUBDIR ".oops"
#define BATCH_PATH_PLANETS_TAIL BATCH_PATH_SUBDIR ".planets"
#define BATCH_PATH_SKY_TAIL BATCH_PATH_SUBDIR ".sky"
#define BATCH_PATH_SPRITES_TAIL BATCH_PATH_SUBDIR ".sprites"
#define BATCH_PATH_IGNORE_TAIL BATCH_PATH_SUBDIR ".ignore"
#define TEST_LEAFNAME "FatChance"
#define WORD_SIZE 4

#define assert_no_error(x) \
do { \
  const _kernel_oserror * const e = x; \
  if (e != NULL) \
  { \
    DEBUGF("Error: 0x%x,%s %s:%d\n", e->errnum, e->errmess, __FILE__, __LINE__); \
    abort(); \
  } \
} \
while(0)

enum
{
  FednetHistoryLog2 = 9, /* Base 2 logarithm of the history size used by
                            the compression algorithm */
  FortifyAllocationLimit = 2048,
  TestDataSize = 12,
  CompressionBufferSize = 1024,
  DestinationIcon = 2,
  DestinationX = 900,
  DestinationY = 34,
  Timeout = 30 * CLOCKS_PER_SEC,
  ComponentId_Scan_Abort_ActButton = 0x01,
  ComponentId_Scan_Pause_ActButton = 0x04,
  ComponentId_SaveDir_Decompress_Radio = 0,
  ComponentId_SaveDir_Extract_Images_Radio = 1,
  ComponentId_SaveDir_Extract_Data_Radio = 2,
  ComponentId_SaveDir_Compress_Radio = 3,
  ComponentId_SaveFile_Decompress_Radio = 0,
  ComponentId_SaveFile_Extract_Images_Radio = 1,
  ComponentId_SaveFile_Extract_Data_Radio = 2,
  OS_FSControl_Copy = 26,
  OS_FSControl_Wipe = 27,
  OS_FSControl_Flag_Recurse = 1,
  PaddingSize = 12,
  PlanetsHdrSize = 36,
  PlanetPaintX0 = -36,
  PlanetPaintY0 = -32,
  PlanetPaintX1 = -9,
  PlanetPaintY1 = -1,
  PlanetBorder = 2,
  NSprites = 36,
  NPlanets = 2,
  PlanetBitmapSize = SFPlanet_Width * SFPlanet_Height,
  SkyHdrSize = 8,
  SkyBitmapSize = SFSky_Width * SFSky_Height,
  SkyPaintOffset = 13,
  SkyStarsHeight = -9,
  TilesHdrSize = 16,
  TileBitmapSize = SFMapTile_Width * SFMapTile_Height,
  TileAnim0 = 35,
  TileAnim1 = 3,
  TileAnim2 = 13,
  TileAnim3 = 9,
  TileBTrig0 = 6, /* and 7 */
  TileBTrig1 = 21, /* and 22 */
  TileBTrig2 = 1, /* and 2 */
  TileBTrig3 = 4, /* and 5 */
  TileBAnim0 = TileBTrig0,
  TileBAnim1 = TileBTrig0 + 1,
  TileBAnim2 = TileBTrig1,
  TileBAnim3 = TileBTrig1 + 1,
  PlanetMagic = 55,
  SkyMagic = 67,
  TileMagic = 7,
  SpriteAreaHdrSize = 12,
  SpriteHdrSize = 44,
  SpriteType = 13,
  PlanetMetadataSize = (6*4),
  SkyMetadataSize = (3*4),
  TileMetadataSize = 16,
  SpriteHdrOffset = 4,
  MaxCSVSize = 256,
  ForeignTaskHandle = 999,
  UnsafeDataSize = -1,
  FSControl_CanonicalisePath = 37,
  MaxNumWindows = 3,
  WorkArea = -1 /* Pseudo icon handle (window's work area) */

};

typedef enum
{
  DTM_RAM,     /* Receiver sends RAM fetch and falls back to data save ack if ignored;
                  sender replies to either RAM fetch or data save ack */
  DTM_File,    /* Receiver sends data save ack; sender ignores (first) RAM fetch */
  DTM_BadRAM,  /* Receiver ignores RAM transmit; sender ignores (2nd or subsequent) RAM fetch */
  DTM_BadFile, /* Receiver ignores data load; sender doesn't send data load */
  DTM_None     /* Receiver ignores data save; sender doesn't send data save */
}
DataTransferMethod;

static int th;

static void wipe(const char *path_name)
{
  _kernel_swi_regs regs;

  assert(path_name != NULL);

  regs.r[0] = OS_FSControl_Wipe;
  regs.r[1] = (uintptr_t)path_name;
  regs.r[3] = OS_FSControl_Flag_Recurse;
  _kernel_swi(OS_FSControl, &regs, &regs);
}

static void copy(const char *src, const char *dst)
{
  _kernel_swi_regs regs;

  assert(src != NULL);
  assert(dst != NULL);

  regs.r[0] = OS_FSControl_Copy;
  regs.r[1] = (uintptr_t)src;
  regs.r[2] = (uintptr_t)dst;
  regs.r[3] = OS_FSControl_Flag_Recurse;
  assert_no_error(_kernel_swi(OS_FSControl, &regs, &regs));
}

static int make_compressed_file(const char *const file_name, void *const data, const size_t size, const int file_type)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(data != NULL);
  assert(size > 0);

  char out_buffer[CompressionBufferSize];
  GKeyComp      *comp;
  int estimated_size = sizeof(int32_t);
  GKeyStatus status;

  FILE *const f = fopen(file_name, "wb");
  assert(f != NULL);

  bool const ok = fwrite_int32le(size, f);
  assert(ok);

  comp = gkeycomp_make(FednetHistoryLog2);
  assert(comp != NULL);

  GKeyParameters params = {
    .in_buffer = data,
    .in_size = size,
    .out_buffer = out_buffer,
    .out_size = sizeof(out_buffer),
    .prog_cb = NULL,
    .cb_arg = NULL,
  };

  DEBUG_SET_OUTPUT(DebugOutput_None, "");
  do
  {
    /* Compress the data from the input buffer to the output buffer */
    status = gkeycomp_compress(comp, &params);

    /* Is the output buffer full or have we finished? */
    if (status == GKeyStatus_Finished ||
        status == GKeyStatus_BufferOverflow ||
        params.out_size == 0)
    {
      /* Empty the output buffer by writing to file */
      const size_t to_write = sizeof(out_buffer) - params.out_size;
      size_t const n = fwrite(out_buffer, to_write, 1, f);
      assert(n == 1);
      estimated_size += to_write;

      params.out_buffer = out_buffer;
      params.out_size = sizeof(out_buffer);

      if (status == GKeyStatus_BufferOverflow)
        status = GKeyStatus_OK; /* Buffer overflow has been fixed up */
    }
  }
  while (status == GKeyStatus_OK);
  DEBUG_SET_OUTPUT(DebugOutput_FlushedFile, "SFtoSprLog");

  assert(status == GKeyStatus_Finished);
  gkeycomp_destroy(comp);

  fclose(f);
  assert_no_error(os_file_set_type(file_name, file_type));

  return estimated_size;
}

static int make_compressed_planets_file(const char *const file_name, const int n, bool metadata)
{
  NOT_USED(metadata);
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n <= 2);

  uint8_t test_data[PlanetsHdrSize + ((PaddingSize + PlanetBitmapSize) * n * 2)];

  size_t i = 0;
  ((int32_t *)test_data)[i++] = n-1;
  ((int32_t *)test_data)[i++] = PlanetPaintX0;
  ((int32_t *)test_data)[i++] = PlanetPaintY0;
  ((int32_t *)test_data)[i++] = PlanetPaintX1;
  ((int32_t *)test_data)[i++] = PlanetPaintY1;

  ((int32_t *)test_data)[i++] = PlanetsHdrSize + PaddingSize;
  ((int32_t *)test_data)[i++] = PlanetsHdrSize + PaddingSize + PlanetBitmapSize + PaddingSize;
  ((int32_t *)test_data)[i++] = PlanetsHdrSize + PaddingSize + (PlanetBitmapSize + PaddingSize) * 2;
  ((int32_t *)test_data)[i++] = PlanetsHdrSize + PaddingSize + (PlanetBitmapSize + PaddingSize) * 3;

  uint8_t p = PlanetMagic;
  for (int j = 0; j < n; ++j) {
    uint8_t *const bm = (uint8_t *)test_data +
                        ((int32_t *)test_data)[5 + (j*2)];
    uint8_t *const bm2 = (uint8_t *)test_data +
                         ((int32_t *)test_data)[6 + (j*2)];

    memset(bm, 0, PlanetBitmapSize);
    memset(bm2, 0, PlanetBitmapSize);

    for (int y = 0; y < SFPlanet_Height; ++y) {
      for (int x = 0; x < (SFPlanet_Width - PlanetBorder); ++x) {
        bm[(y * SFPlanet_Width) + x] = p;
        bm2[(y * SFPlanet_Width) + x + PlanetBorder] = p++;
      }
    }
  }

  return make_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFSkyPic);
}

static int make_compressed_sky_file(const char *const file_name, int const n, bool metadata)
{
  NOT_USED(metadata);
  NOT_USED(n);
  assert(file_name != NULL);
  assert(*file_name != '\0');

  uint8_t test_data[SkyHdrSize + SkyBitmapSize];

  size_t i = 0;
  ((int32_t *)test_data)[i++] = SkyPaintOffset;
  ((int32_t *)test_data)[i++] = SkyStarsHeight;

  uint8_t p = SkyMagic;
  uint8_t *const bm = test_data + SkyHdrSize;

  for (int y = 0; y < SFSky_Height; ++y) {
    for (int x = 0; x < SFSky_Width; ++x) {
      bm[(y * SFSky_Width) + x] = p++;
    }
  }

  return make_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFSkyCol);
}

static int make_compressed_sprites_file(const char *const file_name, const int n, bool metadata)
{
  NOT_USED(metadata);
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n < 256);

  uint8_t test_data[TilesHdrSize + (TileBitmapSize * n)];

  *((int32_t *)test_data) = n-1;

  size_t i = 0;
  ((int8_t *)test_data + 4)[i++] = TileAnim0;
  ((int8_t *)test_data + 4)[i++] = TileAnim1;
  ((int8_t *)test_data + 4)[i++] = TileAnim2;
  ((int8_t *)test_data + 4)[i++] = TileAnim3;

  ((int8_t *)test_data + 4)[i++] = TileBAnim0;
  ((int8_t *)test_data + 4)[i++] = TileBAnim1;
  ((int8_t *)test_data + 4)[i++] = TileBAnim2;
  ((int8_t *)test_data + 4)[i++] = TileBAnim3;

  ((int8_t *)test_data + 4)[i++] = TileBTrig0;
  ((int8_t *)test_data + 4)[i++] = TileBTrig1;
  ((int8_t *)test_data + 4)[i++] = TileBTrig2;
  ((int8_t *)test_data + 4)[i++] = TileBTrig3;

  uint8_t p = TileMagic;
  for (int j = 0; j < n; ++j) {
    uint8_t *const bm = test_data + TilesHdrSize +
                        (TileBitmapSize * j);

    for (int y = 0; y < SFMapTile_Height; ++y) {
      for (int x = 0; x < SFMapTile_Width; ++x) {
        bm[(y * SFMapTile_Width) + x] = p++;
      }
    }
  }

  return make_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFMapGfx);
}

static void check_compressed_file(const char *const file_name, void *const data, const size_t size, const int file_type)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(data != NULL);
  assert(size > 0);

  uint8_t in_buffer[CompressionBufferSize];
  GKeyDecomp     *decomp;
  long int len;
  bool in_pending = false;
  GKeyStatus status;
  OS_File_CatalogueInfo cat;

  assert_no_error(os_file_read_cat_no_path(file_name, &cat));
  assert(cat.object_type == ObjectType_File);
  DEBUGF("Load address: 0x%x\n", cat.load);
  assert(((cat.load >> 8) & 0xfff) == file_type);

  FILE *const f = fopen(file_name, "rb");
  assert(f != NULL);

  bool const ok = fread_int32le(&len, f);
  assert(ok);
  assert(len > 0);

  decomp = gkeydecomp_make(FednetHistoryLog2);
  assert(decomp != NULL);

  GKeyParameters params = {
    .in_buffer = in_buffer,
    .in_size = 0,
    .out_buffer = data,
    .out_size = size,
    .prog_cb = NULL,
    .cb_arg = NULL,
  };

  DEBUG_SET_OUTPUT(DebugOutput_None, "");
  do
  {
    /* Is the input buffer empty? */
    if (params.in_size == 0)
    {
      /* Fill the input buffer by reading from file */
      params.in_buffer = in_buffer;
      params.in_size = fread(in_buffer, 1, sizeof(in_buffer), f);
      assert(!ferror(f));
    }

    /* Decompress the data from the input buffer to the output buffer */
    status = gkeydecomp_decompress(decomp, &params);

    /* If the input buffer is empty and it cannot be (re-)filled then
       there is no more input pending. */
    in_pending = params.in_size > 0 || !feof(f);

    if (in_pending && status == GKeyStatus_TruncatedInput)
    {
      /* False alarm before end of input data */
      status = GKeyStatus_OK;
    }
    assert(status == GKeyStatus_OK);
  }
  while (in_pending);
  DEBUG_SET_OUTPUT(DebugOutput_FlushedFile, "SFtoSprLog");

  gkeydecomp_destroy(decomp);

  fclose(f);
}

static void check_planets_file(const void *const test_data, const int n)
{
  assert(test_data != NULL);
  assert(n > 0);
  assert(n <= 2);

  size_t i = 0;
  assert(((int32_t *)test_data)[i++] == n - 1);
  assert(((int32_t *)test_data)[i++] == PlanetPaintX0);
  assert(((int32_t *)test_data)[i++] == PlanetPaintY0);
  assert(((int32_t *)test_data)[i++] == PlanetPaintX1);
  assert(((int32_t *)test_data)[i++] == PlanetPaintY1);

  uint8_t p = PlanetMagic;
  for (int j = 0; j < n; ++j)
  {
    const uint8_t *const bm = (uint8_t *)test_data +
                              ((int32_t *)test_data)[5 + (j*2)];
    const uint8_t *const bm2 = (uint8_t *)test_data +
                               ((int32_t *)test_data)[6 + (j*2)];

    for (int y = 0; y < SFPlanet_Height; ++y)
    {
      for (int x = 0; x < (SFPlanet_Width - PlanetBorder); ++x)
      {
        DEBUGF("y %d x %x expected %d got %d\n", y, x, p,
               bm[(y * SFPlanet_Width) + x]);
        assert(bm[(y * SFPlanet_Width) + x] == p);
        assert(bm2[(y * SFPlanet_Width) + x + 2] == p++);
      }
      for (int x = 0; x < PlanetBorder; ++x)
      {
        assert(bm[(y * SFPlanet_Width) + (SFPlanet_Width - PlanetBorder) + x] == 0);
        assert(bm2[(y * SFPlanet_Width) + x] == 0);
      }
    }
  }
}

static void check_compressed_planets_file(const char *const file_name, const int n)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n <= 2);
  uint8_t test_data[PlanetsHdrSize + (PlanetBitmapSize * n * 2)];
  check_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFSkyPic);
  check_planets_file(test_data, n);
}

static void check_sky_file(const void *const test_data)
{
  assert(test_data != NULL);

  size_t i = 0;
  assert(((int32_t *)test_data)[i++] == SkyPaintOffset);
  assert(((int32_t *)test_data)[i++] == SkyStarsHeight);

  uint8_t p = SkyMagic;
  const uint8_t *const bm = (uint8_t *)test_data + SkyHdrSize;

  for (int y = 0; y < SFSky_Height; ++y)
  {
    for (int x = 0; x < SFSky_Width; ++x)
    {
      DEBUGF("y %d x %d expected %d got %d\n",
             y, x, p, bm[(y * SFSky_Width) + x]);
      assert(bm[(y * SFSky_Width) + x] == p);
      ++p;
    }
  }
}

static void check_compressed_sky_file(const char *const file_name)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  uint8_t test_data[SkyHdrSize + SkyBitmapSize];
  check_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFSkyCol);
  check_sky_file(test_data);
}

static void check_sprites_file(const void *const test_data, const int n)
{
  assert(test_data != NULL);
  assert(n > 0);
  assert(n < 256);

  assert(((int32_t *)test_data)[0] == n - 1);

  size_t i = 0;
  assert(((int8_t *)test_data + 4)[i++] == TileAnim0);
  assert(((int8_t *)test_data + 4)[i++] == TileAnim1);
  assert(((int8_t *)test_data + 4)[i++] == TileAnim2);
  assert(((int8_t *)test_data + 4)[i++] == TileAnim3);

  assert(((int8_t *)test_data + 4)[i++] == TileBAnim0);
  assert(((int8_t *)test_data + 4)[i++] == TileBAnim1);
  assert(((int8_t *)test_data + 4)[i++] == TileBAnim2);
  assert(((int8_t *)test_data + 4)[i++] == TileBAnim3);

  assert(((int8_t *)test_data + 4)[i++] == TileBTrig0);
  assert(((int8_t *)test_data + 4)[i++] == TileBTrig1);
  assert(((int8_t *)test_data + 4)[i++] == TileBTrig2);
  assert(((int8_t *)test_data + 4)[i++] == TileBTrig3);

  uint8_t p = TileMagic;
  for (int j = 0; j < n; ++j)
  {
    const uint8_t *const bm = (uint8_t *)test_data +
                              TilesHdrSize + (TileBitmapSize * j);

    for (int y = 0; y < SFMapTile_Height; ++y)
    {
      for (int x = 0; x < SFMapTile_Width; ++x)
      {
        assert(bm[(y * SFMapTile_Width) + x] == p);
        ++p;
      }
    }
  }
}

static void check_compressed_sprites_file(const char *const file_name, const int n)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n < 256);
  uint8_t test_data[TilesHdrSize + (TileBitmapSize * n)];
  check_compressed_file(file_name, test_data, sizeof(test_data), FileType_SFMapGfx);
  check_sprites_file(test_data, n);
}

static int make_uncompressed_file(const char *const file_name, const void *const data, const size_t size, const int file_type)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(data != NULL);
  assert(size > 0);

  FILE *const f = fopen(file_name, "wb");
  assert(f != NULL);

  size_t const n = fwrite(data, size, 1, f);
  assert(n == 1);

  fclose(f);

  assert_no_error(os_file_set_type(file_name, file_type));

  return size;
}

static int make_uncompressed_planets_file(const char *const file_name, const int n, bool metadata)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n <= 2);

  int const msize = metadata ? PlanetMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + (SpriteHdrSize + PlanetBitmapSize) * n];

  size_t i = 0;
  ((int32_t *)test_data)[i++] = n;
  int32_t const first_sprite = SpriteHdrOffset + SpriteAreaHdrSize + msize;
  ((int32_t *)test_data)[i++] = first_sprite;
  ((int32_t *)test_data)[i++] = first_sprite + ((SpriteHdrSize + PlanetBitmapSize) * n);

  if (metadata) {
    static const char tag[4] = {'O','F','F','S'};
    memcpy((int32_t *)test_data + (i++), tag, sizeof(tag));

    ((int32_t *)test_data)[i++] = n;
    ((int32_t *)test_data)[i++] = PlanetPaintX0;
    ((int32_t *)test_data)[i++] = PlanetPaintY0;
    ((int32_t *)test_data)[i++] = PlanetPaintX1;
    ((int32_t *)test_data)[i++] = PlanetPaintY1;
  }

  uint8_t p = PlanetMagic;
  for (int j = 0; j < n; ++j) {
    ((int32_t *)test_data)[i++] = (SpriteHdrSize + PlanetBitmapSize);
    char *const src = (void *)((int32_t *)test_data + i);
    i += 3;
    memset(src, '\0', 12);
    sprintf(src, "planet_%d", j);
    const int nwords = (SFPlanet_Width + WORD_SIZE - 1) / WORD_SIZE;
    ((int32_t *)test_data)[i++] = nwords - 1;
    ((int32_t *)test_data)[i++] = SFPlanet_Height - 1;
    ((int32_t *)test_data)[i++] = 0;
    ((int32_t *)test_data)[i++] = 15;
    ((int32_t *)test_data)[i++] = SpriteHdrSize;
    ((int32_t *)test_data)[i++] = SpriteHdrSize;
    ((int32_t *)test_data)[i++] = SpriteType;

    uint8_t *const bm = (void *)((int32_t *)test_data + i);

    for (int y = 0; y < SFPlanet_Height; ++y) {
      i += nwords;
      for (int x = 0; x < SFPlanet_Width; ++x) {
        bm[(y * (nwords * WORD_SIZE)) + x] = (x < (SFPlanet_Width-2) ? p++ : 0);
      }
    }
  }
  return make_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite);
}

static int make_uncompressed_sky_file(const char *const file_name, const int n, bool metadata)
{
  NOT_USED(n);
  assert(file_name != NULL);
  assert(*file_name != '\0');

  int const msize = metadata ? SkyMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + SpriteHdrSize + SkyBitmapSize];

  size_t i = 0;
  ((int32_t *)test_data)[i++] = 1;
  int32_t const first_sprite = SpriteHdrOffset + SpriteAreaHdrSize + msize;
  ((int32_t *)test_data)[i++] = first_sprite;
  ((int32_t *)test_data)[i++] = first_sprite + (SpriteHdrSize + SkyBitmapSize);

  if (metadata) {
    static const char tag[4] = {'H','E','I','G'};
    memcpy((int32_t *)test_data + (i++), tag, sizeof(tag));

    ((int32_t *)test_data)[i++] = SkyPaintOffset;
    ((int32_t *)test_data)[i++] = SkyStarsHeight;
  }

  uint8_t p = SkyMagic;
  ((int32_t *)test_data)[i++] = (SpriteHdrSize + SkyBitmapSize);
  char *const src = (void *)((int32_t *)test_data + i);
  i += 3;
  memset(src, '\0', 12);
  strcpy(src, "sky");
  const int nwords = (SFSky_Width + WORD_SIZE - 1) / WORD_SIZE;
  ((int32_t *)test_data)[i++] = nwords - 1;
  ((int32_t *)test_data)[i++] = SFSky_Height - 1;
  ((int32_t *)test_data)[i++] = 0;
  ((int32_t *)test_data)[i++] = 31;
  ((int32_t *)test_data)[i++] = SpriteHdrSize;
  ((int32_t *)test_data)[i++] = SpriteHdrSize;
  ((int32_t *)test_data)[i++] = SpriteType;

  uint8_t *const bm = (void *)((int32_t *)test_data + i);
  for (int y = 0; y < SFSky_Height; ++y) {
    int const flip_y = (SFSky_Height - 1) - y;
    i += nwords;
    for (int x = 0; x < SFSky_Width; ++x) {
      bm[(flip_y * (nwords * WORD_SIZE)) + x] = p;
      ++p;
    }
  }
  return make_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite);
}

static int make_uncompressed_sprites_file(const char *const file_name, const int n, bool metadata)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n < 256);

  int const msize = metadata ? TileMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + ((SpriteHdrSize + TileBitmapSize) * n)];

  size_t i = 0;
  ((int32_t *)test_data)[i++] = n;
  int32_t const first_sprite = SpriteHdrOffset + SpriteAreaHdrSize + msize;
  ((int32_t *)test_data)[i++] = first_sprite;
  ((int32_t *)test_data)[i++] = first_sprite + ((SpriteHdrSize + TileBitmapSize) * n);

  if (metadata) {
    uint8_t *const anims = (uint8_t *)((int32_t *)test_data + i);
    size_t j = 0;

    static const char tag[4] = {'A','N','I','M'};
    memcpy(anims, tag, sizeof(tag));
    j += sizeof(tag);

    anims[j++] = TileAnim0;
    anims[j++] = TileAnim1;
    anims[j++] = TileAnim2;
    anims[j++] = TileAnim3;

    anims[j++] = TileBAnim0;
    anims[j++] = TileBAnim1;
    anims[j++] = TileBAnim2;
    anims[j++] = TileBAnim3;

    anims[j++] = TileBTrig0;
    anims[j++] = TileBTrig1;
    anims[j++] = TileBTrig2;
    anims[j++] = TileBTrig3;

    i += j / sizeof(int32_t);
  }

  uint8_t p = TileMagic;
  for (int j = 0; j < n; ++j) {
    ((int32_t *)test_data)[i++] = (SpriteHdrSize + TileBitmapSize);
    char *const src = (void *)((int32_t *)test_data + i);
    i += 3;
    memset(src, '\0', 12);
    sprintf(src, "tile_%d", j);
    const int nwords = (SFMapTile_Width + WORD_SIZE - 1) / WORD_SIZE;
    ((int32_t *)test_data)[i++] = nwords - 1;
    ((int32_t *)test_data)[i++] = SFMapTile_Height - 1;
    ((int32_t *)test_data)[i++] = 0;
    ((int32_t *)test_data)[i++] = 31;
    ((int32_t *)test_data)[i++] = SpriteHdrSize;
    ((int32_t *)test_data)[i++] = SpriteHdrSize;
    ((int32_t *)test_data)[i++] = SpriteType;

    uint8_t *const bm = (void *)((int32_t *)test_data + i);
    for (int y = 0; y < SFMapTile_Height; ++y) {
      int const flip_y = (SFMapTile_Height - 1) - y;
      i += nwords;
      for (int x = 0; x < SFMapTile_Width; ++x) {
        bm[(flip_y * (nwords * WORD_SIZE)) + x] = p++;
      }
    }
  }
  return make_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite);
}

static size_t check_uncompressed_file(const char *file_name, void *const test_data, size_t const size, int const file_type)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(test_data != NULL);
  assert(size > 0);

  OS_File_CatalogueInfo cat;
  assert_no_error(os_file_read_cat_no_path(file_name, &cat));
  assert(cat.object_type == ObjectType_File);
  DEBUGF("Load address: 0x%x\n", cat.load);
  assert(((cat.load >> 8) & 0xfff) == file_type);

  FILE *const f = fopen(file_name, "rb");
  assert(f != NULL);

  size_t const n = fread(test_data, 1, size, f);

  fclose(f);
  return n;
}

static void check_uncompressed_planets_file(const char *const file_name, const int n, bool metadata)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n <= 2);
  int const msize = metadata ? PlanetMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + (SpriteHdrSize + PlanetBitmapSize) * n];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite) == sizeof(test_data));

  size_t i = 0;
  assert(((int32_t *)test_data)[i++] == n);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + SpriteAreaHdrSize + msize);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + (int)sizeof(test_data));

  if (metadata) {
    static const char tag[4] = {'O','F','F','S'};
    assert(!memcmp((int32_t *)test_data + (i++), tag, sizeof(tag)));

    int32_t const noffsets = ((int32_t *)test_data)[i++];
    assert(noffsets == n);

    int32_t x[2], y[2];
    x[0] = ((int32_t *)test_data)[i++];
    y[0] = ((int32_t *)test_data)[i++];
    x[1] = ((int32_t *)test_data)[i++];
    y[1] = ((int32_t *)test_data)[i++];

    DEBUGF("Got %d,%d %d,%d Expected %d,%d %d,%d\n",
           x[0], y[0], x[1], y[1], PlanetPaintX0, PlanetPaintY0,
           PlanetPaintX1, PlanetPaintY1);

    assert(PlanetPaintX0 == x[0]);
    assert(PlanetPaintY0 == y[0]);
    assert(PlanetPaintX1 == x[1]);
    assert(PlanetPaintY1 == y[1]);
  }

  uint8_t p = PlanetMagic;
  for (int j = 0; j < n; ++j) {
    assert(((int32_t *)test_data)[i++] == (SpriteHdrSize + PlanetBitmapSize));
    const char *const src = (void *)((int32_t *)test_data + i);
    i += 3;
    char tmp[13] = {'\0'};
    for (size_t k = 0; k < sizeof(tmp) - 1; ++k) {
      tmp[k] = src[k];
    }
    int tileno;
    int l = sscanf(tmp, "planet_%d", &tileno);
    assert(l == 1);
    assert(tileno == j);
    const int nwords = (SFPlanet_Width - PlanetBorder + WORD_SIZE - 1) / WORD_SIZE;
    assert(((int32_t *)test_data)[i++] == nwords - 1);
    assert(((int32_t *)test_data)[i++] == SFPlanet_Height - 1);
    assert(((int32_t *)test_data)[i++] == 0);
    assert(((int32_t *)test_data)[i++] == 15);
    assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
    assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
    assert(((int32_t *)test_data)[i++] == SpriteType);

    uint8_t *const bm = (void *)((int32_t *)test_data + i);

    for (int y = 0; y < SFPlanet_Height; ++y) {
      i += nwords;
      for (int x = 0; x < SFPlanet_Width - PlanetBorder; ++x) {
        assert(bm[(y * (nwords * WORD_SIZE)) + x] == p);
        ++p;
      }
    }
  }
}

static void check_uncompressed_sky_file(const char *const file_name, const int n, bool metadata)
{
  NOT_USED(n);
  assert(file_name != NULL);
  assert(*file_name != '\0');
  int const msize = metadata ? SkyMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + SpriteHdrSize + SkyBitmapSize];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite) == sizeof(test_data));

  size_t i = 0;
  assert(((int32_t *)test_data)[i++] == 1);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + SpriteAreaHdrSize + msize);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + (int)sizeof(test_data));

  if (metadata) {
    static const char tag[4] = {'H','E','I','G'};
    assert(!memcmp((int32_t *)test_data + (i++), tag, sizeof(tag)));

    assert(((int32_t *)test_data)[i++] == SkyPaintOffset);
    assert(((int32_t *)test_data)[i++] == SkyStarsHeight);
  }

  uint8_t p = SkyMagic;
  assert(((int32_t *)test_data)[i++] == (SpriteHdrSize + SkyBitmapSize));
  const char *const src = (void *)((int32_t *)test_data + i);
  i += 3;
  char tmp[13] = {'\0'};
  for (size_t k = 0; k < sizeof(tmp) - 1; ++k) {
    tmp[k] = src[k];
  }
  assert(!strcmp(tmp, "sky"));
  const int nwords = (SFSky_Width + WORD_SIZE - 1) / WORD_SIZE;
  assert(((int32_t *)test_data)[i++] == nwords - 1);
  assert(((int32_t *)test_data)[i++] == SFSky_Height - 1);
  assert(((int32_t *)test_data)[i++] == 0);
  assert(((int32_t *)test_data)[i++] == 31);
  assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
  assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
  assert(((int32_t *)test_data)[i++] == SpriteType);

  uint8_t *const bm = (void *)((int32_t *)test_data + i);
  for (int y = 0; y < SFSky_Height; ++y) {
    int const flip_y = (SFSky_Height - 1) - y;
    i += nwords;
    for (int x = 0; x < SFSky_Width; ++x) {
      assert(bm[(flip_y * (nwords * WORD_SIZE)) + x] == p);
      ++p;
    }
  }
}

static void check_uncompressed_sprites_file(const char *const file_name, const int n, bool metadata)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  assert(n > 0);
  assert(n < 256);
  int const msize = metadata ? TileMetadataSize : 0;
  uint8_t test_data[SpriteAreaHdrSize + msize + ((SpriteHdrSize + TileBitmapSize) * n)];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_Sprite) == sizeof(test_data));

  size_t i = 0;
  assert(((int32_t *)test_data)[i++] == n);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + SpriteAreaHdrSize + msize);
  assert(((int32_t *)test_data)[i++] == SpriteHdrOffset + (int)sizeof(test_data));

  if (metadata) {
    const uint8_t *const anims = (uint8_t *)((int32_t *)test_data + i);
    size_t j = 0;

    static const char tag[4] = {'A','N','I','M'};
    assert(!memcmp(anims, tag, sizeof(tag)));
    j += sizeof(tag);

    assert(anims[j++] == TileAnim0);
    assert(anims[j++] == TileAnim1);
    assert(anims[j++] == TileAnim2);
    assert(anims[j++] == TileAnim3);

    assert(anims[j++] == TileBAnim0);
    assert(anims[j++] == TileBAnim1);
    assert(anims[j++] == TileBAnim2);
    assert(anims[j++] == TileBAnim3);

    assert(anims[j++] == TileBTrig0);
    assert(anims[j++] == TileBTrig1);
    assert(anims[j++] == TileBTrig2);
    assert(anims[j++] == TileBTrig3);

    i += j / sizeof(int32_t);
  }

  uint8_t p = TileMagic;
  for (int j = 0; j < n; ++j) {
    assert(((int32_t *)test_data)[i++] == (SpriteHdrSize + TileBitmapSize));
    const char *const src = (void *)((int32_t *)test_data + i);
    i += 3;
    char tmp[13] = {'\0'};
    for (size_t k = 0; k < sizeof(tmp) - 1; ++k) {
      tmp[k] = src[k];
    }
    int tileno;
    int l = sscanf(tmp, "tile_%d", &tileno);
    assert(l == 1);
    assert(tileno == j);
    const int nwords = (SFMapTile_Width + WORD_SIZE - 1) / WORD_SIZE;
    assert(((int32_t *)test_data)[i++] == nwords - 1);
    assert(((int32_t *)test_data)[i++] == SFMapTile_Height - 1);
    assert(((int32_t *)test_data)[i++] == 0);
    assert(((int32_t *)test_data)[i++] == 31);
    assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
    assert(((int32_t *)test_data)[i++] == SpriteHdrSize);
    assert(((int32_t *)test_data)[i++] == SpriteType);

    uint8_t *const bm = (void *)((int32_t *)test_data + i);
    for (int y = 0; y < SFMapTile_Height; ++y) {
      i += nwords;
      for (int x = 0; x < SFMapTile_Width; ++x) {
        bm[(y * (nwords * WORD_SIZE)) + x] = p++;
      }
    }
  }
}

static void check_planets_metadata_file(const char *const file_name)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  char test_data[MaxCSVSize];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_CSV) > 0);

  int x[2], y[2];
  int const n = sscanf(test_data, "%d,%d\n%d,%d\n", &x[0], &y[0], &x[1], &y[1]);
  assert(n == 4);

  assert(x[0] == PlanetPaintX0);
  assert(y[0] == PlanetPaintY0);
  assert(x[1] == PlanetPaintX1);
  assert(y[1] == PlanetPaintY1);
}

static void check_sky_metadata_file(const char *const file_name)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  char test_data[MaxCSVSize];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_CSV) > 0);

  int paint, stars;
  int const n = sscanf(test_data, "%d,%d\n", &paint, &stars);
  assert(n == 2);

  assert(paint == SkyPaintOffset);
  assert(stars == SkyStarsHeight);
}

static void check_sprites_metadata_file(const char *const file_name)
{
  assert(file_name != NULL);
  assert(*file_name != '\0');
  char test_data[MaxCSVSize];
  assert(check_uncompressed_file(file_name, test_data, sizeof(test_data), FileType_CSV) > 0);

  int animA[4], animB[4], triggerB[4];
  int const n = sscanf(test_data, "%d,%d,%d,%d\n%d,%d,%d,%d\n%d,%d,%d,%d\n",
                       &animA[0], &animA[1], &animA[2], &animA[3],
                       &animB[0], &animB[1], &animB[2], &animB[3],
                       &triggerB[0], &triggerB[1], &triggerB[2], &triggerB[3]);
  assert(n == 12);

  size_t i = 0;
  assert(animA[i++] == TileAnim0);
  assert(animA[i++] == TileAnim1);
  assert(animA[i++] == TileAnim2);
  assert(animA[i++] == TileAnim3);

  i = 0;
  assert(animB[i++] == TileBAnim0);
  assert(animB[i++] == TileBAnim1);
  assert(animB[i++] == TileBAnim2);
  assert(animB[i++] == TileBAnim3);

  i = 0;
  assert(triggerB[i++] == TileBTrig0);
  assert(triggerB[i++] == TileBTrig1);
  assert(triggerB[i++] == TileBTrig2);
  assert(triggerB[i++] == TileBTrig3);
}

static void init_id_block(IdBlock *block, ObjectId id, ComponentId component)
{
  _kernel_oserror *e;

  assert(block != NULL);

  block->self_id = id;
  block->self_component = component;
  e = toolbox_get_parent(0, id, &block->parent_id, &block->parent_component);
  assert(e == NULL);
  e = toolbox_get_ancestor(0, id, &block->ancestor_id, &block->ancestor_component);
  assert(e == NULL);
}

static bool path_is_in_userdata(char *filename)
{
  UserData *window;
  char buffer[1024];
  _kernel_swi_regs regs;

  regs.r[0] = FSControl_CanonicalisePath;
  regs.r[1] = (uintptr_t)filename;
  regs.r[2] = (uintptr_t)buffer;
  regs.r[3] = 0;
  regs.r[4] = 0;
  regs.r[5] = sizeof(buffer);
  assert_no_error(_kernel_swi(OS_FSControl, &regs, &regs));
  assert(regs.r[5] >= 0);

  window = userdata_find_by_file_name(buffer);
  return window != NULL;
}

static bool object_is_on_menu(ObjectId id)
{
  ObjectId it;
  assert(id != NULL_ObjectId);
  for (it = ViewsMenu_getfirst();
       it != NULL_ObjectId;
       it = ViewsMenu_getnext(it))
  {
    if (it == id)
      break;
  }
  return it == id;
}

static int fake_ref;

static void init_savetofile_event(WimpPollBlock *poll_block, unsigned int flags)
{
  SaveAsSaveToFileEvent * const sastfe = (SaveAsSaveToFileEvent *)&poll_block->words;

  sastfe->hdr.size = sizeof(*poll_block);
  sastfe->hdr.reference_number = ++fake_ref;
  sastfe->hdr.event_code = SaveAs_SaveToFile;
  sastfe->hdr.flags = flags;
  STRCPY_SAFE(sastfe->filename, TEST_DATA_OUT);
}

static void init_fillbuffer_event(WimpPollBlock *poll_block, unsigned int flags, int size, char *address, int no_bytes)
{
  SaveAsFillBufferEvent * const safbe = (SaveAsFillBufferEvent *)&poll_block->words;

  safbe->hdr.size = sizeof(*poll_block);
  safbe->hdr.reference_number = ++fake_ref;
  safbe->hdr.event_code = SaveAs_FillBuffer;
  safbe->hdr.flags = flags;
  safbe->size = size;
  safbe->address = address;
  safbe->no_bytes = no_bytes;
}

static void init_savecompleted_event(WimpPollBlock *poll_block, unsigned int flags)
{
  SaveAsSaveCompletedEvent * const sasce = (SaveAsSaveCompletedEvent *)&poll_block->words;

  sasce->hdr.size = sizeof(*poll_block);
  sasce->hdr.reference_number = ++fake_ref;
  sasce->hdr.event_code = SaveAs_SaveCompleted;
  sasce->hdr.flags = flags;
  sasce->wimp_message_no = 0; /* as though no drag took place */
  STRCPY_SAFE(sasce->filename, TEST_DATA_OUT);
}

static void init_radiobutton_event(WimpPollBlock *poll_block, ComponentId const old_on_button)
{
  RadioButtonStateChangedEvent * const rbsce = (RadioButtonStateChangedEvent *)&poll_block->words;

  rbsce->hdr.size = sizeof(*poll_block);
  rbsce->hdr.reference_number = ++fake_ref;
  rbsce->hdr.event_code = RadioButton_StateChanged;
  rbsce->hdr.flags = 0;
  rbsce->state = 1;
  rbsce->old_on_button = old_on_button;
}

static void init_actionbutton_event(WimpPollBlock *poll_block)
{
  ActionButtonSelectedEvent * const abse = (ActionButtonSelectedEvent *)&poll_block->words;

  abse->hdr.size = sizeof(*poll_block);
  abse->hdr.reference_number = ++fake_ref;
  abse->hdr.event_code = ActionButton_Selected;
  abse->hdr.flags = 0;
}

static void init_dialoguecompleted_event(WimpPollBlock *poll_block)
{
  SaveAsDialogueCompletedEvent * const sadce = (SaveAsDialogueCompletedEvent *)&poll_block->words;

  sadce->hdr.size = sizeof(*poll_block);
  sadce->hdr.reference_number = ++fake_ref;
  sadce->hdr.event_code = SaveAs_DialogueCompleted;
  sadce->hdr.flags = 0;
}

static void init_quit_cancel_event(WimpPollBlock *poll_block)
{
  QuitCancelEvent * const qce = (QuitCancelEvent *)&poll_block->words;

  qce->hdr.size = sizeof(*poll_block);
  qce->hdr.reference_number = ++fake_ref;
  qce->hdr.event_code = Quit_Cancel;
  qce->hdr.flags = 0;
}

static void init_quit_quit_event(WimpPollBlock *poll_block)
{
  QuitQuitEvent * const qce = (QuitQuitEvent *)&poll_block->words;

  qce->hdr.size = sizeof(*poll_block);
  qce->hdr.reference_number = ++fake_ref;
  qce->hdr.event_code = Quit_Quit;
  qce->hdr.flags = 0;
}

static void dispatch_event(int const event_code, WimpPollBlock *poll_block)
{
  Fortify_CheckAllMemory();

  DEBUGF("Test dispatches event %d", event_code);

  switch (event_code)
  {
    case Wimp_EToolboxEvent:
      DEBUGF(" (Toolbox event 0x%x)", ((ToolboxEvent *)poll_block)->hdr.event_code);
      break;

    case Wimp_EUserMessage:
    case Wimp_EUserMessageRecorded:
    case Wimp_EUserMessageAcknowledge:
      DEBUGF(" (action %d)", ((WimpMessage *)poll_block)->hdr.action_code);
      break;

    default:
      break;
  }
  DEBUGF("\n");

  assert_no_error(event_dispatch(event_code, poll_block));

  /* Deliver any outgoing broadcasts back to the sender */
  unsigned int count = pseudo_wimp_get_message_count();
  while (count-- > 0)
  {
    int msg_code, handle;
    WimpPollBlock msg_block;
    pseudo_wimp_get_message2(count, &msg_code, &msg_block, &handle, NULL);
    if (handle == 0)
    {
      assert_no_error(event_dispatch(msg_code, &msg_block));
    }
  }

  Fortify_CheckAllMemory();
}

static void dialogue_completed(ObjectId id)
{
  assert(id != NULL_ObjectId);
  WimpPollBlock poll_block;
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static int init_ram_transmit_msg(WimpPollBlock *poll_block, const WimpMessage *ram_fetch, const char *data, int nbytes)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data) + sizeof(WimpRAMTransmitMessage);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = ram_fetch->hdr.my_ref;
  poll_block->user_message.hdr.action_code = Wimp_MRAMTransmit;

  char * const buffer = ram_fetch->data.ram_fetch.buffer;
  assert(nbytes <= ram_fetch->data.ram_fetch.buffer_size);
  for (int i = 0; i < nbytes; ++i)
    buffer[i] = data[i];

  poll_block->user_message.data.ram_transmit.buffer = buffer;
  poll_block->user_message.data.ram_transmit.nbytes = nbytes;

  return poll_block->user_message.hdr.my_ref;
}

static int init_data_load_msg(WimpPollBlock *poll_block, char *filename, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_load.leaf_name) + WORD_ALIGN(strlen(filename)+1);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataLoad;

  poll_block->user_message.data.data_load.destination_window = pointer_info->window_handle;
  poll_block->user_message.data.data_load.destination_icon = pointer_info->icon_handle;
  poll_block->user_message.data.data_load.destination_x = pointer_info->x;
  poll_block->user_message.data.data_load.destination_y = pointer_info->y;
  poll_block->user_message.data.data_load.estimated_size = estimated_size;
  poll_block->user_message.data.data_load.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_load.leaf_name, filename);

  return poll_block->user_message.hdr.my_ref;
}

static void init_pointer_info_for_icon(WimpGetPointerInfoBlock *pointer_info)
{
  pointer_info->x = DestinationX;
  pointer_info->y = DestinationY;
  pointer_info->button_state = 0;
  pointer_info->window_handle = WimpWindow_Iconbar;
  assert_no_error(iconbar_get_icon_handle(0, pseudo_toolbox_find_by_template_name("Iconbar"), &pointer_info->icon_handle));
}

static int init_data_save_msg(WimpPollBlock *poll_block, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info, int your_ref)
{
  poll_block->user_message.hdr.size = offsetof(WimpMessage, data.data_save.leaf_name) + WORD_ALIGN(strlen(TEST_LEAFNAME)+1);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("my_ref %d\n", poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = your_ref;
  poll_block->user_message.hdr.action_code = Wimp_MDataSave;

  poll_block->user_message.data.data_save.destination_window = pointer_info->window_handle;
  poll_block->user_message.data.data_save.destination_icon = pointer_info->icon_handle;
  poll_block->user_message.data.data_save.destination_x = pointer_info->x;
  poll_block->user_message.data.data_save.destination_y = pointer_info->y;
  poll_block->user_message.data.data_save.estimated_size = estimated_size;
  poll_block->user_message.data.data_save.file_type = file_type;
  STRCPY_SAFE(poll_block->user_message.data.data_save.leaf_name, TEST_LEAFNAME);

  return poll_block->user_message.hdr.my_ref;
}

static int init_pre_quit_msg(WimpPollBlock *poll_block, bool desktop_shutdown, bool is_risc_os_3)
{
  poll_block->user_message.hdr.size = sizeof(poll_block->user_message.hdr) + (is_risc_os_3 ? sizeof(poll_block->user_message.data.words[0]) : 0);
  poll_block->user_message.hdr.sender = ForeignTaskHandle;
  poll_block->user_message.hdr.my_ref = ++fake_ref;
  DEBUGF("size %d my_ref %d\n", poll_block->user_message.hdr.size, poll_block->user_message.hdr.my_ref);
  poll_block->user_message.hdr.your_ref = 0;
  poll_block->user_message.hdr.action_code = Wimp_MPreQuit;
  if (is_risc_os_3)
    poll_block->user_message.data.words[0] = desktop_shutdown ? 0 : 1;
  else
    assert(desktop_shutdown);

  return poll_block->user_message.hdr.my_ref;
}

static bool check_data_load_ack_msg(int dl_ref, char *filename, int estimated_size, int file_type, const WimpGetPointerInfoBlock *pointer_info)
{
  /* A dataloadack message should have been sent in reply to the dataload */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataLoadAck))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == dl_ref);
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_load_ack.leaf_name) + WORD_ALIGN(strlen(filename)+1));
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.data.data_load_ack.destination_window == pointer_info->window_handle);

      assert(poll_block.user_message.data.data_load_ack.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_load_ack.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_load_ack.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_load_ack.estimated_size == estimated_size);
      assert(poll_block.user_message.data.data_load_ack.file_type == file_type);
      assert(!strcmp(poll_block.user_message.data.data_load_ack.leaf_name, filename));
      return true;
    }
  }
  return false;
}

static bool check_data_save_ack_msg(int ds_ref, WimpMessage *data_save_ack, const WimpGetPointerInfoBlock *pointer_info)
{
  /* A datasaveack message should have been sent in reply to the datasave */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;
    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    /* There may be an indeterminate delay between us sending DataSaveAck
       and other task responding with a DataLoad message. (Sending
       DataSaveAck as recorded delivery breaks the SaveAs module, for one. */
    if ((code == Wimp_EUserMessage) &&
        (poll_block.user_message.hdr.action_code == Wimp_MDataSaveAck))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == ds_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);

      const char * const filename = "<Wimp$Scrap>";
      assert(poll_block.user_message.hdr.size >= 0);
      assert((size_t)poll_block.user_message.hdr.size == offsetof(WimpMessage, data.data_save_ack.leaf_name) + WORD_ALIGN(strlen(filename)+1));
      assert(poll_block.user_message.data.data_save_ack.destination_window == pointer_info->window_handle);
      assert(poll_block.user_message.data.data_save_ack.destination_icon == pointer_info->icon_handle);
      assert(poll_block.user_message.data.data_save_ack.destination_x == pointer_info->x);
      assert(poll_block.user_message.data.data_save_ack.destination_y == pointer_info->y);
      assert(poll_block.user_message.data.data_save_ack.estimated_size == UnsafeDataSize);
      assert(!strcmp(poll_block.user_message.data.data_save_ack.leaf_name, filename));
      *data_save_ack = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static bool check_ram_fetch_msg(int rt_ref, WimpMessage *ram_fetch)
{
  /* A ramfetch message should have been sent in reply to a datasave or ramtransmit */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageRecorded) &&
        (poll_block.user_message.hdr.action_code == Wimp_MRAMFetch))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == rt_ref);
      assert(poll_block.user_message.hdr.sender == th);
      assert(poll_block.user_message.hdr.my_ref != 0);

      assert(poll_block.user_message.hdr.size == offsetof(WimpMessage, data.ram_fetch) + sizeof(poll_block.user_message.data.ram_fetch));
      assert(poll_block.user_message.data.ram_fetch.buffer != NULL);
      *ram_fetch = poll_block.user_message;
      return true;
    }
  }
  return false;
}

static void check_file_save_completed(ObjectId id, const _kernel_oserror *err)
{
  /* saveas_get_file_save_completed must have been called
     to indicate success or failure */
  unsigned int flags;
  char buffer[256];
  int nbytes;
  const ObjectId quoted_id = pseudo_saveas_get_file_save_completed(
                             &flags, buffer, sizeof(buffer), &nbytes);
  DEBUGF("object 0x%x\n", quoted_id);
  assert(id != NULL_ObjectId);
  assert(nbytes >= 0);
  assert((size_t)nbytes <= sizeof(buffer));
  assert(quoted_id == id);
  assert(!strcmp(buffer, TEST_DATA_OUT));
  if (flags != SaveAs_SuccessfulSave)
  {
    assert(flags == 0);
    assert(err != NULL);
  }
}

static bool check_pre_quit_ack_msg(int pq_ref, WimpMessage *pre_quit)
{
  /* A pre-quit message should have been acknowledged */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if ((code == Wimp_EUserMessageAcknowledge) &&
        (poll_block.user_message.hdr.action_code == Wimp_MPreQuit))
    {
      assert(handle == ForeignTaskHandle);

      assert(poll_block.user_message.hdr.your_ref == pq_ref);
      assert(poll_block.user_message.hdr.sender == pre_quit->hdr.sender);
      assert(poll_block.user_message.hdr.my_ref != 0);
      assert(poll_block.user_message.hdr.size == pre_quit->hdr.size);

      bool expect_shutdown = false, got_shutdown = false;
      assert(pre_quit->hdr.size >= 0);
      if ((size_t)pre_quit->hdr.size >= sizeof(pre_quit->hdr) + sizeof(pre_quit->data.words[0]))
        expect_shutdown = (pre_quit->data.words[0] == 0);

      assert(poll_block.user_message.hdr.size >= 0);
      if ((size_t)poll_block.user_message.hdr.size == sizeof(poll_block.user_message.hdr) + sizeof(poll_block.user_message.data.words[0]))
        got_shutdown = (poll_block.user_message.data.words[0] == 0);

      assert(expect_shutdown == got_shutdown);
      return true;
    }
  }
  return false;
}

static bool check_key_pressed_msg(int key_code)
{
  /* A Ctrl-Shift-F12 key press should have been sent to the originator
     of the pre-quit message */
  unsigned int count = pseudo_wimp_get_message_count();

  while (count-- > 0)
  {
    int code, handle;
    WimpPollBlock poll_block;

    pseudo_wimp_get_message2(count, &code, &poll_block, &handle, NULL);

    if (code == Wimp_EKeyPressed)
    {
      assert(handle == ForeignTaskHandle);
      assert(poll_block.key_pressed.key_code == key_code);

      WimpGetCaretPositionBlock caret;
#undef wimp_get_caret_position
      assert_no_error(wimp_get_caret_position(&caret));

      DEBUGF("Key press %d,%d,%d,%d caret %d,%d,%d,%d\n",
             poll_block.key_pressed.caret.window_handle,
             poll_block.key_pressed.caret.icon_handle,
             poll_block.key_pressed.caret.xoffset,
             poll_block.key_pressed.caret.yoffset,
             caret.window_handle,
             caret.icon_handle,
             caret.xoffset,
             caret.yoffset);

      assert(poll_block.key_pressed.caret.window_handle == caret.window_handle);
      if (poll_block.key_pressed.caret.window_handle != WorkArea)
      {
        assert(poll_block.key_pressed.caret.icon_handle == caret.icon_handle);
      }

      return true;
    }
  }
  return false;
}

static void load_persistent(int estimated_size, int file_type)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  int my_ref = 0;
  OS_File_CatalogueInfo cat;
  const _kernel_oserror *err;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, file_type, &drag_dest, 0);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId id;
    if (file_type == FileType_Sprite)
    {
      id = pseudo_toolbox_find_by_template_name("SprToSky");
      if (id == NULL_ObjectId) {
        id = pseudo_toolbox_find_by_template_name("SprToTex");
      }
      if (id == NULL_ObjectId) {
        id = pseudo_toolbox_find_by_template_name("SprToPla");
      }
    }
    else
    {
      id = pseudo_toolbox_find_by_template_name("ToSpr");
    }
    if (id != NULL_ObjectId) {
      dialogue_completed(id);
    }

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, file_type, &drag_dest);

  /* The receiver must not delete persistent files */
  assert_no_error(os_file_read_cat_no_path(TEST_DATA_IN, &cat));
  assert(cat.object_type == ObjectType_File);
}

static void dispatch_event_with_error_sim(int event_code, WimpPollBlock *poll_block, unsigned long limit, bool wait_for_idle)
{
  DEBUGF("Test sets allocation limit %lu\n", limit);
  Fortify_SetNumAllocationsLimit(limit);
  dispatch_event(event_code, poll_block);

  if (wait_for_idle)
  {
    assert_no_error(pseudo_event_wait_for_idle());
  }

  Fortify_SetNumAllocationsLimit(ULONG_MAX);
}

static void change_radiobutton(ObjectId win_id, ComponentId radio)
{
  WimpPollBlock poll_block;
  ComponentId old_on_button;

#undef radiobutton_get_state
  assert_no_error(radiobutton_get_state(0, win_id, radio, NULL, &old_on_button));
#undef radiobutton_set_state
  assert_no_error(radiobutton_set_state(0, win_id, radio, 1));

  init_radiobutton_event(&poll_block, old_on_button);
  init_id_block(pseudo_event_get_client_id_block(), win_id, radio);

  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void activate_savebox(ObjectId saveas_id, ComponentId radio, unsigned int flags, DataTransferMethod method)
{
  unsigned long limit;
  ObjectId win_id;

  /* The savebox should have been shown */
  assert(pseudo_toolbox_object_is_showing(saveas_id));
  assert_no_error(saveas_get_window_id(0, saveas_id, &win_id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err = NULL;

    /* Recording the new file path can allocate memory so no enter-scope here */
    DEBUGF("Test sets allocation limit %lu\n", limit);
    Fortify_SetNumAllocationsLimit(limit);

    if (radio != NULL_ComponentId)
    {
      err_suppress_errors();
      change_radiobutton(win_id, radio);
      err = err_dump_suppressed();
    }

    init_id_block(pseudo_event_get_client_id_block(), saveas_id, NULL_ComponentId);

    if (err == NULL)
    {
      DEBUGF("Activating savebox 0x%x\n", saveas_id);
      switch (method)
      {
        case DTM_RAM:
        case DTM_BadRAM:
        {
          assert(!(flags & SaveAs_DestinationSafe));
          /* Open a temporary file in which to store the received data. */
          FILE * const f = fopen(TEST_DATA_OUT, "wb");
          assert(f != NULL);
          int total_bytes = 0;

          /* Make sure we don't get all of the data on the first call */
          int size = 1;

          do
          {
            /* Testing RAM transfer, so fake a Fill Buffer event such as might be
               generated by the Toolbox upon receipt of a RAM fetch message. */
            char buffer[128 << 10];

            WimpPollBlock poll_block;
            init_fillbuffer_event(&poll_block, (flags & SaveAs_SelectionSaved) ? SaveAs_SelectionBeingSaved : 0, size, NULL, total_bytes);
            pseudo_saveas_reset_buffer_filled();
            err_suppress_errors();
            dispatch_event(Wimp_EToolboxEvent, &poll_block);
            err = err_dump_suppressed();

            unsigned int flags;
            int nbytes;
            const ObjectId quoted_id = pseudo_saveas_get_buffer_filled(
                                       &flags, buffer, sizeof(buffer), &nbytes);
            if (quoted_id != NULL_ObjectId)
            {
              total_bytes += nbytes;

              assert(nbytes <= size);
              assert(quoted_id == saveas_id);
              assert(flags == 0);

              const size_t n = fwrite(buffer, nbytes, 1, f);
              assert(n == 1);
              if ((method == DTM_BadRAM) || (nbytes < size))
                break; /* Finished */
            }
            else
            {
              /* If data was not sent then it must be because an error occurred. */
              assert(err != NULL);
              break;
            }

            size = sizeof(buffer);
          }
          while (1);

          fclose(f);
          break;
        }
        case DTM_File:
        case DTM_BadFile:
        {
          /* Testing file transfer, so fake a Save To File event such as might be
             generated by the Toolbox upon receipt of a DataSaveAck message. */
          pseudo_saveas_reset_file_save_completed();
          WimpPollBlock poll_block;
          init_savetofile_event(&poll_block, (flags & SaveAs_SelectionSaved) ? SaveAs_SelectionBeingSaved : 0);
          err_suppress_errors();
          dispatch_event(Wimp_EToolboxEvent, &poll_block);
          err = err_dump_suppressed();
          check_file_save_completed(saveas_id, err);
          break;
        }
        default:
        {
          DEBUGF("Method %d is not supported\n", method);
          break;
        }
      }
    }

    if ((err == NULL) && (method != DTM_BadFile) && (method != DTM_BadRAM))
    {
      /* Simulate the save completed event that the Toolbox would have
         delivered had we not intercepted saveas_file_save_completed. */
      err_suppress_errors();

      WimpPollBlock poll_block;
      init_savecompleted_event(&poll_block, flags);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      err = err_dump_suppressed();
    }

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static void test1(void)
{
  /* Load uncompressed planets file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_uncompressed_planets_file(TEST_DATA_IN, NPlanets, true);

  load_persistent(estimated_size, FileType_Sprite);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SprToPla");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test2(void)
{
  /* Load uncompressed sky file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_uncompressed_sky_file(TEST_DATA_IN, 1, true);

  load_persistent(estimated_size, FileType_Sprite);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SprToSky");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test3(void)
{
  /* Load uncompressed sprites file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_uncompressed_sprites_file(TEST_DATA_IN, NSprites, true);

  load_persistent(estimated_size, FileType_Sprite);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SprToTex");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test4(void)
{
  /* Load compressed planets file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_compressed_planets_file(TEST_DATA_IN, NPlanets, true);

  load_persistent(estimated_size, FileType_SFSkyPic);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("ToSpr");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test5(void)
{
  /* Load compressed sky file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_compressed_sky_file(TEST_DATA_IN, 1, true);

  load_persistent(estimated_size, FileType_SFSkyCol);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("ToSpr");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test6(void)
{
  /* Load compressed sprites file */
  WimpPollBlock poll_block;
  ObjectId id;
  const int estimated_size = make_compressed_sprites_file(TEST_DATA_IN, NSprites, true);

  load_persistent(estimated_size, FileType_SFMapGfx);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("ToSpr");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test7(void)
{
  /* Load directory */
  unsigned long limit;
  ObjectId id;
  WimpPollBlock poll_block;
  int my_ref = 0;

  /* Create directory */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;
    my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, &drag_dest, 0);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);
    pseudo_wimp_reset();

    dispatch_event(Wimp_EUserMessage, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    assert(fopen_num() == 0);

    err = err_dump_suppressed();
    if (err == NULL)
      break;

    /* The window may have been created even if an error occurred. */
    ObjectId const id = pseudo_toolbox_find_by_template_name("SaveDir");
    if (id != NULL_ObjectId) {
      dialogue_completed(id);
    }

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory, &drag_dest);

  /* A single savebox should have been created */
  id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void do_data_rec(int file_type, int (*make_file)(const char *filename, int n, bool metadata), char *template_name, DataTransferMethod method, int n, bool metadata, ComponentId radio)
{
  const int estimated_size = make_file(TEST_DATA_IN, n, metadata);
  WimpPollBlock poll_block;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);
  const int my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, estimated_size, file_type, &drag_dest, 0);
  ObjectId id;

  /* Load compressed file */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, estimated_size, file_type, &drag_dest);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name(template_name);
  assert(object_is_on_menu(id));

  activate_savebox(id, radio, 0, method);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void test8(void)
{
  /* Save compressed planets file with file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_planets_file, "SprToPla", DTM_File, NPlanets, true, NULL_ComponentId);
  check_compressed_planets_file(TEST_DATA_OUT, NPlanets);
}

static void test9(void)
{
  /* Save compressed sky file with file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_sky_file, "SprToSky", DTM_File, 1, true, NULL_ComponentId);
  check_compressed_sky_file(TEST_DATA_OUT);
}

static void test10(void)
{
  /* Save compressed sprites file with file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_sprites_file, "SprToTex", DTM_File, NSprites, true, NULL_ComponentId);
  check_compressed_sprites_file(TEST_DATA_OUT, NSprites);
}

static void test11(void)
{
  /* Save uncompressed planets file with file transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_File, NPlanets, true, ComponentId_SaveFile_Decompress_Radio);
  check_uncompressed_planets_file(TEST_DATA_OUT, NPlanets, true);
}

static void test12(void)
{
  /* Save uncompressed sky file with file transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_File, 1, true, ComponentId_SaveFile_Decompress_Radio);
  check_uncompressed_sky_file(TEST_DATA_OUT, 1, true);
}

static void test13(void)
{
  /* Save uncompressed sprites file with file transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_File, NSprites, true, ComponentId_SaveFile_Decompress_Radio);
  check_uncompressed_sprites_file(TEST_DATA_OUT, NSprites, true);
}

static void test14(void)
{
  /* Save directory */
  unsigned long limit;
  WimpPollBlock poll_block;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);
  const int my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, &drag_dest, 0);

  /* Create directory */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));

  /* Load directory */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory, &drag_dest);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  ObjectId const id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    init_savetofile_event(&poll_block, 0);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);

    err_suppress_errors();

    Fortify_EnterScope();
    Fortify_SetNumAllocationsLimit(limit);

    /* Activate the save dialogue */
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    const _kernel_oserror *err = err_dump_suppressed();
    check_file_save_completed(id, err);

    /* A scan dbox should have been created */
    const ObjectId scan_id = pseudo_toolbox_find_by_template_name("Scan");
    if (scan_id != NULL_ObjectId)
    {
      OS_File_CatalogueInfo cat;
      assert(object_is_on_menu(scan_id));
      assert(userdata_count_unsafe() == 1);

      /* An output directory should have been created */
      assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT, &cat));
      assert(cat.object_type == ObjectType_Directory);

      /* Abort the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Abort_ActButton);

      dispatch_event(Wimp_EToolboxEvent, &poll_block);
    }
    else
    {
      /* An error must have prevented creation of the scan */
      assert(err != NULL);
    }

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
    assert(userdata_count_unsafe() == 0);

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void batch_test(ComponentId radio)
{
  unsigned long limit;
  WimpPollBlock poll_block;

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);
  const int my_ref = init_data_load_msg(&poll_block, TEST_DATA_IN, -1, FileType_Directory, &drag_dest, 0);
  ObjectId id, win_id;

  /* Load directory */
  pseudo_wimp_reset();
  dispatch_event(Wimp_EUserMessage, &poll_block);

  check_data_load_ack_msg(my_ref, TEST_DATA_IN, -1, FileType_Directory, &drag_dest);

  /* A single savebox should have been created */
  assert(path_is_in_userdata(TEST_DATA_IN));
  assert(userdata_count_unsafe() == 0);
  id = pseudo_toolbox_find_by_template_name("SaveDir");
  assert(object_is_on_menu(id));

  assert_no_error(saveas_get_window_id(0, id, &win_id));
  change_radiobutton(win_id, radio);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    DEBUGF("Fortify limit %lu\n", limit);
    ObjectId scan_id;
    unsigned int i;
    OS_File_CatalogueInfo cat;
    const _kernel_oserror *err = NULL;

    Fortify_EnterScope();

    /* Activate the save dialogue */
    init_savetofile_event(&poll_block, 0);
    init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
    pseudo_saveas_reset_file_save_completed();
    dispatch_event(Wimp_EToolboxEvent, &poll_block);

    check_file_save_completed(id, NULL);

    /* A scan dbox should have been created */
    scan_id = pseudo_toolbox_find_by_template_name("Scan");
    assert(scan_id != NULL_ObjectId);
    assert(object_is_on_menu(scan_id));
    assert(userdata_count_unsafe() == 1);

    /* An output directory should have been created */
    assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT, &cat));
    assert(cat.object_type == ObjectType_Directory);

    Fortify_SetNumAllocationsLimit(limit);

    for (i = 0; i < 2 && err == NULL; ++i)
    {
      err_suppress_errors();

      /* Pause/unpause the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);

      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Pause_ActButton);

      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      err = err_dump_suppressed();
    }

    while (err == NULL && pseudo_toolbox_find_by_template_name("Scan") != NULL_ObjectId)
    {
      /* Deliver null events until the scan dbox completes or an error occurs */
      err_suppress_errors();
      dispatch_event(Wimp_ENull, NULL);
      err = err_dump_suppressed();
    }
    DEBUGF("Error or complete\n");

    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    /* The scan dbox may have deleted itself on error but always
       should have deleted itself if it completed */
    if (pseudo_toolbox_find_by_template_name("Scan") != NULL_ObjectId)
    {
      DEBUGF("Aborting scan dbox\n");
      assert(err != NULL);

      /* Abort the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(),
                    scan_id,
                    ComponentId_Scan_Abort_ActButton);

      /* Don't risk assigning err = NULL because something failed
         and we want to retry with a higher allocation limit. */
      dispatch_event(Wimp_EToolboxEvent, &poll_block);
    }

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
    assert(userdata_count_unsafe() == 0);

    if (err == NULL)
      break;
  }
  DEBUGF("Finished with limit %lu\n", limit);
  assert(limit != FortifyAllocationLimit);

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);
}

static void test15(void)
{
  /* Batch compress */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be compressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));

  make_uncompressed_planets_file(TEST_DATA_IN BATCH_PATH_PLANETS_TAIL, NPlanets, true);
  make_uncompressed_sky_file(TEST_DATA_IN BATCH_PATH_SKY_TAIL, 1, true);
  make_uncompressed_sprites_file(TEST_DATA_IN BATCH_PATH_SPRITES_TAIL, NSprites, true);

  make_compressed_planets_file(TEST_DATA_IN BATCH_PATH_IGNORE_TAIL, NPlanets, true);

  batch_test(ComponentId_SaveDir_Compress_Radio);

  check_compressed_planets_file(TEST_DATA_OUT BATCH_PATH_PLANETS_TAIL, NPlanets);
  check_compressed_sky_file(TEST_DATA_OUT BATCH_PATH_SKY_TAIL);
  check_compressed_sprites_file(TEST_DATA_OUT BATCH_PATH_SPRITES_TAIL, NSprites);

  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_IGNORE_TAIL, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test16(void)
{
  /* Batch decompress */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be decompressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));

  make_compressed_planets_file(TEST_DATA_IN BATCH_PATH_PLANETS_TAIL, NPlanets, true);
  make_compressed_sky_file(TEST_DATA_IN BATCH_PATH_SKY_TAIL, 1, true);
  make_compressed_sprites_file(TEST_DATA_IN BATCH_PATH_SPRITES_TAIL, NSprites, true);

  make_uncompressed_planets_file(TEST_DATA_IN BATCH_PATH_IGNORE_TAIL, NPlanets, true);

  batch_test(ComponentId_SaveDir_Decompress_Radio);

  check_uncompressed_planets_file(TEST_DATA_OUT BATCH_PATH_PLANETS_TAIL, NPlanets, true);
  check_uncompressed_sky_file(TEST_DATA_OUT BATCH_PATH_SKY_TAIL, 1, true);
  check_uncompressed_sprites_file(TEST_DATA_OUT BATCH_PATH_SPRITES_TAIL, NSprites, true);

  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_IGNORE_TAIL, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test17(void)
{
  /* Batch extract images */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be decompressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));

  make_compressed_planets_file(TEST_DATA_IN BATCH_PATH_PLANETS_TAIL, NPlanets, true);
  make_compressed_sky_file(TEST_DATA_IN BATCH_PATH_SKY_TAIL, 1, true);
  make_compressed_sprites_file(TEST_DATA_IN BATCH_PATH_SPRITES_TAIL, NSprites, true);

  make_uncompressed_planets_file(TEST_DATA_IN BATCH_PATH_IGNORE_TAIL, NPlanets, true);

  batch_test(ComponentId_SaveDir_Extract_Images_Radio);

  check_uncompressed_planets_file(TEST_DATA_OUT BATCH_PATH_PLANETS_TAIL, NPlanets, false);
  check_uncompressed_sky_file(TEST_DATA_OUT BATCH_PATH_SKY_TAIL, 1, false);
  check_uncompressed_sprites_file(TEST_DATA_OUT BATCH_PATH_SPRITES_TAIL, NSprites, false);

  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_IGNORE_TAIL, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test18(void)
{
  /* Batch extract metadata */
  OS_File_CatalogueInfo cat;

  /* Create directory and file to be decompressed */
  assert_no_error(os_file_create_dir(TEST_DATA_IN, OS_File_CreateDir_DefaultNoOfEntries));
  assert_no_error(os_file_create_dir(TEST_DATA_IN BATCH_PATH_SUBDIR, OS_File_CreateDir_DefaultNoOfEntries));

  make_compressed_planets_file(TEST_DATA_IN BATCH_PATH_PLANETS_TAIL, NPlanets, true);
  make_compressed_sky_file(TEST_DATA_IN BATCH_PATH_SKY_TAIL, 1, true);
  make_compressed_sprites_file(TEST_DATA_IN BATCH_PATH_SPRITES_TAIL, NSprites, true);

  make_uncompressed_planets_file(TEST_DATA_IN BATCH_PATH_IGNORE_TAIL, NPlanets, true);

  batch_test(ComponentId_SaveDir_Extract_Data_Radio);

  check_planets_metadata_file(TEST_DATA_OUT BATCH_PATH_PLANETS_TAIL);
  check_sky_metadata_file(TEST_DATA_OUT BATCH_PATH_SKY_TAIL);
  check_sprites_metadata_file(TEST_DATA_OUT BATCH_PATH_SPRITES_TAIL);

  assert_no_error(os_file_read_cat_no_path(TEST_DATA_OUT BATCH_PATH_IGNORE_TAIL, &cat));
  assert(cat.object_type == ObjectType_NotFound);
}

static void test19(void)
{
  /* Save uncompressed planets file with RAM transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_RAM, NPlanets, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_planets_file(TEST_DATA_OUT, NPlanets, true);
}

static void test20(void)
{
  /* Save uncompressed sky file with RAM transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_RAM, 1, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sky_file(TEST_DATA_OUT, 1, true);
}

static void test21(void)
{
  /* Save uncompressed sprites file with RAM transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_RAM, NSprites, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sprites_file(TEST_DATA_OUT, NSprites, true);
}

static void wait(void)
{
  const clock_t start_time = clock();
  clock_t elapsed;

  DEBUGF("Waiting %fs for stalled load operation(s) to be abandoned\n",
         (double)Timeout / CLOCKS_PER_SEC);
  _swix(Hourglass_On, 0);
  do
  {
    elapsed = clock() - start_time;
    _swix(Hourglass_Percentage, _IN(0), (elapsed * 100) / Timeout);
  }
  while (elapsed < Timeout);
  _swix(Hourglass_Off, 0);
}

static void cleanup_stalled(void)
{
  /* Wait for timeout then deliver a null event to clean up the failed load */
  unsigned long limit;
  const _kernel_oserror *err;

  wait();

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    err_suppress_errors();
    Fortify_SetNumAllocationsLimit(limit);

    dispatch_event(Wimp_ENull, NULL);

    Fortify_SetNumAllocationsLimit(ULONG_MAX);
    err = err_dump_suppressed();
    if (err == NULL)
      break;
  }

  Fortify_LeaveScope();
}

static const _kernel_oserror *send_data_core(int file_type, int estimated_size, const WimpGetPointerInfoBlock *pointer_info, DataTransferMethod method, int your_ref)
{
  const _kernel_oserror *err;
  WimpPollBlock poll_block;
  bool use_file = false;

  DEBUGF("send_data_core file_type=%d estimated_size=%d method=%d\n", file_type, estimated_size, method);
  if (method == DTM_None) return NULL;

  pseudo_wimp_reset();
  err_suppress_errors();
  assert_no_error(pseudo_event_wait_for_idle());

  /* Try to ensure that at least two RAMFetch messages are sent */
  int our_ref = init_data_save_msg(&poll_block,
    method == DTM_BadRAM ? estimated_size/2 : estimated_size,
    file_type, pointer_info, your_ref);

  dispatch_event(Wimp_EUserMessage, &poll_block);

  err = err_dump_suppressed();

  WimpMessage data_save_ack;
  if (check_data_save_ack_msg(our_ref, &data_save_ack, pointer_info))
  {
    DEBUGF("file_type 0x%x\n", data_save_ack.data.data_save_ack.file_type);
    assert(data_save_ack.data.data_save_ack.file_type == file_type);
    use_file = true;
  }
  else
  {
    WimpMessage ram_fetch;
    if (check_ram_fetch_msg(our_ref, &ram_fetch))
    {
      switch (method)
      {
        case DTM_RAM:
        case DTM_BadRAM:
        {
          /* Allowed to use RAM transfer. */
          char test_data[estimated_size];
          FILE * const f = fopen(TEST_DATA_IN, "rb");
          assert(f != NULL);
          size_t const n = fread(test_data, estimated_size, 1, f);
          assert(n == 1);
          fclose(f);

          int total_bytes = 0;
          do
          {
            /* Copy as much data into the receiver's buffer as will fit */
            const int buffer_size = ram_fetch.data.ram_fetch.buffer_size;
            assert(total_bytes <= estimated_size);
            const int nbytes = LOWEST(buffer_size, estimated_size - total_bytes);
            our_ref = init_ram_transmit_msg(&poll_block, &ram_fetch,
                                            test_data + total_bytes, nbytes);
            total_bytes += nbytes;

            pseudo_wimp_reset();
            err_suppress_errors();
            dispatch_event(Wimp_EUserMessage, &poll_block);
            err = err_dump_suppressed();

            /* Expect another RAMFetch message in reply only if we completely filled
               the receiver's buffer. */
            if (check_ram_fetch_msg(our_ref, &ram_fetch))
            {
              assert(nbytes == buffer_size);

              if (method == DTM_BadRAM)
              {
                /* Instead of sending another RAMTransmit message to complete the protocol,
                   fake the return of the RAMFetch message to the saver. */
                err_suppress_errors();
                poll_block.user_message_acknowledge = ram_fetch;
                dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
                err = err_dump_suppressed();
                break;
              }
            }
            else
            {
              /* An error must have occurred or the buffer was not filled (means EOF).  */
              assert((err != NULL) || (nbytes < buffer_size));
              break;
            }
          }
          while (1);
          break;
        }

        case DTM_File:
        case DTM_BadFile:
        {
          /* Not allowed to use RAM transfer, so fake the return of the RAMFetch
             message to the loader. */
          pseudo_wimp_reset();
          err_suppress_errors();
          poll_block.user_message_acknowledge = ram_fetch;
          dispatch_event(Wimp_EUserMessageAcknowledge, &poll_block);
          err = err_dump_suppressed();

          /* Expect the loader to retry with a DataSaveAck in response to
             the original DataSave message. */
          if (check_data_save_ack_msg(our_ref, &data_save_ack, pointer_info))
          {
            assert(data_save_ack.data.data_save_ack.file_type == file_type);
            use_file = true;
          }
          else
          {
            /* No reply to the data save message so an error must have occurred */
            assert(err != NULL);
          }
          break;
        }

        default:
        {
          DEBUGF("Method %d is not supported\n", method);
          break;
        }
      }
    }
    else
    {
      /* No reply to the data save message so an error must have occurred */
      assert(err != NULL);
    }
  }

  if (use_file)
  {
    /* We can reach this point with any method because file transfer is the fallback */
    if (method == DTM_BadFile)
    {
      /* There can be an indefinite period between a DataSaveAck and DataLoad message
         so the loader should give up after a while. */
      cleanup_stalled();
    }
    else
    {
      /* Save the data and then reply with a DataLoad message */
      wipe("<Wimp$Scrap>");
      copy(TEST_DATA_IN, "<Wimp$Scrap>");

      const int dataload_ref = init_data_load_msg(&poll_block, "<Wimp$Scrap>", estimated_size, file_type, pointer_info, data_save_ack.hdr.my_ref);

      pseudo_wimp_reset();
      err_suppress_errors();
      dispatch_event(Wimp_EUserMessage, &poll_block);
      err = err_dump_suppressed();

      if (check_data_load_ack_msg(dataload_ref, "<Wimp$Scrap>", estimated_size, file_type, pointer_info))
      {
        /* It's the receiver's responsibility to delete the temporary file */
        assert(fopen("<Wimp$Scrap>", "rb") == NULL);
      }
      /* The recipient doesn't know that the data is safe because it
         didn't load a persistent file. */
      assert(!path_is_in_userdata("<Wimp$Scrap>"));
    }
    /* else do nothing because DataSaveAck messages are not recorded */
  }

  assert(fopen_num() == 0);

  return err;
}

static void test22(void)
{
  /* Uncompressed file from app with incomplete file transfer */
  unsigned long limit;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;

    Fortify_EnterScope();

    Fortify_SetNumAllocationsLimit(limit);
    err = send_data_core(FileType_Sprite, TestDataSize, &drag_dest, DTM_BadFile, 0);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    Fortify_LeaveScope();

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static void test23(void)
{
  /* Compressed file from app with incomplete file transfer */
  unsigned long limit;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;

    Fortify_EnterScope();

    Fortify_SetNumAllocationsLimit(limit);
    err = send_data_core(FileType_SFSkyPic, TestDataSize, &drag_dest, DTM_BadFile, 0);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    Fortify_LeaveScope();

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static void test24(void)
{
  /* Transfer dir from app */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  init_data_save_msg(&poll_block, 0, FileType_Directory, &drag_dest, 0);

  err_suppress_errors();

  Fortify_EnterScope();
  pseudo_wimp_reset();

  dispatch_event(Wimp_EUserMessage, &poll_block);

  Fortify_LeaveScope();

  err = err_dump_suppressed();
  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("AppDir")));
  assert(pseudo_wimp_get_message_count() == 0);
}

static void test25(void)
{
  /* Transfer app from app */
  const _kernel_oserror *err;
  WimpPollBlock poll_block;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  init_data_save_msg(&poll_block, 0, FileType_Application, &drag_dest, 0);

  err_suppress_errors();
  Fortify_EnterScope();
  pseudo_wimp_reset();

  dispatch_event(Wimp_EUserMessage, &poll_block);

  Fortify_LeaveScope();

  err = err_dump_suppressed();
  assert(err != NULL);
  assert(err->errnum == DUMMY_ERRNO);
  assert(!strcmp(err->errmess, msgs_lookup("AppDir")));
  assert(pseudo_wimp_get_message_count() == 0);
}

static void do_data_transfer(int file_type, int (*make_file)(const char *filename, int n, bool metadata), char *template_name, DataTransferMethod method, int n, bool metadata)
{
  WimpPollBlock poll_block;
  unsigned long limit;
  const _kernel_oserror *err;
  UserData *savebox;
  ObjectId id;
  const int estimated_size = make_file(TEST_DATA_IN, n, metadata);

  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_EnterScope();

    Fortify_SetNumAllocationsLimit(limit);
    err = send_data_core(file_type, estimated_size, &drag_dest, method, 0);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (err == NULL)
      break;

    Fortify_LeaveScope();
  }
  assert(limit != FortifyAllocationLimit);

  /* A single savebox should have been created */
  assert(!path_is_in_userdata("<Wimp$Scrap>"));
  assert(userdata_count_unsafe() == 0);
  savebox = userdata_find_by_file_name("");
  assert(savebox != NULL);
  id = pseudo_toolbox_find_by_template_name(template_name);
  assert(object_is_on_menu(id));

  /* Complete the save dialogue */
  init_dialoguecompleted_event(&poll_block);
  init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
  dispatch_event(Wimp_EToolboxEvent, &poll_block);

  Fortify_LeaveScope();
}

static void test26(void)
{
  /* Uncompressed file from app with RAM transfer */
  do_data_transfer(FileType_Sprite, make_uncompressed_planets_file, "SprToPla", DTM_RAM, NPlanets, true);
}

static void test27(void)
{
  /* Compressed file from app with RAM transfer */
  do_data_transfer(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_RAM, NPlanets, true);
}

static void test28(void)
{
  /* Uncompressed file from app */
  do_data_transfer(FileType_Sprite, make_uncompressed_planets_file, "SprToPla", DTM_File, NPlanets, true);
}

static void test29(void)
{
  /* Uncompressed file from app with incomplete RAM transfer */
  unsigned long limit;
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);
  const int estimated_size = make_uncompressed_planets_file(TEST_DATA_IN, NPlanets, true);

  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    const _kernel_oserror *err;

    Fortify_EnterScope();

    Fortify_SetNumAllocationsLimit(limit);
    err = send_data_core(FileType_Sprite, estimated_size, &drag_dest, DTM_BadRAM, 0);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    Fortify_LeaveScope();

    if (err == NULL)
      break;
  }
  assert(limit != FortifyAllocationLimit);
}

static void test30(void)
{
  /* Save uncompressed planets file with incomplete RAM transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_RAM, NPlanets, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_planets_file(TEST_DATA_OUT, NPlanets, true);
}

static void test31(void)
{
  /* Save uncompressed sky file with incomplete RAM transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_RAM, 1, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sky_file(TEST_DATA_OUT, 1, true);
}

static void test32(void)
{
  /* Save uncompressed sprites file with incomplete RAM transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_RAM, NSprites, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sprites_file(TEST_DATA_OUT, NSprites, true);
}

static void test33(void)
{
  /* Save uncompressed planets file with incomplete file transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_RAM, NPlanets, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_planets_file(TEST_DATA_OUT, NPlanets, true);
}

static void test34(void)
{
  /* Save uncompressed sky file with incomplete file transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_RAM, 1, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sky_file(TEST_DATA_OUT, 1, true);
}

static void test35(void)
{
  /* Save uncompressed sprites file with incomplete file transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_RAM, NSprites, true, ComponentId_SaveFile_Decompress_Radio);
  assert_no_error(os_file_set_type(TEST_DATA_OUT, FileType_Sprite));
  check_uncompressed_sprites_file(TEST_DATA_OUT, NSprites, true);
}

static void test36(void)
{
  /* Save compressed planets file with incomplete file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_planets_file, "SprToPla", DTM_BadFile, NPlanets, true, NULL_ComponentId);
  check_compressed_planets_file(TEST_DATA_OUT, NPlanets);
}

static void test37(void)
{
  /* Save compressed sky file with incomplete file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_sky_file, "SprToSky", DTM_BadFile, 1, true, NULL_ComponentId);
  check_compressed_sky_file(TEST_DATA_OUT);
}

static void test38(void)
{
  /* Save compressed sprites file with incomplete file transfer */
  do_data_rec(FileType_Sprite, make_uncompressed_sprites_file, "SprToTex", DTM_BadFile, NSprites, true, NULL_ComponentId);
  check_compressed_sprites_file(TEST_DATA_OUT, NSprites);
}

static void test39(void)
{
  /* Save uncompressed planets images with file transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_File, NPlanets, true, ComponentId_SaveFile_Extract_Images_Radio);
  check_uncompressed_planets_file(TEST_DATA_OUT, NPlanets, false);
}

static void test40(void)
{
  /* Save uncompressed sky images with file transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_File, 1, true, ComponentId_SaveFile_Extract_Images_Radio);
  check_uncompressed_sky_file(TEST_DATA_OUT, 1, false);
}

static void test41(void)
{
  /* Save uncompressed sprites images with file transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_File, NSprites, true, ComponentId_SaveFile_Extract_Images_Radio);
  check_uncompressed_sprites_file(TEST_DATA_OUT, NSprites, false);
}

static void test42(void)
{
  /* Save uncompressed planets metadata with file transfer */
  do_data_rec(FileType_SFSkyPic, make_compressed_planets_file, "ToSpr", DTM_File, NPlanets, true, ComponentId_SaveFile_Extract_Data_Radio);
  check_planets_metadata_file(TEST_DATA_OUT);
}

static void test43(void)
{
  /* Save uncompressed sky metadata with file transfer */
  do_data_rec(FileType_SFSkyCol, make_compressed_sky_file, "ToSpr", DTM_File, 1, true, ComponentId_SaveFile_Extract_Data_Radio);
  check_sky_metadata_file(TEST_DATA_OUT);
}

static void test44(void)
{
  /* Save uncompressed sprites metadata with file transfer */
  do_data_rec(FileType_SFMapGfx, make_compressed_sprites_file, "ToSpr", DTM_File, NSprites, true, ComponentId_SaveFile_Extract_Data_Radio);
  check_sprites_metadata_file(TEST_DATA_OUT);
}

static void quit_with_cancel_core(bool desktop_shutdown, bool is_risc_os_3)
{
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  const ObjectId prequit_id = pseudo_toolbox_find_by_template_name("PreQuit");
  for (unsigned int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    WimpPollBlock poll_block;
    const _kernel_oserror *err;
    unsigned long limit;
    int prequit_ref = 0;

    pseudo_toolbox_reset();
    Fortify_EnterScope();

    for (unsigned int w = 0; w < nwin; ++w)
    {
      /* Load directory */
      char dir_name[256];
      snprintf(dir_name, sizeof(dir_name), "%s%u", TEST_DATA_IN, w);
      assert_no_error(os_file_create_dir(dir_name, OS_File_CreateDir_DefaultNoOfEntries));
      init_data_load_msg(&poll_block, dir_name, -1, FileType_Directory, &drag_dest, 0);
      dispatch_event(Wimp_EUserMessage, &poll_block);

      const ObjectId id = pseudo_toolbox_find_by_template_name("SaveDir");
      assert(userdata_count_unsafe() == w);

      /* Activate the save dialogue */
      init_savetofile_event(&poll_block, 0);
      init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      /* Complete the save dialogue */
      init_dialoguecompleted_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      assert(userdata_count_unsafe() == w+1);
    }

    assert(!pseudo_toolbox_object_is_showing(prequit_id));

    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      err_suppress_errors();
      pseudo_wimp_reset();
      Fortify_EnterScope();

      /* Try to quit the application */
      prequit_ref = init_pre_quit_msg(&poll_block, desktop_shutdown, is_risc_os_3);
      dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit, true /* wait for about-to-be-shown */);

      Fortify_LeaveScope();
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    if (nwin)
    {
      /* Pre-quit dialogue should have been shown
         and the pre-quit message should have been acknowledged. */
      assert(pseudo_toolbox_object_is_showing(prequit_id));
      assert(check_pre_quit_ack_msg(prequit_ref, &poll_block.user_message));

      for (limit = 0; limit < FortifyAllocationLimit; ++limit)
      {
        err_suppress_errors();
        Fortify_EnterScope();

        /* Choose 'cancel' in the Pre-quit dialogue */
        init_quit_cancel_event(&poll_block);
        init_id_block(pseudo_event_get_client_id_block(), prequit_id, 0x82a901);
        dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit, true);

        Fortify_LeaveScope();
        err = err_dump_suppressed();
        if (err == NULL)
          break;
      }
      assert(limit != FortifyAllocationLimit);
    }
    else
    {
      /* Pre-quit dialogue should not have been shown
         and the quit message should have been ignored. */
      assert(!pseudo_toolbox_object_is_showing(prequit_id));
      assert(pseudo_wimp_get_message_count() == 0);
    }

    /* Close the batch processing windows created earlier */
    for (unsigned int w = 0; w < nwin; ++w)
    {
      const ObjectId id = pseudo_toolbox_find_by_template_name("Scan");
      assert(pseudo_toolbox_object_is_showing(id));
      assert(userdata_count_unsafe() == nwin - w);

      /* Abort the scan by simulating a button activation */
      init_actionbutton_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(), id,
                    ComponentId_Scan_Abort_ActButton);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);
    }

    Fortify_LeaveScope();
  }
}

static void test45(void)
{
  /* Quit from task manager with cancel */
  quit_with_cancel_core(false, true /* must be OS 3 to do single task quit */);
}

static void test46(void)
{
  /* Shutdown from task manager with cancel */
  quit_with_cancel_core(true, false);
  quit_with_cancel_core(true, true);
}

static void quit_with_confirm_core(bool desktop_shutdown, bool is_risc_os_3)
{
  WimpGetPointerInfoBlock drag_dest;
  init_pointer_info_for_icon(&drag_dest);

  const ObjectId prequit_id = pseudo_toolbox_find_by_template_name("PreQuit");
  for (unsigned int nwin = 0; nwin <= MaxNumWindows; ++nwin)
  {
    WimpPollBlock poll_block;
    const _kernel_oserror *err;
    unsigned long limit;
    int prequit_ref = 0;

    pseudo_toolbox_reset();
    Fortify_EnterScope();

    for (unsigned int w = 0; w < nwin; ++w)
    {
      /* Load directory */
      char dir_name[256];
      snprintf(dir_name, sizeof(dir_name), "%s%u", TEST_DATA_IN, w);
      assert_no_error(os_file_create_dir(dir_name, OS_File_CreateDir_DefaultNoOfEntries));
      init_data_load_msg(&poll_block, dir_name, -1, FileType_Directory, &drag_dest, 0);
      dispatch_event(Wimp_EUserMessage, &poll_block);

      const ObjectId id = pseudo_toolbox_find_by_template_name("SaveDir");
      assert(userdata_count_unsafe() == w);

      /* Activate the save dialogue */
      init_savetofile_event(&poll_block, 0);
      init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      /* Complete the save dialogue */
      init_dialoguecompleted_event(&poll_block);
      init_id_block(pseudo_event_get_client_id_block(), id, NULL_ComponentId);
      dispatch_event(Wimp_EToolboxEvent, &poll_block);

      assert(userdata_count_unsafe() == w+1);
    }

    assert(!pseudo_toolbox_object_is_showing(prequit_id));

    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      err_suppress_errors();
      pseudo_wimp_reset();
      Fortify_EnterScope();

      /* Try to quit the application */
      prequit_ref = init_pre_quit_msg(&poll_block, desktop_shutdown, is_risc_os_3);
      dispatch_event_with_error_sim(Wimp_EUserMessage, &poll_block, limit, true /* wait for about-to-be-shown */);

      Fortify_LeaveScope();
      err = err_dump_suppressed();
      if (err == NULL)
        break;
    }
    assert(limit != FortifyAllocationLimit);

    if (nwin)
    {
      jmp_buf exit_target;

      /* Pre-quit dialogue should have been shown
         and the pre-quit message should have been acknowledged. */
      assert(pseudo_toolbox_object_is_showing(prequit_id));
      assert(check_pre_quit_ack_msg(prequit_ref, &poll_block.user_message));

      for (limit = 0; limit < FortifyAllocationLimit; ++limit)
      {
        err_suppress_errors();
        Fortify_EnterScope();

        int status = setjmp(exit_target);
        if (status == 0)
        {
          /* Jump target has been set up */
          pseudo_exit_set_target(exit_target);

          /* Choose 'Quit' in the Pre-quit dialogue */
          init_quit_quit_event(&poll_block);
          init_id_block(pseudo_event_get_client_id_block(), prequit_id, 0x82a902);
          dispatch_event_with_error_sim(Wimp_EToolboxEvent, &poll_block, limit, true);

          err = err_dump_suppressed();

          /* In the case of desktop shutdown we expect a keypress to restart the
             shutdown to have been sent, instead of exiting. Otherwise the only
             valid reason for not exiting is an error. */
          assert(desktop_shutdown || err != NULL);
        }
        else
        {
          /* The exit function returned via setjmp */
          Fortify_SetNumAllocationsLimit(ULONG_MAX);

          assert(!desktop_shutdown);
          status--; /* 0 has a special meaning */
          assert(status == EXIT_SUCCESS);
          err = err_dump_suppressed();
        }

        Fortify_LeaveScope();
        if (err == NULL)
          break;
      }
      assert(limit != FortifyAllocationLimit);

      if (desktop_shutdown)
        check_key_pressed_msg(0x1FC);
    }
    else
    {
      /* Pre-quit dialogue should not have been shown
         and the quit message should have been ignored. */
      assert(!pseudo_toolbox_object_is_showing(prequit_id));
      assert(pseudo_wimp_get_message_count() == 0);
    }

    /* The batch processing windows created earlier should have been closed */
    assert(userdata_count_unsafe() == 0);

    Fortify_LeaveScope();
  }
}

static void test47(void)
{
  /* Quit from task manager with confirm */
  quit_with_confirm_core(false, true /* must be OS 3 to do single task quit */);
}

static void test48(void)
{
  /* Shutdown from task manager with confirm */
  quit_with_confirm_core(true, false);
  quit_with_confirm_core(true, true);
}

static bool fortify_detected = false;

static void fortify_check(void)
{
  Fortify_CheckAllMemory();
  assert(!fortify_detected);
}

static void fortify_output(char const *text)
{
  DEBUGF("%s", text);
  if (strstr(text, "Fortify"))
  {
    assert(!fortify_detected);
  }
  if (strstr(text, "detected"))
  {
    fortify_detected = true;
  }
}

int main(int argc, char *argv[])
{
  NOT_USED(argc);
  NOT_USED(argv);

  DEBUG_SET_OUTPUT(DebugOutput_FlushedFile, "SFtoSprLog");
  Fortify_SetOutputFunc(fortify_output);
  atexit(fortify_check);

  static const struct
  {
    const char *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Load uncompressed planets file", test1 },
    { "Load uncompressed sky file", test2 },
    { "Load uncompressed sprites file", test3 },

    { "Load compressed planets file", test4 },
    { "Load compressed sky file", test5 },
    { "Load compressed sprites file", test6 },

    { "Load directory", test7 },

    { "Save compressed planets file with file transfer", test8 },
    { "Save compressed sky file with file transfer", test9 },
    { "Save compressed sprites file with file transfer", test10 },

    { "Save uncompressed planets file with file transfer", test11 },
    { "Save uncompressed sky file with file transfer", test12 },
    { "Save uncompressed sprites file with file transfer", test13 },

    { "Save directory", test14 },

    { "Batch compress", test15 },
    { "Batch decompress", test16 },
    { "Batch extract images", test17 },
    { "Batch extract metadata", test18 },

    { "Save uncompressed planets file with RAM transfer", test19 },
    { "Save uncompressed sky file with RAM transfer", test20 },
    { "Save uncompressed sprites file with RAM transfer", test21 },

    { "Uncompressed file from app with incomplete file transfer", test22 },
    { "Compressed file from app with incomplete file transfer", test23 },
    { "Transfer dir from app", test24 },
    { "Transfer app from app", test25 },
    { "Uncompressed file from app with RAM transfer", test26 },
    { "Compressed file from app with RAM transfer", test27 },
    { "Uncompressed file from app", test28 },
    { "Uncompressed file from app with incomplete RAM transfer", test29 },

    { "Save uncompressed planets file with incomplete RAM transfer", test30 },
    { "Save uncompressed sky file with incomplete RAM transfer", test31 },
    { "Save uncompressed sprites file with incomplete RAM transfer", test32 },

    { "Save uncompressed planets file with incomplete file transfer", test33 },
    { "Save uncompressed sky file with incomplete file transfer", test34 },
    { "Save uncompressed sprites file with incomplete file transfer", test35 },

    { "Save compressed planets file with incomplete file transfer", test36 },
    { "Save compressed sky file with file incomplete transfer", test37 },
    { "Save compressed sprites file with incomplete file transfer", test38 },

    { "Save uncompressed planets images with file transfer", test39 },
    { "Save uncompressed sky images with file transfer", test40 },
    { "Save uncompressed sprites images with file transfer", test41 },

    { "Save uncompressed planets metadata with file transfer", test42 },
    { "Save uncompressed sky metadata with file transfer", test43 },
    { "Save uncompressed sprites metadata with file transfer", test44 },

    { "Quit from task manager with cancel", test45 },
    { "Shutdown from task manager with cancel", test46 },
    { "Quit from task manager with confirm", test47 },
    { "Shutdown from task manager with confirm", test48 }
  };

  initialise();

  /* This isn't ideal but it's better for replies to fake messages to be sent
     to our task rather than to an invalid handle or another task. */
  _kernel_swi_regs regs;
  assert_no_error(toolbox_get_sys_info( Toolbox_GetSysInfo_TaskHandle, &regs));
  th = regs.r[0];

  assert_no_error(pseudo_event_wait_for_idle());

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); count ++)
  {
    DEBUGF("Test %zu/%zu : %s\n",
           1 + count,
           ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);

    wipe(TEST_DATA_DIR);
    assert_no_error(os_file_create_dir(TEST_DATA_DIR, OS_File_CreateDir_DefaultNoOfEntries));

    Fortify_EnterScope();

    unit_tests[count].test_func();

    Fortify_LeaveScope();
    assert(fopen_num() == 0);
  }

  wipe(TEST_DATA_DIR);
  Fortify_OutputStatistics();
  return EXIT_SUCCESS;
}
