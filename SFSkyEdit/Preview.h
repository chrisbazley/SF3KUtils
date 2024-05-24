/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky preview window
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSPreview_h
#define SFSPreview_h

#include <stdbool.h>

#include "toolbox.h"
#include "flex.h"

#include "EditWin.h"

typedef struct PreviewData PreviewData;

void Preview_initialise(void);
PreviewData *Preview_create(SkyFile *file, char const *title);
void Preview_destroy(PreviewData *preview_data);
void Preview_show(PreviewData *preview_data, ObjectId parent_id);
void Preview_set_title(PreviewData *preview_data, char const *title);
void Preview_update(PreviewData *preview_data);
void Preview_destroy(PreviewData *preview_data);
bool Preview_get_toolbars(const PreviewData *prev_data);
void Preview_set_scale(PreviewData *preview_data, int scale);
int Preview_get_scale(const PreviewData *preview_data);
flex_ptr Preview_get_anchor(PreviewData *preview_data);

#endif
