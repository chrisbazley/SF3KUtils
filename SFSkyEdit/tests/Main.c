/*
 *  SFSkyEdit test: main program
 *  Copyright (C) 2019 Christopher Bazley
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public Licence as published by
 *  the Free Software Foundation; either version 2 of the Licence, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public Licence for more details.
 *
 *  You should have received a copy of the GNU General Public Licence
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/* ISO library headers */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "Fortify.h"

/* CBLibrary headers */
#include "Macros.h"
#include "Debug.h"

/* Local headers */
#include "Tests.h"

#ifdef FORTIFY
#include "Fortify.h"
#endif

#ifdef USE_OPTIONAL
#include "Optional.h"
#endif


static bool fortify_detected = false;

static void fortify_check(void)
{
  Fortify_CheckAllMemory();
  assert(!fortify_detected);
}

static void fortify_output(char const *text)
{
  DEBUGF("%s", text);
  if (strstr(text, "Fortify"))
  {
    assert(!fortify_detected);
  }
  if (strstr(text, "detected"))
  {
    fortify_detected = true;
  }
}

int main(int argc, char *argv[])
{
  static const struct
  {
    char const *test_name;
    void (*test_func)(void);
  }
  test_groups[] =
  {
    { "Sky", Sky_tests },
    { "Editor", Editor_tests },
    { "App", App_tests },
  };

  NOT_USED(argc);
  NOT_USED(argv);

  DEBUG_SET_OUTPUT(DebugOutput_FlushedFile, "SFSkyEditLog");
  Fortify_SetOutputFunc(fortify_output);
  atexit(fortify_check);

  for (size_t count = 0; count < ARRAY_SIZE(test_groups); count ++)
  {
    /* Print title of this group of tests */
    DEBUGF("%s tests\n", test_groups[count].test_name);

    /* Call a function to perform the group of tests */
    test_groups[count].test_func();

    DEBUGF("\n");
  }

  Fortify_OutputStatistics();

  return EXIT_SUCCESS;
}
