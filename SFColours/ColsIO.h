/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Input/output for palette window
 *  Copyright (C) 2006 Christopher Bazley
 */

#ifndef SFCIO_h
#define SFCIO_h

#include <stdbool.h>
#include "wimp.h"
#include "WimpExtra.h"
#include "EditWin.h"
#include "Reader.h"
#include "ColMap.h"

typedef struct IOCoords
{
  int x;
  int y;
}
IOCoords;

void IO_initialise(void);
void IO_receive(WimpMessage const *message);
void IO_load_file(int file_type, char const *load_path);
bool IO_copy(EditWin *edit_win);
void IO_paste(EditWin *edit_win);
bool IO_view_created(EditWin *edit_win);
void IO_view_deleted(EditWin *edit_win);
bool IO_start_drag(EditWin *edit_win, IOCoords pos,
  const BBox *bbox);

void IO_dragging_msg(const WimpDraggingMessage *dragging);
void IO_cancel(EditWin *edit_win);
void IO_update_can_paste(EditWin *edit_win);

bool IO_report_read(ColMap const *colmap, ColMapState state);
bool IO_read_colmap(ColMap *colmap, Reader *reader);

typedef bool IOImportColMapFn(ColMapFile *, Reader *);

bool IO_export_colmap_file(EditWin *edit_win, char const *path);
int IO_estimate_colmap(EditWin *edit_win);

#endif
