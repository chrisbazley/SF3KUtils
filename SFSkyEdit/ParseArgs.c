/*
 *  SFSkyEdit - Star Fighter 3000 sky colours editor
 *  Command line parser
 *  Copyright (C) 2015 Christopher Bazley
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

/* My library files */
#include "err.h"
#include "Debug.h"
#include "Macros.h"
#include "StrExtra.h"
#include "OSFile.h"
#include "msgtrans.h"

/* Local headers */
#include "SkyIO.h"
#include "SFSIconbar.h"
#include "ParseArgs.h"
#include "EditWin.h"

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */

void parse_arguments(int argc, char *argv[])
{
  /*
   * Interpret any command-line arguments
   */
  bool end_of_switches = false;

  assert(argc == 0 || argv != NULL);

  for (int i = 1; i < argc; i++)
  {
    if (!end_of_switches && *argv[i] == '-')
    {
      /* Arguments preceded by '-' are interpreted as switches */
      if (stricmp(argv[i], "-nowarn") == 0)
      {
        format_warning = false;
      }
      else if (stricmp(argv[i], "-notrap") == 0)
      {
        trap_caret = false;
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
        EF(os_file_generate_error(argv[i],
           cat.object_type == ObjectType_NotFound ?
             OS_File_GenerateError_FileNotFound :
             OS_File_GenerateError_IsADirectory));
      }
      else
      {
        /* Attempt to load the file, if it is a recognised type */
        int const file_type = decode_load_exec(cat.load, cat.exec, NULL);
        IO_load_file(file_type, argv[i]);
      }
    }
  } /* next parameter */
}
