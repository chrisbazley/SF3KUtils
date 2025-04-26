/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Plotting and layout of editing window
 *  Copyright (C) 2018 Christopher Bazley
 */

#ifndef SFSLayout_h
#define SFSLayout_h

#include <stdbool.h>
#include "wimp.h"
#include "PalEntry.h"
#include "Editor.h"

int layout_decode_y_coord(int y);

int layout_get_width(void);

int layout_get_height(void);

void layout_get_bands_bbox(int start_row, int end_row, BBox *bbox);

void layout_get_caret_bbox(int row, BBox *bbox);

void layout_get_selection_bbox(int start_row, int end_row, BBox *bbox);

void layout_redraw_bbox(int xmin, int ymax, BBox *bbox,
  Editor const *editor, _Optional Editor const *ghost, const PaletteEntry palette[],
  bool draw_caret);

#endif
