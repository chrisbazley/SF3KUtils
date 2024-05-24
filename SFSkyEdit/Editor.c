/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Editor back-end functions
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

/* ISO library files */
#include "stdlib.h"
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

/* My library files */
#include "Macros.h"
#include "SFformats.h"
#include "Debug.h"
#include "PalEntry.h"
#include "Reader.h"
#include "LinkedList.h"

/* Local headers */
#include "Editor.h"
#include "Sky.h"

enum {
  BadPixelColour = 0, /* black */
  ExtendPixelColour = 0,
};

typedef struct {
  int old;
  int rep;
} EditValueSwap;

typedef enum {
  EditRecordType_SetStarsHeight,
  EditRecordType_SetRenderOffset,
  EditRecordType_AddRenderOffset,
  EditRecordType_SetPlain,
  EditRecordType_Smooth,
  EditRecordType_Interpolate,
  EditRecordType_InsertArray,
  EditRecordType_InsertPlain,
  EditRecordType_InsertGradient,
  EditRecordType_Move,
  EditRecordType_Copy,
} EditRecordType;

typedef struct EditFill {
  /* Number of colours that would be filled if not truncated */
  int len;
  /* Start colour (for EditRecordType_InsertPlain
     or EditRecordType_InsertGradient) */
  unsigned char start;
  /* End colour (only for EditRecordType_InsertGradient) */
  unsigned char end;
  /* Whether or not to include the start colour (only for
     EditRecordType_InsertGradient) */
  bool inc_start;
  /* Whether or not to include the end colour (only for
     EditRecordType_InsertGradient) */
  bool inc_end;
} EditFill;

/* # = budge_lost
   % = lost
     BUDGE DOWN
    Before  After
   |~~~~~| |~~~~~|
   |_____| |_ _ _| old_dst_end
   |#####| |  |  |
   |#####| |_\|/_| new_dst_end
   |%%%%%| |     |           |
   |%%%%%| |     |           |lsize
   |%%%%%| |_____| dst_start |
   |     | |     |
    ~~~~~  '~~~~~'
      BUDGE UP
    Before  After
    _____   _____  End of file
   |#####| |  2  |
   |#####| |  1  |
   |  2  | |__0__| new_dst_end
   |  1  | | /|\ |
   |__0__| |_ | _| old_dst_end
   |%%%%%| |     |           |
   |%%%%%| |     |           |lsize
   |%%%%%| |_____| dst_start |
   |     | |     |
   '~~~~~' '~~~~~' */
typedef struct EditRecord {
  LinkedListItem link;
  EditRecordType type;
  union {
    /* For EditRecordType_SetStarsHeight, EditRecordType_SetRenderOffset or
       EditRecordType_AddRenderOffset */
    struct {
      /* For EditRecordType_SetStarsHeight or EditRecordType_AddRenderOffset */
      EditValueSwap stars;
      /* For EditRecordType_SetRenderOffset or EditRecordType_AddRenderOffset */
      EditValueSwap render;
    } values;
    struct {
      /* Index of first colour to be replaced */
      int dst_start;
      /* Index one beyond the last colour to be replaced */
      int old_dst_end;
      /* Index one beyond the last substitute colour */
      int new_dst_end;
      /* Index of first colour to move (only for EditRecordType_Move) */
      int src_start;
      /* Number of colours replaced, not including any lost by budging down
         or gained by budging up (to allow deletion to be treated like
         replacement with nothing). */
      int lsize;
      /* Colours lost immediately below the end of the file (budge up) or
         immediately above the end of the replaced colours (budge down) when
         the number of replaced and replacement colours differs */
      unsigned char *budge_lost;
      /* Colours replaced, not including any lost by budging down or
         gained by budging up. */
      unsigned char *lost;
      /* Replacement colours (for EditRecordType_InsertArray,
         EditRecordType_Move or EditRecordType_Copy) */
      unsigned char *fresh;
      /* The parameters for a colour fill */
      EditFill fill;
   } edit;
  } data;
  unsigned char store[];
} EditRecord;

static int clamp_colour(int colour)
{
  if (colour < 0)
  {
    DEBUGF("Clamped colour %d\n", colour);
    colour = 0;
  }
  else if (colour >= NPixelColours)
  {
    DEBUGF("Clamped colour %d\n", colour);
    colour = NPixelColours - 1;
  }
  return colour;
}

static int clamp_pos(int pos)
{
  if (pos < 0)
  {
    DEBUGF("Clamped pos %d\n", pos);
    pos = 0;
  }
  else if (pos > NColourBands)
  {
    DEBUGF("Clamped pos %d\n", pos);
    pos = NColourBands;
  }
  return pos;
}

static int budge_index(int const index, int const change_pos, int const ncols)
{
  /* Returns a new value for 'index', having adjusted it for 'ncols'
     inserted/removed (+ve/-ve) at 'change_pos' in a sky file. */
  assert(index >= 0);
  assert(index <= NColourBands);
  assert(change_pos >= 0);
  assert(change_pos <= NColourBands);

  int new_index = index;
  if (index >= change_pos)
  {
    new_index += ncols;
    if (new_index < change_pos)
    {
      new_index = change_pos;
    }
    else if (new_index > NColourBands)
    {
      new_index = NColourBands;
    }
  }

  DEBUGF("Budged index %d to %d for %d bands added at %d\n",
    index, new_index, ncols, change_pos);

  assert(new_index >= 0);
  assert(new_index <= NColourBands);
  return new_index;
}

/* Force the given range of colour bands to be redrawn using the
   registered callback. */
static inline void redraw_bands(EditSky *const edit_sky,
  int const start, int const end)
{
  assert(edit_sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  DEBUGF("Redraw %d..%d in file %p\n", start, end, (void *)edit_sky);
  edit_sky->redraw_bands_cb(edit_sky, start, end);
}

static inline void redraw_render_offset(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  DEBUGF("Redraw render offset in file %p\n", (void *)edit_sky);
  edit_sky->redraw_render_offset_cb(edit_sky);
}

static inline void redraw_stars_height(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  DEBUGF("Redraw stars height in file %p\n", (void *)edit_sky);
  edit_sky->redraw_stars_height_cb(edit_sky);
}

static void redraw_move(EditSky *const edit_sky, EditRecord const *const rec)
{
  assert(edit_sky != NULL);
  assert(rec != NULL);
  assert(rec->type == EditRecordType_Move);

  int const src_size = rec->data.edit.new_dst_end -
                       rec->data.edit.dst_start;

  int const src_end = rec->data.edit.src_start + src_size;

  /* Update the replace location in case the source data precedes it
     and the replace location will therefore shift upward */
  int const dst_start = budge_index(rec->data.edit.dst_start,
    rec->data.edit.src_start, src_size);

  int const dst_end = budge_index(rec->data.edit.old_dst_end,
    rec->data.edit.src_start, src_size);

  int redraw_end = NColourBands;
  if (dst_start == dst_end)
  {
    redraw_end = HIGHEST(src_end, dst_end);
    DEBUGF("Data moved, so colours above %d are unchanged\n", redraw_end);
  }
  redraw_bands(edit_sky, LOWEST(rec->data.edit.src_start, dst_start), redraw_end);
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

static LinkedListItem *get_redo_item(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);

  LinkedListItem *redo_item = NULL;
  if (edit_sky->next_undo)
  {
    redo_item = linkedlist_get_next(edit_sky->next_undo);
  }
  else
  {
    redo_item = linkedlist_get_head(&edit_sky->undo_list);
  }
  return redo_item;
}

static void add_undo_item(EditSky *const edit_sky, LinkedListItem *const new_item)
{
  assert(edit_sky != NULL);
  assert(new_item != NULL);

  LinkedListItem *const redo_item = get_redo_item(edit_sky);
  LinkedListItem *next = NULL;

  for (LinkedListItem *item = redo_item; item != NULL; item = next)
  {
    next = linkedlist_get_next(item);
    destroy_record(&edit_sky->undo_list, item, NULL);
  }

  linkedlist_insert(&edit_sky->undo_list, edit_sky->next_undo, new_item);

  DEBUGF("Added undo record %p\n", (void *)new_item);
  edit_sky->next_undo = new_item;
}

static EditRecord *make_undo_values(EditSky *const edit_sky,
  EditRecordType const type)
{
  assert(edit_sky != NULL);
  EditRecord *const rec = malloc(sizeof(*rec));
  if (rec != NULL)
  {
    *rec = (EditRecord){.type = type};
    add_undo_item(edit_sky, &rec->link);
  }
  else
  {
    DEBUGF("Not enough memory for undo\n");
  }
  return rec;
}

static EditRecord *make_undo_edit(EditSky *const edit_sky,
  EditRecordType const type, int const dst_start, int const dst_end,
  int const src_start, EditFill const fill)
{
  assert(edit_sky != NULL);
  assert(dst_start >= 0);
  assert(dst_start <= dst_end);
  assert(dst_end <= NColourBands);
  assert(fill.len >= 0);

  int trim_src_size = fill.len;
  if (dst_start + trim_src_size > NColourBands)
  {
    trim_src_size = NColourBands - dst_start;
    DEBUGF("Truncated import to %d bands\n", trim_src_size);
  }

  int const dst_size = dst_end - dst_start;
  int const budge_size = abs(dst_size - trim_src_size);
  int const lost_size = LOWEST(trim_src_size, dst_size);

  int fresh_size = 0;
  if (type == EditRecordType_Move || type == EditRecordType_Copy ||
      type == EditRecordType_InsertArray)
  {
    fresh_size = trim_src_size;
  }
  EditRecord *const rec = malloc(sizeof(*rec) + lost_size + fresh_size +
                                 budge_size);
  if (rec != NULL)
  {
    *rec = (EditRecord){
      .type = type,
      .data = {
        .edit = {
          .dst_start = dst_start,
          .old_dst_end = dst_start + dst_size,
          .new_dst_end = dst_start + trim_src_size,
          .src_start = src_start,
          .fill = fill,
          .fresh = rec->store,
          .lost = rec->store + fresh_size,
          .lsize = lost_size,
          .budge_lost = rec->store + fresh_size + lost_size,
        },
      },
    };

    add_undo_item(edit_sky, &rec->link);
  }
  else
  {
    DEBUGF("Not enough memory for undo\n");
  }
  return rec;
}

static inline EditRecord *make_undo_move(EditSky *const edit_sky,
  int const dst_start, int const dst_end,
  int const src_start, int const src_end)
{
  return make_undo_edit(edit_sky, EditRecordType_Move,
    dst_start, dst_end, src_start, (EditFill){src_end - src_start});
}

static inline EditRecord *make_undo_copy(EditSky *const edit_sky,
  int const dst_start, int const dst_end,
  int const src_start, int const src_end)
{
  /* Storing src_start would be folly because it may belong to a
     different file. */
  return make_undo_edit(edit_sky, EditRecordType_Copy,
    dst_start, dst_end, 0, (EditFill){src_end - src_start});
}

static inline EditRecord *make_undo_insert_array(EditSky *const edit_sky,
  int const start, int const end, int new_size)
{
  return make_undo_edit(edit_sky, EditRecordType_InsertArray, start, end, 0,
    (EditFill){new_size});
}

static inline EditRecord *make_undo_insert_plain(EditSky *const edit_sky,
  int const start, int const end, int new_size, int const start_colour)
{
  return make_undo_edit(edit_sky, EditRecordType_InsertPlain, start, end, 0,
    (EditFill){new_size, start_colour});
}

static inline EditRecord *make_undo_insert_gradient(EditSky *const edit_sky,
  int const start, int const end, EditFill const fill)
{
  return make_undo_edit(edit_sky, EditRecordType_InsertGradient, start, end, 0,
    fill);
}

static inline EditRecord *make_undo_smooth(EditSky *const edit_sky,
  int const start, int const end)
{
  return make_undo_edit(edit_sky, EditRecordType_Smooth, start, end, 0,
    (EditFill){end - start});
}

static inline EditRecord *make_undo_set_plain(EditSky *const edit_sky,
  int const start, int const end, int const colour)
{
  return make_undo_edit(edit_sky, EditRecordType_SetPlain, start, end, 0,
    (EditFill){end - start, colour});
}

static inline EditRecord *make_undo_interpolate(EditSky *const edit_sky,
  int const start, int const end, int const start_colour,
  int const end_colour)
{
  return make_undo_edit(edit_sky, EditRecordType_Interpolate, start, end, 0,
    (EditFill){end - start, start_colour, end_colour, true, true});
}

static bool s_set_colour(Sky *const sky, int const pos, int const rep,
  unsigned char *const lost)
{
  assert(sky != NULL);
  assert(pos >= 0);
  assert(pos < NColourBands);
  assert(rep >= 0);
  assert(rep < NPixelColours);

  int const old = sky_get_colour(sky, pos);
  if (lost != NULL)
  {
    *lost = old;
  }

  bool changed = false;
  if (old != rep)
  {
    sky_set_colour(sky, pos, rep);
    changed = true;
  }
  return changed;
}

static bool s_write_plain(Sky *const sky, int const start, int const end,
  int const colour, unsigned char *const lost, int const lsize)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(lsize >= 0);
  assert(lsize <= end - start);
  assert(lost != NULL || lsize == 0);

  DEBUGF("Overwriting bands %d..%d in sky file %p with colour %d\n", start,
        end, (void *)sky, colour);

  /* Change colour bands to specified shade */
  bool changed = false;
  for (int pos = start; pos < end; pos++)
  {
    int const idx = pos - start;
    if (s_set_colour(sky, pos, colour, (idx < lsize) ? (lost + idx) : NULL))
    {
      changed = true;
    }
  }
  return changed;
}

static void s_get_array(Sky const *const sky, int const start, int const end,
  int *const dst)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(dst != NULL);

  DEBUGF("Copying bands %d..%d of sky file %p to array %p\n",
    start, end, (void *)sky, (void *)dst);

  for (int pos = start; pos < end; pos++)
  {
    dst[pos - start] = sky_get_colour(sky, pos);
  }
}

static bool s_set_array(Sky *const sky, int const start, int const end,
  int const *const src, unsigned char *const lost, int const lsize,
  bool *const is_valid)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(end >= start);
  assert(end <= NColourBands);
  assert(src != NULL);
  assert(lsize >= 0);
  assert(lsize <= end - start);
  assert(lost != NULL || lsize == 0);
  assert(is_valid != NULL);

  DEBUGF("Replacing %d..%d in sky file %p from array %p\n",
        start, end, (void *)sky, (void *)src);

  *is_valid = true;
  bool changed = false;
  for (int pos = start; pos < end; pos++)
  {
    int const idx = pos - start;
    int rep = src[idx];
    if (rep < 0 || rep >= NPixelColours)
    {
      rep = BadPixelColour;
      *is_valid = false;
      DEBUGF("Replaced invalid colour %d with %d\n", src[idx], rep);
      /* Continue to ensure that all bands are overwritten anyway */
    }
    if (s_set_colour(sky, pos, rep, (idx < lsize) ? (lost + idx) : NULL))
    {
      changed = true;
    }
  }
  return changed;
}

static bool s_copy_between(Sky *const dst, int const start, int const end,
  Sky const *const src, unsigned char *const lost, int const lsize)
{
  assert(src != NULL);
  assert(dst != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(lsize >= 0);
  assert(lsize <= end - start);
  assert(lost != NULL || lsize == 0);

  DEBUGF("Copying bands %d..%d in sky file %p to %p\n", start, end,
    (void *)src, (void *)dst);

  bool changed = false;
  for (int pos = start; pos < end; pos++)
  {
    int const idx = pos - start;
    int const rep = sky_get_colour(src, idx);
    if (s_set_colour(dst, pos, rep, (idx < lsize) ? (lost + idx) : NULL))
    {
      changed = true;
    }
  }
  return changed;
}

static void s_get_barray(Sky const *const sky, int const start, int const end,
   unsigned char *const dst)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(dst != NULL);

  DEBUGF("Copying bands %d..%d of sky file %p to byte array %p\n",
    start, end, (void *)sky, (void *)dst);

  for (int pos = start; pos < end; pos++)
  {
    dst[pos - start] = sky_get_colour(sky, pos);
  }
}

static bool s_set_barray(Sky *const sky, int const start, int const end,
   unsigned char const *const src, unsigned char *const lost, int const lsize)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(src != NULL);
  assert(lsize >= 0);
  assert(lsize <= end - start);
  assert(lost != NULL || lsize == 0);

  DEBUGF("Replacing %d..%d in sky file %p from byte array %p\n",
        start, end, (void *)sky, (void *)src);

  bool changed = false;
  for (int pos = start; pos < end; pos++)
  {
    int const idx = pos - start;
    if (s_set_colour(sky, pos, src[idx], (idx < lsize) ? (lost + idx) : NULL))
    {
      changed = true;
    }
  }
  return changed;
}

static bool s_budge_down(Sky *const sky, int const start,
  int const end, unsigned char *const lost)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);

  DEBUGF("Removing bands %d..%d from sky file %p\n", start, end, (void *)sky);

  int const size = end - start;
  if (size <= 0)
  {
    return false;
  }

  /* Copy colour bands downward, squashing the offending ones */
  bool changed = false;
  for (int pos = start; pos < NColourBands; pos++)
  {
    int const old = sky_get_colour(sky, pos);
    int rep = ExtendPixelColour;

    if (pos + size < NColourBands)
    {
      rep = sky_get_colour(sky, pos + size);
    }

    if (lost && (pos - start) < size)
    {
      int const idx = pos - start;
      lost[idx] = old;
      DEBUGF("Budge down saved %d:%d at %d\n", pos, old, idx);
    }

    if (old != rep)
    {
      sky_set_colour(sky, pos, rep);
      changed = true;
    }
  }
  return changed;
}

static bool s_budge_up(Sky *const sky, int const start,
  int const end, unsigned char *const lost)
{
  assert(sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);

  DEBUGF("Inserting bands %d..%d in sky file %p\n", start, end, (void *)sky);

  int const size = end - start;
  if (size <= 0)
  {
    return false;
  }

  /* Preserve a copy of the colour bands budged off the top.
     These aren't all overwritten in the loop below if EOF-end < size. */
  if (lost)
  {
    s_get_barray(sky, NColourBands - size, NColourBands, lost);
  }

  /* Copy colour bands upward to make room */
  bool changed = false;
  for (int pos = NColourBands - 1; pos >= end; pos--)
  {
    assert(pos >= size);

    int const old = sky_get_colour(sky, pos);
    int const rep = sky_get_colour(sky, pos - size);

    if (old != rep)
    {
      sky_set_colour(sky, pos, rep);
      changed = true;
    }
  }
  return changed;
}

static bool s_budge(Sky *const sky,
  int const old_end, int const new_end, unsigned char *lost)
{
  assert(sky != NULL);
  assert(old_end >= 0);
  assert(old_end <= NColourBands);
  assert(new_end >= 0);
  assert(new_end <= NColourBands);

  bool changed = false;

  if (new_end > old_end)
  {
    /* Replace data with larger data */
    if (s_budge_up(sky, old_end, new_end, lost))
    {
      changed = true;
    }
  }
  else if (new_end < old_end)
  {
    /* Replace data with smaller data (including deletions) */
    if (s_budge_down(sky, new_end, old_end, lost))
    {
      changed = true;
    }
  }
  return changed;
}

static bool s_unbudge(Sky *const sky, int const old_end, int const new_end,
  unsigned char *const lost)
{
  assert(sky != NULL);
  assert(old_end >= 0);
  assert(old_end <= NColourBands);
  assert(new_end >= 0);
  assert(new_end <= NColourBands);

  bool changed = false;

  if (new_end > old_end)
  {
    /* Undo replacing data with larger data */
    if (s_budge_down(sky, old_end, new_end, NULL))
    {
      changed = true;
    }

    /* Restore data budged off the top of the file */
    if (s_set_barray(sky, NColourBands - (new_end - old_end),
      NColourBands, lost, NULL, 0))
    {
      changed = true;
    }
  }
  else if (new_end < old_end)
  {
    /* Undo replacing data with smaller data (including deletions) */
    if (s_budge_up(sky, new_end, old_end, NULL))
    {
      changed = true;
    }

    /* Restore data lost above the (smaller) inserted data */
    if (s_set_barray(sky, new_end, old_end, lost, NULL, 0))
    {
      changed = true;
    }
  }

  return changed;
}

static inline void redraw_select(Editor *const editor,
  int const old_start, int const old_end, int const new_start, int const new_end)
{
  assert(editor != NULL);
  assert(old_start >= 0);
  assert(old_start <= NColourBands);
  assert(old_end >= 0);
  assert(old_end <= NColourBands);
  assert(new_start >= 0);
  assert(new_start <= NColourBands);
  assert(new_end >= 0);
  assert(new_end <= NColourBands);
  assert(old_start != new_start || old_end != new_end);

  int const new_low = LOWEST(new_start, new_end),
            new_high = HIGHEST(new_start, new_end);

  int const old_low = LOWEST(old_start, old_end),
            old_high = HIGHEST(old_start, old_end);

  if (new_low != old_low || new_high != old_high)
  {
    DEBUGF("Redraw selection %d..%d to %d..%d in editor %p of sky %p\n",
           old_low, old_high, new_low, new_high,
           (void *)editor, (void *)editor->edit_sky);

    editor->redraw_select_cb(editor, old_low, old_high, new_low, new_high);
  }
}

static bool set_selection(Editor *const editor, int const new_sel_start,
  int const new_sel_end)
{
  assert(editor != NULL);
  assert(new_sel_start >= 0);
  assert(new_sel_start <= NColourBands);
  assert(new_sel_end >= 0);
  assert(new_sel_end <= NColourBands);

  int const sel_start = editor->start;
  int const sel_end = editor->end;
  DEBUGF("Changing selection from %d..%d to %d..%d in editor %p\n",
         sel_start, sel_end, new_sel_start, new_sel_end, (void *)editor);

  if (new_sel_start == sel_start && new_sel_end == sel_end)
  {
    return false;
  }

  /* Record the new selection limits */
  editor->start = new_sel_start;
  editor->end = new_sel_end;

  redraw_select(editor, sel_start, sel_end, new_sel_start, new_sel_end);

  return true;
}

static int update_index(int const index, int const change_pos,
  int const ndel, int const nadd)
{
  assert(ndel >= 0);
  assert(nadd >= 0);
  return budge_index(budge_index(index, change_pos, -ndel), change_pos, nadd);
}

/* Update the selection to take account of 'ndel' colour bands replaced
   with 'nadd' colour bands at position 'pos'. */
static void update_indices(Editor *const editor, int const pos,
  int const ndel, int const nadd)
{
  assert(editor != NULL);

  /* The end of a selection can be extended by inserting data immediately
     after it! This is justifiable if one considers the selection as
     "everything up to but not including the next item". */
  int const new_start = update_index(editor->start, pos, ndel, nadd),
            new_end = update_index(editor->end, pos, ndel, nadd);

  (void)set_selection(editor, new_start, new_end);
}

static void all_update_indices(Editor const *const editor, int const start,
  int const old_end, int const new_end)
{
  assert(editor != NULL);
  assert(start >= 0);
  assert(start <= old_end);
  assert(start <= new_end);

  int const ndel = old_end - start;
  int const nadd = new_end - start;
  EditSky const *const edit_sky = editor->edit_sky;

  if (ndel || nadd) {
    /* Update every other editor to take account of 'ndel' colour bands
       replaced with 'nadd' colour bands at position 'pos'. The editor
       being used is updated separately to minimize redraws. */
    for (LinkedListItem *item = linkedlist_get_head(&edit_sky->editors);
         item != NULL;
         item = linkedlist_get_next(item))
    {
      Editor *const editor_item = CONTAINER_OF(item, Editor, node);
      if (editor != editor_item)
      {
        update_indices(editor_item, start, ndel, nadd);
      }
    }
  }
}

static bool delete_range(Editor *const editor, int const start,
  int const end, unsigned char *const lost)
{
  bool const changed = s_budge_down(&editor->edit_sky->sky, start, end, lost);
  all_update_indices(editor, start, end, start);
  return changed;
}

static bool s_interpolate(Sky *const sky, PaletteEntry const palette[],
  int const start, int const end, const EditFill fill, unsigned char *const lost,
  int const lsize)
{
  /* Write gradient fill between specified colours */
  assert(sky != NULL);
  assert(palette != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
  assert(fill.len >= 0);
  assert(fill.len >= end - start);
  assert(lsize >= 0);
  assert(lsize <= end - start);
  assert(lost != NULL || lsize == 0);

  DEBUGF("Interpolating %d bands %d..%d\n"
         "start colour:%d (%s) end colour:%d (%s)\n",
         fill.len, start, end,
         fill.start, fill.inc_start ? "inclusive" : "exclusive",
         fill.end, fill.inc_end ? "inclusive" : "exclusive");

  bool changed = false;
  int dist = fill.len;

  /* Include start colour? */
  int effective_start = start;
  if (fill.inc_start)
  {
    if (start < end)
    {
      int const idx = effective_start - start;
      if (s_set_colour(sky, effective_start, fill.start,
        (idx < lsize) ? (lost + idx): NULL))
      {
        changed = true;
      }
    }
    ++effective_start;
  }
  else
  {
    ++dist;
  }

  /* Include end colour? */
  int effective_end = start + fill.len;
  if (fill.inc_end)
  {
    --effective_end;
    if (effective_end < end && effective_end >= effective_start)
    {
      int const idx = effective_end - start;
      if (s_set_colour(sky, effective_end, fill.end,
        (idx < lsize) ? (lost + idx): NULL))
      {
        changed = true;
      }
    }
  }
  else
  {
    ++dist;
  }

  /* Middle part of colour gradient may be non-existent */
  if (effective_start >= effective_end)
  {
    return changed;
  }

  /* No. of transitions is one less than the no. of colours */
  assert(dist > 1);
  --dist;

  /* Top of the gradient may be out of range */
  if (effective_end > end)
  {
    effective_end = end;
  }

  /* Get 24-bit palette entries for start/end colours */
  const PaletteEntry start_palette = palette[fill.start],
                     end_palette = palette[fill.end];

  /* Calculate initial R/G/B values and increments for smooth gradient */
  assert(dist != 0);
  int col = PALETTE_GET_RED(start_palette);
  int diff = PALETTE_GET_RED(end_palette) - col;
  const float red_inc = (float)diff / dist;
  float red_frac = col;
  DEBUGF("RED start=%d diff=%d dist=%d increment=%f\n", col, diff, dist, red_inc);

  col = PALETTE_GET_GREEN(start_palette);
  diff = PALETTE_GET_GREEN(end_palette) - col;
  const float green_inc = (float)diff / dist;
  float green_frac = col;
  DEBUGF("GREEN start=%d diff=%d dist=%d increment=%f\n", col, diff, dist, green_inc);

  col = PALETTE_GET_BLUE(start_palette);
  diff = PALETTE_GET_BLUE(end_palette) - col;
  const float blue_inc = (float)diff / dist;
  float blue_frac = col;
  DEBUGF("BLUE start=%d diff=%d dist=%d increment=%f\n", col, diff, dist, blue_inc);

  /* Write middle part of colour gradient (this loop never draws the
     start and end colours, even if one or both is 'included') */
  for (int pos = effective_start; pos < effective_end; pos++)
  {
    /* Calculate transitional colour */
    red_frac += red_inc;
    green_frac += green_inc;
    blue_frac += blue_inc;
    DEBUG_VERBOSEF("Ideal colour for band %d is R=%f G=%f B=%f\n",
                  pos, red_frac, green_frac, blue_frac);

    unsigned int const near = nearest_palette_entry_rgb(
      palette, NPixelColours, (int)(red_frac + 0.5f),
      (int)(green_frac + 0.5f), (int)(blue_frac + 0.5f));

    DEBUG_VERBOSEF("Nearest mode 13 colour:%u (palette 0x%08x)\n",
                   near, palette[near]);

    int const idx = pos - start;
    if (s_set_colour(sky, pos, near,
      (idx < lsize) ? (lost + idx) : NULL))
    {
      changed = true;
    }
  }
  return changed;
}

static bool do_smooth(EditSky *const edit_sky, int const start, int const end,
  PaletteEntry const palette[])
{
  assert(edit_sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(start <= NColourBands);
  assert(palette != NULL);

  bool changed = false;
  int last_trans = start;
  int last_centre = start;
  Sky *const sky = &edit_sky->sky;

  for (int row = start + 1; row < end; row++)
  {
    if (sky_get_colour(sky, row) == sky_get_colour(sky, last_trans))
    {
      DEBUG_VERBOSEF("No transition detected on row %d\n", row);
      continue;
    }
    DEBUG_VERBOSEF("Transition found on row %d\n", row);

    /* Check for first transition (e.g. where none prior) */
    if (last_trans == start)
    {
      /* For first gradient, pretend that first row is prev centre */
      DEBUG_VERBOSEF("This was the first transition\n");
      last_centre = start;
    }
    else
    {
      DEBUG_VERBOSEF("Start of current homogenous area was %d\n", last_trans);

      /* Calculate centrepoint of previous colour area */
      assert(row > last_trans);
      int const centre = last_trans + (row - last_trans) / 2;
      DEBUG_VERBOSEF("Centre of current area is %d (centre of last was %d)\n",
                    centre, last_centre);

      /* Re-paint transition between previous area and preceeding one */
      assert(centre >= last_centre);
      if (centre - last_centre >= 2)
      {
        if (s_interpolate(&edit_sky->sky, palette,
          last_centre + 1, centre,
          (EditFill){centre - last_centre - 1,
                     sky_get_colour(sky, last_centre),
                     sky_get_colour(sky, centre), false, false}, NULL, 0))
        {
          redraw_bands(edit_sky, last_centre + 1, centre);
          changed = true;
        }
      }

      /* init search for next transition / centre of new area */
      last_centre = centre;
    }
    DEBUG_VERBOSEF("Centre of last homogenous area was %d\n", last_centre);
    last_trans = row;
  }

  if (last_trans == start)
  {
    DEBUGF("No transitions detected\n");
  }
  else
  {
    /* To smooth to last row, pretend that it is a final centre */
    assert(end > last_centre);
    DEBUGF("Last row is %d, last centre is %d\n", end - 1, last_centre);
    if (end - last_centre >= 3)
    {
      if (s_interpolate(&edit_sky->sky, palette,
        last_centre + 1, end - 1,
        (EditFill){end - last_centre - 2,
                   sky_get_colour(sky, last_centre),
                   sky_get_colour(sky, end - 1),
                   false, false}, NULL, 0))
      {
        redraw_bands(edit_sky, last_centre + 1, end - 1);
        changed = true;
      }
    }
  }

  DEBUGF("Finished smoothing (file %schanged)\n", changed ? "" : "not ");
  return changed;
}

static bool prepare_import(Editor *const editor, EditRecord *const rec)
{
  assert(rec != NULL);

  EditSky *const edit_sky = editor->edit_sky;

  bool changed = s_budge(&edit_sky->sky,
    rec->data.edit.old_dst_end, rec->data.edit.new_dst_end,
    rec->data.edit.budge_lost);

  all_update_indices(editor, rec->data.edit.dst_start,
    rec->data.edit.old_dst_end, rec->data.edit.new_dst_end);

  return changed;
}

static void caret_after_insert(Editor *const editor, EditRecord *const rec)
{
  assert(rec != NULL);
  (void)set_selection(editor, rec->data.edit.new_dst_end, rec->data.edit.new_dst_end);
}

static void select_inserted(Editor *const editor, EditRecord *const rec)
{
  assert(rec != NULL);
  (void)set_selection(editor, rec->data.edit.dst_start, rec->data.edit.new_dst_end);
}

static void select_replaced(Editor *const editor, EditRecord *const rec)
{
  assert(rec != NULL);
  (void)set_selection(editor, rec->data.edit.dst_start, rec->data.edit.old_dst_end);
}

static void select_move_dst(Editor *const editor, EditRecord *const rec)
{
  assert(rec != NULL);
  int const src_size = rec->data.edit.new_dst_end -
                       rec->data.edit.dst_start;

  /* Update the replace location in case the source data precedes it
     and the replace location will therefore shift upward */
  int const dst_start = budge_index(rec->data.edit.dst_start,
    rec->data.edit.src_start, src_size);

  int const dst_end = budge_index(rec->data.edit.old_dst_end,
    rec->data.edit.src_start, src_size);

  (void)set_selection(editor, dst_start, dst_end);
}

static void redraw_changed(EditSky *const edit_sky,
  EditRecord const *const rec)
{
  assert(rec != NULL);

  if (rec->data.edit.old_dst_end == rec->data.edit.new_dst_end)
  {
    DEBUGF("Have replaced selection with data of equal size\n");
    redraw_bands(edit_sky, rec->data.edit.dst_start, rec->data.edit.old_dst_end);
  }
  else
  {
    DEBUGF("All data above the insertion point will have shifted\n");
    redraw_bands(edit_sky, rec->data.edit.dst_start, NColourBands);
  }
}

static bool undo_edit(Editor *const editor, EditRecord const *const rec)
{
  assert(editor != NULL);
  assert(rec != NULL);

  bool changed = false;
  EditSky *const edit_sky = editor->edit_sky;

  /* Restore data that was overwritten in-place.
     (Often none, e.g. if data was inserted at the caret.) */
  if (s_set_barray(&edit_sky->sky, rec->data.edit.dst_start,
    rec->data.edit.dst_start + rec->data.edit.lsize,
    rec->data.edit.lost, NULL, 0))
  {
    changed = true;
  }

  if (s_unbudge(&edit_sky->sky, rec->data.edit.old_dst_end,
    rec->data.edit.new_dst_end, rec->data.edit.budge_lost))
  {
    changed = true;
  }

  switch (rec->type)
  {
  case EditRecordType_Move:
  case EditRecordType_Copy:
  case EditRecordType_InsertArray:
  case EditRecordType_InsertPlain:
  case EditRecordType_InsertGradient:
    all_update_indices(editor, rec->data.edit.dst_start,
      rec->data.edit.new_dst_end, rec->data.edit.old_dst_end);
    break;

  default:
    break;
  }
  return changed;
}

static bool undo_move(Editor *const editor, EditRecord const *const rec)
{
  assert(editor != NULL);
  assert(rec != NULL);
  assert(rec->type == EditRecordType_Move);

  /* Undo the insert */
  bool changed = undo_edit(editor, rec);

  EditSky *const edit_sky = editor->edit_sky;

  /* Reinstate the source data */
  int const src_size = rec->data.edit.new_dst_end -
                       rec->data.edit.dst_start;
  int const src_end = rec->data.edit.src_start + src_size;

  if (s_budge_up(&edit_sky->sky, rec->data.edit.src_start, src_end, NULL))
  {
    changed = true;
  }

  if (s_set_barray(&edit_sky->sky, rec->data.edit.src_start, src_end,
    rec->data.edit.fresh, NULL, 0))
  {
    changed = true;
  }

  all_update_indices(editor, rec->data.edit.src_start,
    rec->data.edit.src_start, src_end);

  return changed;
}

static bool redo_insert(Editor *const editor, EditRecord const *const rec,
  PaletteEntry const palette[])
{
  assert(editor != NULL);
  assert(rec != NULL);

  EditSky *const edit_sky = editor->edit_sky;

  bool changed = s_budge(&edit_sky->sky,
    rec->data.edit.old_dst_end, rec->data.edit.new_dst_end, NULL);

  all_update_indices(editor, rec->data.edit.dst_start,
    rec->data.edit.old_dst_end, rec->data.edit.new_dst_end);

  switch (rec->type)
  {
  case EditRecordType_Move:
  case EditRecordType_Copy:
  case EditRecordType_InsertArray:
    if (s_set_barray(&edit_sky->sky, rec->data.edit.dst_start,
      rec->data.edit.new_dst_end, rec->data.edit.fresh, NULL, 0))
    {
      changed = true;
    }
    break;
  case EditRecordType_InsertPlain:
    if (s_write_plain(&edit_sky->sky, rec->data.edit.dst_start,
      rec->data.edit.new_dst_end, rec->data.edit.fill.start, NULL, 0))
    {
      changed = true;
    }
    break;
  case EditRecordType_InsertGradient:
    if (s_interpolate(&edit_sky->sky, palette,
                      rec->data.edit.dst_start, rec->data.edit.new_dst_end,
                      rec->data.edit.fill, NULL, 0))
    {
      changed = true;
    }
    break;
  default:
    DEBUGF("Bad redo type\n");
    break;
  }

  return changed;
}

static bool redo_move(Editor *const editor, EditRecord const *const rec)
{
  assert(editor != NULL);
  assert(rec != NULL);
  assert(rec->type == EditRecordType_Move);

  int const src_end = rec->data.edit.src_start +
                      (rec->data.edit.new_dst_end - rec->data.edit.dst_start);

  bool changed = delete_range(editor, rec->data.edit.src_start, src_end, NULL);

  if (redo_insert(editor, rec, NULL))
  {
    changed = true;
  }

  return changed;
}

static bool set_stars_height(EditSky *const edit_sky,
  int const stars_height, EditRecord *const rec)
{
  assert(edit_sky);
  int const old = sky_get_stars_height(&edit_sky->sky);
  if (rec != NULL)
  {
    rec->data.values.stars = (EditValueSwap){.old = old, .rep = stars_height};
  }
  if (stars_height == old)
  {
    return false;
  }
  sky_set_stars_height(&edit_sky->sky, stars_height);
  redraw_stars_height(edit_sky);
  return true;
}

static bool set_render_offset(EditSky *const edit_sky,
  int const render_offset, EditRecord *const rec)
{
  assert(edit_sky);
  int const old = sky_get_render_offset(&edit_sky->sky);
  if (rec != NULL)
  {
    rec->data.values.render = (EditValueSwap){.old = old, .rep = render_offset};
  }
  if (render_offset == old)
  {
    return false;
  }
  sky_set_render_offset(&edit_sky->sky, render_offset);
  redraw_render_offset(edit_sky);
  return true;
}

static void dummy_redraw_range(EditSky *const edit_sky,
  int const start, int const end)
{
  NOT_USED(edit_sky);
  NOT_USED(start);
  NOT_USED(end);
  assert(edit_sky != NULL);
  assert(start >= 0);
  assert(start <= end);
  assert(end <= NColourBands);
}

static void dummy_redraw_value(EditSky *const edit_sky)
{
  NOT_USED(edit_sky);
  assert(edit_sky != NULL);
}

SkyState edit_sky_init(EditSky *const edit_sky, Reader *const reader,
  void (*const redraw_bands_cb)(EditSky *, int, int),
  void (*const redraw_render_offset_cb)(EditSky *),
  void (*const redraw_stars_height_cb)(EditSky *))
{
  assert(edit_sky != NULL);
  SkyState state = SkyState_OK;
  if (reader)
  {
    state = sky_read_file(&edit_sky->sky, reader);
  }
  else
  {
    sky_init(&edit_sky->sky);
  }
  linkedlist_init(&edit_sky->editors);

  edit_sky->redraw_bands_cb = redraw_bands_cb ?
    redraw_bands_cb : dummy_redraw_range;

  edit_sky->redraw_render_offset_cb = redraw_render_offset_cb ?
    redraw_render_offset_cb : dummy_redraw_value;

  edit_sky->redraw_stars_height_cb = redraw_stars_height_cb ?
    redraw_stars_height_cb : dummy_redraw_value;

  linkedlist_init(&edit_sky->undo_list);
  edit_sky->next_undo = NULL;

  return state;
}

void edit_sky_destroy(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  (void)linkedlist_for_each(&edit_sky->undo_list, destroy_record, NULL);
}

Sky *edit_sky_get_sky(EditSky *const edit_sky)
{
  assert(edit_sky != NULL);
  return &edit_sky->sky;
}

bool editor_can_undo(Editor const *const editor)
{
  assert(editor != NULL);
  EditSky const *const edit_sky = editor->edit_sky;
  assert(edit_sky != NULL);
  return edit_sky->next_undo != NULL;
}

bool editor_can_redo(Editor const *const editor)
{
  assert(editor != NULL);
  EditSky *const edit_sky = editor->edit_sky;
  assert(edit_sky != NULL);
  return edit_sky->next_undo !=
         linkedlist_get_tail(&edit_sky->undo_list);
}

bool editor_undo(Editor *const editor)
{
  assert(editor != NULL);
  if (!editor_can_undo(editor))
  {
    DEBUGF("Nothing to undo\n");
    return false;
  }

  EditSky *const edit_sky = editor->edit_sky;
  assert(edit_sky != NULL);
  EditRecord *const rec = CONTAINER_OF(edit_sky->next_undo,
    EditRecord, link);

  edit_sky->next_undo = linkedlist_get_prev(&rec->link);

  bool changed = false;
  DEBUGF("Undo of type %d\n", (int)rec->type);
  switch (rec->type)
  {
  case EditRecordType_SetStarsHeight:
    changed = set_stars_height(edit_sky, rec->data.values.stars.old, NULL);
    break;
  case EditRecordType_SetRenderOffset:
    changed = set_render_offset(edit_sky, rec->data.values.render.old, NULL);
    break;
  case EditRecordType_AddRenderOffset:
    if (set_stars_height(edit_sky, rec->data.values.stars.old, NULL))
    {
      changed = true;
    }
    if (set_render_offset(edit_sky, rec->data.values.render.old, NULL))
    {
      changed = true;
    }
    break;
  case EditRecordType_Move:
    changed = undo_move(editor, rec);
    if (changed)
    {
      redraw_move(edit_sky, rec);
    }
    break;
  case EditRecordType_Copy:
  case EditRecordType_SetPlain:
  case EditRecordType_Smooth:
  case EditRecordType_Interpolate:
  case EditRecordType_InsertArray:
  case EditRecordType_InsertPlain:
  case EditRecordType_InsertGradient:
    changed = undo_edit(editor, rec);
    if (changed)
    {
      redraw_changed(edit_sky, rec);
    }
    break;
  default:
    DEBUGF("Bad undo type\n");
    break;
  }

  switch (rec->type)
  {
  case EditRecordType_Move:
    select_move_dst(editor, rec);
    break;
  case EditRecordType_Copy:
  case EditRecordType_SetPlain:
  case EditRecordType_Smooth:
  case EditRecordType_Interpolate:
  case EditRecordType_InsertArray:
  case EditRecordType_InsertPlain:
  case EditRecordType_InsertGradient:
    select_replaced(editor, rec);
    break;
  case EditRecordType_SetStarsHeight:
  case EditRecordType_SetRenderOffset:
  case EditRecordType_AddRenderOffset:
    break;
  }
  return changed;
}

bool editor_redo(Editor *const editor, PaletteEntry const palette[])
{
  assert(editor != NULL);
  if (!editor_can_redo(editor))
  {
    DEBUGF("Nothing to redo\n");
    return false;
  }

  EditSky *const edit_sky = editor->edit_sky;
  assert(edit_sky != NULL);
  LinkedListItem *const redo_item = get_redo_item(edit_sky);
  assert(redo_item != NULL);
  EditRecord *const rec = CONTAINER_OF(redo_item, EditRecord, link);
  edit_sky->next_undo = redo_item;

  bool changed = false;
  DEBUGF("Redo of type %d\n", (int)rec->type);
  switch (rec->type)
  {
  case EditRecordType_SetStarsHeight:
    changed = set_stars_height(edit_sky, rec->data.values.stars.rep, NULL);
    break;
  case EditRecordType_SetRenderOffset:
    changed = set_render_offset(edit_sky, rec->data.values.render.rep, NULL);
    break;
  case EditRecordType_AddRenderOffset:
    if (set_stars_height(edit_sky, rec->data.values.stars.rep, NULL))
    {
      changed = true;
    }
    if (set_render_offset(edit_sky, rec->data.values.render.rep, NULL))
    {
      changed = true;
    }
    break;
  case EditRecordType_SetPlain:
    if (s_write_plain(&edit_sky->sky, rec->data.edit.dst_start,
      rec->data.edit.old_dst_end, rec->data.edit.fill.start, NULL, 0))
    {
      redraw_bands(edit_sky, rec->data.edit.dst_start, rec->data.edit.old_dst_end);
      changed = true;
    }
    break;
  case EditRecordType_Smooth:
    changed = do_smooth(edit_sky, rec->data.edit.dst_start,
                rec->data.edit.old_dst_end, palette);
    break;
  case EditRecordType_Interpolate:
    if (s_interpolate(&edit_sky->sky, palette,
      rec->data.edit.dst_start, rec->data.edit.old_dst_end,
      rec->data.edit.fill, NULL, 0))
    {
      redraw_bands(edit_sky, rec->data.edit.dst_start,
                   rec->data.edit.old_dst_end);
      changed = true;
    }
    break;
  case EditRecordType_Move:
    changed = redo_move(editor, rec);
    if (changed)
    {
      redraw_move(edit_sky, rec);
    }
    break;
  case EditRecordType_Copy:
  case EditRecordType_InsertArray:
  case EditRecordType_InsertPlain:
  case EditRecordType_InsertGradient:
    changed = redo_insert(editor, rec, palette);
    if (changed)
    {
      redraw_changed(edit_sky, rec);
    }
    break;
  default:
    DEBUGF("Bad redo type\n");
    break;
  }

  switch (rec->type)
  {
  case EditRecordType_Move:
  case EditRecordType_Copy:
  case EditRecordType_SetPlain:
  case EditRecordType_Smooth:
  case EditRecordType_Interpolate:
  case EditRecordType_InsertArray:
    select_inserted(editor, rec);
    break;
  case EditRecordType_InsertPlain:
  case EditRecordType_InsertGradient:
    (void)set_selection(editor, rec->data.edit.new_dst_end,
                        rec->data.edit.new_dst_end);
    break;
  case EditRecordType_SetStarsHeight:
  case EditRecordType_SetRenderOffset:
  case EditRecordType_AddRenderOffset:
    break;
  }
  return changed;
}

EditResult edit_sky_set_render_offset(EditSky *const edit_sky,
  int render_offset)
{
  assert(edit_sky != NULL);
  if (render_offset < MinRenderOffset)
  {
    DEBUGF("Clamped render offset %d\n", render_offset);
    render_offset = MinRenderOffset;
  }
  else if (render_offset > MaxRenderOffset)
  {
    DEBUGF("Clamped render offset %d\n", render_offset);
    render_offset = MaxRenderOffset;
  }
  DEBUGF("Setting render offset %d\n", render_offset);

  EditRecord *const rec = make_undo_values(edit_sky, EditRecordType_SetRenderOffset);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  if (set_render_offset(edit_sky, render_offset, rec))
  {
    return EditResult_Changed;
  }

  return EditResult_Unchanged;
}

EditResult edit_sky_add_render_offset(EditSky *const edit_sky, int offset)
{
  assert(edit_sky != NULL);

  DEBUGF("Increasing render offset by %d\n", offset);

  int render_offset = sky_get_render_offset(&edit_sky->sky);
  assert(render_offset >= MinRenderOffset);
  assert(render_offset <= MaxRenderOffset);

  if (offset < MinRenderOffset - render_offset)
  {
    DEBUGF("Clamped offset %d\n", offset);
    offset = MinRenderOffset - render_offset;
  }
  else if (offset > MaxRenderOffset - render_offset)
  {
    DEBUGF("Clamped offset %d\n", offset);
    offset = MaxRenderOffset - render_offset;
  }
  render_offset += offset;
  DEBUGF("Setting render offset %d\n", render_offset);

  int stars_height = sky_get_stars_height(&edit_sky->sky);
  assert(stars_height >= MinStarsHeight);
  assert(stars_height <= MaxRenderOffset);

  if (offset > stars_height - MinStarsHeight)
  {
    DEBUGF("Clamped offset %d\n", offset);
    offset = stars_height - MinStarsHeight;
  }
  else if (offset < stars_height - MaxStarsHeight)
  {
    DEBUGF("Clamped offset %d\n", offset);
    offset = stars_height - MaxStarsHeight;
  }
  stars_height -= offset;
  DEBUGF("Setting stars height %d\n", stars_height);

  EditRecord *const rec = make_undo_values(edit_sky, EditRecordType_AddRenderOffset);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  bool changed = false;

  if (set_stars_height(edit_sky, stars_height, rec))
  {
    changed = true;
  }

  if (set_render_offset(edit_sky, render_offset, rec))
  {
    changed = true;
  }

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult edit_sky_set_stars_height(EditSky *const edit_sky,
  int stars_height)
{
  assert(edit_sky != NULL);

  if (stars_height < MinStarsHeight)
  {
    DEBUGF("Clamped stars_height %d\n", stars_height);
    stars_height = MinStarsHeight;
  }
  else if (stars_height > MaxStarsHeight)
  {
    DEBUGF("Clamped stars_height %d\n", stars_height);
    stars_height = MaxStarsHeight;
  }
  DEBUGF("Setting stars height %d\n", stars_height);

  EditRecord *const rec = make_undo_values(edit_sky, EditRecordType_SetStarsHeight);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  if (set_stars_height(edit_sky, stars_height, rec))
  {
    return EditResult_Changed;
  }

  return EditResult_Unchanged;
}

static void dummy_redraw_select(Editor *const editor,
  int const old_low, int const old_high,
  int const new_low, int const new_high)
{
  NOT_USED(editor);
  NOT_USED(old_low);
  NOT_USED(old_high);
  NOT_USED(new_low);
  NOT_USED(new_high);
  assert(editor != NULL);
  assert(old_low >= 0);
  assert(old_low <= old_high);
  assert(old_high <= NColourBands);
  assert(new_low >= 0);
  assert(new_low <= new_high);
  assert(new_high <= NColourBands);
  assert(old_low != new_low || old_high != new_high);
}

void editor_init(Editor *const editor, EditSky *const edit_sky,
  void (*const redraw_select_cb)(Editor *, int, int, int, int))
{
  assert(editor != NULL);
  assert(edit_sky != NULL);

  editor->edit_sky = edit_sky;
  editor->redraw_select_cb = redraw_select_cb ?
                             redraw_select_cb : dummy_redraw_select;
  editor->start = 0; /* caret starts at bottom */
  editor->end = 0;

  linkedlist_insert(&edit_sky->editors, NULL, &editor->node);
}

void editor_destroy(Editor *const editor)
{
  assert(editor != NULL);
  EditSky *const edit_sky = editor->edit_sky;
  assert(edit_sky != NULL);
  linkedlist_remove(&edit_sky->editors, &editor->node);
}

Sky *editor_get_sky(Editor const *const editor)
{
  assert(editor != NULL);
  return edit_sky_get_sky(editor->edit_sky);
}

bool editor_has_selection(Editor const *editor)
{
  assert(editor != NULL);
  return editor->end != editor->start;
}

void editor_get_selection_range(Editor const *editor,
  int *const sel_low, int *const sel_high)
{
  assert(editor != NULL);
  int const sel_start = editor->start,
            sel_end = editor->end;

  if (sel_low)
  {
    *sel_low = LOWEST(sel_start, sel_end);
  }
  if (sel_high)
  {
    *sel_high = HIGHEST(sel_start, sel_end);
  }
}

bool editor_clear_selection(Editor *const editor)
{
  assert(editor != NULL);
  DEBUGF("Clearing selection in editor %p\n", (void *)editor);
  return editor_set_caret_pos(editor, editor->start);
}

bool editor_select_all(Editor *const editor)
{
  return set_selection(editor, 0, NColourBands);
}

bool editor_set_selection_nearest(Editor *const editor, int pos)
{
  assert(editor != NULL);
  pos = clamp_pos(pos);
  DEBUGF("Setting selection_nearest %d\n", pos);

  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);

  int const keep = abs(pos - sel_low) < abs(pos - sel_high) ?
                   sel_high : sel_low;

  return set_selection(editor, keep, pos);
}

int editor_get_caret_pos(Editor const *const editor)
{
  assert(editor != NULL);
  return editor->start;
}

bool editor_set_caret_pos(Editor *const editor, int pos)
{
  assert(editor != NULL);
  pos = clamp_pos(pos);
  DEBUGF("Setting caret_pos %d\n", pos);

  return set_selection(editor, pos, pos);
}

bool editor_set_selection_end(Editor *const editor, int pos)
{
  pos = clamp_pos(pos);
  DEBUGF("Setting selection_end %d\n", pos);

  return set_selection(editor, editor->start, pos);
}

int editor_get_selected_colour(Editor const *const editor)
{
  assert(editor != NULL);
  assert(editor_has_selection(editor));

  return sky_get_colour(&editor->edit_sky->sky,
    LOWEST(editor->start, editor->end));
}

int editor_get_array(Editor const *const editor, int *const dst,
  int const dst_size)
{
  assert(editor != NULL);
  assert(dst != NULL);
  assert(dst_size >= 0);

  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);
  if (sel_low == sel_high)
  {
    return 0;
  }

  assert(sel_high >= sel_low);
  int const start = sel_low;
  int end = sel_high;
  int const src_size = sel_high - sel_low;
  if (src_size > dst_size)
  {
    end = start + dst_size;
  }

  s_get_array(&editor->edit_sky->sky, start, end, dst);
  return sel_high - sel_low;
}

EditResult editor_smooth(Editor *const editor, PaletteEntry const palette[])
{
  assert(editor != NULL);
  assert(palette != NULL);

  int start, end;
  editor_get_selection_range(editor, &start, &end);
  DEBUGF("Smoothing %d..%d in editor %p\n",
    start, end, (void *)editor);

  EditSky *const edit_sky = editor->edit_sky;
  EditRecord *const rec = make_undo_smooth(edit_sky, start, end);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  Sky *const sky = edit_sky_get_sky(edit_sky);
  s_get_barray(sky, start, end, rec->data.edit.lost);

  return do_smooth(edit_sky, start, end, palette) ?
    EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_set_plain(Editor *const editor, int colour)
{
  assert(editor != NULL);
  colour = clamp_colour(colour);

  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);
  DEBUGF("Replacing %d..%d in editor %p with colour %d\n",
    sel_low, sel_high, (void *)editor, colour);

  EditRecord *const rec = make_undo_set_plain(editor->edit_sky,
    sel_low, sel_high, colour);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  bool changed = false;
  EditSky *const edit_sky = editor->edit_sky;

  if (s_write_plain(&edit_sky->sky, sel_low, sel_high, colour,
    rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
    redraw_bands(edit_sky, sel_low, sel_high);
  }
  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_interpolate(Editor *const editor,
  PaletteEntry const palette[], int start_col, int end_col)
{
  assert(editor != NULL);
  start_col = clamp_colour(start_col);
  end_col = clamp_colour(end_col);

  int sel_low, sel_high;
  editor_get_selection_range(editor, &sel_low, &sel_high);
  DEBUGF("Replacing %d..%d in editor %p with gradient %d,%d\n",
    sel_low, sel_high, (void *)editor, start_col, end_col);

  EditSky *const edit_sky = editor->edit_sky;
  EditRecord *const rec = make_undo_interpolate(edit_sky,
    sel_low, sel_high, start_col, end_col);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  if (!s_interpolate(&edit_sky->sky, palette,
                     sel_low, sel_high,
                     (EditFill){sel_high - sel_low,
                                start_col, end_col, true, true},
                     rec->data.edit.lost, rec->data.edit.lsize))
  {
    return EditResult_Unchanged;
  }

  redraw_bands(edit_sky, sel_low, sel_high);
  return EditResult_Changed;
}

EditResult editor_insert_array(Editor *const editor, int const src_size,
  int const *const src, bool *const is_valid)
{
  assert(editor != NULL);
  assert(src_size >= 0);
  assert(src != NULL);

  int dst_start, dst_end;
  editor_get_selection_range(editor, &dst_start, &dst_end);
  DEBUGF("Replacing %d..%d in editor %p from array %p of size %d\n",
         dst_start, dst_end, (void *)editor, (void *)src, src_size);

  EditSky *const edit_sky = editor->edit_sky;
  EditRecord *const rec = make_undo_insert_array(edit_sky,
    dst_start, dst_end, src_size);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  bool changed = prepare_import(editor, rec);

  if (s_set_array(&edit_sky->sky, dst_start, rec->data.edit.new_dst_end,
    src, rec->data.edit.lost, rec->data.edit.lsize, is_valid))
  {
    changed = true;
  }

  s_get_barray(&edit_sky->sky, dst_start, rec->data.edit.new_dst_end,
    rec->data.edit.fresh);

  if (changed)
  {
    redraw_changed(edit_sky, rec);
  }

  /* Select the inserted data "so that the user can immediately cut
     it again should this be desired". */
  select_inserted(editor, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_insert_sky(Editor *const editor, Sky const *const src)
{
  assert(editor != NULL);
  assert(src != NULL);
  assert(src != &editor->edit_sky->sky);

  int dst_start, dst_end;
  editor_get_selection_range(editor, &dst_start, &dst_end);
  DEBUGF("Replacing %d..%d in editor %p from sky file %p\n",
        dst_start, dst_end, (void *)editor, (void *)src);

  EditSky *const edit_sky = editor->edit_sky;
  EditRecord *const rec = make_undo_insert_array(edit_sky,
    dst_start, dst_end, NColourBands);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  int const trunc_src_size = rec->data.edit.new_dst_end - dst_start;
  assert(trunc_src_size <= NColourBands);
  s_get_barray(src, 0, trunc_src_size, rec->data.edit.fresh);

  bool changed = prepare_import(editor, rec);

  if (s_copy_between(&edit_sky->sky, dst_start,
    rec->data.edit.new_dst_end, src, rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
  }

  if (changed)
  {
    redraw_changed(edit_sky, rec);
  }

  /* Select the inserted data "so that the user can immediately cut
     it again should this be desired". */
  select_inserted(editor, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_insert_plain(Editor *const editor, int const number, int col)
{
  assert(editor != NULL);
  assert(number >= 0);
  col = clamp_colour(col);

  int dst_start, dst_end;
  editor_get_selection_range(editor, &dst_start, &dst_end);
  DEBUGF("Replacing %d..%d in editor %p with colour %d of size %d\n",
    dst_start, dst_end, (void *)editor, col, number);

  EditSky *const edit_sky = editor->edit_sky;
  EditRecord *const rec = make_undo_insert_plain(edit_sky,
    dst_start, dst_end, number, col);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  bool changed = prepare_import(editor, rec);

  if (s_write_plain(&edit_sky->sky, dst_start, rec->data.edit.new_dst_end,
    col, rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
  }

  if (changed)
  {
    redraw_changed(edit_sky, rec);
  }

  /* Move caret above the inserted data to make it easy to append */
  caret_after_insert(editor, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_insert_gradient(Editor *const editor,
  PaletteEntry const palette[], int const number, int start_col,
  int end_col, bool const inc_start, bool const inc_end)
{
  assert(editor != NULL);
  assert(number >= 0);
  start_col = clamp_colour(start_col);
  end_col = clamp_colour(end_col);

  int dst_start, dst_end;
  editor_get_selection_range(editor, &dst_start, &dst_end);
  DEBUGF("Replacing %d..%d in editor %p with gradient %d,%d of size %d\n",
    dst_start, dst_end, (void *)editor, start_col, end_col, number);

  EditSky *const edit_sky = editor->edit_sky;
  EditFill const fill = {number, start_col, end_col, inc_start, inc_end};
  EditRecord *const rec = make_undo_insert_gradient(edit_sky,
    dst_start, dst_end, fill);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  bool changed = prepare_import(editor, rec);

  if (s_interpolate(&edit_sky->sky, palette, dst_start,
      rec->data.edit.new_dst_end, fill,
      rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
  }

  if (changed)
  {
    redraw_changed(edit_sky, rec);
  }

  /* Move caret above the inserted data to make it easy to append */
  caret_after_insert(editor, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_delete_colours(Editor *const editor)
{
  return editor_insert_plain(editor, 0, BadPixelColour);
}

EditResult editor_copy(Editor *const dst, Editor const *const src)
{
  assert(dst != NULL);
  assert(src != NULL);

  int src_start, src_end;
  editor_get_selection_range(src, &src_start, &src_end);

  int dst_start, dst_end;
  editor_get_selection_range(dst, &dst_start, &dst_end);

  EditSky *const src_sky = src->edit_sky;
  EditSky *const dst_sky = dst->edit_sky;

  if (src_sky == dst_sky && dst_start == src_start &&
      dst_end == src_end)
  {
    DEBUGF("Copy block (%d,%d) to itself\n", src_start, src_end);
    return EditResult_Unchanged;
  }

  DEBUGF("Copy from %d,%d in editor %p to %d,%d in editor %p\n",
    src_start, src_end, (void *)src, dst_start, dst_end, (void *)dst);

  EditRecord *const rec = make_undo_copy(dst->edit_sky, dst_start, dst_end,
                                         src_start, src_end);
  if (!rec)
  {
    return EditResult_NoMem;
  }

  int const trunc_src_size = rec->data.edit.new_dst_end - dst_start;

  /* We would need to copy the source data to a temporary buffer regardless
     of undo/redo because it can be budged off the top of the file. In any
     case the source file may be closed before the destination file. */
  s_get_barray(&src_sky->sky, src_start, src_start + trunc_src_size,
    rec->data.edit.fresh);

  bool changed = prepare_import(dst, rec);

  if (s_set_barray(&dst_sky->sky, dst_start, rec->data.edit.new_dst_end,
    rec->data.edit.fresh, rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
  }

  if (changed)
  {
    redraw_changed(dst_sky, rec);
  }

  /* Select the inserted data "so that the user can immediately cut
     it again should this be desired". */
  select_inserted(dst, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}

EditResult editor_move(Editor *const dst, Editor const *const src)
{
  assert(dst != NULL);
  assert(src != NULL);
  assert(src->edit_sky == dst->edit_sky);

  int src_start, src_end;
  editor_get_selection_range(src, &src_start, &src_end);

  int dst_start, dst_end;
  editor_get_selection_range(dst, &dst_start, &dst_end);

  if (dst_start >= src_start && dst_end <= src_end)
  {
    DEBUGF("Move block (%d,%d) to itself\n", src_start, src_end);
    return EditResult_Unchanged;
  }

  DEBUGF("Move from %d,%d in editor %p to %d,%d in editor %p\n",
    src_start, src_end, (void *)src, dst_start, dst_end, (void *)dst);

  /* Update the replace location in case the source data precedes it
     and the replace location will therefore shift downward */
  int const src_size = src_end - src_start;
  int const n_dst_start = budge_index(dst_start, src_start, -src_size);
  int const n_dst_end = budge_index(dst_end, src_start, -src_size);

  EditSky *const edit_sky = dst->edit_sky;
  EditRecord *const rec = make_undo_move(edit_sky,
    n_dst_start, n_dst_end, src_start, src_end);

  if (!rec)
  {
    return EditResult_NoMem;
  }

  /* We would need to copy the source data to a temporary buffer regardless
     of undo/redo because it can be budged off the top of the file */
  bool changed = delete_range(dst, src_start, src_end,
    rec->data.edit.fresh);

  if (prepare_import(dst, rec))
  {
    changed = true;
  }

  if (s_set_barray(&edit_sky->sky, n_dst_start, rec->data.edit.new_dst_end,
    rec->data.edit.fresh, rec->data.edit.lost, rec->data.edit.lsize))
  {
    changed = true;
  }

  if (changed)
  {
    redraw_move(edit_sky, rec);
  }

  /* Select the inserted data "so that the user can immediately cut
     it again should this be desired". */
  select_inserted(dst, rec);

  return changed ? EditResult_Changed : EditResult_Unchanged;
}
