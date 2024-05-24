/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Errors
 *  Copyright (C) 2020 Christopher Bazley
 */

#ifndef SFError_h
#define SFError_h

typedef enum {
#define DECLARE_ERROR(ms) SFError_ ## ms,
#include "DeclErrors.h"
#undef DECLARE_ERROR
} SFError;

#endif
