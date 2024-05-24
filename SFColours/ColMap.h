/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colour map file back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 */

#ifndef SFCBackend_h
#define SFCBackend_h

#include "Reader.h"
#include "Writer.h"

enum
{
  ColMap_MaxSize = 320
};

typedef struct
{
  int size;
  unsigned char map[ColMap_MaxSize];
}
ColMap;

void colmap_init(ColMap *colmap, int size);
int colmap_get_colour(ColMap const *colmap, int pos);
void colmap_set_colour(ColMap *colmap, int pos, int colour);

int colmap_get_size(ColMap const *file);

typedef enum {
  ColMapState_OK,
  ColMapState_ReadFail,
  ColMapState_BadLen,
} ColMapState;

ColMapState colmap_read_file(ColMap *colmap, Reader *reader);
void colmap_write_file(ColMap const *colmap, Writer *writer);

#endif
