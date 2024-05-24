/*
 *  FednetCmp - Fednet file compression/decompression
 *  Compressed file savebox
 *  Copyright (C) 2014 Christopher Bazley
 */

#ifndef FNCSaveComp_h
#define FNCSaveComp_h

#include <stdbool.h>
#include "FNCSaveBox.h"
#include "Reader.h"

FNCSaveBox *SaveComp_create(char const *filename,
                                   bool data_saved,
                                   Reader *reader,
                                   int estimated_size,
                                   int x,
                                   FNCSaveBoxDeletedFn *deleted_cb);

#endif
