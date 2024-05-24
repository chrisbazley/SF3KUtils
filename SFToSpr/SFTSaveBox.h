/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Save dialogue box superclass
 *  Copyright (C) 2017 Christopher Bazley
 */

#ifndef SFTSaveBox_h
#define SFTSaveBox_h

#include <stdbool.h>
#include "toolbox.h"

#include "UserData.h"

struct SFTSaveBox;

typedef void SFTSaveBoxDeletedFn(struct SFTSaveBox *savebox);

typedef struct SFTSaveBox
{
  UserData             super;
  ObjectId             saveas_id;
  ObjectId             window_id;
  SFTSaveBoxDeletedFn *deleted_cb;
}
SFTSaveBox;

SFTSaveBox *SFTSaveBox_initialise(SFTSaveBox *savebox, char const *input_path, bool data_saved, int file_type, char *template_name, const char *menu_token, int x, SFTSaveBoxDeletedFn *deleted_cb);
void SFTSaveBox_finalise(SFTSaveBox *savebox);
void SFTSaveBox_destroy(SFTSaveBox *savebox);
void SFTSaveBox_show(const SFTSaveBox *savebox);

#endif
