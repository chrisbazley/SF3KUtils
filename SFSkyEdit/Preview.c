/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky preview window
 *  Copyright (C) 2001 Christopher Bazley
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
#include <assert.h>
#include "stdlib.h"
#include "stdio.h"
#include <string.h>
#include <stdbool.h>
#include <limits.h>

/* RISC OS library files */
#include "kernel.h"
#include "swis.h"
#include "wimp.h"
#include "toolbox.h"
#include "event.h"
#include "wimplib.h"
#include "window.h"
#include "menu.h"
#include "flex.h"

/* My library files */
#include "Err.h"
#include "msgtrans.h"
#include "Macros.h"
#include "SFFormats.h"
#include "SprFormats.h"
#include "SpriteArea.h"
#include "NoBudge.h"
#include "Entity2.h"
#include "WimpExtra.h"
#include "TrigTable.h"
#include "Debug.h"
#include "Hourglass.h"
#include "ClrTrans.h"
#include "OSVDU.h"
#include "OSSpriteOp.h"
#include "WriterFlex.h"
#include "EventExtra.h"

/* Local headers */
#include "Utils.h"
#include "EditWin.h"
#include "Render.h"
#include "Preview.h"
#include "SFSInit.h"
#include "OurEvents.h"
#include "Preview.h"
#include "PrevUMenu.h"
#include "SavePrev.h"
#include "ScalePrev.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


/* Toolbar component IDs */
enum
{
  ComponentId_Direction_NumRange = 0x00,
  ComponentId_Height_Slider      = 0x00,
  ComponentId_Height_NumRange    = 0x01,
  ComponentId_Angle_Slider       = 0x02,
  ComponentId_Angle_NumRange     = 0x03
};

/* Constant numeric values */
enum
{
  Screen_Width      = 320,  /* Width of sprite (in pixels) */
  Screen_Height     = 256,  /* Height of sprite (in pixels) */
  Screen_Eigen      = 2,    /* Log 2 of the no. of pixels per OS unit */
  Screen_Log2BPP    = 3,    /* Number of colours in sprite's palette */
  BitsPerPixel      = 1 << Screen_Log2BPP,
  NColours          = 1 << BitsPerPixel,
  Screen_Mode       = 13,   /* Mode number (45 dpi, 8 bits per pixel) */
  Scale_Default     = 50,   /* Percentage scale */
  Height_Min        = 0,    /* Ground level (in internal units) */
  Height_Max        = 3648, /* observed limit */
  Height_Step       = 16,
  Height_Default    = 0,
  Direction_Min     = 0,    /* North (in degrees clockwise) */
  Direction_Max     = 359,
  Direction_Step    = 4,
  Direction_Default = 0,
  Angle_Min         = 0,    /* Horizontal (in degrees) */
  Angle_Max         = 60,
  Angle_Step        = 1,
  Angle_Default     = 0,
  Degrees           = 90,    /* Degrees per quarter turn (PI/2 in radians) */
  SineMultiplier    = 1024,  /* Scaler applied to make sine values whole
                                (SF3K uses 1023, which seems wrong) */
  QuarterTurn       = 128,   /* No. of sine values to pre-calculate for a
                                quarter turn (from SF3000) */
  HorizonDist       = 16384, /* Distance from camera of a point to be rotated
                                to calculate vertical position of horizon. */
  NStars            = 255,
  MaxStarSize       = 16,
  NStarColours      = 16,
  MaxStarBright     = 8192,
  Colour_Red        = 23,
  Colour_Cyan       = 235,
  Colour_Blue       = 139,
  Colour_Yellow     = 119,
  Colour_White      = 255,
  StarHeightScaler  = 32,
  MinStarDist       = 32768, /* Stars closer than this are assumed to be
                                outside the viewable volume */
  StarDist          = 8192, /* Distance from camera to stars */
  MinStarHeight     = 128,
  PerspDividend     = 1<<28,
  PerspDivisorBase  = -45,
  PerspDivisorStep  = 768,
  PerspTableLen     = StarDist > HorizonDist ? StarDist : HorizonDist,
  ScreenScaler      = 2048,
  PostRotateScaler  = 8,
  DistScaler        = 12,
  PreExpandHeap     = 512 /* Number of bytes to pre-allocate before disabling
                             flex budging (and thus heap expansion). */
};

typedef struct
{
  int x;
  int y;
  int z;
}
Point3D;

typedef struct
{
  Point3D        pos;
  uint8_t        colour;
  uint8_t        size;
  unsigned short bright;
}
StarData;

struct PreviewData
{
  ObjectId      window_id;
  ObjectId      height_toolbar;
  ObjectId      direction_toolbar;
  int           render_height;    /* in esoteric sky plotter units */
  int           render_direction; /* in degrees clockwise from north */
  int           render_angle;     /* in degrees from horizontal */
  int           scale;            /* percentage scale */
  ScaleFactors  scale_factors;
  bool          have_caret;
  bool          toolbars;
  bool          no_scale;
  bool          plot_err;
  void         *cached_image; /* flex anchor */
  void         *export; /* flex anchor */
  SkyFile      *file;
  void         *stars;    /* flex anchor */
};

static bool translate_cols = true;
static _Optional void *col_trans_table = NULL; /* table of colour numbers for drawing
                                                  sprite in desktop */
static _Optional TrigTable *trig_table = NULL; /* table of (co)sine values */
static bool def_toolbars = true; /* default toolbar show state */
static int def_scale = Scale_Default; /* default percentage scale */
static _Optional int *persp_table = NULL; /* table of reciprocal values for perspective
                                             projection */

/* ----------------------------------------------------------------------- */
/*                          Private functions                              */

static void cam_rotate(Point3D *const p, int const x_angle, int const y_angle)
{
  /* Use the trigonometric look-up table to rotate a point in 3D space
     and apply perspective division to convert to screen coordinates */
  assert(p != NULL);

  DEBUGF("About to rotate %d,%d,%d by %d,%d\n",
         p->x, p->y, p->z, x_angle, y_angle);

  int const x_in = p->x;
  int y_in = p->y;
  int const z_in = p->z;

  if (!trig_table) {
    return;
  }
  const TrigTable *const tt = &*trig_table;

  /* Apply X rotation */
  int cos = TrigTable_look_up_cosine(tt, x_angle),
      sin = TrigTable_look_up_sine(tt, x_angle);

  p->x = (x_in * cos) / (SineMultiplier / PostRotateScaler) -
         (y_in * sin) / (SineMultiplier / PostRotateScaler);

  y_in = (x_in * sin) / SineMultiplier +
         (y_in * cos) / SineMultiplier;

  /* Apply Y rotation */
  cos = TrigTable_look_up_cosine(tt, y_angle);
  sin = TrigTable_look_up_sine(tt, y_angle);

  p->y = (y_in * cos) / (SineMultiplier / PostRotateScaler) -
         (z_in * sin) / (SineMultiplier / PostRotateScaler);

  p->z = (y_in * sin) / (SineMultiplier / PostRotateScaler) +
         (z_in * cos) / (SineMultiplier / PostRotateScaler);

  DEBUGF("Rotated point is %d,%d,%d\n", p->x, p->y, p->z);
}

/* ----------------------------------------------------------------------- */

static void persp_project(const Point3D *const p, _Optional int *const screen_x,
  _Optional int *const screen_y)
{
  int scr_x, scr_y;

  assert(p != NULL);

  int const index = p->y / (PerspDivisorStep / DistScaler);
  if (index <= 0 || persp_table == NULL)
  {
    /* Don't attempt perspective projection of coordinates behind the camera */
    scr_x = p->x;
    scr_y = p->z;
  }
  else
  {
    /* Calculate screen coordinates by multiplying by the reciprocal of a
       value derived from the distance. */
    assert(index < PerspTableLen);
    int const reciprocal = persp_table[index];
    assert(reciprocal == PerspDividend /
                         (PerspDivisorBase + PerspDivisorStep * index));

    scr_x = (p->x * reciprocal) / (PerspDividend / ScreenScaler);
    scr_y = (p->z * reciprocal) / (PerspDividend / ScreenScaler);
  }
  DEBUGF("Screen coordinates are %d,%d\n", scr_x, scr_y);

  if (screen_x != NULL)
    *screen_x = scr_x;

  if (screen_y != NULL)
    *screen_y = scr_y;
}

/* ----------------------------------------------------------------------- */

void render_scene(const PreviewData *const preview_data)
{
  BBox redraw_box;

  assert(preview_data != NULL);
  if (preview_data->export == NULL)
  {
    DEBUGF("Unable to render: no sky file\n");
    return;
  }

  /* Convert the camera angles from degrees to internal angle units */
  int const x_rot = (preview_data->render_direction * QuarterTurn) / Degrees;
  int const y_rot = (preview_data->render_angle * QuarterTurn) / Degrees;

  /* Rotate a 3D point to find the position of the horizon relative to the
     camera */
  Point3D tmp = {
    .x = 0,
    .y = HorizonDist,
    .z = 0
  };
  cam_rotate(&tmp, 0, y_rot);

  /* Project the rotated 3D point onto the 2D screen to find the
     vertical offset of the horizon from the vanishing point */
  int screen_y = 0;
  persp_project(&tmp, NULL, &screen_y);

  nobudge_register(PreExpandHeap);

  /* Render sky to image cache at current height */
  assert(preview_data->cached_image != NULL);
  SpriteHeader *const first_spr =
    (SpriteHeader *)((char *)preview_data->cached_image +
    ((SpriteAreaHeader *)preview_data->cached_image)->first);

  void *const screen = (char *)first_spr + first_spr->image;

  /* Final argument is the offset to the first word to be plotted! (4 bytes
     before the end of the lowest scan line to be filled from right to left) */
  sky_drawsky(preview_data->render_height, &*preview_data->export, screen,
              (Screen_Height * Screen_Width) - 4 + (screen_y * Screen_Width));

  SFSky const *const s = (SFSky *)preview_data->export;
  int star_tint = preview_data->render_height - s->min_stars_height;
  DEBUGF("Stars tint (based on height) is %d\n", star_tint);
  const StarData *star = preview_data->stars;
  assert(star != NULL);

  if (star_tint >= 0)
  {
    star_tint *= StarHeightScaler;

    for (int s = 0; s < NStars; s++, star++)
    {
      /* Rotate the 3D coordinates of the star to find its position relative
         to the camera */
      tmp = star->pos;
      cam_rotate(&tmp, x_rot, y_rot);
      if (tmp.y < MinStarDist)
      {
        DEBUGF("Star is too close to render (%d < %d)\n", tmp.y, MinStarDist);
        continue;
      }

      /* Project the rotated 3D coordinates onto the 2D screen */
      int screen_x = 0;
      persp_project(&tmp, &screen_x, &screen_y);

      /* Plot a star of the appropriate colour and brightness at the screen
         coordinates */
      star_plot(star_tint, screen,
                Screen_Width/2 + screen_x, Screen_Height + screen_y,
                star->colour, star->bright, star->size);
    }
  }

  nobudge_deregister();

  if (!E(window_get_extent(0, preview_data->window_id, &redraw_box)))
  {
    ON_ERR_RPT(window_force_redraw(0, preview_data->window_id, &redraw_box));
  }
}

/* ----------------------------------------------------------------------- */

static int slider_value_changed(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  const SliderValueChangedEvent * const svce =
    (SliderValueChangedEvent *)event;

  PreviewData *const preview_data = handle;
  ComponentId nr = NULL_ComponentId;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  switch (id_block->self_component)
  {
    case ComponentId_Height_Slider:
      if (svce->new_value != preview_data->render_height)
      {
        /* Set viewing height for sky render */
        preview_data->render_height = svce->new_value;
        nr = ComponentId_Height_NumRange;
      }
      break;

    case ComponentId_Angle_Slider:
      if (svce->new_value != preview_data->render_angle)
      {
        /* Set viewing direction for sky render */
        preview_data->render_angle = svce->new_value;
        nr = ComponentId_Angle_NumRange;
      }
      break;

    default:
      return 0; /* Unknown component */
  }

  if (nr != NULL_ComponentId)
  {
    /* Update the number range to reflect the value of the associated slider */
    ON_ERR_RPT(numberrange_set_value(0, id_block->self_id, nr,
                 svce->new_value));
    render_scene(preview_data);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int h_numberrange_value_changed(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  const NumberRangeValueChangedEvent * const nrvce =
    (NumberRangeValueChangedEvent *)event;
  PreviewData *const preview_data = handle;
  ComponentId sl = NULL_ComponentId;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  switch (id_block->self_component)
  {
    case ComponentId_Height_NumRange:
      if (nrvce->new_value != preview_data->render_height)
      {
        /* Set viewing height for sky render */
        preview_data->render_height = nrvce->new_value;
        sl = ComponentId_Height_Slider;
      }
      break;

    case ComponentId_Angle_NumRange:
      if (nrvce->new_value != preview_data->render_angle)
      {
        /* Set viewing direction for sky render */
        preview_data->render_angle = nrvce->new_value;
        sl = ComponentId_Angle_Slider;
      }
      break;

    default:
      return 0; /* Unknown component */
  }

  if (sl != NULL_ComponentId)
  {
    /* Update the slider to reflect the value of the associated number range */
    ON_ERR_RPT(slider_set_value(0, id_block->self_id, sl, nrvce->new_value));
    render_scene(preview_data);
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int a_numberrange_value_changed(int const event_code,
  ToolboxEvent *const event, IdBlock *const id_block, void *const handle)
{
  const NumberRangeValueChangedEvent * const nrvce =
    (NumberRangeValueChangedEvent *)event;
  PreviewData *const preview_data = handle;

  NOT_USED(event_code);
  assert(event != NULL);
  assert(id_block != NULL);
  assert(handle != NULL);

  if (id_block->self_component != ComponentId_Direction_NumRange ||
      nrvce->new_value == preview_data->render_direction)
  {
    return 0; /* Wrong component or no change in value */
  }

  /* Set viewing direction for sky render */
  preview_data->render_direction = nrvce->new_value;
  render_scene(preview_data);

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void final_tool_bars(PreviewData *const preview_data)
{
  assert(preview_data != NULL);

  /* Deregister event handlers for toolbars */
  ON_ERR_RPT(event_deregister_toolbox_handlers_for_object(
               preview_data->height_toolbar));

  ON_ERR_RPT(event_deregister_toolbox_handlers_for_object(
               preview_data->direction_toolbar));
}

/* ----------------------------------------------------------------------- */

static bool init_tool_bars(PreviewData *const preview_data)
{
  assert(preview_data != NULL);

  if (E(window_get_tool_bars(
          Window_ExternalBottomLeftToolbar | Window_ExternalTopLeftToolbar,
          preview_data->window_id, NULL, NULL,
          &preview_data->direction_toolbar,
          &preview_data->height_toolbar)))
  {
    return false;
  }

  do
  {

    /* Register Toolbox event handlers to called when the user alters
       the viewing height or direction */
    if (E(event_register_toolbox_handler(preview_data->height_toolbar,
            Slider_ValueChanged, slider_value_changed, preview_data)))
    {
      break;
    }

    if (E(event_register_toolbox_handler(preview_data->height_toolbar,
            NumberRange_ValueChanged, h_numberrange_value_changed,
            preview_data)))
    {
      break;
    }

    if (E(event_register_toolbox_handler(preview_data->direction_toolbar,
            NumberRange_ValueChanged, a_numberrange_value_changed,
            preview_data)))
    {
      break;
    }
    return true;
  }
  while (0);

  final_tool_bars(preview_data);
  return false;
}

/* ----------------------------------------------------------------------- */

/* Use this function to wrap calls which may return an error, to ensure that
   only the first of each run of multiple consecutive errors is reported. */
static bool handle_redraw_err(bool *suppress_errors, _Optional const _kernel_oserror *e)
{
  assert(suppress_errors != NULL);

  if (e == NULL)
  {
    /* No error occurred: enable reporting of subsequent errors */
    if (*suppress_errors)
    {
      DEBUGF("Re-enabling redraw error reporting\n");
      *suppress_errors = false;
    }
    return false; /* No error occurred */
  }
  else
  {
    /* An error occurred: is error reporting currently suppressed? */
    if (!*suppress_errors)
    {
      /* Report this error but suppress subsequent reports */
      ON_ERR_RPT(e);
      DEBUGF("Suppressing subsequent redraw error reports\n");
      *suppress_errors = true;
    }
    return true; /* An error occurred */
  }
}

/* ----------------------------------------------------------------------- */

static _Optional const _kernel_oserror *generate_col_table(void)
{
  _Optional const _kernel_oserror *e = NULL;
  int Log2BPP;
  size_t size;
  ColourTransGenerateTableBlock block;
  bool valid;

  /* Shouldn't call this function if there is an existing colour translation table */
  assert(col_trans_table == NULL);

  hourglass_on();

  /* Find the colour depth of the current screen mode */
  e = os_read_mode_variable(OS_ReadModeVariable_CurrentMode,
                            ModeVar_Log2BPP, &Log2BPP, &valid);
  if (e == NULL)
  {
    if (valid)
    {
      DEBUGF("Current screen mode has %d bits per pixel\n", 1 << Log2BPP);
    }
    else
    {
      Log2BPP = -1; /* couldn't determine no. of bits per pixel */
    }

    /* Find required memory for colour translation table */
    block.source.type = ColourTransContextType_Screen;
    block.source.data.screen.mode = Screen_Mode;
    block.source.data.screen.palette = ColourTrans_DefaultPalette;

    block.destination.type = ColourTransContextType_Screen;
    block.destination.data.screen.mode = ColourTrans_CurrentMode;
    block.destination.data.screen.palette = ColourTrans_CurrentPalette;

    e = colourtrans_generate_table(0, &block, NULL, 0, &size);
  }

  if (e == NULL)
  {
    DEBUGF("%zu bytes are required for colour translation table\n", size);

    /* Allocate a buffer of the required size for the translation table */
    _Optional char * const ct = malloc(size);
    if (ct == NULL)
    {
      e = msgs_error(DUMMY_ERRNO, "ColTransMem");
    }
    else
    {
      /* Create colour translation table */
      e = colourtrans_generate_table(0, &block, &*ct, size, NULL);
      if (e == NULL)
      {
        DEBUGF("Created colour translation table at %p\n", (void *)ct);

        /* Is the translation table really necessary? */
        if (Log2BPP == Screen_Log2BPP && size == NColours)
        {
          size_t i;

          for (i = 0; i < NColours && ct[i] == i; i++)
          {}
          if (i >= NColours)
          {
            /* Translation table is a one-to-one mapping, so discard it */
            DEBUGF("Discarding superfluous colour translation table\n");
            FREE_SAFE(col_trans_table);
            translate_cols = false;
          }
        }
	col_trans_table = ct;
      }
      else
      {
        free(ct);
      }
    }
  }

  hourglass_off();

  return e;
}

/* ----------------------------------------------------------------------- */

static int redraw_window(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  /* Custom redraw from cached sky image */
  PreviewData *const preview_data = handle;
  WimpRedrawWindowBlock block;
  int more;
  bool simple_redraw = false;
  static bool sup = false;

  NOT_USED(event_code);
  assert(event != NULL);
  NOT_USED(id_block);
  assert(handle != NULL);

  DEBUGF("Request to redraw preview window handle 0x%x\n",
    event->redraw_window_request.window_handle);

  /* If no colour translation table has been generated, or the existing table
     is not suitable for the current screen mode/palette then regenerate it. */
  if (translate_cols && (col_trans_table == NULL))
  {
    simple_redraw = handle_redraw_err(&sup, generate_col_table());
  }

  /* Successfully getting the first redraw rectangle shouldn't re-enable
     redraw error reporting. */
  block.window_handle = event->redraw_window_request.window_handle;
  if (!E(wimp_redraw_window(&block, &more)))
  {
    int const botleft_x = block.visible_area.xmin - block.xscroll;
    int const botleft_y = block.visible_area.ymax - block.yscroll -
                          preview_data->scale_factors.ymul;

    nobudge_register(PreExpandHeap);

    while (more)
    {
      if (!simple_redraw)
      {
        /* Plot redraw cache sprite */
        _Optional ScaleFactors *scale = NULL;
        if (!preview_data->no_scale)
        {
          scale = &preview_data->scale_factors;
        }

        _Optional void *colours = NULL;
        if (translate_cols)
        {
          assert(col_trans_table != NULL);
          colours = col_trans_table;
        }

        simple_redraw = handle_redraw_err(
          &preview_data->plot_err,
          os_sprite_op_plot_scaled_sprite(preview_data->cached_image,
            "cache", botleft_x, botleft_y, SPRITE_ACTION_OVERWRITE,
            scale, colours));
      }
      if (simple_redraw)
      {
        /* Draw plain background */
        if (wimp_set_colour(WimpColour_Black) == NULL)
        {
          if (os_plot(PlotOp_SolidInclBoth + PlotOp_MoveAbs,
                      block.redraw_area.xmin,
                      block.redraw_area.ymin) == NULL)
          {
            (void)os_plot(PlotOp_RectangleFill + PlotOp_PlotFGAbs,
                          block.redraw_area.xmax,
                          block.redraw_area.ymax);
          }
        }
      }

      /* Successfully getting the next redraw rectangle shouldn't re-enable
         redraw error reporting.*/
      if (E(wimp_get_rectangle(&block, &more)))
      {
        simple_redraw = true;
        break; /* can't determine whether we have finished, so assume we did */
      }
    }

    nobudge_deregister();
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static int mouse_click(int const event_code, WimpPollBlock *const event,
  IdBlock *const id_block, void *const handle)
{
  NOT_USED(id_block);
  assert(event != NULL);
  NOT_USED(event_code);

  if (event->mouse_click.buttons == Wimp_MouseButtonSelect ||
      event->mouse_click.buttons == Wimp_MouseButtonAdjust)
  {
    /* Claim the input focus for the preview window */
    if (!E(wimp_set_caret_position(event->mouse_click.window_handle,
             -1, /* icon handle (window area) */
             0,0, /* coordinate offset */
             -1, /* height and flags */
             -1 /* index */ )))
    {
      /* Notify the current owner of the caret/selection that we have claimed
         it (e.g. the editing window will redraw its selection in grey) */
      ON_ERR_RPT(entity2_claim(Wimp_MClaimEntity_CaretOrSelection,
                               NULL, (Entity2EstimateMethod *)NULL,
                               (Saver2WriteMethod *)NULL,
                               (Entity2LostMethod *)NULL, handle));
    }
  }

  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */

static void set_height(PreviewData *const preview_data, int height)
{
  /* Set viewing height and attached slider/numeric display */
  assert(preview_data != NULL);
  assert(height >= Height_Min);
  assert(height <= Height_Max);

  ON_ERR_RPT(slider_set_value(0, preview_data->height_toolbar,
               ComponentId_Height_Slider, height));

  ON_ERR_RPT(numberrange_set_value(0, preview_data->height_toolbar,
               ComponentId_Height_NumRange, height));

  preview_data->render_height = height;
}

/* ----------------------------------------------------------------------- */

static void set_direction(PreviewData *const preview_data, int direction)
{
  /* Set viewing direction and attached slider/numeric display */
  assert(preview_data != NULL);
  assert(direction >= Direction_Min);
  assert(direction <= Direction_Max);

  ON_ERR_RPT(numberrange_set_value(0, preview_data->direction_toolbar,
               ComponentId_Direction_NumRange, direction));

  preview_data->render_direction = direction;
}

/* ----------------------------------------------------------------------- */

static void set_angle(PreviewData *const preview_data, int angle)
{
  /* Set viewing direction and attached slider/numeric display */
  assert(preview_data != NULL);
  assert(angle >= Angle_Min);
  assert(angle <= Angle_Max);

  ON_ERR_RPT(slider_set_value(0, preview_data->height_toolbar,
               ComponentId_Angle_Slider, angle));

  ON_ERR_RPT(numberrange_set_value(0, preview_data->height_toolbar,
               ComponentId_Angle_NumRange, angle));

  preview_data->render_angle = angle;
}

/* ----------------------------------------------------------------------- */

static void show_or_hide_tb(PreviewData *const preview_data, bool show)
{
  assert(preview_data != NULL);
  if (show)
  {
    ON_ERR_RPT(toolbox_show_object(0, preview_data->height_toolbar,
                 Toolbox_ShowObject_Default, NULL, preview_data->window_id,
                 NULL_ComponentId));

    ON_ERR_RPT(toolbox_show_object(0, preview_data->direction_toolbar,
                 Toolbox_ShowObject_Default, NULL, preview_data->window_id,
                 NULL_ComponentId));
  }
  else
  {
    ON_ERR_RPT(toolbox_hide_object(0, preview_data->height_toolbar));
    ON_ERR_RPT(toolbox_hide_object(0, preview_data->direction_toolbar));
  }

  preview_data->toolbars = show;
}

/* ----------------------------------------------------------------------- */

static void make_scale_factors(PreviewData *const preview_data)
{
  assert(preview_data != NULL);

  /* Calculate sprite scaling factors from the current eigen values and
     actual (adjusted) window extent */
  ScaleFactors *const scale_factors = &preview_data->scale_factors;

  BBox extent;
  if (E(window_get_extent(0, preview_data->window_id, &extent)))
  {
    *scale_factors = (ScaleFactors){
      .xmul = 1, .ymul = 1, .xdiv = 1, .ydiv = 1,
    };
  }
  else
  {
    *scale_factors = (ScaleFactors){
      /* Multiplication factors are the window's work area dimensions */
      .xmul = extent.xmax - extent.xmin,
      .ymul = extent.ymax - extent.ymin,
       /* Division factors are the sprite's dimensions in pixels,
          scaled according to the pixel density of the desktop screen mode */
      .xdiv = Screen_Width << x_eigen,
      .ydiv = Screen_Height << y_eigen,
    };
  }

  preview_data->no_scale = (scale_factors->xmul == scale_factors->xdiv &&
                            scale_factors->ymul == scale_factors->ydiv);
}

/* ----------------------------------------------------------------------- */

static int message_handler(WimpMessage *const message, void *const handle)
{
  PreviewData *const preview_data = handle;

  assert(message != NULL);
  assert(handle != NULL);

  switch(message->hdr.action_code)
  {
    case Wimp_MModeChange:
      /* Wimp re-rounds the window extent on mode change */
      make_scale_factors(preview_data);
      /* fallthrough */

    case Wimp_MPaletteChange:
      /* Simply discard the existing colour translation table (saves
         time when dealing with PaletteChange and ModeChanged broadcast in
         quick succession). */
      DEBUGF("Discarding colour translation table at %p\n", col_trans_table);
      FREE_SAFE(col_trans_table);
      translate_cols = true;
      break;

    default:
      break; /* not interested in this type of message */
  }

  return 0; /* don't claim event */
}

/* ----------------------------------------------------------------------- */

static void cleanup(void)
{
  DEBUGF("Cleaning up on exit\n");
  free(col_trans_table);
  free(persp_table);
  TrigTable_destroy(trig_table);
}

/* ----------------------------------------------------------------------- */

static void generate_stars(StarData *stars)
{
  assert(stars != NULL);

  if (!trig_table) {
    return;
  }
  const TrigTable *const tt = &*trig_table;

  for (int s = 0; s < NStars; s++, stars++)
  {
    static const uint8_t star_colours[] =
    {
      Colour_Red,
      Colour_Cyan,
      Colour_Blue,
      Colour_Yellow,
      Colour_White
    };

    /* Generate two random angles:
       1. Angle from directly in front (z rotation)
       2. Angle from the vertical (x rotation) */
    int const angle1 = (unsigned)rand() % (QuarterTurn * 4);
    int const angle2 = (unsigned)rand() % (QuarterTurn * 4);

    /* Get length of the adjacent side of a right-angle triangle with a
       hypotenuse of length SineMultiplier. Assume adjacent is codirectional
       with z axis and use as the elevation of a star. */
    int z = TrigTable_look_up_cosine(tt, angle2);
    if (z > 0)
    {
      z = -z; /* Force point above ground level by sign reversal */
    }
    z = z - MinStarHeight; /* Ensure minimum elevation */

    /* Get length of opposite side of same triangle. Assume opposite is
       codirectional with y axis and use as horizontal distance to the star. */
    int y = TrigTable_look_up_sine(tt, angle2);
    if (y < 0)
    {
      y = -y;
    }

    /* Rotate the vector (0, y, z) around the z axis by a random angle, to
       make it three-dimensional. Standard rotation formula is simplified
       because the x coordinate is always 0. */
    int const x = (y * TrigTable_look_up_cosine(tt, angle1)) /
                  SineMultiplier;
    y = (y * TrigTable_look_up_sine(tt, angle1)) / SineMultiplier;

    stars->pos.x = x * (StarDist / SineMultiplier);
    stars->pos.y = y * (StarDist / SineMultiplier);
    stars->pos.z = z * (StarDist / SineMultiplier);

    /* Choose a random star colour from the array (biased towards the last) */
    assert(NStarColours >= ARRAY_SIZE(star_colours));
    size_t c = (unsigned)rand() % NStarColours;
    if (c >= ARRAY_SIZE(star_colours))
    {
      c = ARRAY_SIZE(star_colours) - 1;
    }
    stars->colour = star_colours[c];

    stars->bright = (unsigned)rand() % MaxStarBright;
    stars->size = (unsigned)rand() % MaxStarSize;
  }
}

/* ----------------------------------------------------------------------- */

static bool generate_persp(void)
{
  /* Pre-calculate reciprocals to be used for perspective projection */
  _Optional int *const pt = malloc(sizeof(*pt) * PerspTableLen);
  if (pt == NULL)
  {
    return false; /* failure */
  }

  DEBUGF("Making reciprocal table with %u entries\n", PerspTableLen);
  unsigned int divisor = PerspDivisorBase;

  for (unsigned int r = 0; r < PerspTableLen; r++)
  {
    pt[r] = PerspDividend / divisor;
    DEBUG_VERBOSEF("%u: %u / %u = %u\n", r, PerspDividend, divisor,
                   pt[r]);
    divisor += PerspDivisorStep;
  }
  persp_table = pt;

  return true; /* success */
}

/* ----------------------------------------------------------------------- */

static void set_scale(PreviewData *const preview_data, int scale)
{

  DEBUGF("Setting scale %d of preview %p\n", scale, (void *)preview_data);
  preview_data->scale = scale;

  BBox extent = {
    .xmin = 0,
  /* Convert the sprite's dimensions from pixel coordinates to OS coordinates
     and then scale them by the configured percentage value */
    .ymin = -SCALE(Screen_Height << Screen_Eigen, scale),
    .xmax = SCALE(Screen_Width << Screen_Eigen, scale),
    .ymax = 0
  };
  ON_ERR_RPT_RTN(window_set_extent(0, preview_data->window_id, &extent));

  /* The window manager rounds the window's extent to a whole no. of pixels,
     so calculate sprite scaling factors using its actual extent */
  make_scale_factors(preview_data);

  /* If the window is already showing then reshow it with its new extent */
  unsigned int state;
  ON_ERR_RPT_RTN(toolbox_get_object_state(0, preview_data->window_id, &state));
  if ((state & Toolbox_GetObjectState_Showing) == 0)
    return;

  WimpGetWindowStateBlock reopen;
  ON_ERR_RPT_RTN(window_get_wimp_handle(0,
                                        preview_data->window_id,
                                        &reopen.window_handle));

  ON_ERR_RPT_RTN(wimp_get_window_state(&reopen));

  ON_ERR_RPT_RTN(toolbox_show_object(0,
                                     preview_data->window_id,
                                     Toolbox_ShowObject_FullSpec,
                                     &reopen.visible_area,
                                     NULL_ObjectId,
                                     NULL_ComponentId));

  ON_ERR_RPT(window_force_redraw(0, preview_data->window_id, &extent));
}

/* -------------------------------------------------------------------------- */

static int misc_tb_event(int const event_code, ToolboxEvent *const event,
  IdBlock *const id_block, void *const handle)
{
  PreviewData *const preview_data = handle;

  NOT_USED(event);
  assert(id_block != NULL);
  assert(handle != NULL);

  /* Careful - handler is called for unclaimed toolbox events on any object */
  if (id_block->self_id != preview_data->window_id &&
      id_block->ancestor_id != preview_data->window_id)
    return 0; /* event not for us - pass it on */

  /* Handle hotkey/menu selection events */
  switch (event_code)
  {
    case EventCode_PreviewSetCompOff: /* Set ground level */
    {
      /* Shift viewing height to new ground 0 (so it looks the same) */
      int const temp_render_height = preview_data->render_height;
      set_height(preview_data, 0);

      /* Tell editing window to update header values and redraw preview */
      SkyFile_add_render_offset(preview_data->file, temp_render_height);
      break;
    }
    case EventCode_PreviewSetStarsAlt:
    {
      /* Set height to start plotting stars */
      SkyFile_set_star_height(preview_data->file,
                              preview_data->render_height);
      break;
    }
    case EventCode_PreviewUp:
    case EventCode_PreviewDown:
    {
      /* Up/Down - increase/decrease viewing height */
      int height = preview_data->render_height;
      if (event_code == EventCode_PreviewUp)
      {
        height += Height_Step;
        if (height > Height_Max)
          height = Height_Max;
      }
      else
      {
        height -= Height_Step;
        if (height < Height_Min)
          height = Height_Min;
      }
      if (preview_data->render_height != height)
      {
        set_height(preview_data, height);
        render_scene(preview_data);
      }
      break;
    }
    case EventCode_PreviewClose:
    {
      /* ESC - Close preview */
      ON_ERR_RPT(toolbox_hide_object(0, preview_data->window_id));
      /* N.B. Don't need to worry about the window being iconised
              since iconised windows get no keypresses */
      break;
    }
    case EventCode_PreviewRotateRight:
    case EventCode_PreviewRotateLeft:
    {
      /* Ctrl-up/down - Tilt view downward/upward */
      int direction = preview_data->render_direction;
      if (event_code == EventCode_PreviewRotateRight)
      {
        direction += Direction_Step;
        if (direction > Direction_Max)
          direction = Direction_Min - 1 +
                      (direction - Direction_Max); /* wrap around */
      }
      else
      {
        direction -= Direction_Step;
        if (direction < Direction_Min)
          direction = Direction_Max + 1 -
                      (Direction_Min - direction); /* wrap around */
      }
      if (preview_data->render_direction != direction)
      {
        set_direction(preview_data, direction);
        render_scene(preview_data);
      }
      break;
    }
    case EventCode_PreviewTiltUp:
    case EventCode_PreviewTiltDown:
    {
      /* Ctrl-up/down - Tilt view downward/upward */
      int angle = preview_data->render_angle;
      if (event_code == EventCode_PreviewTiltUp)
      {
        angle += Angle_Step;
        if (angle > Angle_Max)
          angle = Angle_Max;
      }
      else
      {
        angle -= Angle_Step;
        if (angle < Angle_Min)
          angle = Angle_Min;
      }
      if (preview_data->render_angle != angle)
      {
        set_angle(preview_data, angle);
        render_scene(preview_data);
      }
      break;
    }
    case EventCode_PreviewToolbars:
    {
      /* Show/hide toolbars */
      bool toolbars = !preview_data->toolbars;

      /* Update tick on menu item */
      if (showing_as_descendant(PrevUMenu_sharedid, preview_data->window_id))
        PrevUMenu_set_toolbars(toolbars);

      show_or_hide_tb(preview_data, toolbars);
      break;
    }
    case EventCode_PreviewNewStars:
    {
      /* Generate a different set of pseudo-random stars */
      nobudge_register(PreExpandHeap);
      generate_stars(&*preview_data->stars);
      nobudge_deregister();

      render_scene(preview_data);
      break;
    }
    case EventCode_PreviewSave:
    case EventCode_PreviewScale:
    {
      show_object_relative(Toolbox_ShowObject_AsMenu,
                           event_code == EventCode_PreviewSave ?
                           SavePrev_sharedid : ScalePrev_sharedid,
                           preview_data->window_id,
                           id_block->self_id,
                           id_block->self_component);
      break;
    }
    case EventCode_PreviewDefault:
    {
      /* Save the current scale and toolbar state as the default for
         previews subsequently created */
      def_scale = preview_data->scale;
      def_toolbars = preview_data->toolbars;
      break;
    }
    default:
    {
      return 0; /* Not an event that we handle */
    }
  }
  return 1; /* claim event */
}

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void Preview_initialise(void)
{
  atexit(cleanup);

  /* Generate trigonometric look-up tables and reciprocals for
     perspective projection */
  trig_table = TrigTable_make(SineMultiplier, QuarterTurn);

  if (trig_table == NULL || !generate_persp())
  {
    err_complain_fatal(DUMMY_ERRNO, msgs_lookup("NoMem"));
  }
}

/* ----------------------------------------------------------------------- */

_Optional PreviewData *Preview_create(SkyFile *const file, char const *const title)
{
  const size_t sprite_area_size = sizeof(SpriteAreaHeader) +
                                  sizeof(SpriteHeader) +
                                  (Screen_Width * Screen_Height);

  assert(file != NULL);
  assert(title != NULL);

  /* Create data block for this window */
  _Optional PreviewData *const preview_data = malloc(sizeof(*preview_data));
  if (preview_data == NULL)
  {
    RPT_ERR("NoMem");
    return NULL;
  }

  *preview_data = (PreviewData){
    .no_scale = false,
    .have_caret = false,
    .plot_err = false,
    .file = file,
  };

  /* Create Window object */
  ObjectId window_id;
  if (!E(toolbox_create_object(0, "Preview", &window_id)))
  {
    preview_data->window_id = window_id;

    /* Register the handler for custom Toolbox events
       (generated by key shortcuts and menu entries) */
    if (!E(event_register_toolbox_handler(-1, -1, misc_tb_event,
      &*preview_data)))
    {
      /* Register handler to monitor screen mode or palette changes */
      if (!E(event_register_message_handler(-1, message_handler,
        &*preview_data)))
      {
        /* Allocate memory for a sprite in which to render the sky
           and for stars data */
        if (!flex_alloc(&preview_data->stars,
                        sizeof(StarData) * NStars))
        {
          RPT_ERR("NoMem");
        }
        else
        {
          if (!flex_alloc(&preview_data->cached_image, sprite_area_size))
          {
            RPT_ERR("NoMem");
          }
          else
          {
            if (init_tool_bars(&*preview_data))
            {
              do
              {
                if (E(toolbox_set_client_handle(0, window_id, &*preview_data)))
                {
                  break;
                }

                if (E(event_register_wimp_handler(window_id,
                        Wimp_ERedrawWindow, redraw_window, &*preview_data)))
                {
                  break;
                }

                if (E(event_register_wimp_handler(window_id,
                        Wimp_EMouseClick, mouse_click, &*preview_data)))
                {
                  break;
                }

                if (E(event_register_wimp_handler(window_id, -1, watch_caret,
                        &preview_data->have_caret)))
                {
                  break;
                }

                if (E(event_register_toolbox_handler(window_id,
                        Window_HasBeenHidden, hand_back_caret,
                        &preview_data->have_caret)))
                {
                  break;
                }

                nobudge_register(PreExpandHeap); /* protect cached_image & stars */

                /* Create a sprite in which to render the sky */
                spritearea_init(preview_data->cached_image, sprite_area_size);

                SpriteHeader *const sprite = spritearea_alloc_spr(
                  preview_data->cached_image,
                  sizeof(SpriteHeader) + (Screen_Width * Screen_Height));

                assert(sprite != NULL);
                memset(sprite->name, 0, sizeof(sprite->name));
                strncpy(sprite->name, "cache", sizeof(sprite->name));
                sprite->width = WORD_ALIGN(Screen_Width) / 4 - 1;
                sprite->height = Screen_Height - 1;
                sprite->left_bit = 0; /* lefthand wastage is deprecated */
                sprite->right_bit = SPRITE_RIGHT_BIT(Screen_Width, 8);
                sprite->image = sizeof(*sprite);
                sprite->mask = sizeof(*sprite);
                sprite->type = Screen_Mode;

                /* Generate a set of pseudo-random stars */
                generate_stars(preview_data->stars);

                nobudge_deregister();

                /* Start at horizontal ground level */
                set_height(&*preview_data, Height_Default);
                set_direction(&*preview_data, Direction_Default);
                set_angle(&*preview_data, Angle_Default);
                set_scale(&*preview_data, def_scale);
                show_or_hide_tb(&*preview_data, def_toolbars);
                Preview_set_title(&*preview_data, title);

                return preview_data;
              }
              while (0);

              /* Clean up in case we managed to register any event
                 handlers */
              final_tool_bars(&*preview_data);
            }
            flex_free(&preview_data->cached_image);
          }
          flex_free(&preview_data->stars);
        }
        (void)event_deregister_message_handler(-1, message_handler,
            &*preview_data);
      }
      (void)event_deregister_toolbox_handler(-1, -1, misc_tb_event,
          &*preview_data);
    }

    (void)remove_event_handlers_delete(window_id);
  }
  free(preview_data);
  return NULL;
}

/* ----------------------------------------------------------------------- */

void Preview_destroy(_Optional PreviewData *const preview_data)
{
  if (preview_data == NULL)
  {
    return;
  }

  DEBUGF("Destroying preview %p (object 0x%x)\n",
    (void *)preview_data, preview_data->window_id);

  /* Destroy main Window object */
  ON_ERR_RPT(remove_event_handlers_delete(preview_data->window_id));

  /* Hide any transient dialogue boxes that may have been shown as
     children of the deleted Window object. If such objects are shown
     repeatedly then the Toolbox can forget they are showing and
     refuse to hide them. */
  ON_ERR_RPT(wimp_create_menu(CloseMenu,0,0));

  final_tool_bars(&*preview_data);

  /* Deregister the Wimp message handler belonging to this preview
   */
  ON_ERR_RPT(event_deregister_message_handler(-1,
                                              message_handler,
                                              &*preview_data));

  /* Deregister the handler for custom Toolbox events
     (generated by key shortcuts and menu entries) */
  ON_ERR_RPT(event_deregister_toolbox_handler(-1,
                                              -1,
                                              misc_tb_event,
                                              &*preview_data));

  /* Free sprite area used for quick rendering */
  if (preview_data->cached_image)
  {
    flex_free(&preview_data->cached_image);
  }

  /* Free array of random stars */
  if (preview_data->stars)
  {
    flex_free(&preview_data->stars);
  }

  /* Free file being previewed */
  if (preview_data->export)
  {
    flex_free(&preview_data->export);
  }

  free(preview_data);
}

/* -------------------------------------------------------------------------- */

bool Preview_get_toolbars(const PreviewData *prev_data)
{
  assert(prev_data != NULL);
  return prev_data->toolbars;
}

/* -------------------------------------------------------------------------- */

void Preview_set_title(PreviewData *const preview_data, char const *title)
{
  assert(preview_data != NULL);
  assert(title != NULL);

  ON_ERR_RPT(window_set_title(0,
                              preview_data->window_id,
                              msgs_lookup_subn("PrevTitle", 1, title)));
}

/* -------------------------------------------------------------------------- */

void Preview_show(PreviewData *const preview_data, ObjectId parent_id)
{
  assert(preview_data != NULL);

  /* Get the current state of the preview window (a flags word) */
  unsigned int preview_state;
  ON_ERR_RPT_RTN(toolbox_get_object_state(0,
                                          preview_data->window_id,
                                          &preview_state));

  if ((preview_state & Toolbox_GetObjectState_Showing) != 0)
  {
    /* Preview window is already showing - just bring it to the top of the
       window stack */
    ON_ERR_RPT_RTN(toolbox_show_object(0,
                                       preview_data->window_id,
                                       Toolbox_ShowObject_Default,
                                       NULL,
                                       parent_id,
                                       NULL_ComponentId));
  }
  else
  {
    /* Preview window is not showing - open it relative to the position
       of the editing window. */
    show_object_relative(0,
                         preview_data->window_id,
                         parent_id,
                         parent_id,
                         NULL_ComponentId);
    DEBUGF("Preview object 0x%x has been shown\n",
      (unsigned)preview_data->window_id);
  }

  /* Claim the input focus for the preview window */
  int wimp_handle;
  ON_ERR_RPT_RTN(window_get_wimp_handle(0,
                                        preview_data->window_id,
                                        &wimp_handle));

  ON_ERR_RPT_RTN(wimp_set_caret_position(wimp_handle, -1, 0, 0, -1, -1));

  /* Notify the current owner of the caret/selection that we have claimed it
     (e.g. the editing window will redraw its selection in grey) */
  ON_ERR_RPT_RTN(entity2_claim(Wimp_MClaimEntity_CaretOrSelection,
                               NULL, (Entity2EstimateMethod *)NULL,
                               (Saver2WriteMethod *)NULL,
                               (Entity2LostMethod *)NULL, preview_data));

  /* Render sky preview */
  Preview_update(preview_data);
}

/* -------------------------------------------------------------------------- */

void Preview_update(PreviewData *const preview_data)
{
  Writer writer;
  writer_flex_init(&writer, &preview_data->export);

  SkyFile_export(preview_data->file, &writer);

  bool success = true;
  if (writer_destroy(&writer) < 0 && success)
  {
    RPT_ERR("NoMem");
    success = false;
  }

  if (!success && preview_data->export)
  {
    flex_free(&preview_data->export);
  }
  else
  {
    render_scene(preview_data);
  }
}

/* -------------------------------------------------------------------------- */

void Preview_set_scale(PreviewData *const preview_data, int scale)
{
  assert(preview_data != NULL);
  if (preview_data->scale != scale)
    set_scale(preview_data, scale);
}

/* ----------------------------------------------------------------------- */

int Preview_get_scale(const PreviewData *const preview_data)
{
  assert(preview_data != NULL);
  DEBUGF("Getting scale %d of preview %p\n",
         preview_data->scale, (void *)preview_data);

  return preview_data->scale;
}

/* ----------------------------------------------------------------------- */

flex_ptr Preview_get_anchor(PreviewData *const preview_data)
{
  assert(preview_data != NULL);
  return &preview_data->cached_image;
}
