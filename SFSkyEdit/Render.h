/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Sky renderer
 *  Copyright (C) 2001 Christopher Bazley
 */

#ifndef SFSRender_h
#define SFSRender_h

#include "SFFormats.h"

/* Final argument is the offset to the first word to be plotted! (4 bytes
   before the end of the lowest scan line to be filled from right to left) */
void sky_drawsky(int height_scaler, const SFSky *sky, void *screen_address, int scrstart_offset);

void star_plot(int height, void *screen_address, int x, int y, int colour, int bright, int size);

#endif
