/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky editing window
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSEditWin_h
#define SFSEditWin_h

#include <stdbool.h>

#include "wimp.h"

#include "Reader.h"
#include "Writer.h"

#include "Sky.h"

typedef struct SkyFile SkyFile;
typedef struct EditWin EditWin;

_Optional SkyFile *SkyFile_find_by_file_name(char const *load_path);
_Optional SkyFile *SkyFile_create(_Optional Reader *reader,
  _Optional char const *load_path, bool is_safe);
void SkyFile_destroy(_Optional SkyFile *file);
void SkyFile_export(SkyFile *file, Writer *writer);
void SkyFile_set_star_height(SkyFile *file, int height);
void SkyFile_set_render_offset(SkyFile *file, int height);
void SkyFile_add_render_offset(SkyFile *file, int offset);
EditWin *SkyFile_get_win(SkyFile *file);
void SkyFile_show(SkyFile *file);

extern bool trap_caret;

void EditWin_initialise(void);

SkyFile *EditWin_get_sky(EditWin *edit_win);
void EditWin_give_focus(EditWin *edit_win);
void EditWin_file_saved(EditWin *edit_win, _Optional char *save_path);
void EditWin_show_parent_dir(const EditWin *edit_win);
void EditWin_delete_colours(EditWin *edit_win);
void EditWin_insert_plain(EditWin *edit_win, int number, int colour);
void EditWin_insert_gradient(EditWin *edit_win, int number,
  int start_col, int end_col, bool inc_start, bool inc_end);

void EditWin_interpolate(EditWin *edit_win,
  int start_col, int end_col);

void EditWin_drop_handler(EditWin *dest_view, EditWin *source_view,
  bool shift_held);

int EditWin_get_colour(EditWin *edit_win, int pos);
int EditWin_get_array(EditWin *edit_win, int dst[], int dst_size);
void EditWin_insert_sky(EditWin *edit_win, Sky const *src);
bool EditWin_has_unsaved(const EditWin *edit_win);
void EditWin_set_caret_pos(EditWin *edit_win, int new_pos);
void EditWin_get_selection(EditWin *edit_win, _Optional int *start, _Optional int *end);
int *EditWin_get_stamp(const EditWin *edit_win);
_Optional char *EditWin_get_file_path(const EditWin *edit_win);
void EditWin_do_save(EditWin *edit_win, bool destroy, bool parent);
void EditWin_destroy(EditWin *edit_win);
bool EditWin_owns_wimp_handle(const EditWin *edit_win, int wimp_handle);
int EditWin_get_wimp_handle(const EditWin *edit_win);

_Optional EditWin *EditWin_from_wimp_handle(int window_handle);

void EditWin_set_insert_pos(EditWin *edit_win,
  const WimpGetWindowStateBlock *window_state, int y);

void EditWin_remove_insert_pos(EditWin *edit_win);
void EditWin_confirm_insert_pos(EditWin *edit_win);

void EditWin_start_auto_scroll(const EditWin *edit_win,
  const BBox *visible_area, int pause_time, _Optional unsigned int *flags_out);

void EditWin_stop_auto_scroll(const EditWin *edit_win);
bool EditWin_export(EditWin *edit_win, Writer *writer);

bool EditWin_export_sel(EditWin *edit_win, Writer *writer);
void EditWin_colour_selected(EditWin *edit_win, int colour);

bool EditWin_insert_array(EditWin *edit_win, int number,
  int const *src);

bool EditWin_can_undo(EditWin *edit_win);
bool EditWin_can_redo(EditWin *edit_win);
bool EditWin_can_paste(EditWin *edit_win);
void EditWin_set_paste_enabled(EditWin *edit_win, bool can_paste);
void EditWin_update_can_paste(EditWin *edit_win);

#endif
