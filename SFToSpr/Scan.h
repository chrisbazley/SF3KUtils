/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Directory scan
 *  Copyright (C) 2000 Christopher Bazley
 */

#ifndef SFTScan_h
#define SFTScan_h

#include <stdbool.h>

void Scan_create(char *load_root, const char *save_root, bool extract_images, bool extract_data);

#endif
