/*
 *  SFSkyEdit test: Editor back-end functions
 *  Copyright (C) 2019 Christopher Bazley
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
  DefaultStarsHeight = 0,
  DefaultRenderOffset = 0,
  NumColours = 256,
  SelectStart = 15,
  SelectEnd = 3,
  BufferOverrun = 2,
  Colour = 54,
  StartCol = 170,
  InsertPos = 9,
  MaxInsertLen = 9,
  BlockSize = 3,
  NBlocks = (NColourBands + BlockSize - 1) / BlockSize,
  BlockColourGap = BlockSize + (BlockSize / 2),
  NCallbacks = NBlocks * NBlocks,
  Marker = 0x3d,
  NBlocksToReplace = 2,
  RenderOffset = 979,
  StarsHeight = -999,
  FortifyAllocationLimit = 2048,
  NUndoRedo = 2,
  NSmoothBlocks = 3,
};

enum {
  Copy_Destination,
  Copy_Source,
  Copy_Count
};

enum {
  Editor_Destination,
  Editor_High,
  Editor_Middle,
  Editor_Low,
  Editor_Count
};

static void check_select(Editor *const editor, int const a, int const b)
{
  int start = INT_MIN, end = INT_MIN;
  editor_get_selection_range(editor, &start, &end);
  assert(start == LOWEST(a, b));
  assert(end == HIGHEST(a, b));
}

static void check_caret(Editor *const editor, int const pos)
{
  assert(editor_get_caret_pos(editor) == pos);
  check_select(editor, pos, pos);
}

static int select_count;
static struct {
  Editor *editor;
  int old_low;
  int old_high;
  int new_low;
  int new_high;
} select_args[NCallbacks];

static void redraw_select_cb(Editor *const editor, int const old_low,
  int const old_high, int const new_low, int const new_high)
{
  assert(editor != NULL);
  assert(old_low >= 0);
  assert(old_low <= old_high);
  assert(old_high <= NColourBands);
  assert(new_low >= 0);
  assert(new_low <= new_high);
  assert(new_high <= NColourBands);
  assert(new_low != old_low || new_high != old_high);
  assert(select_count >= 0);
  assert(select_count < NCallbacks);

  DEBUGF("Selection redraw %d: %p, %d..%d to %d..%d\n", select_count,
    (void *)editor, old_low, old_high, new_low, new_high);

  select_args[select_count].editor = editor;
  select_args[select_count].old_low = old_low;
  select_args[select_count].old_high = old_high;
  select_args[select_count].new_low = new_low;
  select_args[select_count].new_high = new_high;
  select_count++;
}

static void check_redraw_select(int const n, Editor *const editor,
  int const old_start, int const old_end,
  int const new_start, int const new_end)
{
  assert(select_count > n);

  assert(select_args[n].editor == editor);
  assert(select_args[n].old_low == LOWEST(old_start, old_end));
  assert(select_args[n].old_high == HIGHEST(old_start, old_end));
  assert(select_args[n].new_low == LOWEST(new_start, new_end));
  assert(select_args[n].new_high == HIGHEST(new_start, new_end));
}

static int bands_count;
static struct {
  EditSky *edit_sky;
  int start;
  int end;
} bands_args[NCallbacks];

static void redraw_bands_cb(EditSky *const edit_sky, int start, int end)
{
  assert(edit_sky != NULL);
  assert(start >= 0);
  assert(start < end);
  assert(end <= NColourBands);
  assert(bands_count >= 0);
  assert(bands_count < NCallbacks);

  DEBUGF("Colours redraw %d: %p, %d..%d\n", bands_count,
    (void *)edit_sky, start, end);

  bands_args[bands_count].edit_sky = edit_sky;
  bands_args[bands_count].start = start;
  bands_args[bands_count++].end = end;
}

static void check_redraw_bands(int const n, EditSky *const edit_sky,
  int const start, int const end)
{
  assert(bands_count > n);
  assert(bands_args[n].edit_sky == edit_sky);
  assert(bands_args[n].start == LOWEST(start, end));
  assert(bands_args[n].end == HIGHEST(start, end));
}

static int render_offset_count;
static struct {
  EditSky *edit_sky;
} render_offset_args[NCallbacks];

static void redraw_render_offset_cb(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  assert(render_offset_count >= 0);
  assert(render_offset_count < NCallbacks);

  DEBUGF("Render offset redraw %d: %p\n", render_offset_count, (void *)edit_sky);
  render_offset_args[render_offset_count++].edit_sky = edit_sky;
}

static void check_redraw_render_offset(int const n, EditSky *const edit_sky)
{
  assert(render_offset_count > n);
  assert(render_offset_args[n].edit_sky == edit_sky);
}

static int stars_height_count;
static struct {
  EditSky *edit_sky;
} stars_height_args[NCallbacks];

static void redraw_stars_height_cb(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  assert(stars_height_count >= 0);
  assert(stars_height_count < NCallbacks);

  DEBUGF("Stars height redraw %d: %p\n", stars_height_count, (void *)edit_sky);
  stars_height_args[stars_height_count++].edit_sky = edit_sky;
}

static void check_redraw_stars_height(int const n, EditSky *const edit_sky)
{
  assert(stars_height_count > n);
  assert(stars_height_args[n].edit_sky == edit_sky);
}

static void set_plain_blocks(EditSky *const edit_sky, Editor *const editor)
{
  for (int n = 0; n < NBlocks; ++n)
  {
    int const cpos = n * BlockSize;
    int const send = (n + 1) * BlockSize;
    editor_set_caret_pos(editor, cpos);
    editor_set_selection_end(editor, send);

    select_count = bands_count = 0;

    if (n > 0) {
      assert(editor_set_plain(editor, n * BlockColourGap) == EditResult_Changed);
    } else {
      assert(editor_set_plain(editor, 0) == EditResult_Unchanged);
    }

    assert(select_count == 0);
    if (n > 0)
    {
      assert(bands_count >= 1);
      check_redraw_bands(bands_count-1, edit_sky, cpos, send);
    }
    else
    {
      assert(bands_count == 0);
    }

    check_select(editor, cpos, send);
  }
}

static void get_all(Editor *const editor, int (*dst)[NColourBands])
{
  Sky *const sky = editor_get_sky(editor);
  for (size_t pos = 0; pos < ARRAY_SIZE(*dst); ++pos) {
    (*dst)[pos] = sky_get_colour(sky, pos);
  }
}

static void check_plain_blocks(Editor *const editor,
  int const del, int const dsize, int const ins, int const isize)
{
  int dst[NColourBands];
  get_all(editor, &dst);

  for (int i = 0; i < NBlocks * BlockSize && i < NColourBands; ++i)
  {
    if (i == del && dsize > 0) {
      DEBUGF("Skip %d deleted colours at %d\n", dsize, i);
      i += dsize - 1;
      continue;
    }
    int adj = i;
    if (i >= del)
    {
      adj -= dsize; /* skip the deleted colours */
    }
    if (i >= ins)
    {
      adj += isize; /* skip the inserted colours */
      if (adj >= NColourBands) break;
    }
    int const expect = (i / BlockSize) * BlockColourGap;
    DEBUGF("%d (%d): %d (expect %d)\n", adj, i, dst[adj], expect);
    assert(dst[adj] == expect);
  }
}

static void check_one_block(Editor *const editor, int const cpos,
  int const isize, int (* const getter)(int))
{
  int dst[NColourBands];
  get_all(editor, &dst);

  for (int i = cpos; i < cpos + isize && i < NColourBands; ++i)
  {
    DEBUGF("%d: %d\n", i, dst[i]);
    assert(dst[i] == getter(i - cpos));
  }
}

static void check_plain_blocks_after_move(Editor *const editor,
  int const ins, int const del, int const isize, int (* const getter)(int))
{
  check_one_block(editor, del < ins ? ins - isize : ins, isize, getter);
  check_plain_blocks(editor, del, isize, ins, isize);
}

static void check_plain_blocks_after_replace(Editor *const editor,
  int const ins, int const dsize, int const isize, int (* const getter)(int))
{
  check_one_block(editor, ins, isize, getter);
  check_plain_blocks(editor, ins, dsize, ins, isize);
}

static void check_plain_blocks_after_insert(Editor *const editor,
  int const ins, int const size, int (* const getter)(int))
{
  check_plain_blocks_after_replace(editor, ins, 0, size, getter);
}

static void pal_init(PaletteEntry (*const pal)[NumColours])
{
  for (int c = 0; c < NumColours; ++c)
  {
    (*pal)[c] = make_palette_entry(
      c, (3 + c) % NumColours, NumColours - 1 - c);
  }
}

static int get_valid_colour(int const n)
{
  return Colour + n;
}

static int get_invalid_colour(int const n)
{
  return n % 2 ? Colour + n : -n - 1;
}

static int get_validated_colour(int const n)
{
  int const expected = get_invalid_colour(n);
  return expected < 0 ? 0 : expected;
}

static void make_sky(Sky *const sky)
{
  sky_init(sky);
  for (int n = 0; n < NColourBands; ++n)
  {
    sky_set_colour(sky, n, get_valid_colour(n));
  }
}

static int get_smooth_colour(int const n)
{
  int const smooth = NBlocks / 2;
  return n + ((smooth - 1) * BlockColourGap);
}

static int get_interp_colour(int const n)
{
  return StartCol + n;
}

static int get_plain_colour(int const n)
{
  NOT_USED(n);
  return Colour;
}

static int get_gradient_colour(int n)
{
  return Colour - n;
}

static int get_copied(int const n)
{
  int const src = (NBlocks * BlockSize) / 2 + n;
  return (src / BlockSize) * BlockColourGap;
}

static int get_copied_up(int const n)
{
  int const src = ((NBlocks * BlockSize) / 4) + n;
  return (src / BlockSize) * BlockColourGap;
}

static int get_moved_to_end(int const n)
{
  NOT_USED(n);
  return (NBlocks / 2) * BlockColourGap;
}

static void check_nop(Editor *const editor, PaletteEntry const *const palette, int const cpos)
{
  check_caret(editor, cpos);
  check_plain_blocks(editor, -1, 0, -1, 0);

  assert(editor_can_undo(editor));
  assert(!editor_undo(editor));

  check_caret(editor, cpos);
  check_plain_blocks(editor, -1, 0, -1, 0);

  assert(editor_can_redo(editor));
  assert(!editor_redo(editor, palette));
  assert(editor_can_undo(editor));

  assert(select_count == 0);
  assert(bands_count == 0);
  assert(render_offset_count == 0);
  assert(stars_height_count == 0);

  check_caret(editor, cpos);
  check_plain_blocks(editor, -1, 0, -1, 0);
}

static void check_set_select_twice(EditSky *const edit_sky, Editor *const editor,
  PaletteEntry const *const palette, int const cpos, int const isize,
  int (* const getter)(int))
{
  /* You only check set select twice, Mister Bond. */
  assert(bands_count == 0);
  assert(select_count == 0);

  check_plain_blocks_after_replace(editor, cpos, isize, isize, getter);
  check_select(editor, cpos, cpos + isize);

  assert(editor_can_undo(editor));
  assert(!editor_undo(editor));

  assert(bands_count == 0);
  assert(select_count == 0);

  check_plain_blocks_after_replace(editor, cpos, isize, isize, getter);
  check_select(editor, cpos, cpos + isize);

  assert(editor_can_redo(editor));
  assert(!editor_redo(editor, palette));

  assert(bands_count == 0);
  assert(select_count == 0);

  check_plain_blocks_after_replace(editor, cpos, isize, isize, getter);
  check_select(editor, cpos, cpos + isize);

  assert(!editor_undo(editor));

  for (int u = 0; u < NUndoRedo; ++u)
  {
    assert(bands_count == 0);

    assert(editor_can_undo(editor));
    assert(editor_undo(editor));

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, cpos, cpos + isize);
    bands_count = 0;

    check_plain_blocks(editor, -1, 0, -1, 0);
    check_select(editor, cpos, cpos + isize);

    assert(editor_can_redo(editor));
    assert(editor_redo(editor, palette));

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, cpos, cpos + isize);
    bands_count = 0;

    check_plain_blocks_after_replace(editor, cpos, isize, isize, getter);
    check_select(editor, cpos, cpos + isize);
  }

  assert(select_count == 0);
  assert(render_offset_count == 0);
  assert(stars_height_count == 0);
}

static void check_replace_twice(EditSky *const edit_sky, Editor *const editor,
  PaletteEntry const *const palette, int const cpos, int const dsize,
  int const isize, int (* const getter)(int))
{
  assert(bands_count == 0);

  assert(select_count == 1);
  check_redraw_select(0, editor, cpos, cpos + isize, cpos + isize, cpos + isize);

  check_caret(editor, cpos + isize);
  check_plain_blocks_after_replace(editor, cpos, dsize, isize, getter);

  assert(editor_can_undo(editor));
  assert(!editor_undo(editor));

  assert(bands_count == 0);

  check_select(editor, cpos, cpos + isize);
  check_plain_blocks_after_replace(editor, cpos, dsize, isize, getter);

  assert(editor_can_redo(editor));
  assert(!editor_redo(editor, palette));

  assert(bands_count == 0);

  check_caret(editor, cpos + isize);
  check_plain_blocks_after_replace(editor, cpos, dsize, isize, getter);

  assert(!editor_undo(editor));

  editor_set_caret_pos(editor, cpos + isize);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    bands_count = select_count = 0;

    assert(editor_can_undo(editor));
    assert(editor_undo(editor));

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, cpos, dsize == isize ? cpos + isize : NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, editor, cpos + isize, cpos + isize, cpos, cpos + dsize);

    check_select(editor, cpos, cpos + dsize);
    check_plain_blocks(editor, -1, 0, -1, 0);

    bands_count = select_count = 0;

    assert(editor_can_redo(editor));
    assert(editor_redo(editor, palette));

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, cpos, dsize == isize ? cpos + isize : NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, editor, cpos, cpos + dsize, cpos + isize, cpos + isize);

    check_plain_blocks_after_replace(editor, cpos, dsize, isize, getter);
  }
  assert(render_offset_count == 0);
  assert(stars_height_count == 0);
}

static void test1(void)
{
  /* Initialise session */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Sky *const sky = edit_sky_get_sky(&edit_sky);
  for (int i = 0; i < NColourBands; ++i)
  {
    assert(sky_get_colour(sky, i) == DefaultPixelColour);
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == DefaultStarsHeight);
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == DefaultRenderOffset);
  }

  edit_sky_destroy(&edit_sky);
}

static void test2(void)
{
  /* Initialise editors */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky, NULL);
  editor_init(&editor2, &edit_sky, NULL);

  assert(edit_sky_get_sky(&edit_sky) == editor_get_sky(&editor));
  assert(editor_get_sky(&editor) == editor_get_sky(&editor2));

  assert(!editor_can_undo(&editor));
  assert(!editor_can_redo(&editor));

  assert(!editor_undo(&editor));
  assert(!editor_redo(&editor, NULL));

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test3a(void)
{
  /* Set caret position */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky, redraw_select_cb);
  editor_init(&editor2, &edit_sky, redraw_select_cb);

  check_caret(&editor, 0);
  check_caret(&editor2, 0);

  select_count = bands_count = 0;

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(!editor_has_selection(&editor));
  assert(!editor_can_undo(&editor));

  assert(select_count == 1);
  check_redraw_select(0, &editor, 0, 0, SelectStart, SelectStart);

  check_caret(&editor, SelectStart);
  check_caret(&editor2, 0);

  assert(!editor_set_caret_pos(&editor, SelectStart));
  assert(select_count == 1);

  assert(editor_set_caret_pos(&editor, SelectEnd));
  assert(!editor_has_selection(&editor));

  assert(select_count == 2);
  check_redraw_select(1, &editor, SelectStart, SelectStart, SelectEnd, SelectEnd);

  check_caret(&editor, SelectEnd);
  check_caret(&editor2, 0);

  assert(editor_set_caret_pos(&editor2, SelectStart));
  assert(!editor_has_selection(&editor2));

  assert(select_count == 3);
  check_redraw_select(2, &editor2, 0, 0, SelectStart, SelectStart);

  check_caret(&editor, SelectEnd);
  check_caret(&editor2, SelectStart);

  assert(editor_set_caret_pos(&editor, INT_MAX));

  assert(select_count == 4);
  check_redraw_select(3, &editor, SelectEnd, SelectEnd, NColourBands, NColourBands);

  check_caret(&editor, NColourBands);
  check_caret(&editor2, SelectStart);

  assert(editor_set_caret_pos(&editor, INT_MIN));

  assert(bands_count == 0);

  assert(select_count == 5);
  check_redraw_select(4, &editor, NColourBands, NColourBands, 0, 0);

  check_caret(&editor, 0);
  check_caret(&editor2, SelectStart);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test3b(void)
{
  /* Make selection */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  editor_init(&editor2, &edit_sky,
    redraw_select_cb);

  assert(!editor_has_selection(&editor));
  assert(!editor_has_selection(&editor2));

  assert(editor_set_caret_pos(&editor, SelectStart));

  select_count = bands_count = 0;

  assert(!editor_set_selection_end(&editor, SelectStart));
  assert(!editor_has_selection(&editor));

  assert(select_count == 0);

  assert(editor_set_selection_end(&editor, SelectEnd));
  assert(!editor_can_undo(&editor));
  assert(editor_has_selection(&editor));
  assert(!editor_has_selection(&editor2));

  assert(select_count == 1);
  check_redraw_select(0, &editor, SelectStart, SelectStart, SelectStart, SelectEnd);

  check_select(&editor, SelectStart, SelectEnd);
  assert(editor_get_caret_pos(&editor) == SelectStart);
  check_caret(&editor2, 0);

  assert(!editor_set_selection_end(&editor, SelectEnd));
  assert(select_count == 1);

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(!editor_has_selection(&editor));
  assert(!editor_has_selection(&editor2));

  assert(select_count == 2);
  check_redraw_select(1, &editor, SelectStart, SelectEnd, SelectStart, SelectStart);

  check_caret(&editor, SelectStart);
  check_caret(&editor2, 0);

  assert(editor_set_selection_end(&editor2, SelectEnd));
  assert(!editor_has_selection(&editor));
  assert(editor_has_selection(&editor2));

  assert(select_count == 3);
  check_redraw_select(2, &editor2, 0, 0, 0, SelectEnd);

  check_caret(&editor, SelectStart);
  check_select(&editor2, 0, SelectEnd);

  assert(editor_set_selection_end(&editor, INT_MAX));
  assert(editor_has_selection(&editor));
  assert(editor_has_selection(&editor2));

  assert(select_count == 4);
  check_redraw_select(3, &editor, SelectStart, SelectStart, SelectStart, NColourBands);

  check_select(&editor, SelectStart, NColourBands);
  check_select(&editor2, 0, SelectEnd);

  assert(!editor_set_selection_end(&editor, INT_MAX));
  assert(select_count == 4);

  assert(editor_set_selection_end(&editor, INT_MIN));
  assert(editor_has_selection(&editor));
  assert(editor_has_selection(&editor2));

  assert(select_count == 5);
  check_redraw_select(4, &editor, SelectStart, NColourBands, 0, SelectStart);

  check_select(&editor, 0, SelectStart);
  check_select(&editor2, 0, SelectEnd);

  assert(!editor_set_selection_end(&editor, INT_MIN));
  assert(select_count == 5);
  assert(bands_count == 0);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test4(void)
{
  /* Redraw caret (no callback) */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, NULL);

  editor_set_caret_pos(&editor, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test5(void)
{
  /* Redraw caret */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  editor_init(&editor2, &edit_sky,
    redraw_select_cb);

  editor_set_caret_pos(&editor, InsertPos);

  assert(select_count == 1);
  assert(bands_count == 0);
  check_redraw_select(0, &editor, 0, 0, InsertPos, InsertPos);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test6(void)
{
  /* Redraw selection (no callback) */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, NULL);

  assert(editor_set_selection_end(&editor, SelectEnd));

  editor_destroy(&editor);
}

static void test7(void)
{
  /* Redraw selection */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  editor_init(&editor2, &edit_sky,
    redraw_select_cb);

  assert(editor_set_selection_end(&editor, SelectEnd));

  assert(bands_count == 0);
  assert(select_count == 1);
  check_redraw_select(0, &editor, 0, 0, 0, SelectEnd);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test8(void)
{
  /* Redraw colours (no callback) */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, NULL);
  assert(editor_set_selection_end(&editor, SelectEnd));
  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test9(void)
{
  /* Redraw colours */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky, redraw_select_cb);
  editor_init(&editor2, &edit_sky, redraw_select_cb);

  assert(editor_set_selection_end(&editor, SelectEnd));

  assert(select_count == 1);
  check_redraw_select(0, &editor, 0, 0, 0, SelectEnd);
  assert(bands_count == 0);

  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  assert(select_count == 1);
  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, 0, SelectEnd);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test10(void)
{
  /* Get selected colours */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(editor_set_selection_end(&editor, SelectEnd));

  int dst[NColourBands];
  int ncols = abs(SelectEnd - SelectStart);
  assert(editor_get_array(&editor, dst, ARRAY_SIZE(dst)) == ncols);
  assert(!editor_can_undo(&editor));

  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  for (size_t n = 0; n < ARRAY_SIZE(dst); ++n)
  {
    dst[n] = Marker;
  }

  select_count = bands_count = 0;

  assert(editor_get_array(&editor, dst, ARRAY_SIZE(dst)) == ncols);

  assert(bands_count == 0);
  assert(select_count == 0);

  for (int n = 0; n < ncols; ++n)
  {
    DEBUGF("%d: %d\n", n, dst[n]);
    assert(dst[n] == Colour);
    dst[n] = Marker;
  }

  for (size_t n = ncols; n < ARRAY_SIZE(dst); ++n)
  {
    DEBUGF("%zu: %d\n", n, dst[n]);
    assert(dst[n] == Marker);
  }

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test11(void)
{
  /* Select all */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  editor_init(&editor2, &edit_sky,
    redraw_select_cb);

  select_count = bands_count = 0;

  assert(editor_select_all(&editor));
  assert(!editor_can_undo(&editor));

  assert(select_count == 1);
  assert(bands_count == 0);
  check_redraw_select(0, &editor, 0, 0, 0, NColourBands);

  assert(!editor_select_all(&editor));

  assert(editor_has_selection(&editor));
  assert(!editor_has_selection(&editor2));

  check_select(&editor, 0, NColourBands);
  check_caret(&editor2, 0);

  assert(editor_get_caret_pos(&editor) == 0);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test12(void)
{
  /* Clear selection */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor, editor2;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  editor_init(&editor2, &edit_sky,
    redraw_select_cb);

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(editor_set_caret_pos(&editor2, SelectStart));

  assert(editor_set_selection_end(&editor, SelectEnd));
  assert(editor_set_selection_end(&editor2, SelectEnd));

  select_count = bands_count = 0;

  assert(editor_clear_selection(&editor));
  assert(!editor_can_undo(&editor));

  assert(select_count == 1);
  check_redraw_select(0, &editor, SelectStart, SelectEnd, SelectStart, SelectStart);

  assert(!editor_clear_selection(&editor));
  assert(select_count == 1);
  assert(bands_count == 0);

  assert(!editor_has_selection(&editor));
  assert(editor_has_selection(&editor2));

  check_caret(&editor, SelectStart);
  check_select(&editor2, SelectStart, SelectEnd);
  assert(editor_get_caret_pos(&editor2) == SelectStart);

  editor_destroy(&editor);
  editor_destroy(&editor2);
  edit_sky_destroy(&edit_sky);
}

static void test13(void)
{
  /* Set selection nearest */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(editor_set_selection_end(&editor, SelectEnd));

  assert(!editor_set_selection_nearest(&editor, SelectEnd));
  assert(editor_get_caret_pos(&editor) == SelectStart);
  check_select(&editor, SelectStart, SelectEnd);

  assert(editor_set_selection_nearest(&editor, SelectStart));
  assert(!editor_can_undo(&editor));
  assert(editor_get_caret_pos(&editor) == SelectEnd);
  check_select(&editor, SelectStart, SelectEnd);

  int const dist = (abs(SelectStart - SelectEnd) / 2) - 1;
  int old_end = 0, new_end = 0;

  for (new_end = SelectStart - dist;
       new_end <= SelectStart + dist;
       new_end++)
  {
    if (new_end < 0) continue;
    assert(editor_set_selection_nearest(&editor, new_end));
    assert(editor_get_caret_pos(&editor) == SelectEnd);
    check_select(&editor, SelectEnd, new_end);
    old_end = new_end;
  }

  for (new_end = SelectEnd - dist;
       new_end <= SelectEnd + dist;
       new_end++)
  {
    if (new_end < 0) continue;
    assert(editor_set_selection_nearest(&editor, new_end));
    assert(editor_get_caret_pos(&editor) == old_end);
    check_select(&editor, old_end, new_end);
  }

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static EditResult set_plain(Editor *const editor, int const colour)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_set_plain(editor, colour);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);
  return r;
}

static void test14(void)
{
  /* Set plain at caret */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  assert(set_plain(&editor, Colour) == EditResult_Unchanged);
  check_nop(&editor, NULL, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test15(void)
{
  /* Set plain selection */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    int const send = cpos + isize;
    assert(editor_set_caret_pos(&editor, cpos));
    assert(editor_set_selection_end(&editor, send));

    assert(set_plain(&editor, Colour) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, send);
    bands_count = 0;

    check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_plain_colour);
    check_select(&editor, cpos, send);

    assert(editor_set_plain(&editor, Colour) == EditResult_Unchanged);
    check_set_select_twice(&edit_sky, &editor, NULL, cpos, isize, get_plain_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static EditResult interpolate(Editor *const editor, PaletteEntry const palette[],
  int const start_col, int const end_col)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_interpolate(editor, palette, start_col, end_col);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);
  return r;
}

static void test16(void)
{
  /* Interpolate at caret */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  assert(interpolate(&editor, palette, NumColours - 1, 0) == EditResult_Unchanged);
  check_nop(&editor, palette, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test17(void)
{
  /* Interpolate selection */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    int const send = cpos + isize;
    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, send);

    assert(interpolate(&editor, palette, StartCol,
      StartCol + isize - 1) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, send);
    bands_count = 0;

    check_plain_blocks_after_replace(&editor, cpos, isize, isize,
      get_interp_colour);

    check_select(&editor, cpos, send);

    assert(editor_interpolate(&editor, palette, StartCol,
        StartCol + isize - 1) == EditResult_Unchanged);

    check_set_select_twice(&edit_sky, &editor, palette, cpos, isize, get_interp_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test18(void)
{
  /* Smooth at caret */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  select_count = bands_count = 0;

  assert(editor_smooth(&editor, palette) == EditResult_Unchanged);

  check_nop(&editor, palette, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void check_redraw_smooth(EditSky *const edit_sky, int const cpos, int const send)
{
  assert(bands_count == NSmoothBlocks - 1);
  int min = INT_MAX, max = INT_MIN;
  for (int n = 0; n < bands_count; ++n)
  {
    assert(bands_args[n].edit_sky == edit_sky);
    for (int o = 0; o < n; ++o) {
      assert(bands_args[n].start > bands_args[o].end ||
             bands_args[n].end < bands_args[o].start);
    }
    min = LOWEST(min, bands_args[n].start);
    max = HIGHEST(max, bands_args[n].end);
  }
  assert(min == cpos + 1);
  assert(max == send - 1);
}

static void test19(void)
{
  /* Smooth selection */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const smooth = NBlocks / 2;
  int const cpos = (smooth - 1) * BlockSize;
  int const send = (smooth - 1 + NSmoothBlocks) * BlockSize;
  int const isize = send - cpos;
  editor_set_caret_pos(&editor, cpos);
  editor_set_selection_end(&editor, send);

  select_count = bands_count = 0;

  unsigned long limit;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    EditResult const r = editor_smooth(&editor, palette);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);

    if (r != EditResult_NoMem)
    {
      assert(r == EditResult_Changed);
      break;
    }

    assert(bands_count == 0);
    check_plain_blocks(&editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  check_redraw_smooth(&edit_sky, cpos, send);
  bands_count = 0;

  check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_smooth_colour);
  check_select(&editor, cpos, send);

  assert(editor_smooth(&editor, palette) == EditResult_Unchanged);

  check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_smooth_colour);
  check_select(&editor, cpos, send);

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));

  check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_smooth_colour);
  check_select(&editor, cpos, send);

  assert(editor_can_redo(&editor));
  assert(!editor_redo(&editor, palette));

  check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_smooth_colour);
  check_select(&editor, cpos, send);

  assert(!editor_undo(&editor));

  for (int u = 0; u < NUndoRedo; ++u)
  {
    assert(bands_count == 0);

    assert(editor_can_undo(&editor));
    assert(editor_undo(&editor));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, send);
    bands_count = 0;

    check_plain_blocks(&editor, -1, 0, -1, 0);
    check_select(&editor, cpos, send);

    assert(editor_can_redo(&editor));
    assert(editor_redo(&editor, palette));

    check_redraw_smooth(&edit_sky, cpos, send);
    bands_count = 0;

    check_plain_blocks_after_replace(&editor, cpos, isize, isize, get_smooth_colour);
    check_select(&editor, cpos, send);
  }

  assert(select_count == 0);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test20(void)
{
  /* Delete at caret */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  select_count = bands_count = 0;

  assert(editor_delete_colours(&editor) == EditResult_Unchanged);

  check_nop(&editor, NULL, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test21(void)
{
  /* Delete selection */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Editor_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }
  set_plain_blocks(&edit_sky, &editors[Editor_Destination]);

  int const del = NBlocks / 2;
  int const cpos = del * BlockSize;
  int const send = (del + 1) * BlockSize;
  editor_set_caret_pos(&editors[Editor_Destination], cpos);
  editor_set_selection_end(&editors[Editor_Destination], send);

  editor_set_caret_pos(&editors[Editor_High], send);
  editor_set_selection_end(&editors[Editor_High], send + BlockSize);

  int const cpos2 = (cpos + send) / 2;
  int const send2 = cpos2 + BlockSize;
  editor_set_caret_pos(&editors[Editor_Middle], cpos2);
  editor_set_selection_end(&editors[Editor_Middle], send2);

  editor_set_caret_pos(&editors[Editor_Low], cpos - BlockSize);
  editor_set_selection_end(&editors[Editor_Low], cpos);

  select_count = bands_count = 0;

  unsigned long limit;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    EditResult const r = editor_delete_colours(&editors[Editor_Destination]);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      assert(r == EditResult_Changed);
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(&editors[Editor_Destination], -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  assert(select_count == 3);
  check_redraw_select(select_count-1, &editors[Editor_Destination], cpos, send, cpos, cpos);

  check_caret(&editors[Editor_Destination], cpos);
  check_select(&editors[Editor_High], cpos, cpos + BlockSize);
  check_select(&editors[Editor_Middle], cpos, cpos + (send2 - send));
  check_select(&editors[Editor_Low], cpos - BlockSize, cpos);

  check_plain_blocks(&editors[Editor_Destination], cpos, BlockSize, -1, 0);

  bands_count = select_count = 0;

  assert(editor_delete_colours(&editors[Editor_Destination]) == EditResult_Unchanged);

  check_plain_blocks(&editors[Editor_Destination], cpos, BlockSize, cpos, 0);
  check_caret(&editors[Editor_Destination], cpos);

  assert(editor_can_undo(&editors[Editor_Destination]));
  assert(!editor_undo(&editors[Editor_Destination]));

  check_plain_blocks(&editors[Editor_Destination], cpos, BlockSize, cpos, 0);
  check_caret(&editors[Editor_Destination], cpos);

  assert(editor_can_redo(&editors[Editor_Destination]));
  assert(!editor_redo(&editors[Editor_Destination], NULL));

  check_plain_blocks(&editors[Editor_Destination], cpos, BlockSize, cpos, 0);
  check_caret(&editors[Editor_Destination], cpos);

  assert(!editor_undo(&editors[Editor_Destination]));

  assert(select_count == 0);
  assert(bands_count == 0);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    bands_count = select_count = 0;

    assert(editor_can_undo(&editors[Editor_Destination]));
    assert(editor_undo(&editors[Editor_Destination]));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count >= 3);
    assert(select_count <= 6);
    check_redraw_select(select_count-1, &editors[Editor_Destination], cpos, cpos, cpos, send);

    check_select(&editors[Editor_Destination], cpos, send);
    check_select(&editors[Editor_High], send, send + BlockSize);
    check_select(&editors[Editor_Middle], send, send2);
    check_select(&editors[Editor_Low], cpos - BlockSize, send);

    check_plain_blocks(&editors[Editor_Destination], -1, 0, -1, 0);

    bands_count = select_count = 0;

    assert(editor_can_redo(&editors[Editor_Destination]));
    assert(editor_redo(&editors[Editor_Destination], NULL));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 4);
    check_redraw_select(select_count-1, &editors[Editor_Destination], cpos, send, cpos, cpos);

    check_caret(&editors[Editor_Destination], cpos);
    check_select(&editors[Editor_High], cpos, cpos + BlockSize);
    check_select(&editors[Editor_Middle], cpos, cpos2);
    check_select(&editors[Editor_Low], cpos - BlockSize, cpos);

    check_plain_blocks(&editors[Editor_Destination], cpos, BlockSize, -1, 0);
  }

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static EditResult insert_array(Editor *const editor, int const isize,
  int const *const src, bool *const is_valid)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_insert_array(editor, isize, src, is_valid);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      assert(is_valid);
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  assert(editor_can_undo(editor));
  return r;
}

static void test22(void)
{
  /* Insert array at caret */
  int src[MaxInsertLen];
  for (int n = 0; n < MaxInsertLen; ++n)
  {
    src[n] = get_valid_colour(n);
  }

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editors[Editor_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Editor_Destination]);

    int const cpos = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editors[Editor_Destination], cpos);
    editor_set_caret_pos(&editors[Editor_High], cpos + 1);
    editor_set_caret_pos(&editors[Editor_Low], cpos - 1);

    bool is_valid = false;
    assert(insert_array(&editors[Editor_Destination], isize, src, &is_valid) == EditResult_Changed);
    assert(is_valid);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 2);
    check_redraw_select(0, &editors[Editor_High], cpos + 1, cpos + 1,
      cpos + 1 + isize, cpos + 1 + isize);

    check_redraw_select(select_count-1, &editors[Editor_Destination], cpos, cpos,
      cpos, cpos + isize);

    check_select(&editors[Editor_Destination], cpos, cpos + isize);
    check_caret(&editors[Editor_High], cpos + 1 + isize);
    check_caret(&editors[Editor_Low], cpos - 1);

    check_plain_blocks_after_insert(&editors[Editor_Destination],
      cpos, isize, get_valid_colour);

    bands_count = select_count = 0;

    assert(editor_insert_array(&editors[Editor_Destination], isize, src,
      &is_valid) == EditResult_Unchanged);

    check_select(&editors[Editor_Destination], cpos, cpos + isize);
    check_caret(&editors[Editor_High], cpos + 1 + isize);
    check_caret(&editors[Editor_Low], cpos - 1);

    check_plain_blocks_after_insert(&editors[Editor_Destination],
      cpos, isize, get_valid_colour);

    assert(editor_can_undo(&editors[Editor_Destination]));
    assert(!editor_undo(&editors[Editor_Destination]));

    check_select(&editors[Editor_Destination], cpos, cpos + isize);
    check_caret(&editors[Editor_High], cpos + 1 + isize);
    check_caret(&editors[Editor_Low], cpos - 1);

    check_plain_blocks_after_insert(&editors[Editor_Destination],
      cpos, isize, get_valid_colour);

    assert(select_count == 0);
    assert(bands_count == 0);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;

      assert(editor_can_undo(&editors[Editor_Destination]));
      assert(editor_undo(&editors[Editor_Destination]));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);

      assert(select_count == 2);
      check_redraw_select(0, &editors[Editor_High], cpos + 1 + isize, cpos + 1 + isize, cpos + 1, cpos + 1);
      check_redraw_select(1, &editors[Editor_Destination], cpos, cpos + isize, cpos, cpos);

      check_caret(&editors[Editor_Destination], cpos);
      check_caret(&editors[Editor_High], cpos + 1);
      check_caret(&editors[Editor_Low], cpos - 1);

      check_plain_blocks(&editors[Editor_Destination], -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editors[Editor_Destination]));
      assert(editor_redo(&editors[Editor_Destination], NULL));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);
      bands_count = 0;

      assert(select_count == 2);
      check_redraw_select(0, &editors[Editor_High], cpos + 1, cpos + 1, cpos + 1 + isize, cpos + 1 + isize);
      check_redraw_select(1, &editors[Editor_Destination], cpos, cpos, cpos, cpos + isize);

      check_select(&editors[Editor_Destination], cpos, cpos + isize);
      check_caret(&editors[Editor_High], cpos + 1 + isize);
      check_caret(&editors[Editor_Low], cpos - 1);

      check_plain_blocks_after_insert(&editors[Editor_Destination],
        cpos, isize, get_valid_colour);
    }

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void test23(void)
{
  /* Replace selection with array */
  int src[MaxInsertLen];
  for (int n = 0; n < MaxInsertLen; ++n)
  {
    src[n] = get_valid_colour(n);
  }

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editors[Editor_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Editor_Destination]);

    int const cpos = (NBlocks * BlockSize) / 2;
    int const send = cpos + BlockSize;
    editor_set_caret_pos(&editors[Editor_Destination], cpos);
    editor_set_selection_end(&editors[Editor_Destination], send);

    editor_set_caret_pos(&editors[Editor_High], send);
    editor_set_selection_end(&editors[Editor_High], send + BlockSize);

    int const cpos2 = (cpos + send) / 2;
    editor_set_caret_pos(&editors[Editor_Middle], cpos2);
    editor_set_selection_end(&editors[Editor_Middle], cpos2 + BlockSize);

    editor_set_caret_pos(&editors[Editor_Low], cpos - BlockSize);
    editor_set_selection_end(&editors[Editor_Low], cpos);

    bool is_valid = false;
    assert(insert_array(&editors[Editor_Destination], isize, src, &is_valid) ==
      EditResult_Changed);
    assert(is_valid);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky,
      cpos, isize == BlockSize ? cpos + isize : NColourBands);

    assert(select_count >= 2); /* equal sized replacement */
    /* Two adjusted selections overlap their original (4 redraws) and
       two do not (2 redraws). */
    assert(select_count <= 6);
    if (send - cpos != isize)
    {
      check_redraw_select(select_count-1, &editors[Editor_Destination],
        cpos, send, cpos, cpos + isize);
    }

    check_select(&editors[Editor_Destination], cpos, cpos + isize);
    check_select(&editors[Editor_High], cpos + isize,
      cpos + isize + BlockSize);
    check_select(&editors[Editor_Middle], cpos + isize, cpos2 + isize);
    check_select(&editors[Editor_Low], cpos - BlockSize,
      cpos + isize);

    check_plain_blocks_after_replace(&editors[Editor_Destination],
      cpos, send - cpos, isize, get_valid_colour);

    assert(editor_insert_array(&editors[Editor_Destination], isize, src, &is_valid) ==
      EditResult_Unchanged);
    assert(is_valid);

    check_select(&editors[Editor_Destination], cpos, cpos + isize);
    check_select(&editors[Editor_High], cpos + isize,
      cpos + isize + BlockSize);
    check_select(&editors[Editor_Middle], cpos + isize, cpos2 + isize);
    check_select(&editors[Editor_Low], cpos - BlockSize,
      cpos + isize);

    check_plain_blocks_after_replace(&editors[Editor_Destination],
      cpos, send - cpos, isize, get_valid_colour);

    assert(editor_can_undo(&editors[Editor_Destination]));
    assert(!editor_undo(&editors[Editor_Destination]));

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;

      assert(editor_can_undo(&editors[Editor_Destination]));
      assert(editor_undo(&editors[Editor_Destination]));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky,
        cpos, isize == BlockSize ? cpos + isize : NColourBands);

      check_select(&editors[Editor_Destination], cpos, send);
      check_select(&editors[Editor_High], send, send + BlockSize);
      check_select(&editors[Editor_Middle], send, cpos2 + BlockSize);
      check_select(&editors[Editor_Low], cpos - BlockSize, send);

      check_plain_blocks(&editors[Editor_Destination], -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editors[Editor_Destination]));
      assert(editor_redo(&editors[Editor_Destination], NULL));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky,
        cpos, isize == BlockSize ? cpos + isize : NColourBands);

      check_select(&editors[Editor_Destination], cpos, cpos + isize);
      check_select(&editors[Editor_High], cpos + isize,
        cpos + isize + BlockSize);
      check_select(&editors[Editor_Middle], cpos + isize, cpos2 + isize);
      check_select(&editors[Editor_Low], cpos - BlockSize,
        cpos + isize);

      check_plain_blocks_after_replace(&editors[Editor_Destination],
        cpos, send - cpos, isize, get_valid_colour);
    }

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void test24(void)
{
  /* Insert array at end */
  int src[MaxInsertLen];
  for (int n = 0; n < MaxInsertLen; ++n)
  {
    src[n] = get_valid_colour(n);
  }

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);
    editor_set_caret_pos(&editor, NColourBands);

    bool is_valid = false;
    assert(insert_array(&editor, isize, src, &is_valid) == EditResult_Unchanged);
    assert(is_valid);
    check_nop(&editor, NULL, NColourBands);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test25(void)
{
  /* Insert array overlapping end */
  int src[MaxInsertLen];
  for (int n = 0; n < MaxInsertLen; ++n)
  {
    src[n] = get_valid_colour(n);
  }

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = NColourBands - 1;
    editor_set_caret_pos(&editor, cpos);

    bool is_valid = false;
    assert(insert_array(&editor, isize, src, &is_valid) == EditResult_Changed);
    assert(is_valid);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, cpos, NColourBands);

    check_select(&editor, cpos, NColourBands);
    check_plain_blocks_after_insert(&editor, cpos, isize, get_valid_colour);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;

      assert(editor_can_undo(&editor));
      assert(editor_undo(&editor));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);

      assert(select_count == 1);
      check_redraw_select(0, &editor, cpos, NColourBands, cpos, cpos);

      check_caret(&editor, cpos);
      check_plain_blocks(&editor, -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editor));
      assert(editor_redo(&editor, NULL));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);

      assert(select_count == 1);
      check_redraw_select(0, &editor, cpos, cpos, cpos, NColourBands);

      check_select(&editor, cpos, NColourBands);
      check_plain_blocks_after_insert(&editor, cpos, isize, get_valid_colour);
    }

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test26(void)
{
  /* Insert zero-length array */
  int src[MaxInsertLen] = {0};
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  bool is_valid = false;
  assert(insert_array(&editor, 0, src, &is_valid) == EditResult_Unchanged);
  assert(is_valid);
  check_nop(&editor, NULL, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test27(void)
{
  /* Replace selection with zero-length array */
  int src[MaxInsertLen];
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const del = NBlocks / 2;
  int const cpos = del * BlockSize;
  int const send = (del + 1) * BlockSize;
  editor_set_caret_pos(&editor, cpos);
  editor_set_selection_end(&editor, send);

  bool is_valid = false;
  assert(insert_array(&editor, 0, src, &is_valid) == EditResult_Changed);
  assert(is_valid);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  assert(select_count == 1);
  check_redraw_select(0, &editor, cpos, send, cpos, cpos);

  check_plain_blocks(&editor, cpos, BlockSize, -1, 0);
  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test28(void)
{
  /* Insert invalid array at caret */
  int src[MaxInsertLen] = {0};
  for (int n = 0; n < MaxInsertLen; ++n)
  {
    src[n] = get_invalid_colour(n);
  }

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editor, cpos);

    bool is_valid = true;
    assert(insert_array(&editor, isize, src, &is_valid) == EditResult_Changed);
    assert(!is_valid);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, cpos, cpos + isize);

    check_plain_blocks_after_insert(&editor, cpos, isize,
      get_validated_colour);

    assert(editor_can_undo(&editor));

    bands_count = select_count = 0;

    is_valid = true;
    assert(editor_insert_array(&editor, isize, src, &is_valid) ==
      EditResult_Unchanged);
    assert(!is_valid);

    assert(select_count == 0);
    assert(bands_count == 0);

    check_plain_blocks_after_insert(&editor, cpos, isize,
      get_validated_colour);

    assert(editor_can_undo(&editor));
    assert(!editor_undo(&editor));

    assert(bands_count == 0);

    check_plain_blocks_after_insert(&editor, cpos, isize,
      get_validated_colour);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;

      assert(editor_can_undo(&editor));
      assert(editor_undo(&editor));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);

      assert(select_count == 1);
      check_redraw_select(0, &editor, cpos, cpos + isize, cpos, cpos);

      check_caret(&editor, cpos);
      check_plain_blocks(&editor, -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editor));
      assert(editor_redo(&editor, NULL));

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, cpos, NColourBands);

      assert(select_count == 1);
      check_redraw_select(0, &editor, cpos, cpos, cpos, cpos + isize);

      check_select(&editor, cpos, cpos + isize);
      check_plain_blocks_after_insert(&editor, cpos, isize,
        get_validated_colour);
    }

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static EditResult insert_sky(Editor *const editor, Sky const *const src)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_insert_sky(editor, src);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  assert(editor_can_undo(editor));
  return r;
}

static void test29(void)
{
  /* Insert sky at caret */
  Sky src;
  make_sky(&src);
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const cpos = (NBlocks * BlockSize) / 2;
  editor_set_caret_pos(&editor, cpos);

  assert(insert_sky(&editor, &src) == EditResult_Changed);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  assert(select_count == 1);
  check_redraw_select(0, &editor, cpos, cpos, cpos, NColourBands);

  check_select(&editor, cpos, NColourBands);

  check_plain_blocks_after_insert(&editor, cpos, NColourBands,
    get_valid_colour);

  bands_count = select_count = 0;

  assert(editor_insert_sky(&editor, &src) == EditResult_Unchanged);

  check_select(&editor, cpos, NColourBands);

  check_plain_blocks_after_insert(&editor, cpos, NColourBands,
    get_valid_colour);

  assert(editor_can_undo(&editor));
  assert(!editor_undo(&editor));

  check_select(&editor, cpos, NColourBands);

  check_plain_blocks_after_insert(&editor, cpos, NColourBands,
    get_valid_colour);

  assert(select_count == 0);
  assert(bands_count == 0);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    bands_count = select_count = 0;

    assert(editor_can_undo(&editor));
    assert(editor_undo(&editor));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    check_caret(&editor, cpos);
    check_plain_blocks(&editor, -1, 0, -1, 0);

    bands_count = select_count = 0;

    assert(editor_can_redo(&editor));
    assert(editor_redo(&editor, NULL));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, cpos, NColourBands);

    check_select(&editor, cpos, NColourBands);
    check_plain_blocks_after_insert(&editor, cpos, NColourBands,
      get_valid_colour);
  }

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test30(void)
{
  /* Replace selection with sky */
  Sky src;
  make_sky(&src);
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const cpos = (NBlocks * BlockSize) / 2;
  int const send = cpos + BlockSize;
  editor_set_caret_pos(&editor, cpos);
  editor_set_selection_end(&editor, send);

  assert(insert_sky(&editor, &src) == EditResult_Changed);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  assert(select_count == 1);
  check_redraw_select(0, &editor, cpos, send, cpos, NColourBands);

  check_select(&editor, cpos, NColourBands);

  check_plain_blocks_after_replace(&editor, cpos, send - cpos, NColourBands,
    get_valid_colour);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test31(void)
{
  /* Insert sky at end */
  Sky src;
  make_sky(&src);
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, NColourBands);

  assert(insert_sky(&editor, &src) == EditResult_Unchanged);
  check_nop(&editor, NULL, NColourBands);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static EditResult insert_plain(Editor *const editor, int const isize,
  int const colour)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_insert_plain(editor, isize, colour);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  assert(editor_can_undo(editor));
  return r;
}

static void test32(void)
{
  /* Insert plain at caret */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
      redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editor, cpos);

    select_count = bands_count = 0;

    assert(insert_plain(&editor, isize, Colour) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, cpos + isize, cpos + isize);

    check_caret(&editor, cpos + isize);

    check_plain_blocks_after_insert(&editor, cpos, isize,
      get_plain_colour);

    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, cpos + isize);

    select_count = bands_count = 0;
    assert(editor_insert_plain(&editor, isize, Colour) == EditResult_Unchanged);
    check_replace_twice(&edit_sky, &editor, NULL, cpos, 0, isize, get_plain_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test33(void)
{
  /* Replace selection with plain */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    int const send = cpos + BlockSize;
    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, send);

    select_count = bands_count = 0;

    assert(insert_plain(&editor, isize, Colour) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos,
      isize == BlockSize ? cpos + isize : NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, send, cpos + isize, cpos + isize);

    check_caret(&editor, cpos + isize);

    check_plain_blocks_after_replace(&editor, cpos, send - cpos, isize,
      get_plain_colour);

    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, cpos + isize);

    select_count = bands_count = 0;
    assert(editor_insert_plain(&editor, isize, Colour) == EditResult_Unchanged);
    check_replace_twice(&edit_sky, &editor, NULL, cpos, send - cpos, isize, get_plain_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test34(void)
{
  /* Insert plain at end */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, NColourBands);

  select_count = bands_count = 0;

  assert(insert_plain(&editor, BlockSize, Colour) == EditResult_Unchanged);
  check_nop(&editor, NULL, NColourBands);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test35(void)
{
  /* Insert plain overlapping end */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = NColourBands - 1;
    editor_set_caret_pos(&editor, cpos);

    select_count = bands_count = 0;

    assert(insert_plain(&editor, isize, Colour) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, NColourBands, NColourBands);

    check_caret(&editor, NColourBands);
    check_plain_blocks_after_insert(&editor, cpos, isize, get_plain_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test36(void)
{
  /* Insert zero-length plain */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  select_count = bands_count = 0;

  assert(insert_plain(&editor, 0, Colour) == EditResult_Unchanged);

  assert(bands_count == 0);
  assert(select_count == 0);

  check_caret(&editor, InsertPos);

  check_plain_blocks(&editor, -1, 0, -1, 0);
  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test37(void)
{
  /* Replace selection with zero-length plain */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const del = NBlocks / 2;
  int const cpos = del * BlockSize;
  int const send = (del + 1) * BlockSize;
  editor_set_caret_pos(&editor, cpos);
  editor_set_selection_end(&editor, send);

  select_count = bands_count = 0;

  assert(insert_plain(&editor, 0, Colour) == EditResult_Changed);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  assert(select_count == 1);
  check_redraw_select(0, &editor, cpos, send, cpos, cpos);

  check_caret(&editor, cpos);

  check_plain_blocks(&editor, cpos, BlockSize, -1, 0);
  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static EditResult insert_gradient(Editor *const editor,
  PaletteEntry const palette[], int const number, int const start_col,
  int const end_col, bool const inc_start, bool const inc_end)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_insert_gradient(editor, palette, number, start_col, end_col,
          inc_start, inc_end);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(editor, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  assert(editor_can_undo(editor));
  return r;
}

static void test38(void)
{
  /* Insert gradient at caret */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editor, cpos);

    select_count = bands_count = 0;

    assert(insert_gradient(&editor, palette, isize,
      Colour, Colour - (isize - 1), true, true) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, cpos + isize, cpos + isize);

    check_caret(&editor, cpos + isize);

    check_plain_blocks_after_insert(&editor, cpos, isize,
      get_gradient_colour);

    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, cpos + isize);

    select_count = bands_count = 0;
    assert(editor_insert_gradient(&editor, palette, isize,
      Colour, Colour - (isize - 1), true, true) == EditResult_Unchanged);
    check_replace_twice(&edit_sky, &editor, palette, cpos, 0, isize, get_gradient_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test39(void)
{
  /* Replace selection with gradient */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (NBlocks * BlockSize) / 2;
    int const send = cpos + BlockSize;
    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, send);

    select_count = bands_count = 0;

    assert(insert_gradient(&editor, palette, isize,
      Colour, Colour - (isize - 1), true, true) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos,
      isize == BlockSize ? cpos + isize : NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, send, cpos + isize, cpos + isize);

    check_caret(&editor, cpos + isize);

    check_plain_blocks_after_replace(&editor, cpos, send - cpos, isize,
      get_gradient_colour);

    editor_set_caret_pos(&editor, cpos);
    editor_set_selection_end(&editor, cpos + isize);

    select_count = bands_count = 0;
    assert(editor_insert_gradient(&editor, palette, isize,
      Colour, Colour - (isize - 1), true, true) == EditResult_Unchanged);
    check_replace_twice(&edit_sky, &editor, palette, cpos, send - cpos, isize, get_gradient_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test40(void)
{
  /* Insert gradient at end */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, NColourBands);

  PaletteEntry palette[NumColours] = {0};

  select_count = bands_count = 0;

  assert(insert_gradient(&editor, palette, BlockSize,
    Colour, Colour, true, true) == EditResult_Unchanged);

  check_nop(&editor, palette, NColourBands);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test41(void)
{
  /* Insert gradient overlapping end */
  PaletteEntry palette[NumColours] = {0};
  pal_init(&palette);

  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = NColourBands - 1;
    editor_set_caret_pos(&editor, cpos);

    select_count = bands_count = 0;

    assert(insert_gradient(&editor, palette, isize,
      Colour, Colour - (isize - 1), true, true) == EditResult_Changed);

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, cpos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editor, cpos, cpos, NColourBands, NColourBands);

    check_caret(&editor, NColourBands);
    check_plain_blocks_after_insert(&editor, cpos, isize, get_gradient_colour);

    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static void test42(void)
{
  /* Insert zero-length gradient */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);
  editor_set_caret_pos(&editor, InsertPos);

  PaletteEntry palette[NumColours] = {0};

  select_count = bands_count = 0;

  assert(insert_gradient(&editor, palette, 0,
    Colour, Colour, true, true) == EditResult_Unchanged);

  check_nop(&editor, palette, InsertPos);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test43(void)
{
  /* Replace selection with zero-length gradient */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  set_plain_blocks(&edit_sky, &editor);

  int const del = NBlocks / 2;
  int const cpos = del * BlockSize;
  int const send = (del + 1) * BlockSize;
  editor_set_caret_pos(&editor, cpos);
  editor_set_selection_end(&editor, send);

  PaletteEntry palette[NumColours] = {0};

  select_count = bands_count = 0;

  assert(insert_gradient(&editor, palette, 0,
    Colour, Colour, true, true) == EditResult_Changed);

  assert(select_count == 1);
  check_redraw_select(0, &editor, cpos, send, cpos, cpos);

  assert(bands_count == 1);
  check_redraw_bands(0, &edit_sky, cpos, NColourBands);

  check_caret(&editor, cpos);

  check_plain_blocks(&editor, cpos, BlockSize, -1, 0);
  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test44(void)
{
  /* Get no selected colours  */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  int dst[NColourBands];

  for (size_t n = 0; n < ARRAY_SIZE(dst); ++n)
  {
    dst[n] = Marker;
  }

  assert(editor_get_array(&editor, dst, ARRAY_SIZE(dst)) == 0);

  assert(bands_count == 0);
  assert(select_count == 0);

  for (size_t n = 0; n < ARRAY_SIZE(dst); ++n)
  {
    DEBUGF("%zu: %d\n", n, dst[n]);
    assert(dst[n] == Marker);
  }

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test45(void)
{
  /* Get too many selected colours */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky,
    redraw_select_cb);

  assert(editor_set_caret_pos(&editor, SelectStart));
  assert(editor_set_selection_end(&editor, SelectEnd));
  assert(editor_set_plain(&editor, Colour) == EditResult_Changed);

  int dst[NColourBands];

  for (size_t n = 0; n < ARRAY_SIZE(dst); ++n)
  {
    dst[n] = Marker;
  }

  int ncols = abs(SelectEnd - SelectStart);
  assert(editor_get_array(&editor, dst, ncols - BufferOverrun) == ncols);

  for (int n = 0; n < ncols - BufferOverrun; ++n)
  {
    DEBUGF("%d: %d\n", n, dst[n]);
    assert(dst[n] == Colour);
    dst[n] = Marker;
  }

  for (size_t n = ncols - BufferOverrun; n < ARRAY_SIZE(dst); ++n)
  {
    DEBUGF("%zu: %d\n", n, dst[n]);
    assert(dst[n] == Marker);
  }

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test46(void)
{
  /* Get selected colour */
  for (int n = 0; n < NBlocks; ++n)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editor;
    editor_init(&editor, &edit_sky,
      redraw_select_cb);

    set_plain_blocks(&edit_sky, &editor);

    int const cpos = (n * BlockSize) + (BlockSize - 1);
    int const send = cpos + 2;

    for (int m = 0; m < 2; ++m)
    {
      editor_set_caret_pos(&editor, m ? cpos : send);
      editor_set_selection_end(&editor, m ? send : cpos);

      select_count = bands_count = 0;

      assert(editor_get_selected_colour(&editor) == n * BlockColourGap);

      assert(bands_count == 0);
      assert(select_count == 0);
    }
    editor_destroy(&editor);
    edit_sky_destroy(&edit_sky);
  }
}

static EditResult copy(Editor *const dst, Editor *const src)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_copy(dst, src);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(dst, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  return r;
}

static void test61(void)
{
  /* Copy zero-length */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  editor_set_caret_pos(&editors[Copy_Destination], InsertPos);
  editor_set_caret_pos(&editors[Copy_Source], SelectStart);

  assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Unchanged);

  check_nop(&editors[Copy_Destination], NULL, InsertPos);

  check_caret(&editors[Copy_Source], SelectStart);
  check_plain_blocks(&editors[Copy_Source], -1, 0, -1, 0);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void test62(void)
{
  /* Copy invalid insert pos */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_set_caret_pos(&editors[i], SelectStart);
    editor_set_selection_end(&editors[i], SelectEnd);
  }

  assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Unchanged);

  assert(!editor_can_undo(&editors[Copy_Destination]));
  assert(!editor_can_undo(&editors[Copy_Source]));

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_set_caret_pos(&editors[i], SelectStart);
    editor_set_selection_end(&editors[i], SelectEnd);
  }

  assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Unchanged);

  assert(bands_count == 0);
  assert(select_count == 0);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    check_select(&editors[i], SelectStart, SelectEnd);
    check_plain_blocks(&editors[i], -1, 0, -1, 0);
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void test63(void)
{
  /* Copy to end */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  editor_set_caret_pos(&editors[Copy_Source], SelectStart);
  editor_set_selection_end(&editors[Copy_Source], SelectEnd);
  editor_set_caret_pos(&editors[Copy_Destination], NColourBands);

  assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Unchanged);

  check_nop(&editors[Copy_Destination], NULL, NColourBands);
  check_select(&editors[Copy_Source], SelectStart, SelectEnd);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void test64(void)
{
  /* Copy overlapping end */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = NColourBands - 1;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    assert(editor_can_undo(&editors[Copy_Destination]));
    assert(editor_can_undo(&editors[Copy_Source]));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, ipos, NColourBands);

    assert(select_count == 1);
    check_redraw_select(0, &editors[Copy_Destination], ipos, ipos, ipos, NColourBands);

    check_select(&editors[Copy_Source], start + isize, start);
    check_select(&editors[Copy_Destination], ipos, NColourBands);

    check_plain_blocks_after_insert(&editors[Copy_Destination], ipos, isize, get_copied);

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void check_copy_down(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  assert(editor_can_undo(&editors[Copy_Destination]));
  assert(editor_can_undo(&editors[Copy_Source]));

  assert(bands_count == 1);
  check_redraw_bands(0, edit_sky, ipos, NColourBands);

  assert(select_count == 2);
  check_redraw_select(0, &editors[Copy_Source], start + isize, start,
    start + (2 * isize), start + isize);

  check_redraw_select(1, &editors[Copy_Destination], ipos, ipos, ipos, ipos + isize);

  check_select(&editors[Copy_Source],
    start + (2 * isize), start + isize);

  check_select(&editors[Copy_Destination], ipos, ipos + isize);

  check_plain_blocks_after_insert(&editors[Copy_Destination],
    ipos, isize, getter);
}

static void test65(void)
{
  /* Copy down */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = start - 1;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    check_copy_down(&edit_sky, editors, start, ipos, isize, get_copied);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;

      assert(editor_undo(&editors[Copy_Destination]));

      assert(select_count == 2);
      check_redraw_select(0, &editors[Copy_Source],
        start + (2 * isize), start + isize, start + isize, start);

      check_redraw_select(1, &editors[Copy_Destination],
        ipos, ipos + isize, ipos, ipos);

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, ipos, NColourBands);

      check_select(&editors[Copy_Source], start, start + isize);
      check_caret(&editors[Copy_Destination], ipos);
      check_plain_blocks(&editors[Copy_Destination], -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editors[Copy_Source]));
      assert(editor_can_redo(&editors[Copy_Destination]));
      assert(editor_redo(&editors[Copy_Destination], NULL));

      check_copy_down(&edit_sky, editors, start, ipos, isize, get_copied);
    }

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void check_copy_up(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  assert(editor_can_undo(&editors[Copy_Destination]));
  assert(editor_can_undo(&editors[Copy_Source]));

  assert(bands_count == 1);
  check_redraw_bands(0, edit_sky, ipos, NColourBands);

  assert(select_count == 1);
  check_redraw_select(0, &editors[Copy_Destination], ipos, ipos, ipos, ipos + isize);

  check_select(&editors[Copy_Source], start + isize, start);
  check_select(&editors[Copy_Destination], ipos, ipos + isize);

  check_plain_blocks_after_insert(&editors[Copy_Destination],
    ipos, isize, getter);
}

static void test66(void)
{
  /* Copy up */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 4;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = (NBlocks * BlockSize) - MaxInsertLen;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(copy(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    check_copy_up(&edit_sky, editors, start, ipos, isize, get_copied_up);

    for (int u = 0; u < NUndoRedo; ++u)
    {
      bands_count = select_count = 0;
      assert(editor_undo(&editors[Copy_Destination]));

      assert(select_count == 1);
      check_redraw_select(0, &editors[Copy_Destination], ipos, ipos + isize, ipos, ipos);

      assert(bands_count == 1);
      check_redraw_bands(0, &edit_sky, ipos, NColourBands);

      check_caret(&editors[Copy_Destination], ipos);
      check_plain_blocks(&editors[Copy_Destination], -1, 0, -1, 0);

      bands_count = select_count = 0;

      assert(editor_can_redo(&editors[Copy_Source]));
      assert(editor_can_redo(&editors[Copy_Destination]));
      assert(editor_redo(&editors[Copy_Destination], NULL));

      check_copy_up(&edit_sky, editors, start, ipos, isize, get_copied_up);
    }

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static EditResult move(Editor *const dst, Editor *const src)
{
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = editor_move(dst, src);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(dst, -1, 0, -1, 0);
  }
  assert(limit != FortifyAllocationLimit);

  return r;
}

static void test67(void)
{
  /* Move zero-length */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  editor_set_caret_pos(&editors[Copy_Destination], InsertPos);
  editor_set_caret_pos(&editors[Copy_Source], SelectStart);

  assert(move(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Unchanged);

  check_nop(&editors[Copy_Destination], NULL, InsertPos);
  check_caret(&editors[Copy_Source], SelectStart);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    check_plain_blocks(&editors[i], -1, 0, -1, 0);
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void test68(void)
{
  /* Move invalid insert pos */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_set_caret_pos(&editors[i], SelectStart);
    editor_set_selection_end(&editors[i], SelectEnd);
  }

  assert(move(&editors[Copy_Destination], &editors[Copy_Source]) ==
    EditResult_Unchanged);

  assert(!editor_can_undo(&editors[Copy_Destination]));
  assert(!editor_can_undo(&editors[Copy_Source]));

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_set_caret_pos(&editors[i], SelectStart);
    editor_set_selection_end(&editors[i], SelectEnd);
  }

  assert(move(&editors[Copy_Destination], &editors[Copy_Source]) ==
    EditResult_Unchanged);

  assert(bands_count == 0);
  assert(select_count == 0);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    check_select(&editors[i], SelectStart, SelectEnd);
    check_plain_blocks(&editors[i], -1, 0, -1, 0);
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void check_move_up(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  assert(editor_can_undo(&editors[Copy_Destination]));
  assert(editor_can_undo(&editors[Copy_Source]));

  assert(bands_count == 1);
  check_redraw_bands(0, edit_sky, start, ipos);

  assert(select_count == 2);
  check_redraw_select(select_count - 1, &editors[Copy_Destination],
    ipos, ipos, ipos - isize, ipos);

  check_select(&editors[Copy_Destination], ipos - isize, ipos);

  check_plain_blocks_after_move(&editors[Copy_Destination],
      ipos, start, isize, getter);
}

static void check_and_redo_move_up(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  check_redraw_select(0, &editors[Copy_Source], start, start + isize, start, start);
  check_caret(&editors[Copy_Source], start);
  check_move_up(edit_sky, editors, start, ipos, isize, getter);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    bands_count = select_count = 0;
    assert(editor_undo(&editors[Copy_Destination]));

    assert(select_count == 2);
    check_redraw_select(select_count-1, &editors[Copy_Destination],
      ipos - isize, ipos, ipos, ipos);

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, start, ipos);

    /* Can't restore the source editor's selection because the source editor
       may no longer exist. */
    check_caret(&editors[Copy_Source], start + isize);

    check_caret(&editors[Copy_Destination], ipos);
    check_plain_blocks(&editors[Copy_Destination], -1, 0, -1, 0);

    bands_count = select_count = 0;

    assert(editor_can_redo(&editors[Copy_Source]));
    assert(editor_can_redo(&editors[Copy_Destination]));
    assert(editor_redo(&editors[Copy_Destination], NULL));

    check_move_up(edit_sky, editors, start, ipos, isize, getter);
  }
}

static void check_move_down(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  assert(editor_can_undo(&editors[Copy_Destination]));
  assert(editor_can_undo(&editors[Copy_Source]));

  assert(bands_count == 1);
  check_redraw_bands(0, edit_sky, start + isize, ipos);

  assert(select_count == 3);
  check_redraw_select(select_count - 1, &editors[Copy_Destination],
    ipos, ipos, ipos, ipos + isize);

  check_select(&editors[Copy_Destination], ipos, ipos + isize);

  check_plain_blocks_after_move(&editors[Copy_Destination],
      ipos, start, isize, getter);
}

static void check_and_redo_move_down(EditSky *const edit_sky, Editor *const editors,
  int const start, int const ipos, int const isize, int (* const getter)(int))
{
  check_redraw_select(0, &editors[Copy_Source], start, start + isize,
    start, start);
  check_caret(&editors[Copy_Source], start + isize);
  check_move_down(edit_sky, editors, start, ipos, isize, getter);

  for (int u = 0; u < NUndoRedo; ++u)
  {
    bands_count = select_count = 0;
    assert(editor_undo(&editors[Copy_Destination]));

    assert(select_count == 3);
    check_redraw_select(select_count-1, &editors[Copy_Destination], ipos, ipos + isize, ipos, ipos);

    assert(bands_count == 1);
    check_redraw_bands(0, edit_sky, ipos, start + isize);

    /* Can't restore the source editor's selection because the source editor
       may no longer exist. */
    check_caret(&editors[Copy_Source], start + isize);

    check_caret(&editors[Copy_Destination], ipos);
    check_plain_blocks(&editors[Copy_Destination], -1, 0, -1, 0);

    bands_count = select_count = 0;

    assert(editor_can_redo(&editors[Copy_Source]));
    assert(editor_can_redo(&editors[Copy_Destination]));
    assert(editor_redo(&editors[Copy_Destination], NULL));

    check_move_down(edit_sky, editors, start, ipos, isize, getter);
  }
}

static void test69(void)
{
  /* Move to end */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editors[Copy_Count];
  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_init(&editors[i], &edit_sky,
      redraw_select_cb);
  }

  set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

  int const del = NBlocks / 2;
  int const cpos = del * BlockSize;
  int const send = (del + 1) * BlockSize;
  editor_set_caret_pos(&editors[Copy_Source], cpos);
  editor_set_selection_end(&editors[Copy_Source], send);

  editor_set_caret_pos(&editors[Copy_Destination], NColourBands);

  assert(move(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

  check_and_redo_move_up(&edit_sky, editors, cpos, NColourBands, send - cpos,
    get_moved_to_end);

  for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
  {
    editor_destroy(&editors[i]);
  }
  edit_sky_destroy(&edit_sky);
}

static void test70(void)
{
  /* Move overlapping end */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = NColourBands - 1;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(move(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    assert(editor_can_undo(&editors[Copy_Destination]));
    assert(editor_can_undo(&editors[Copy_Source]));

    assert(bands_count == 1);
    check_redraw_bands(0, &edit_sky, start, ipos);

    assert(select_count == 2);
    check_redraw_select(0, &editors[Copy_Source], start, start + isize, start, start);
    check_redraw_select(1, &editors[Copy_Destination], ipos, ipos, ipos - isize, ipos);

    check_caret(&editors[Copy_Source], start);
    check_select(&editors[Copy_Destination], ipos - isize, ipos);

    check_plain_blocks_after_move(&editors[Copy_Destination],
        ipos, start, isize, get_copied);

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void test71(void)
{
  /* Move down */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 2;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = start - 1;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(move(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    check_and_redo_move_down(&edit_sky, editors, start, ipos, isize, get_copied);

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void test72(void)
{
  /* Move up */
  for (int isize = 1; isize <= MaxInsertLen; ++isize)
  {
    EditSky edit_sky;
    edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

    Editor editors[Copy_Count];
    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_init(&editors[i], &edit_sky,
        redraw_select_cb);
    }

    set_plain_blocks(&edit_sky, &editors[Copy_Destination]);

    int const start = (NBlocks * BlockSize) / 4;
    editor_set_caret_pos(&editors[Copy_Source], start + isize);
    editor_set_selection_end(&editors[Copy_Source], start);

    int const ipos = (NBlocks * BlockSize) - MaxInsertLen;
    editor_set_caret_pos(&editors[Copy_Destination], ipos);

    assert(move(&editors[Copy_Destination],
      &editors[Copy_Source]) == EditResult_Changed);

    check_and_redo_move_up(&edit_sky, editors, start, ipos, isize, get_copied_up);

    for (size_t i = 0; i < ARRAY_SIZE(editors); ++i)
    {
      editor_destroy(&editors[i]);
    }
    edit_sky_destroy(&edit_sky);
  }
}

static void test73(void)
{
  /* Set render offset */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, redraw_select_cb);
  set_plain_blocks(&edit_sky, &editor);
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = edit_sky_set_render_offset(&edit_sky, RenderOffset);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(&editor, -1, 0, -1, 0);
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == DefaultStarsHeight);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == DefaultRenderOffset);
  }
  assert(limit != FortifyAllocationLimit);
  assert(r == EditResult_Changed);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  int i = 0;
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MaxRenderOffset) == EditResult_Changed);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MaxRenderOffset + 1) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, INT_MAX) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MaxRenderOffset) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MinRenderOffset) == EditResult_Changed);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MinRenderOffset - 1) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, INT_MIN) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  assert(render_offset_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, MinRenderOffset) == EditResult_Unchanged);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  assert(render_offset_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_undo(&editor));
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
    assert(render_offset_count == i);
  }

  assert(editor_undo(&editor));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_undo(&editor));
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
    assert(render_offset_count == i);
  }

  assert(editor_undo(&editor));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(editor_undo(&editor));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == DefaultRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(editor_redo(&editor, NULL));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  assert(editor_redo(&editor, NULL));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_redo(&editor, NULL));
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
    assert(render_offset_count == i);
  }

  assert(editor_redo(&editor, NULL));
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  check_redraw_render_offset(i++, &edit_sky);
  assert(render_offset_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_redo(&editor, NULL));
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
    assert(render_offset_count == i);
  }

  assert(select_count == 0);
  assert(bands_count == 0);
  check_plain_blocks(&editor, -1, 0, -1, 0);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test74(void)
{
  /* Set stars height */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, redraw_select_cb);
  set_plain_blocks(&edit_sky, &editor);
  select_count = bands_count = 0;

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = edit_sky_set_stars_height(&edit_sky, StarsHeight);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(&editor, -1, 0, -1, 0);
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == DefaultRenderOffset);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == DefaultStarsHeight);
  }
  assert(limit != FortifyAllocationLimit);
  assert(r == EditResult_Changed);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  int i = 0;
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MaxStarsHeight) == EditResult_Changed);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MaxStarsHeight + 1) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, INT_MAX) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MaxStarsHeight) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MinStarsHeight) == EditResult_Changed);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MinStarsHeight - 1) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, INT_MIN) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
  assert(stars_height_count == i);

  assert(edit_sky_set_stars_height(&edit_sky, MinStarsHeight) == EditResult_Unchanged);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
  assert(stars_height_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_undo(&editor));
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
    assert(stars_height_count == i);
  }

  assert(editor_undo(&editor));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_undo(&editor));
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
    assert(stars_height_count == i);
  }

  assert(editor_undo(&editor));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(editor_undo(&editor));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == DefaultStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(editor_redo(&editor, NULL));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(editor_redo(&editor, NULL));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_redo(&editor, NULL));
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MaxStarsHeight);
    assert(stars_height_count == i);
  }

  assert(editor_redo(&editor, NULL));
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  for (int n = 0; n < 3; ++n)
  {
    assert(!editor_redo(&editor, NULL));
    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == MinStarsHeight);
    assert(stars_height_count == i);
  }

  assert(select_count == 0);
  assert(bands_count == 0);
  check_plain_blocks(&editor, -1, 0, -1, 0);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test75(void)
{
  /* Add render offset */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb,
    redraw_stars_height_cb);

  Editor editor;
  editor_init(&editor, &edit_sky, redraw_select_cb);
  set_plain_blocks(&edit_sky, &editor);
  select_count = bands_count = 0;

  assert(edit_sky_set_stars_height(&edit_sky, StarsHeight) == EditResult_Changed);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  int i = 0;
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_set_render_offset(&edit_sky, RenderOffset) == EditResult_Changed);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  int j = 0;
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  unsigned long limit;
  EditResult r = EditResult_Unchanged;
  for (limit = 0; limit < FortifyAllocationLimit; ++limit)
  {
    Fortify_SetNumAllocationsLimit(limit);
    r = edit_sky_add_render_offset(&edit_sky, RenderOffset);
    Fortify_SetNumAllocationsLimit(ULONG_MAX);

    assert(select_count == 0);
    assert(bands_count == 0);
    check_plain_blocks(&editor, -1, 0, -1, 0);

    if (r != EditResult_NoMem)
    {
      break;
    }

    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  }
  assert(limit != FortifyAllocationLimit);
  assert(r == EditResult_Changed);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, 0) == EditResult_Unchanged);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, INT_MAX) == EditResult_Changed);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, RenderOffset) == EditResult_Unchanged);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, INT_MIN) == EditResult_Changed);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MinRenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, 0) == EditResult_Unchanged);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MinRenderOffset);
  assert(stars_height_count == i);

  assert(edit_sky_add_render_offset(&edit_sky, -RenderOffset) == EditResult_Unchanged);

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MinRenderOffset);
  assert(stars_height_count == i);

  for (int n = 0; n < 2; ++n)
  {
    assert(!editor_undo(&editor));

    assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MinRenderOffset);
    assert(render_offset_count == j);

    assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
      StarsHeight + RenderOffset - MinRenderOffset);
    assert(stars_height_count == i);
  }

  assert(editor_undo(&editor));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(!editor_undo(&editor));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  assert(stars_height_count == i);

  assert(editor_undo(&editor));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(!editor_undo(&editor));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  assert(stars_height_count == i);

  assert(editor_undo(&editor));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(editor_redo(&editor, NULL));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(!editor_redo(&editor, NULL));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == 2 * RenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight - RenderOffset);
  assert(stars_height_count == i);

  assert(editor_redo(&editor, NULL));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  check_redraw_render_offset(j++, &edit_sky);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  check_redraw_stars_height(i++, &edit_sky);
  assert(stars_height_count == i);

  assert(!editor_redo(&editor, NULL));

  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == MaxRenderOffset);
  assert(render_offset_count == j);

  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) ==
    StarsHeight + RenderOffset - MaxRenderOffset);
  assert(stars_height_count == i);

  assert(select_count == 0);
  assert(bands_count == 0);
  check_plain_blocks(&editor, -1, 0, -1, 0);

  editor_destroy(&editor);
  edit_sky_destroy(&edit_sky);
}

static void test76(void)
{
  /* Set render offset (no callback) */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, NULL, redraw_stars_height_cb);

  assert(edit_sky_set_render_offset(&edit_sky, RenderOffset) == EditResult_Changed);
  assert(sky_get_render_offset(edit_sky_get_sky(&edit_sky)) == RenderOffset);
  assert(render_offset_count == 0);

  edit_sky_destroy(&edit_sky);
}

static void test77(void)
{
  /* Set stars height (no callback) */
  EditSky edit_sky;
  edit_sky_init(&edit_sky, NULL, redraw_bands_cb, redraw_render_offset_cb, NULL);

  assert(edit_sky_set_stars_height(&edit_sky, StarsHeight) == EditResult_Changed);
  assert(sky_get_stars_height(edit_sky_get_sky(&edit_sky)) == StarsHeight);
  assert(stars_height_count == 0);

  edit_sky_destroy(&edit_sky);
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
    { "Set caret position", test3a },
    { "Make selection", test3b },
    { "Redraw caret (no callback)", test4 },
    { "Redraw caret", test5 },
    { "Redraw selection (no callback)", test6 },
    { "Redraw selection", test7 },
    { "Redraw colours (no callback)", test8 },
    { "Redraw colours", test9 },
    { "Get selected colours", test10 },
    { "Select all", test11 },
    { "Clear selection", test12 },
    { "Set selection nearest", test13 },
    { "Set plain at caret", test14 },
    { "Set plain selection", test15 },
    { "Interpolate at caret", test16 },
    { "Interpolate selection", test17 },
    { "Smooth at caret", test18 },
    { "Smooth selection", test19 },
    { "Delete at caret", test20 },
    { "Delete selection", test21 },
    { "Insert array at caret", test22 },
    { "Replace selection with array", test23 },
    { "Insert array at end", test24 },
    { "Insert array overlapping end", test25 },
    { "Insert zero-length array", test26 },
    { "Replace selection with zero-length array", test27 },
    { "Insert invalid array at caret", test28 },
    { "Insert sky at caret", test29 },
    { "Replace selection with sky", test30 },
    { "Insert sky at end", test31 },
    { "Insert plain at caret", test32 },
    { "Replace selection with plain", test33 },
    { "Insert plain at end", test34 },
    { "Insert plain overlapping end", test35 },
    { "Insert zero-length plain", test36 },
    { "Replace selection with zero-length plain", test37 },
    { "Insert gradient at caret", test38 },
    { "Replace selection with gradient", test39 },
    { "Insert gradient at end", test40 },
    { "Insert gradient overlapping end", test41 },
    { "Insert zero-length gradient", test42 },
    { "Replace selection with zero-length gradient", test43 },
    { "Get no selected colours", test44 },
    { "Get too many selected colours", test45 },
    { "Get selected colour", test46 },
    { "Copy zero-length", test61 },
    { "Copy invalid insert pos", test62 },
    { "Copy to end", test63 },
    { "Copy overlapping end", test64 },
    { "Copy down", test65 },
    { "Copy up", test66 },
    { "Move zero-length", test67 },
    { "Move invalid insert pos", test68 },
    { "Move to end", test69 },
    { "Move overlapping end", test70 },
    { "Move down", test71 },
    { "Move up", test72 },
    { "Set render offset", test73 },
    { "Set stars height", test74 },
    { "Add render offset", test75 },
    { "Set render offset (no callback)", test76 },
    { "Set stars height (no callback)", test77 },
  };

  for (size_t count = 0; count < ARRAY_SIZE(unit_tests); ++count)
  {
    DEBUGF("Test %zu/%zu : %s\n",
           1 + count,
           ARRAY_SIZE(unit_tests),
           unit_tests[count].test_name);

    select_count = bands_count = render_offset_count = stars_height_count = 0;
    Fortify_EnterScope();
    unit_tests[count].test_func();
    Fortify_LeaveScope();
  }
}
