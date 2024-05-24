/*
 *  FednetCmp - Fednet file compression/decompression
 *  Directory scan
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef FNCScan_h
#define FNCScan_h

#include <stdbool.h>

void Scan_create(char *load_root, char const *save_root, bool compress, int comp_type);

#endif

