/*
 *  SFToSpr - Star Fighter 3000 graphics converter
 *  Command line parser
 *  Copyright (C) 2016  Christopher Bazley
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

/* ISO library files */
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>

/* My library files */
#include "Err.h"
#include "Debug.h"
#include "Macros.h"
#include "StrExtra.h"
#include "OSFile.h"
#include "msgtrans.h"
#include "DateStamp.h"
#include "scheduler.h"

/* Local headers */
#include "SFTIconbar.h"
#include "ParseArgs.h"
#include "QuickView.h"
#include "SFgfxconv.h"

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void parse_arguments(int argc, char *argv[])
{
  /*
   * Interpret any command-line arguments
   */
  bool end_of_switches = false;
  bool quit_parse_cl = false;

  assert(argv != NULL || argc == 0);

  for (int i = 1; i < argc; i++)
  {
    if (!end_of_switches && *argv[i] == '-')
    {
      /* Arguments preceded by '-' are interpreted as switches */
      if (stricmp(argv[i], "-quit") == 0)
      {
        quit_parse_cl = true;
      }
      else if (stricmp(argv[i], "-multi") == 0)
      {
        Iconbar_set_multi_dboxes(true);
      }
      else if (stricmp(argv[i], "-timeslice") == 0 && i + 1 < argc)
      {
        const char *decimal = argv[++i];
        const int len = strlen(decimal);

        for (int c = 0; c < len; c++)
        {
          if (!isdigit(decimal[c]))
          {
            err_complain_fatal(DUMMY_ERRNO, msgs_lookup("BadParm"));
          }
        }
        scheduler_set_time_slice((SchedulerTime)strtol(decimal, NULL, 10));
      }
      else
      {
        err_complain_fatal(DUMMY_ERRNO, msgs_lookup("BadParm"));
      }
    }
    else
    {
      /* Other arguments are interpreted as file paths to load */
      end_of_switches = true;

      OS_File_CatalogueInfo cat;
      EF(os_file_read_cat_no_path(argv[i], &cat));

      if (cat.object_type == ObjectType_NotFound ||
          cat.object_type == ObjectType_Directory)
      {
        /* Object not found or is a directory - generate appropriate error */
        EF(os_file_generate_error(
             argv[i],
             cat.object_type == ObjectType_NotFound ?
             OS_File_GenerateError_FileNotFound :
             OS_File_GenerateError_IsADirectory));
      }
      else
      {
        /* Attempt to load the file, if it is a recognised type */
        const int file_type = decode_load_exec(cat.load, cat.exec, NULL);
        quick_view(argv[i], file_type);
      }
    }
  }

  if (quit_parse_cl)
    exit(EXIT_SUCCESS);
}
