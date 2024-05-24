/*
 *  FednetCmp - Fednet file compression/decompression
 *  Utility functions
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCUtils_h
#define FNCUtils_h

#include <stdbool.h>
#include "flex.h"
#include "saveas.h"
#include "Reader.h"
#include "Writer.h"

bool compressed_file_type(int file_type);

bool copy_to_buf(void *handle, Reader *src,
  int src_size, char const *filename);

int get_decomp_size(flex_ptr buffer);
int get_comp_size(flex_ptr buffer);

bool decomp_from_buf(Writer *dst, void *handle,
  char const *filename);

bool comp_from_buf(Writer *dst, void *handle,
  char const *filename);

bool load_file(char const *filename, void *handle,
  bool (*read_method)(void *, Reader *, int, char const *));

bool save_file(char const *filename, int file_type,
  void *handle,
  bool (*write_method)(Writer *, void *, char const *));

void tbox_send_data(const SaveAsFillBufferEvent *safbe,
  ObjectId saveas_id, flex_ptr dst, void *handle,
  bool (*write_method)(Writer *, void *, char const *));

void tbox_save_file(SaveAsSaveToFileEvent *sastfe,
  ObjectId saveas_id, void *handle,
  bool (*write_method)(Writer *, void *, char const *));

#endif
