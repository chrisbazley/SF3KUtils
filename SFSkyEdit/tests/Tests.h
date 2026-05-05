/*
 * SFSkyEdit test: test suite definitions
 * Copyright (C) 2019 Christopher Bazley
 */

#ifndef Tests_h
#define Tests_h

void Sky_tests(void);
void Editor_tests(void);
void App_tests(void);

#ifdef FORTIFY
#include "Fortify.h"
#else
#define Fortify_SetAllocationLimit(x)
#define Fortify_SetNumAllocationsLimit(x)
#define Fortify_EnterScope()
#define Fortify_LeaveScope()
#define Fortify_OutputStatistics()
#define Fortify_CheckAllMemory()
#define Fortify_GetCurrentAllocation() (0)
#endif

#endif /* Tests_h */
