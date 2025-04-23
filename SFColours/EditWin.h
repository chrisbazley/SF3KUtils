/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Colours data editing windows
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFCEditWin_h
#define SFCEditWin_h

#include <stdbool.h>

#include "wimp.h"
#include "toolbox.h"

#include "Reader.h"
#include "Writer.h"
#include "SFFormats.h"

#include "ColMap.h"
#include "ExpColFile.h"

enum
{
  EditWin_MaxSize = ARRAY_SIZE( ((SFObjectColours *)0)->colour_mappings ) -
                    ARRAY_SIZE( ((SFObjectColours *)0)->areas.static_colours ),
};

/* Use relative colour positions when pasting from the clipboard? */
#define CLIPBOARD_HOLD_POS 0

typedef struct ColMapFile ColMapFile;
typedef struct EditWin EditWin;

ColMapFile *ColMapFile_find_by_file_name(char const *load_path);
ColMapFile *ColMapFile_create(Reader *reader, char const *load_path,
  bool is_safe, bool hillcols);
void ColMapFile_destroy(ColMapFile *file);
bool ColMapFile_import(ColMapFile *file, Reader *reader);
EditWin *ColMapFile_get_win(ColMapFile *const file);
void ColMapFile_show(ColMapFile *file);
bool ColMapFile_export(ColMapFile *file, Writer *writer);


void EditWin_initialise(void);
ColMapFile *EditWin_get_colmap(EditWin const *edit_win);
int EditWin_get_colour(EditWin const *edit_win, int index);
void EditWin_colour_selected(EditWin *edit_win, int colour);
void EditWin_file_saved(EditWin *edit_win, char *save_path);
void EditWin_show_parent_dir(EditWin const *edit_win);
int EditWin_get_next_selected(EditWin *edit_win, int index);
int EditWin_get_num_selected(EditWin *edit_win, int *num_selectable);
void EditWin_give_focus(EditWin *edit_win);
void EditWin_set_hint(EditWin *edit_win, ComponentId component);
bool EditWin_has_unsaved(EditWin const *edit_win);
int *EditWin_get_stamp(EditWin const *edit_win);
char *EditWin_get_file_path(EditWin const *edit_win);
void EditWin_do_save(EditWin *edit_win, bool destroy, bool parent);
void EditWin_destroy(EditWin *edit_win);
bool EditWin_owns_wimp_handle(EditWin const *edit_win, int wimp_handle);
int EditWin_get_wimp_handle(EditWin const *edit_win);
EditWin *EditWin_from_wimp_handle(int window_handle);

void EditWin_start_auto_scroll(EditWin const *edit_win, const BBox *visible_area,
  int pause_time, unsigned int *flags_out);
void EditWin_stop_auto_scroll(EditWin const *edit_win);
void EditWin_coords_from_index(EditWin const *edit_win, int index,
  int *x, int *y);
void EditWin_bbox_from_index(EditWin const *edit_win, int index, BBox *bbox);

void EditWin_colour_selected(EditWin *edit_win, int colour);

bool EditWin_export(EditWin *edit_win, Writer *writer);
bool EditWin_set_array(EditWin *edit_win, int x, int y, int number,
  int const *src);

void EditWin_set_colmap(EditWin *edit_win, int x, int y, ColMap const *colmap);

void EditWin_set_expcol(EditWin *edit_win, int x, int y,
  ExpColFile const *file);

#if !CLIPBOARD_HOLD_POS
void EditWin_set_expcol_flat(EditWin *edit_win, int x, int y,
  ExpColFile const *file);
#endif

bool EditWin_get_expcol(EditWin *edit_win, int x, int y,
  ExpColFile *export_file);

bool EditWin_can_undo(EditWin *edit_win);
bool EditWin_can_redo(EditWin *edit_win);
bool EditWin_can_paste(EditWin *edit_win);
void EditWin_set_paste_enabled(EditWin *edit_win, bool can_paste);
void EditWin_update_can_paste(EditWin *edit_win);

#endif
