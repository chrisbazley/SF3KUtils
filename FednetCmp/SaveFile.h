/*
 *  FednetCmp - Fednet file compression/decompression
 *  Single file savebox
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCSaveFile_h
#define FNCSaveFile_h

#include <stdbool.h>
#include "FNCSaveBox.h"
#include "Reader.h"

FNCSaveBox *SaveFile_create(char const *filename,
                                   bool data_saved,
                                   Reader *reader,
                                   int estimated_size,
                                   int x,
                                   FNCSaveBoxDeletedFn *deleted_cb);

#endif
