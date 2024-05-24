/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colours file format (includes relative positions)
 *  Copyright (C) 2006 Christopher Bazley
 */

#ifndef ExpColFile_h
#define ExpColFile_h

#include <stdbool.h>
#include "Reader.h"
#include "Writer.h"

typedef struct
{
  int num_cols;
  void *records; /* flex anchor */
}
ExpColFile;

typedef enum
{
  ExpColFileState_OK,
  ExpColFileState_ReadFail,
  ExpColFileState_UnknownVersion,
  ExpColFileState_BadLen,
  ExpColFileState_BadCol,
  ExpColFileState_BadNumCols,
  ExpColFileState_NoMem,
  ExpColFileState_BadTag
}
ExpColFileState;

bool ExpColFile_init(ExpColFile *file, int num_cols);
void ExpColFile_destroy(ExpColFile *file);

int ExpColFile_get_colour(ExpColFile const *file, int index,
  int *x_offset, int *y_offset);

bool ExpColFile_set_colour(ExpColFile *file, int index,
  int x_offset, int y_offset, int colour);

int ExpColFile_get_size(ExpColFile const *file);
ExpColFileState ExpColFile_read(ExpColFile *file, Reader *reader);

int ExpColFile_estimate(int num_cols);
void ExpColFile_write(ExpColFile const *file, Writer *writer);

int ExpColFile_estimate_CSV(int num_cols);
void ExpColFile_write_CSV(ExpColFile const *file, Writer *writer);

#endif
