/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Initialisation
 *  Copyright (C) 2014 Christopher Bazley
 */

#ifndef SFSInit_h
#define SFSInit_h

#include "toolbox.h"
#include "PalEntry.h"

#define APP_NAME "SFSkyEdit"

enum
{
  NumColours = 256
};

extern PaletteEntry palette[NumColours];
extern int x_eigen, y_eigen;
extern char         taskname[];
extern int          wimp_version;
extern MessagesFD   mfd;

void initialise(void);

#endif
