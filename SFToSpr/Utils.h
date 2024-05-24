/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Utility functions
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTUtils_h
#define SFTUtils_h

#include <stdbool.h>

#include "kernel.h"
#include "saveas.h"

#include "SFError.h"
#include "Reader.h"
#include "Writer.h"

#include "flex.h"

void load_failed(CONST _kernel_oserror *error, void *client_handle);

bool dialogue_confirm(const char *mess);

bool copy_to_buf(void *handle, Reader *src,
  int src_size, char const *filename);

void tbox_send_data(ToolboxEvent *event, IdBlock *id_block, void *handle,
  flex_ptr dst, bool (*write_method)(Writer *, void *, char const *));

void tbox_save_file(ToolboxEvent *event, IdBlock *id_block, void *handle,
  bool (*write_method)(Writer *, void *, char const *));

const _kernel_oserror *conv_error(SFError const err, char const *read_filename, char const *write_filename);

bool handle_error(SFError const err, char const *read_filename, char const *write_filename);

int worst_comp_size(int orig_size);

#endif
