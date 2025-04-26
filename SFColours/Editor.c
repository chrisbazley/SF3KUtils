/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Editor back-end functions
 *  Copyright (C) 2020 Christopher Bazley
 */

#include <stdbool.h>
#include "stdlib.h"
#include <string.h>
#include <assert.h>

#include "Debug.h"
#include "Macros.h"
#include "Reader.h"
#include "PalEntry.h"
#include "LinkedList.h"

#include "Editor.h"
#include "ColMap.h"

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


enum
{
  NPixelColours = 256,
  InvalidColour = 0,
};

typedef struct {
  int pos;
  unsigned char old;
  unsigned char rep;
} EditSubrecord;


typedef struct EditRecord {
  LinkedListItem link;
  int size;
  EditSubrecord subrec[];
} EditRecord;

static bool set_and_redraw(EditColMap *const edit_colmap, int const pos,
  int const colour, _Optional EditRecord *const rec)

{
  assert(edit_colmap != NULL);
  assert(pos >= 0);
  assert(pos < ColMap_MaxSize);

  bool changed = false;

  int const old = colmap_get_colour(&edit_colmap->colmap, pos);
  if (old != colour)
  {
    if (rec)
    {
      EditSubrecord *const subrec = &rec->subrec[rec->size++];
      *subrec = (EditSubrecord){
        .old = old,
        .rep = colour,
        .pos = pos,
      };
    }
    colmap_set_colour(&edit_colmap->colmap, pos, colour);

    DEBUG_VERBOSEF("Redraw entry %d in file %p\n", pos, (void *)edit_colmap);
    edit_colmap->redraw_entry_cb(edit_colmap, pos);
    changed = true;
  }
  return changed;
}

static void dummy_redraw(EditColMap *edit_colmap, int pos)
{
  NOT_USED(edit_colmap);
  NOT_USED(pos);
}

static inline bool get_is_selected(Editor const *const editor, int const pos)
{
  assert(editor != NULL);
  assert(pos >= 0);
  assert(pos < colmap_get_size(editor_get_colmap(editor)));
  unsigned int const offset = (unsigned)pos / CHAR_BIT;
  unsigned int const mask = 1u << ((unsigned)pos % CHAR_BIT);
  return editor->selected[offset] & mask;
}

static _Optional LinkedListItem *get_redo_item(EditColMap *const edit_colmap)
{
  assert(edit_colmap != NULL);

  _Optional LinkedListItem *redo_item = NULL;
  if (edit_colmap->next_undo)
  {
    redo_item = linkedlist_get_next(&*edit_colmap->next_undo);
  }
  else
  {
    redo_item = linkedlist_get_head(&edit_colmap->undo_list);
  }
  return redo_item;
}

static bool destroy_record(LinkedList *const list, LinkedListItem *const item,
  void *const arg)
{
  NOT_USED(arg);
  EditRecord *const rec = CONTAINER_OF(item, EditRecord, link);
  DEBUGF("Discarding undo record %p\n", (void *)rec);
  linkedlist_remove(list, item);
  free(item);
  return false; /* continue */
}

static _Optional EditRecord *make_record(EditColMap *const edit_colmap, int const size)
{
  assert(edit_colmap != NULL);

  _Optional EditRecord *const rec = malloc(sizeof(*rec) + (sizeof(rec->subrec[0]) * size));
  if (rec)
  {
    *rec = (EditRecord){.size = 0};

    _Optional LinkedListItem *const redo_item = get_redo_item(edit_colmap);
    _Optional LinkedListItem *next = NULL;

    for (_Optional LinkedListItem *item = redo_item; item != NULL; item = next)
    {
      next = linkedlist_get_next(&*item);
      destroy_record(&edit_colmap->undo_list, &*item, edit_colmap);
    }

    linkedlist_insert(&edit_colmap->undo_list, edit_colmap->next_undo,
      &rec->link);

    DEBUGF("Created undo record %p\n", (void *)rec);
    edit_colmap->next_undo = &rec->link;
  }
  else
  {
    DEBUGF("Not enough memory for undo\n");
  }

  return rec;
}

ColMapState edit_colmap_init(EditColMap *const edit_colmap,
  _Optional Reader *const reader, int const size,
  void (*redraw_entry_cb)(EditColMap *, int))
{
  assert(edit_colmap != NULL);
  ColMapState state = ColMapState_OK;

  if (reader)
  {
    state = colmap_read_file(&edit_colmap->colmap, &*reader);
  }
  else
  {
    colmap_init(&edit_colmap->colmap, size);
  }

  edit_colmap->redraw_entry_cb = redraw_entry_cb ?
    &*redraw_entry_cb : dummy_redraw;

  linkedlist_init(&edit_colmap->undo_list);
  edit_colmap->next_undo = NULL;

  return state;
}

void edit_colmap_destroy(EditColMap *const edit_colmap)
{
  assert(edit_colmap != NULL);
  (void)linkedlist_for_each(&edit_colmap->undo_list, destroy_record, edit_colmap);
}

ColMap *edit_colmap_get_colmap(EditColMap *const edit_colmap)
{
  assert(edit_colmap != NULL);
  return &edit_colmap->colmap;
}

bool editor_can_undo(Editor const *const editor)
{
  assert(editor != NULL);
  EditColMap const *const edit_colmap = editor->edit_colmap;
  assert(edit_colmap != NULL);
  return edit_colmap->next_undo != NULL;
}

bool editor_can_redo(Editor const *const editor)
{
  assert(editor != NULL);
  EditColMap *const edit_colmap = editor->edit_colmap;
  assert(edit_colmap != NULL);
  return edit_colmap->next_undo !=
         linkedlist_get_tail(&edit_colmap->undo_list);
}

bool editor_undo(Editor const *const editor)
{
  assert(editor != NULL);
  if (!editor_can_undo(editor))
  {
    DEBUGF("Nothing to undo\n");
    return false;
  }

  EditColMap *const edit_colmap = editor->edit_colmap;
  assert(edit_colmap != NULL);
  EditRecord *const undo = CONTAINER_OF(edit_colmap->next_undo,
    EditRecord, link);

  edit_colmap->next_undo = linkedlist_get_prev(&undo->link);

  bool changed = false;
  int const size = undo->size;
  DEBUGF("Undoing %d changes\n", size);

  for (int index = 0; index < size; ++index)
  {
    if (set_and_redraw(edit_colmap, undo->subrec[index].pos,
      undo->subrec[index].old, NULL))
    {
      changed = true;
    }
  }
  return changed;
}

bool editor_redo(Editor const *const editor)
{
  assert(editor != NULL);
  if (!editor_can_redo(editor))
  {
    DEBUGF("Nothing to redo\n");
    return false;
  }

  EditColMap *const edit_colmap = editor->edit_colmap;
  assert(edit_colmap != NULL);
  _Optional LinkedListItem *const redo_item = get_redo_item(edit_colmap);
  assert(redo_item != NULL);
  EditRecord *const redo = CONTAINER_OF(redo_item, EditRecord, link);
  edit_colmap->next_undo = redo_item;

  bool changed = false;
  int const size = redo->size;
  DEBUGF("Redoing %d changes\n", size);

  for (int index = 0; index < size; ++index)
  {
    if (set_and_redraw(edit_colmap, redo->subrec[index].pos,
      redo->subrec[index].rep, NULL))
    {
      changed = true;
    }
  }

  return changed;
}

/* Force the given entry to be redrawn using the registered callback. */
static inline void redraw_select(Editor *const editor, int const pos)
{
  assert(editor != NULL);
  assert(pos >= 0);
  assert(pos < ColMap_MaxSize);
  DEBUGF("Redraw select %d in editor %p of file %p\n", pos, (void *)editor,
         (void *)editor->edit_colmap);

  editor->redraw_select_cb(editor, pos);
}

static void dummy_redraw_sel(Editor *editor, int pos)
{
  NOT_USED(editor);
  NOT_USED(pos);
}

void editor_init(Editor *const editor, EditColMap *const edit_colmap,
  _Optional EditorRedrawSelectFn *redraw_select_cb)
{
  assert(editor != NULL);
  assert(edit_colmap != NULL);

  editor->edit_colmap = edit_colmap;
  editor->num_selected = 0;
  memset(editor->selected, 0, sizeof(editor->selected));

  editor->redraw_select_cb = redraw_select_cb ?
    &*redraw_select_cb : dummy_redraw_sel;
}

ColMap *editor_get_colmap(Editor const *const editor)
{
  assert(editor != NULL);
  return edit_colmap_get_colmap(editor->edit_colmap);
}

bool editor_is_selected(Editor const *const editor, int const pos)
{
  bool const is_selected = get_is_selected(editor, pos);
  DEBUG_VERBOSEF("Colour %d %s selected\n", pos,
    is_selected ? "is" : "isn't");
  return is_selected;
}

bool editor_select(Editor *const editor, int const start, int const end)
{
  assert(editor != NULL);
  assert(start >= 0);
  assert(end >= start);
  int const num_cols = colmap_get_size(editor_get_colmap(editor));
  assert(end <= num_cols);

  bool changed = false;
  int num_selected = editor->num_selected;
  unsigned char *const selected = editor->selected;

  for (int pos = start; pos < end && num_selected < num_cols; pos ++)
  {
    unsigned int const offset = (unsigned)pos / CHAR_BIT;
    unsigned int const mask = 1u << ((unsigned)pos % CHAR_BIT);

    if (!(selected[offset] & mask))
    {
      DEBUG_VERBOSEF("Selecting bit 0x%x in byte 0x%x\n", mask, offset);
      selected[offset] |= mask;
      ++num_selected;
      changed = true;
      redraw_select(editor, pos);
    }
  }

  editor->num_selected = num_selected;
  return changed;
}

bool editor_deselect(Editor *const editor, int const start, int const end)
{
  assert(editor != NULL);
  assert(start >= 0);
  assert(end >= start);
  assert(end <= colmap_get_size(editor_get_colmap(editor)));

  bool changed = false;
  int num_selected = editor->num_selected;
  unsigned char *const selected = editor->selected;

  for (int pos = start; pos < end && num_selected > 0; pos ++)
  {
    unsigned int const offset = (unsigned)pos / CHAR_BIT;
    unsigned int const mask = 1u << ((unsigned)pos % CHAR_BIT);

    if (selected[offset] & mask)
    {
      DEBUG_VERBOSEF("Deselecting bit 0x%x in byte 0x%x\n", mask, offset);
      selected[offset] &= ~mask;
      --num_selected;
      changed = true;
      redraw_select(editor, pos);
    }
  }

  editor->num_selected = num_selected;
  return changed;
}

bool editor_exc_select(Editor *const editor, int const pos)
{
  int const num_cols = colmap_get_size(editor_get_colmap(editor));
  assert(pos >= 0);
  assert(pos < num_cols);
  bool changed = editor_deselect(editor, 0, pos);

  if (editor_select(editor, pos, pos + 1))
  {
    changed = true;
  }

  if (editor_deselect(editor, pos + 1, num_cols))
  {
    changed = true;
  }

  return changed;
}

bool editor_has_selection(Editor const *const editor)
{
  assert(editor != NULL);
  assert(editor->num_selected >= 0);
  assert(editor->num_selected <= colmap_get_size(editor_get_colmap(editor)));
  return editor->num_selected > 0;
}

bool editor_clear_selection(Editor *const editor)
{
  int const num_cols = colmap_get_size(editor_get_colmap(editor));
  return editor_deselect(editor, 0, num_cols);
}

int editor_get_selected_colour(Editor const *const editor)
{
  assert(editor != NULL);
  assert(editor->num_selected > 0);

  ColMap *const colmap = editor_get_colmap(editor);
  int const num_cols = colmap_get_size(colmap);

  for (int pos = 0; pos < num_cols; pos ++)
  {
    if (get_is_selected(editor, pos))
    {
      int const colour = colmap_get_colour(colmap, pos);
      DEBUGF("Selected colour is %d at %d\n", colour, pos);
      return colour;
    }
  }

  return 0;
}

int editor_get_num_selected(Editor const *const editor)
{
  assert(editor != NULL);
  assert(editor->num_selected >= 0);
  assert(editor->num_selected <= colmap_get_size(editor_get_colmap(editor)));
  return editor->num_selected;
}

int editor_get_next_selected(Editor const *const editor, int pos)
{
  assert(editor != NULL);
  ColMap *const colmap = editor_get_colmap(editor);
  int const num_cols = colmap_get_size(colmap);
  assert(pos < 0 || pos < num_cols);

  int match = -1;
  if (pos < 0)
  {
    pos = 0; /* Start search at first colour */
  }
  else
  {
    ++pos; /* Continue search at next colour */
  }

  for (; pos < num_cols; pos++)
  {
    if (get_is_selected(editor, pos))
    {
      match = pos;
      break;
    }
  }

  DEBUGF("Colour %d is the next selected after %d\n", match, pos);
  return match;
}

EditResult editor_set_plain(Editor *const editor, int const colour)
{
  assert(editor != NULL);

  int const num_to_set = editor->num_selected;
  _Optional EditRecord *const rec = make_record(editor->edit_colmap, num_to_set);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  EditResult changed = EditResult_Unchanged;
  ColMap *const colmap = editor_get_colmap(editor);
  int const num_cols = colmap_get_size(colmap);
  int num_found = 0;
  DEBUGF("Setting %d colours in file %p to plain %d\n",
         num_to_set, (void *)colmap, colour);

  for (int pos = 0; pos < num_cols && num_found < num_to_set; pos ++)
  {
    if (!get_is_selected(editor, pos))
    {
      continue;
    }

    if (set_and_redraw(editor->edit_colmap, pos, colour, &*rec))
    {
      changed = EditResult_Changed;
    }
  }

  return changed;
}

EditResult editor_interpolate(Editor *const editor,
  PaletteEntry const palette[])
{
  assert(editor != NULL);
  int const num_selected = editor->num_selected;

  _Optional EditRecord *const rec = make_record(editor->edit_colmap,
    num_selected >= 2 ? num_selected - 2 : 0);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  if (num_selected < 2)
  {
    DEBUGF("Too few (%d) to interpolate\n", num_selected);
    return EditResult_Unchanged;
  }

  int first = -1, last = -1, num_found = 0;
  ColMap *const colmap = editor_get_colmap(editor);
  int const num_cols = colmap_get_size(colmap);

  for (int pos = 0; pos < num_cols && num_found < num_selected; pos++)
  {
    if (!get_is_selected(editor, pos))
    {
      continue;
    }

    num_found ++;

    if (first < 0)
    {
      first = pos;
    }

    last = pos;
  }

  assert(first >= 0);
  assert(last >= 0);
  assert(first < last);

  int const steps = num_selected - 1;
  DEBUGF("Smoothing transitions between %d..%d (%d steps) in file %p\n",
         first, last, steps, (void *)colmap);

  PaletteEntry const
    first_BGR0 = palette[colmap_get_colour(colmap, first)],
    last_BGR0 = palette[colmap_get_colour(colmap, last)];

  int const r = PALETTE_GET_RED(first_BGR0);
  int const red_diff = PALETTE_GET_RED(last_BGR0) - r;
  float red_component = r;
  float const red_inc = (float)red_diff / steps;

  int const g = PALETTE_GET_GREEN(first_BGR0);
  int const green_diff = PALETTE_GET_GREEN(last_BGR0) - g;
  float green_component = g;
  float const green_inc = (float)green_diff / steps;

  int const b = PALETTE_GET_BLUE(first_BGR0);
  int const blue_diff = PALETTE_GET_BLUE(last_BGR0) - b;
  float blue_component = b;
  float const blue_inc = (float)blue_diff / steps;

  EditResult changed = EditResult_Unchanged;
  for (int pos = first + 1; pos < last; ++pos)
  {
    if (!get_is_selected(editor, pos))
    {
      continue;
    }

    /* Calculate transitional colour */
    red_component += red_inc;
    green_component += green_inc;
    blue_component += blue_inc;

    /* Find nearest to ideal colour in default mode 13 palette */
    int const nearest_colour = nearest_palette_entry_rgb(
      palette, NPixelColours, (int)(red_component + 0.5f),
      (int)(green_component + 0.5f), (int)(blue_component + 0.5f));

    if (set_and_redraw(editor->edit_colmap, pos, nearest_colour, &*rec))
    {
      changed = EditResult_Changed;
    }
  }

  return changed;
}

EditResult editor_set_array(Editor *const editor, int const *const colours,
  int const ncol, bool *const is_valid)
{
  assert(editor != NULL);
  assert(is_valid != NULL);
  assert(colours != NULL);

  *is_valid = true;

  int const num_to_set = LOWEST(editor->num_selected, ncol);
  _Optional EditRecord *const rec = make_record(editor->edit_colmap, num_to_set);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  EditResult changed = EditResult_Unchanged;
  int num_found = 0;
  ColMap *const colmap = editor_get_colmap(editor);
  int const num_cols = colmap_get_size(colmap);

  DEBUGF("Setting %d colours in file %p from array %p\n",
         num_to_set, (void *)colmap, (void *)colours);

  for (int pos = 0; pos < num_cols && num_found < num_to_set; pos++)
  {
    if (!get_is_selected(editor, pos))
    {
      continue;
    }

    int colour = colours[num_found++];
    if (colour < 0 || colour >= NPixelColours)
    {
      colour = InvalidColour;
      *is_valid = false;
    }

    if (set_and_redraw(editor->edit_colmap, pos, colour, &*rec))
    {
      changed = EditResult_Changed;
    }
  }

  return changed;
}
