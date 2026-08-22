/*
 * SFToSpr test: test suite definitions
 * Copyright (C) 2026 Christopher Bazley
 */

#ifndef Tests_h
#define Tests_h

void Conv_tests(void);
void App_tests(void);

#ifdef FORTIFY
#include "Fortify.h"
#else
#define Fortify_SetAllocationLimit(x) ((void)(x))
#define Fortify_SetNumAllocationsLimit(x) ((void)(x))
#define Fortify_EnterScope()
#define Fortify_LeaveScope()
#define Fortify_OutputStatistics()
#define Fortify_CheckAllMemory()
#define Fortify_GetCurrentAllocation() (1)
#define Fortify_SetOutputFunc(x)
#endif

#endif /* Tests_h */
