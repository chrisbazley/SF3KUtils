/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Input/output for sky editing window
 *  Copyright (C) 2006 Christopher Bazley
 */

#ifndef SFSIO_h
#define SFSIO_h

#include <stdbool.h>
#include "wimp.h"
#include "WimpExtra.h"
#include "Writer.h"
#include "EditWin.h"

extern bool format_warning;

void IO_initialise(void);
void IO_receive(WimpMessage const *message);
void IO_load_file(int file_type, char const *load_path);
bool IO_copy(EditWin *edit_win);
void IO_paste(EditWin *edit_win);
bool IO_view_created(EditWin *edit_win);
void IO_view_deleted(EditWin *edit_win);
bool IO_start_drag(EditWin *edit_win, int start_x, int start_y, const BBox *bbox);
void IO_dragging_msg(const WimpDraggingMessage *dragging);
void IO_cancel(EditWin *edit_win);
void IO_update_can_paste(EditWin *edit_win);

typedef bool IOExportSkyFn(EditWin *, Writer *);

bool IO_export_sky_file(EditWin *edit_win, char const *path,
  IOExportSkyFn *fn);

int IO_estimate_sky(EditWin *edit_win, IOExportSkyFn *fn);

bool IO_report_read(SkyState state);

#endif
