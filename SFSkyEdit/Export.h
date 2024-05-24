/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Export back-end functions
 *  Copyright (C) 2019 Christopher Bazley
 */

#ifndef SFSExport_h
#define SFSExport_h

#include "Writer.h"

/* Returns estimated no. of bytes in a comma-separated values file generated
   from an array of the given number of colours. */
int estimate_CSV_file(int ncols);

/* Write an array of colours as a comma-separated values file. */
void write_CSV_file(int const cols_array[], int ncols, Writer *writer);

/* Returns estimated no. of bytes in a sprite file generated from an array
   of the given number of colours. */
int estimate_sprite_file(int ncols);

/* Write an array of colours as a sprite file. */
void write_sprite_file(int const cols_array[], int ncols, Writer *writer);

#endif
