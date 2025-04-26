/*
 *  FednetCmp - Fednet file compression/decompression
 *  Save dialogue box superclass
 *  Copyright (C) 2014 Christopher Bazley
 */

#ifndef FNCSaveBox_h
#define FNCSaveBox_h

#include "toolbox.h"

#include "UserData.h"

struct FNCSaveBox;

typedef void FNCSaveBoxDeletedFn(struct FNCSaveBox *savebox);

typedef struct FNCSaveBox
{
  UserData             super;
  ObjectId             saveas_id;
  ObjectId             window_id;
  FNCSaveBoxDeletedFn *deleted_cb;
}
FNCSaveBox;

_Optional FNCSaveBox *FNCSaveBox_initialise(FNCSaveBox *savebox,
  char const *input_path, bool data_saved, int file_type,
  char *template_name, char const *menu_token, int x,
  FNCSaveBoxDeletedFn *deleted_cb);

void FNCSaveBox_finalise(FNCSaveBox *savebox);
void FNCSaveBox_destroy(_Optional FNCSaveBox *savebox);
void FNCSaveBox_show(const FNCSaveBox *savebox);

#endif
