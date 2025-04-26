/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Editor back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 */

#ifndef SFSEditor_h
#define SFSEditor_h

#include <stdbool.h>

#include "LinkedList.h"
#include "Reader.h"
#include "Sky.h"
#include "PalEntry.h"

typedef enum {
  EditResult_Unchanged,
  EditResult_Changed,
  EditResult_NoMem,
} EditResult;

typedef struct EditSky {
  Sky sky;
  LinkedList editors;
  void (*redraw_bands_cb)(struct EditSky *, int, int);
  void (*redraw_render_offset_cb)(struct EditSky *);
  void (*redraw_stars_height_cb)(struct EditSky *);
  LinkedList undo_list;
  _Optional LinkedListItem *next_undo;
} EditSky;

typedef struct Editor {
  LinkedListItem node;
  EditSky *edit_sky;
  void (*redraw_select_cb)(struct Editor *, int, int, int, int);
  unsigned char start;
  unsigned char end; /* 1st & 2nd band would be start=0, end=2 */
} Editor;

/*
 * Provide enough functionality to implement Support Group Application Notes
 * 240 and 241 without enforcing every detail (e.g. per-view selection).
 */

typedef void EditSkyRedrawBandsFn(EditSky *, int, int);
typedef void EditSkyRedrawRenderOffsetFn(EditSky *);
typedef void EditSkyRedrawStarsHeightFn(EditSky *);

/* Initialize an editing session for a sky file.
   If 'reader' is null then a default sky is created.  */
SkyState edit_sky_init(EditSky *edit_sky, _Optional Reader *reader,
   _Optional EditSkyRedrawBandsFn *redraw_bands_cb,
   _Optional EditSkyRedrawRenderOffsetFn *redraw_render_offset_cb,
   _Optional EditSkyRedrawStarsHeightFn *redraw_stars_height_cb);

/* Destroy an editing session for a sky file. */
void edit_sky_destroy(EditSky *edit_sky);

/* Get the sky file in an editing session */
Sky *edit_sky_get_sky(EditSky *edit_sky);

/* Returns false if there is nothing to undo. */
bool editor_can_undo(Editor const *editor);

/* Returns false if there is nothing to redo. */
bool editor_can_redo(Editor const *editor);

/* Undo the previous editing operation.
   Returns false if unchanged. */
bool editor_undo(Editor *editor);

/* Redo the previous editing operation.
   Returns false if unchanged. */
bool editor_redo(Editor *editor, PaletteEntry const palette[]);

/* Set the colour bands compression offset at ground level. */
EditResult edit_sky_set_render_offset(EditSky *edit_sky, int render_offset);

/* Increase the colour bands compression offset at ground level and
   decrease the height at which to plot stars by the same amount. */
EditResult edit_sky_add_render_offset(EditSky *edit_sky, int offset);

/* Set the height at which to plot stars. */
EditResult edit_sky_set_stars_height(EditSky *edit_sky,
  int stars_height);

typedef void EditorRedrawSelectFn(Editor *editor,
   int old_low, int old_high,
   int new_low, int new_high);

/* Initialize an editor of a sky file. */
void editor_init(Editor *editor, EditSky *edit_sky,
   _Optional EditorRedrawSelectFn *redraw_select_cb);

/* Get the sky file in an editor */
Sky *editor_get_sky(Editor const *editor);

/* Destroy an editor of a sky file. */
void editor_destroy(Editor *editor);

/* Returns true if any colours are selected. */
bool editor_has_selection(Editor const *editor);

/* Get the ordered selection endpoints for redraw and mouse click decoding. */
void editor_get_selection_range(Editor const *editor,
   _Optional int *sel_low, _Optional int *sel_high);

/* Set the selection end to equal the selection start.
   Returns false if unchanged. */
bool editor_clear_selection(Editor *editor);

/* Select all colours. Returns false if unchanged. */
bool editor_select_all(Editor *editor);

/* Move the nearest end of the selection to the given position and swap the
   two ends if the moved end was the caret position (selection start).
   Returns false if unchanged. */
bool editor_set_selection_nearest(Editor *editor, int pos);

/* Get the caret position (selection start). */
int editor_get_caret_pos(Editor const *editor);

/* Set the caret position (selection start). Returns false if unchanged. */
bool editor_set_caret_pos(Editor *editor, int pos);

/* Set the other (non-caret) selection end. Returns false if unchanged. */
bool editor_set_selection_end(Editor *editor, int pos);

/* Get the lowest selected colour */
int editor_get_selected_colour(Editor const *editor);

/* Copy up to 'dst_size' selected colours to an array.
   Returns the no. of colours that would have been copied to 'dst'
   had the supplied array been big enough. */
int editor_get_array(Editor const *editor, int *dst, int dst_size);

/* Interpolates between centres of homogenous colour blocks within
   the selected region. */
EditResult editor_smooth(Editor *editor, PaletteEntry const palette[]);

/* Change selected colours to a homogenous colour block. */
EditResult editor_set_plain(Editor *editor, int colour);

/* Interpolate between start and end of the selected region. */
EditResult editor_interpolate(Editor *editor, PaletteEntry const palette[],
  int start_col, int end_col);

/* Replace the selected colours with colours from an array and select the
   inserted colours. Outputs whether or not any colour is invalid. */
EditResult editor_insert_array(Editor *editor, int number, int const *src,
  bool *is_valid);

/* Replace the selected colours with all colours from another sky
   and select the inserted colours. */
EditResult editor_insert_sky(Editor *editor, Sky const *src);

/* Replace the selected colours with a homogenous colour block
   and set the caret to the end of the inserted colours. */
EditResult editor_insert_plain(Editor *editor, int number, int col);

/* Replace the selected colours with an interpolated gradient fill
   and set the caret to the end of the inserted colours. */
EditResult editor_insert_gradient(Editor *editor, PaletteEntry const palette[],
  int number, int start_col, int end_col, bool inc_start, bool inc_end);

/* Deletes selected colours. */
EditResult editor_delete_colours(Editor *editor);

/* Replace the selected colours with selected colours from another editor
   (which may be for a different sky file). */
EditResult editor_copy(Editor *dst, Editor const *src);

/* Move the selected colours to replace selected colours in another editor
   (which must be for the same sky file). */
EditResult editor_move(Editor *dst, Editor const *src);

#endif
