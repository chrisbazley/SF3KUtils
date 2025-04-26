/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Editor back-end functions
 *  Copyright (C) 2020 Christopher Bazley
 */

#ifndef SFCEditor_h
#define SFCEditor_h

#include <stdbool.h>
#include <limits.h>

#include "Reader.h"
#include "ColMap.h"
#include "PalEntry.h"
#include "LinkedList.h"

typedef struct EditColMap {
  ColMap colmap;
  void (*redraw_entry_cb)(struct EditColMap *, int);
  LinkedList undo_list;
  _Optional LinkedListItem *next_undo;
} EditColMap;

typedef struct Editor {
  EditColMap *edit_colmap;
  void (*redraw_select_cb)(struct Editor *, int);
  unsigned char selected[(ColMap_MaxSize + CHAR_BIT - 1) / CHAR_BIT];
  int num_selected;
} Editor;

/*
 * Provide enough functionality to implement Support Group Application Notes
 * 240 and 241 without enforcing every detail (e.g. auto-selection of inserted
 * data or per-view selection).
 */

/* Initialize an editing session for a colmap file.
   If 'reader' is null then a default colmap of the given size is created. */
ColMapState edit_colmap_init(EditColMap *edit_colmap, _Optional Reader *reader,
  int size, void (*redraw_entry_cb)(EditColMap *, int));

/* Destroy an editing session for a colmap file. */
void edit_colmap_destroy(EditColMap *edit_colmap);

/* Get the colmap file in an editing session */
ColMap *edit_colmap_get_colmap(EditColMap *edit_colmap);

/* Returns false if there is nothing to undo. */
bool editor_can_undo(Editor const *editor);

/* Returns false if there is nothing to redo. */
bool editor_can_redo(Editor const *editor);

/* Undo the previous editing operation.
   Returns false if unchanged. */
bool editor_undo(Editor const *editor);

/* Redo the previous editing operation.
   Returns false if unchanged. */
bool editor_redo(Editor const *editor);

/* Get the colmap file addressed by an editor */
ColMap *editor_get_colmap(Editor const *editor);

typedef void EditorRedrawSelectFn(Editor *editor, int pos);

/* Initialize an editor of a colmap file. */
void editor_init(Editor *editor, EditColMap *edit_colmap,
  _Optional EditorRedrawSelectFn *redraw_select_cb);

typedef enum {
  EditResult_Unchanged,
  EditResult_Changed,
  EditResult_NoMem,
} EditResult;

/* Change selected colours to a single value. */
EditResult editor_set_plain(Editor *editor, int colour);

/* Interpolate between start and end of the selected region. */
EditResult editor_interpolate(Editor *editor, PaletteEntry const palette[]);

/* Set colours from an array. */
EditResult editor_set_array(Editor *editor, int const *colours,
  int ncol, bool *is_valid);

/* Returns true if the specified colour is selected. */
bool editor_is_selected(Editor const *editor, int pos);

/* Select a range of colours. Returns false if unchanged. */
bool editor_select(Editor *editor, int start, int end);

/* Exclusively select a colour. Returns false if unchanged. */
bool editor_exc_select(Editor *editor, int pos);

/* Deselect a range of colours. Returns false if unchanged. */
bool editor_deselect(Editor *editor, int start, int end);

/* Returns true if any colours are selected. */
bool editor_has_selection(Editor const *editor);

/* Deselect all colours. Returns false if unchanged. */
bool editor_clear_selection(Editor *editor);

/* Get the number of selected colours. */
int editor_get_num_selected(Editor const *editor);

/* Get the next selected colour at a higher index than 'pos'.
   Returns -1 if none is selected beyond 'pos'. */
int editor_get_next_selected(Editor const *editor, int pos);

/* Get the lowest selected colour */
int editor_get_selected_colour(Editor const *editor);

#endif
