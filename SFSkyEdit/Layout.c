/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Plotting and layout of editing window
 *  Copyright (C) 2018 Christopher Bazley
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

/* ISO library files */
#include "stdio.h"
#include <stdbool.h>

/* RISC OS library files */
#include "kernel.h"
#include "wimp.h"
#include "wimplib.h"

/* My library files */
#include "err.h"
#include "Debug.h"
#include "Macros.h"
#include "OSVDU.h"
#include "PalEntry.h"
#include "SFformats.h"
#include "WimpExtra.h"

/* Local headers */
#include "Layout.h"
#include "Editor.h"

#define WIMP_FORE_COLOUR (0)
#define SHOW_INDEX_NOT_COLNUM (0)

/* Constant numeric values */
enum
{
  WorkAreaHeight      = 3180,
  WorkAreaWidth       = 548,
  ColourBandHeight    = 32,
  ColourBandVGap      = 16,
  ColourBandHGap      = 8,
  ColourBandDFGColour = 0xffffff, /* BbGgRr format, for dark colours */
  ColourBandLFGColour = 0x000000, /* BbGgRr format, for light colours */
  CaretThickness      = 4,
  RowHeight           = ColourBandHeight + ColourBandVGap
};


/* ----------------------------------------------------------------------- */
/*                         Private functions                               */

static int layout_encode_y_coord(int const row)
{
  int const y = (row * RowHeight) - WorkAreaHeight;
  DEBUGF("Row %d encodes as Y coords %d\n", row, y);
  return y;
}

/* ----------------------------------------------------------------------- */

static void plot_caret(int const xmin, int const ymax,
  int const row, int const colour)
{
  assert(row >= 0);
  assert(colour >= 0);
  assert(colour < 16);
  DEBUGF("Drawing caret at %d in colour %d\n", row, colour);

  ON_ERR_RPT(wimp_set_colour(colour));

  int const y = ymax + layout_encode_y_coord(row);

  ON_ERR_RPT(os_plot(PlotOp_SolidInclBoth + PlotOp_MoveAbs,
                       xmin + ColourBandHGap/2,
                       y + ColourBandVGap/2 - CaretThickness/2));

  ON_ERR_RPT(os_plot(PlotOp_RectangleFill + PlotOp_PlotFGRel,
                       WorkAreaWidth - ColourBandHGap,
                       CaretThickness - 1));

  ON_ERR_RPT(os_plot(PlotOp_SolidInclBoth + PlotOp_MoveAbs,
                       xmin + WorkAreaWidth - ColourBandHGap/2 -
                         CaretThickness/2,
                       y));

  ON_ERR_RPT(os_plot(PlotOp_RectangleFill + PlotOp_PlotFGRel,
                       CaretThickness - 1,
                       ColourBandVGap - 1));

  ON_ERR_RPT(os_plot(PlotOp_SolidInclBoth + PlotOp_MoveAbs,
                       xmin + ColourBandHGap/2 - CaretThickness/2,
                       y));

  ON_ERR_RPT(os_plot(PlotOp_RectangleFill + PlotOp_PlotFGRel,
                     CaretThickness - 1,
                     ColourBandVGap - 1));
}

/* ----------------------------------------------------------------------- */

static void plot_selection(int const xmin, int const ymax,
  int const start_row, int const end_row, int const colour)
{
  assert(start_row >= 0);
  assert(start_row < end_row);
  assert(colour >= 0);
  assert(colour < 16);
  DEBUGF("Drawing selection %d..%d (ex.) in colour %d\n",
         start_row, end_row, colour);

  ON_ERR_RPT(wimp_set_colour(colour));

  ON_ERR_RPT(os_plot(PlotOp_SolidInclBoth + PlotOp_MoveAbs,
                       xmin,
                       ymax + ColourBandVGap/2 +
                         layout_encode_y_coord(start_row)));

  ON_ERR_RPT(os_plot(PlotOp_RectangleFill + PlotOp_PlotFGAbs,
                     xmin + WorkAreaWidth - 1,
                     ymax + ColourBandVGap/2 - 1 +
                       layout_encode_y_coord(end_row)));
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

int layout_decode_y_coord(int const y)
{
  int const y_dist = y + WorkAreaHeight + (ColourBandHeight / 2);
  int const row = y_dist / RowHeight;
  DEBUGF("Y coord %d decodes as row %d\n", y, row);
  return row;
}

/* ----------------------------------------------------------------------- */

int layout_get_width(void)
{
  return WorkAreaWidth;
}

/* ----------------------------------------------------------------------- */

int layout_get_height(void)
{
  return WorkAreaHeight;
}

/* ----------------------------------------------------------------------- */

void layout_get_bands_bbox(int const start_row, int const end_row,
  BBox *const bbox)
{
  assert(start_row >= 0);
  assert(end_row > start_row);
  assert(bbox != NULL);

  bbox->xmin = 0;
  bbox->ymin = layout_encode_y_coord(start_row) + (ColourBandVGap / 2);
  bbox->xmax = WorkAreaWidth;
  bbox->ymax = layout_encode_y_coord(end_row) + (ColourBandVGap / 2);
}

/* ----------------------------------------------------------------------- */

void layout_get_caret_bbox(int const row, BBox *const bbox)
{
  assert(row >= 0);
  assert(bbox != NULL);

  bbox->xmin = 0;
  bbox->ymin = layout_encode_y_coord(row);
  bbox->xmax = WorkAreaWidth;
  bbox->ymax = bbox->ymin + ColourBandVGap;
}

/* ----------------------------------------------------------------------- */

void layout_get_selection_bbox(int const start_row, int const end_row,
  BBox *const bbox)
{
  assert(start_row >= 0);
  assert(end_row > start_row);
  assert(bbox != NULL);

  bbox->xmin = ColourBandHGap;
  bbox->ymin = ColourBandVGap + layout_encode_y_coord(start_row);
  bbox->xmax = WorkAreaWidth - ColourBandHGap;
  bbox->ymax = layout_encode_y_coord(end_row);
}

/* ----------------------------------------------------------------------- */

void layout_redraw_bbox(int const xmin, int const ymax, BBox *const bbox,
  Editor const *const editor, Editor const *const ghost,
  const PaletteEntry palette[], bool const draw_caret)
{
  assert(bbox != NULL);
  assert(bbox->xmin >= 0);
  assert(bbox->xmax >= bbox->xmin);
  assert(bbox->ymax <= 0);
  assert(bbox->ymax >= bbox->ymin);

  DEBUGF("Redraw origin is %d,%d\n", xmin, ymax);
  DEBUGF("Redraw rectangle is %d,%d,%d,%d\n", bbox->xmin, bbox->ymin,
         bbox->xmax, bbox->ymax);

  /* Set common values of icons */
  char num_as_text[16], validation_string[sizeof("C000000/000000")];
  WimpPlotIconBlock ploticonblock = {
    .bbox = {
      .xmin = ColourBandHGap,
      .xmax = WorkAreaWidth - ColourBandHGap
    },
    .data = {
      .it = {
        .buffer = num_as_text,
        .buffer_size = sizeof(num_as_text),
        .validation = validation_string
      }
    }
  };

  /* Which rows should be drawn? */
  int const min_row = (WorkAreaHeight + bbox->ymin) / RowHeight;
  if (min_row < 0)
    return;

  int const max_row = (WorkAreaHeight + bbox->ymax) / RowHeight;
  assert(max_row >= min_row);
  if (max_row < 0)
    return;

  /* We don't have data for an infinite number of bands... */
  if (max_row > SFSky_Height / 2)
    return;

  DEBUGF("Colour bands to be drawn :%d..%d (inc.)\n", min_row, max_row);

  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);

  /* Although 'sel_high' is nominally exclusive, even a minimal selection
     (i.e. a caret) occupies ColourBandVGap and any other selection overlaps
     row 'sel_high' by ColourBandVGap/2. Therefore sel_high > min_row isn't
     an adequate test. */
  if (sel_high >= min_row && sel_low <= max_row)
  {
    if (sel_low == sel_high)
    {
      /* Plot caret */
      if (draw_caret)
      {
        plot_caret(xmin, ymax, sel_low, WimpColour_Red);
      }
    }
    else
    {
      /* Plot selection.
        The top of the selection rectangle will overlap the row above by
         ColourBandVGap/2. Subtract one from 'min_row' to allow for the
         possibility that 'sel_high' == 'min_row'. */
      int const sel_rect_min = (min_row == 0) ?
                               sel_low : HIGHEST(sel_low, min_row - 1);

      /* 'sel_high' is exclusive whereas 'max_row' is inclusive */
      int const sel_rect_max = LOWEST(sel_high, max_row + 1);

      /* Selection colour is faded when input focus is elsewhere */
      plot_selection(xmin, ymax,
                     sel_rect_min, sel_rect_max,
                     draw_caret ?
                       WimpColour_DarkGrey : WimpColour_MidLightGrey);
    }
  }

  /* Plot colour bands (not the actual patterns) */
  Sky const *const sky = editor_get_sky(editor);
  for (int row = min_row; row <= max_row; row++)
  {
    if (row >= SFSky_Height / 2)
      break;

    int const colour = sky_get_colour(sky, row);
    const PaletteEntry entry = palette[colour];
    unsigned int const brightness = palette_entry_brightness(entry);

    /* Plot colour band */
    ploticonblock.bbox.ymin = ColourBandVGap + layout_encode_y_coord(row);
    ploticonblock.bbox.ymax = ploticonblock.bbox.ymin + ColourBandHeight;
    ploticonblock.flags = WimpIcon_Text | WimpIcon_Indirected |
                          WimpIcon_HCentred | WimpIcon_VCentred |
                          WimpIcon_Filled;
#if WIMP_FORE_COLOUR
    ploticonblock.flags |= WimpIcon_FGColour *
                           (brightness > MaxBrightness/2 ?
                             WimpColour_Black : WimpColour_White);
#endif
    if (row >= sel_low && row < sel_high) {
      ploticonblock.flags |= WimpIcon_Border;
#if SHOW_INDEX_NOT_COLNUM
      sprintf(num_as_text, "%d", row);
#else
      sprintf(num_as_text, "%d", colour);
#endif
    } else {
      num_as_text[0] = '\0';
    }

#if WIMP_FORE_COLOUR
    /* Background colour is 24-bit RGB in icon validation string */
    sprintf(validation_string,
            "C/%X",
            entry >> PaletteEntry_RedShift);
#else
    /* Both colours are 24-bit RGB */
    sprintf(validation_string,
            "C%X/%X",
            brightness > MaxBrightness/2 ?
              ColourBandLFGColour : ColourBandDFGColour,
            entry >> PaletteEntry_RedShift);
#endif

    ON_ERR_RPT(wimp_plot_icon(&ploticonblock));
  }

  /* Plot ghost caret */
  if (ghost && !editor_has_selection(ghost))
  {
    int const insert_pos = editor_get_caret_pos(ghost);
    if (insert_pos >= min_row && insert_pos <= max_row)
    {
      plot_caret(xmin, ymax, insert_pos, WimpColour_LightGreen);
    }
  }
}
