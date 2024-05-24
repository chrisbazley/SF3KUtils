/*
 *  SFColours - Star Fighter 3000 colours editor
 *  Initialisation
 *  Copyright (C) 2016 Christopher Bazley
 */

#ifndef SFCInit_h
#define SFCInit_h

#include "toolbox.h"
#include "PalEntry.h"

#define APP_NAME "SFColours"

enum
{
  NumColours = 256
};

extern PaletteEntry palette[NumColours];
extern int          x_eigen, y_eigen;
extern int          wimp_version;
extern MessagesFD   mfd;

void initialise(void);

#endif
