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

_Optional FNCSaveBox *SaveComp_create(char const *filename,
                                   bool data_saved,
                                   Reader *reader,
                                   int estimated_size,
                                   int x,
                                   _Optional FNCSaveBoxDeletedFn *deleted_cb);

#endif
