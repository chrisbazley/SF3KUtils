/*
 *  SFColours test: Editor back-end functions
 *  Copyright (C) 2020 Christopher Bazley
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

/* ANSI library files */
#include <stdio.h>
#include <string.h>
#include <limits.h>

/* My library files */
#include "Macros.h"
#include "Debug.h"
#include "PalEntry.h"

/* Local headers */
#include "Tests.h"
#include "Editor.h"

#include "FORTIFY.h"

enum {
  DefaultPixelColour = 0,
  NumColours = 256,
  MinColour = 0,
  MaxColour = NumColours - 1,
  SelectStart = 3,
  SelectEnd = 15,
  SelectInterval = 2,
  Colour = 7,
  NSelect = SelectEnd - SelectStart,
  NCallbacks = ColMap_MaxSize * 3,
  Marker = 0x3d,
  MinSize = 256,
  FileSizeStep = ColMap_MaxSize - MinSize,
  FortifyAllocationLimit = 2048,
  NUndoRedo = 2,
};

static void pal_init(PaletteEntry (*const pal)[NumColours])
{
  for (int c = 0; c < NumColours; ++c)
  {
    (*pal)[c] = make_palette_entry(
      c, (3 + c) % NumColours, MaxColour - c);
  }
}

static int get_colour(int i)
{
  i %= NumColours;
  i = i % 2 ? i : MaxColour - i;
  if (i == Marker)
  {
    i = 0;
  }
  return i;
}

static int entry_count;
static struct {
  EditColMap *edit_colmap;
  int pos;
} entry_args[NCallbacks];

static void redraw_entry_cb(EditColMap *const edit_colmap, int const pos)
{
  assert(edit_colmap != NULL);
  assert(pos >= 0);
  assert(pos < colmap_get_size(edit_colmap_get_colmap(edit_colmap)));
  assert(entry_count >= 0);
  assert(entry_count < NCallbacks);
  entry_args[entry_count].edit_colmap = edit_colmap;
  entry_args[entry_count].pos = pos;
  entry_count++;
}

static void check_redraw_entry(int const n, EditColMap *const edit_colmap,
  int const pos)
{
  assert(entry_count > n);

  DEBUGF("Colour redraw %d: %p, %d\n", n,
    (void *)entry_args[n].edit_colmap, entry_args[n].pos);

  assert(entry_args[n].edit_colmap == edit_colmap);
  assert(entry_args[n].pos == pos);
}

static int select_count;
static struct {
  Editor *editor;
  int pos;
} select_args[NCallbacks];

static void redraw_select_cb(Editor *const editor, int const pos)
{
  assert(editor != NULL);
  assert(pos >= 0);
  assert(pos < colmap_get_size(editor_get_colmap(editor)));
  assert(select_count >= 0);
  assert(select_count < NCallbacks);
  select_args[select_count].editor = editor;
  select_args[select_count].pos = pos;
  select_count++;
}

static void check_redraw_select(int const n, Editor *const editor,
  int const pos)
{
  assert(select_count > n);

  DEBUGF("Selection redraw %d: %p, %d\n", n,
    (void *)select_args[n].editor, select_args[n].pos);

  assert(select_args[n].editor == editor);
  assert(select_args[n].pos == pos);
}

static void test1(void)
{
  /* Initialise session */
  for (int s = 0; s <= ColMap_MaxSize; s += FileSizeStep)
  {
    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, s, redraw_entry_cb);
    ColMap *const colmap = edit_colmap_get_colmap(&edit_colmap);
    for (int i = 0; i < s; ++i)
    {
      assert(colmap_get_colour(colmap, i) == DefaultPixelColour);
    }

    Editor editor;
    editor_init(&editor, &edit_colmap, NULL);

    assert(!editor_can_undo(&editor));
    assert(!editor_can_redo(&editor));

    assert(!editor_undo(&editor));
    assert(!editor_redo(&editor));

    edit_colmap_destroy(&edit_colmap);
  }
}

static void test2(void)
{
  /* Initialise editors */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_colmap, NULL);
  editor_init(&editor2, &edit_colmap, NULL);

  assert(edit_colmap_get_colmap(&edit_colmap) == editor_get_colmap(&editor));
  assert(editor_get_colmap(&editor) == editor_get_colmap(&editor2));

  edit_colmap_destroy(&edit_colmap);
}

static void test3(void)
{
  /* Make selection */
  for (int s = MinSize; s <= ColMap_MaxSize; s += FileSizeStep)
  {
    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, s, redraw_entry_cb);

    Editor editor, editor2;
    editor_init(&editor, &edit_colmap, redraw_select_cb);
    editor_init(&editor2, &edit_colmap, redraw_select_cb);

    assert(!editor_has_selection(&editor));
    assert(!editor_has_selection(&editor2));

    select_count = entry_count = 0;
    assert(!editor_select(&editor, SelectStart, SelectStart));
    assert(select_count == 0);
    assert(entry_count == 0);

    assert(!editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == 0);

    for (int i = 0; i < s; ++i)
    {
      assert(!editor_is_selected(&editor, i));
    }

    assert(editor_select(&editor, SelectStart, SelectEnd));
    assert(select_count == NSelect);
    assert(entry_count == 0);

    for (int i = SelectStart; i < SelectEnd; ++i)
    {
      check_redraw_select(i - SelectStart, &editor, i);
      assert(editor_is_selected(&editor, i));
    }

    for (int i = 0; i < SelectStart; ++i)
    {
      assert(!editor_is_selected(&editor, i));
    }

    for (int i = SelectEnd; i < s; ++i)
    {
      assert(!editor_is_selected(&editor, i));
    }

    assert(editor_has_selection(&editor));
    assert(!editor_has_selection(&editor2));
    assert(editor_get_num_selected(&editor) == NSelect);
    assert(!editor_can_undo(&editor));

    for (int i = SelectStart; i <= SelectEnd; ++i)
    {
      assert(!editor_select(&editor, i, SelectEnd));
    }

    assert(select_count == NSelect);
    assert(editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == NSelect);

    edit_colmap_destroy(&edit_colmap);
  }
}

static void test4(void)
{
  /* Deselection */
  for (int s = MinSize; s <= ColMap_MaxSize; s += FileSizeStep)
  {
    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, s, redraw_entry_cb);

    Editor editor, editor2;
    editor_init(&editor, &edit_colmap, redraw_select_cb);
    editor_init(&editor2, &edit_colmap, redraw_select_cb);

    select_count = entry_count = 0;
    assert(!editor_deselect(&editor, SelectStart, SelectEnd));
    assert(select_count == 0);
    assert(entry_count == 0);

    assert(editor_select(&editor, 0, s));
    assert(editor_select(&editor2, 0, s));

    select_count = entry_count = 0;
    assert(!editor_deselect(&editor, SelectStart, SelectStart));
    assert(select_count == 0);
    assert(entry_count == 0);

    assert(editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == s);

    for (int i = 0; i < s; ++i)
    {
      assert(editor_is_selected(&editor, i));
    }

    assert(editor_deselect(&editor, SelectStart, SelectEnd));

    assert(select_count == NSelect);
    assert(entry_count == 0);

    for (int i = SelectStart; i < SelectEnd; ++i)
    {
      check_redraw_select(i - SelectStart, &editor, i);
      assert(!editor_is_selected(&editor, i));
    }

    for (int i = 0; i < SelectStart; ++i)
    {
      assert(editor_is_selected(&editor, i));
    }

    for (int i = SelectEnd; i < s; ++i)
    {
      assert(editor_is_selected(&editor, i));
    }

    assert(editor_has_selection(&editor));
    assert(editor_has_selection(&editor2));
    assert(editor_get_num_selected(&editor) == s - NSelect);
    assert(!editor_can_undo(&editor));

    for (int i = SelectStart; i <= SelectEnd; ++i)
    {
      assert(!editor_deselect(&editor, i, SelectEnd));
    }

    assert(select_count == NSelect);
    assert(editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == s - NSelect);

    assert(editor_deselect(&editor, 0, SelectStart));
    assert(select_count == SelectEnd);
    assert(editor_has_selection(&editor) == (s != SelectEnd));
    assert(editor_get_num_selected(&editor) == s - SelectEnd);

    assert(editor_deselect(&editor, SelectEnd, s) == (s != SelectEnd));
    assert(select_count == s);
    assert(!editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == 0);

    assert(editor_has_selection(&editor2));

    edit_colmap_destroy(&edit_colmap);
  }
}

static void test5(void)
{
  /* Clear selection */
  for (int s = MinSize; s <= ColMap_MaxSize; s += FileSizeStep)
  {
    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, s, redraw_entry_cb);

    Editor editor, editor2;
    editor_init(&editor, &edit_colmap, redraw_select_cb);
    editor_init(&editor2, &edit_colmap, redraw_select_cb);

    select_count = entry_count = 0;
    assert(!editor_clear_selection(&editor));
    assert(select_count == 0);
    assert(entry_count == 0);

    assert(editor_select(&editor, 0, s));
    assert(editor_select(&editor2, 0, s));

    select_count = entry_count = 0;
    assert(editor_clear_selection(&editor));
    assert(select_count == s);
    assert(entry_count == 0);

    for (int i = 0; i < s; ++i)
    {
      check_redraw_select(i, &editor, i);
      assert(!editor_is_selected(&editor, i));
    }

    assert(!editor_has_selection(&editor));
    assert(editor_has_selection(&editor2));
    assert(editor_get_num_selected(&editor) == 0);
    assert(!editor_can_undo(&editor));

    assert(!editor_clear_selection(&editor));

    edit_colmap_destroy(&edit_colmap);
  }
}

static void test6(void)
{
  /* Exclusive select */
  for (int s = MinSize; s <= ColMap_MaxSize; s += FileSizeStep)
  {
    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, s, redraw_entry_cb);

    Editor editor, editor2;
    editor_init(&editor, &edit_colmap, redraw_select_cb);
    editor_init(&editor2, &edit_colmap, redraw_select_cb);

    select_count = entry_count = 0;
    assert(editor_exc_select(&editor, SelectStart));
    assert(select_count == 1);
    assert(entry_count == 0);

    check_redraw_select(0, &editor, SelectStart);

    for (int i = 0; i < s; ++i)
    {
      assert(editor_is_selected(&editor, i) == (i == SelectStart));
    }

    assert(editor_has_selection(&editor));
    assert(!editor_has_selection(&editor2));
    assert(editor_get_num_selected(&editor) == 1);

    assert(!editor_exc_select(&editor, SelectStart));
    assert(select_count == 1);
    assert(entry_count == 0);
    assert(editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == 1);

    assert(editor_select(&editor, 0, SelectStart));

    select_count = entry_count = 0;
    assert(editor_exc_select(&editor, SelectStart));
    assert(select_count == SelectStart);
    assert(entry_count == 0);

    for (int i = 0; i < SelectStart; ++i)
    {
      check_redraw_select(i, &editor, i);
    }

    for (int i = 0; i < s; ++i)
    {
      assert(editor_is_selected(&editor, i) == (i == SelectStart));
    }

    assert(editor_has_selection(&editor));
    assert(editor_get_num_selected(&editor) == 1);
    assert(!editor_can_undo(&editor));

    if (s != SelectStart + 1)
    {
      assert(editor_select(&editor, SelectStart + 1, s));

      select_count = entry_count = 0;
      assert(editor_exc_select(&editor, SelectStart));
      assert(select_count == s - SelectStart - 1);
      assert(entry_count == 0);

      for (int i = SelectStart + 1; i < s; ++i)
      {
        check_redraw_select(i - SelectStart - 1, &editor, i);
      }

      for (int i = 0; i < s; ++i)
      {
        assert(editor_is_selected(&editor, i) == (i == SelectStart));
      }

      assert(editor_has_selection(&editor));
      assert(editor_get_num_selected(&editor) == 1);
    }

    edit_colmap_destroy(&edit_colmap);
  }
}

static void test7(void)
{
  /* Redraw selection (no callback) */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor;
  editor_init(&editor, &edit_colmap, NULL);
  assert(editor_select(&editor, SelectStart, SelectEnd));

  edit_colmap_destroy(&edit_colmap);
}

static void test8(void)
{
  /* Redraw selection */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_colmap, redraw_select_cb);
  editor_init(&editor2, &edit_colmap, redraw_select_cb);

  assert(editor_select(&editor, SelectStart, SelectEnd));

  assert(entry_count == 0);
  assert(select_count == SelectEnd - SelectStart);

  for (int i = SelectStart; i < SelectEnd; ++i)
  {
    check_redraw_select(i - SelectStart, &editor, i);
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test9(void)
{
  /* Redraw colours (no callback) */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor;
  editor_init(&editor, &edit_colmap, NULL);
  assert(editor_select(&editor, SelectStart, SelectEnd));
  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  edit_colmap_destroy(&edit_colmap);
}

static void test10(void)
{
  /* Redraw colours */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_colmap, redraw_select_cb);
  editor_init(&editor2, &edit_colmap, redraw_select_cb);

  assert(editor_select(&editor, SelectStart, SelectEnd));

  select_count = entry_count = 0;
  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  assert(select_count == 0);
  assert(entry_count == SelectEnd - SelectStart);

  for (int i = SelectStart; i < SelectEnd; ++i)
  {
    check_redraw_entry(i - SelectStart, &edit_colmap, i);
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test11(void)
{
  /* Get selected colour */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_colmap, redraw_select_cb);
  editor_init(&editor2, &edit_colmap, redraw_select_cb);

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    select_count = entry_count = 0;

    assert(editor_exc_select(&editor, pos));
    editor_set_plain(&editor, get_colour(pos));
  }

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    select_count = 0;

    editor_select(&editor, pos, pos + 1);
    assert(editor_get_selected_colour(&editor) == get_colour(0));

    assert(editor_select(&editor2, pos, pos + 1));
    assert(editor_get_selected_colour(&editor2) == get_colour(0));
  }

  select_count = 0;
  assert(editor_clear_selection(&editor));
  assert(editor_clear_selection(&editor2));

  for (int pos = ColMap_MaxSize - 1; pos >= 0; --pos)
  {
    select_count = 0;

    assert(editor_select(&editor, pos, pos + 1));
    assert(editor_get_selected_colour(&editor) == get_colour(pos));

    assert(editor_select(&editor2, pos, pos + 1));
    assert(editor_get_selected_colour(&editor2) == get_colour(pos));
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test12(void)
{
  /* Set plain */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);
  ColMap *const colmap = edit_colmap_get_colmap(&edit_colmap);

  Editor editor;
  editor_init(&editor, &edit_colmap, redraw_select_cb);

  assert(editor_set_plain(&editor, Marker) == EditResult_Unchanged);

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    assert(colmap_get_colour(colmap, pos) == DefaultPixelColour);
  }

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));
  assert(editor_can_redo(&editor));
  assert(!editor_redo(&editor));
  assert(select_count == 0);
  assert(entry_count == 0);

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    select_count = 0;
    assert(editor_exc_select(&editor, pos));
    editor_set_plain(&editor, get_colour(pos));
  }
  assert(editor_clear_selection(&editor));

  int exp_count = 0;
  for (int pos = 0; pos < ColMap_MaxSize; pos += SelectInterval)
  {
    select_count = 0;
    editor_select(&editor, pos, pos + 1);
    exp_count++;
  }

  select_count = entry_count = 0;

  unsigned long limit;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    EditResult const r = editor_set_plain(&editor, Marker);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);

    if (r != EditResult_NoMem)
    {
      assert(r == EditResult_Changed);
      break;
    }

    assert(entry_count == 0);

    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      assert(colmap_get_colour(colmap, pos) == get_colour(pos));
    }
  }
  assert(limit != FortifyAllocationLimit);

  assert(entry_count == exp_count);

  entry_count = 0;
  assert(editor_set_plain(&editor, Marker) == EditResult_Unchanged);

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));
  assert(editor_can_redo(&editor));
  assert(!editor_redo(&editor));
  assert(!editor_undo(&editor));
  assert(select_count == 0);
  assert(entry_count == 0);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      int const expected = pos % SelectInterval ? get_colour(pos) : Marker;
      assert(colmap_get_colour(colmap, pos) == expected);
    }

    assert(editor_can_undo(&editor));
    assert(editor_undo(&editor));

    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      assert(colmap_get_colour(colmap, pos) == get_colour(pos));
    }

    assert(editor_can_redo(&editor));
    assert(editor_redo(&editor));
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test13(void)
{
  /* Interpolate selection */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);
  ColMap *const colmap = edit_colmap_get_colmap(&edit_colmap);

  Editor editor;
  editor_init(&editor, &edit_colmap, redraw_select_cb);

  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  assert(editor_interpolate(&editor, palette) == EditResult_Unchanged);

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    assert(colmap_get_colour(colmap, pos) == DefaultPixelColour);
  }

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));
  assert(editor_can_redo(&editor));
  assert(!editor_redo(&editor));
  assert(select_count == 0);
  assert(entry_count == 0);

  for (int pos = 0; pos < ColMap_MaxSize; ++pos)
  {
    select_count = 0;
    editor_exc_select(&editor, pos);
    editor_set_plain(&editor, get_colour(pos));
  }

  int const first = (SelectStart / SelectInterval) * SelectInterval;
  int const last = (SelectEnd / SelectInterval) * SelectInterval;
  int const steps = (last / SelectInterval) - (first / SelectInterval);
  DEBUGF("params %d,%d,%d\n", first, last, steps);

  editor_exc_select(&editor, first);
  editor_set_plain(&editor, Colour);

  editor_exc_select(&editor, last);
  editor_set_plain(&editor, Colour + steps);

  editor_clear_selection(&editor);
  for (int pos = first; pos < SelectEnd; ++pos)
  {
    if (!(pos % SelectInterval))
    {
      editor_select(&editor, pos, pos + 1);
    }
  }

  select_count = entry_count = 0;

  unsigned long limit;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    EditResult const r = editor_interpolate(&editor, palette);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);

    if (r != EditResult_NoMem)
    {
      assert(r == EditResult_Changed);
      break;
    }

    assert(entry_count == 0);

    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      int const col = colmap_get_colour(colmap, pos);
      if (pos == first)
      {
        assert(col == Colour);
      }
      else if (pos == last)
      {
        assert(col == Colour + steps);
      }
      else
      {
        assert(col == get_colour(pos));
      }
    }
  }
  assert(limit != FortifyAllocationLimit);

  assert(entry_count == steps - 1);

  entry_count = 0;
  assert(editor_interpolate(&editor, palette) == EditResult_Unchanged);

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));
  assert(editor_can_redo(&editor));
  assert(!editor_redo(&editor));
  assert(!editor_undo(&editor));
  assert(select_count == 0);
  assert(entry_count == 0);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    int expected = Colour;
    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      int const col = colmap_get_colour(colmap, pos);
      if ((pos % SelectInterval) || pos < first || pos > last)
      {
        assert(col == get_colour(pos));
      }
      else
      {
        DEBUGF("%d: Expect %d, got %d\n", pos, expected, col);
        assert(col == expected);
        expected++;
      }
    }

    assert(editor_can_undo(&editor));
    assert(editor_undo(&editor));

    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      int const col = colmap_get_colour(colmap, pos);
      if (pos == first)
      {
        assert(col == Colour);
      }
      else if (pos == last)
      {
        assert(col == Colour + steps);
      }
      else
      {
        assert(col == get_colour(pos));
      }
    }

    assert(editor_can_redo(&editor));
    assert(editor_redo(&editor));
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test14(void)
{
  /* Set array */
  int array[ColMap_MaxSize];
  for (size_t i = 0; i < ARRAY_SIZE(array); ++i)
  {
    array[i] = get_colour(i);
  }

  int sizes[] = {0, 1, ColMap_MaxSize / SelectInterval, ARRAY_SIZE(array)};
  for (size_t size_index = 0; size_index < ARRAY_SIZE(sizes); ++size_index)
  {
    DEBUGF("Array size is %d\n", sizes[size_index]);

    EditColMap edit_colmap;
    edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);
    ColMap *const colmap = edit_colmap_get_colmap(&edit_colmap);

    Editor editor;
    editor_init(&editor, &edit_colmap, redraw_select_cb);

    bool is_valid = false;
    select_count = entry_count = 0;

    assert(editor_set_array(&editor, array, sizes[size_index], &is_valid) ==
      EditResult_Unchanged);

    assert(is_valid);
    for (int pos = 0; pos < ColMap_MaxSize; ++pos)
    {
      assert(colmap_get_colour(colmap, pos) == DefaultPixelColour);
    }

    assert(editor_can_undo(&editor));
    assert(!editor_undo(&editor));
    assert(editor_can_redo(&editor));
    assert(!editor_redo(&editor));
    assert(!editor_undo(&editor));
    assert(select_count == 0);
    assert(entry_count == 0);

    editor_select(&editor, 0, ColMap_MaxSize);
    editor_set_plain(&editor, Marker);

    int exp_count = 0;
    editor_clear_selection(&editor);
    for (int pos = 0; pos < ColMap_MaxSize; pos += SelectInterval)
    {
      select_count = 0;
      editor_select(&editor, pos, pos + 1);
      exp_count++;
    }
    assert(exp_count == ColMap_MaxSize / SelectInterval);

    entry_count = select_count = 0;

    unsigned long limit;
    for (limit = 0; limit < FortifyAllocationLimit; ++limit)
    {
      Fortify_SetNumAllocationsLimit(limit);
      bool is_valid = false;
      EditResult const r = editor_set_array(&editor, array,
        sizes[size_index], &is_valid);

      Fortify_SetNumAllocationsLimit(ULONG_MAX);
      assert(select_count == 0);
      assert(is_valid);

      if (r != EditResult_NoMem)
      {
        if (sizes[size_index] != 0)
        {
          assert(r == EditResult_Changed);
        }
        else
        {
          assert(r == EditResult_Unchanged);
        }
        break;
      }

      assert(entry_count == 0);

      for (int i = 0; i < ColMap_MaxSize; ++i)
      {
        assert(colmap_get_colour(colmap, i) == Marker);
      }
    }
    assert(limit != FortifyAllocationLimit);

    assert(entry_count == LOWEST(sizes[size_index], exp_count));

    entry_count = 0;
    is_valid = false;
    assert(editor_set_array(&editor, array, sizes[size_index], &is_valid) ==
      EditResult_Unchanged);

    assert(is_valid);

    assert(editor_can_undo(&editor));
    assert(!editor_undo(&editor));
    assert(editor_can_redo(&editor));
    assert(!editor_redo(&editor));
    assert(!editor_undo(&editor));
    assert(select_count == 0);
    assert(entry_count == 0);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      for (int pos = 0; pos < ColMap_MaxSize; ++pos)
      {
        int const col = colmap_get_colour(colmap, pos);
        int const src_index = pos / SelectInterval;
        if ((pos % SelectInterval) || (src_index >= sizes[size_index]))
        {
          assert(col == Marker);
        }
        else
        {
          assert(col == get_colour(src_index));
        }
      }

      assert(editor_can_undo(&editor));
      assert(editor_undo(&editor) ==
             (sizes[size_index] != 0));

      for (int pos = 0; pos < ColMap_MaxSize; ++pos)
      {
        assert(colmap_get_colour(colmap, pos) == Marker);
      }

      assert(editor_can_redo(&editor));
      assert(editor_redo(&editor) ==
             (sizes[size_index] != 0));
    }

    edit_colmap_destroy(&edit_colmap);
  }

  for (int i = 0; i < ColMap_MaxSize; ++i)
  {
    assert(array[i] == get_colour(i));
  }
}

static void test15(void)
{
  /* Set invalid */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);
  ColMap *const colmap = edit_colmap_get_colmap(&edit_colmap);

  Editor editor;
  editor_init(&editor, &edit_colmap, redraw_select_cb);

  select_count = 0;
  editor_select(&editor, 0, ColMap_MaxSize);
  editor_set_plain(&editor, Marker);

  editor_clear_selection(&editor);
  int array[] = {-1, 3, 0, 256, 43};
  editor_select(&editor, SelectStart, SelectStart + ARRAY_SIZE(array));

  bool is_valid = false;
  select_count = entry_count = 0;
  assert(editor_set_array(&editor, array, ARRAY_SIZE(array), &is_valid) ==
    EditResult_Changed);

  assert(select_count == 0);
  assert(entry_count == ARRAY_SIZE(array));
  assert(!is_valid);

  for (int i = 0; i < ColMap_MaxSize; ++i)
  {
    int const col = colmap_get_colour(colmap, i);

    if (i >= SelectStart && i < SelectStart + (int)ARRAY_SIZE(array))
    {
      int const src_index = i - SelectStart;
      if (array[src_index] >= MinColour && array[src_index] <= MaxColour)
      {
        assert(col == array[src_index]);
      }
      else
      {
        assert(col == DefaultPixelColour);
      }
    }
    else
    {
        assert(col == Marker);
    }
  }

  edit_colmap_destroy(&edit_colmap);
}

static void test16(void)
{
  /* Get next selected */
  EditColMap edit_colmap;
  edit_colmap_init(&edit_colmap, NULL, ColMap_MaxSize, redraw_entry_cb);

  Editor editor;
  editor_init(&editor, &edit_colmap, redraw_select_cb);

  assert(editor_get_next_selected(&editor, INT_MIN) == -1);
  assert(editor_get_next_selected(&editor, -1) == -1);
  assert(editor_get_next_selected(&editor, 0) == -1);

  assert(entry_count == 0);
  assert(select_count == 0);

  editor_select(&editor, SelectStart, SelectEnd);
  editor_select(&editor, ColMap_MaxSize - 1, ColMap_MaxSize);
  entry_count = select_count = 0;

  assert(editor_get_next_selected(&editor, INT_MIN) == SelectStart);
  assert(editor_get_next_selected(&editor, -1) == SelectStart);
  assert(editor_get_next_selected(&editor, 0) == SelectStart);

  for (int pos = SelectStart; pos < SelectEnd - 1; ++pos)
  {
    assert(editor_get_next_selected(&editor, pos) == pos + 1);
  }

  assert(editor_get_next_selected(&editor, SelectEnd - 1) ==
    ColMap_MaxSize - 1);
  assert(editor_get_next_selected(&editor, ColMap_MaxSize - 1) == -1);

  assert(entry_count == 0);
  assert(select_count == 0);

  edit_colmap_destroy(&edit_colmap);
}

void Editor_tests(void)
{
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  unit_tests[] =
  {
    { "Initialise session", test1 },
    { "Initialise editors", test2 },
    { "Make selection", test3 },
    { "Deselect", test4 },
    { "Clear selection", test5 },
    { "Exclusive select", test6 },
    { "Redraw selection (no callback)", test7 },
    { "Redraw selection", test8 },
    { "Redraw colours (no callback)", test9 },
    { "Redraw colours", test10 },
    { "Get selected colour", test11 },
    { "Set plain", test12 },
    { "Interpolate selection", test13 },
    { "Set array", test14 },
    { "Set invalid", test15 },
    { "Get next selected", test16 },
  };

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); ++count)
  {
    DEBUGF("Test %zu/%zu : %s\n",
           1 + count,
           ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);

    select_count = 0;
    entry_count = 0;
    Fortify_EnterScope();
    unit_tests[count].test_func();
    Fortify_LeaveScope();
  }
}
