/*
 *  FednetCmp - Fednet file compression/decompression
 *  Command line parser
 *  Copyright (C) 2017 Christopher Bazley
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

/* ANSI library files */
#include <assert.h>
#include "stdlib.h"
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

/* RISC OS library files */
#include "kernel.h"
#include "flex.h"

/* My library files */
#include "Err.h"
#include "Macros.h"
#include "Scheduler.h"
#include "StrExtra.h"
#include "FileUtils.h"
#include "Debug.h"
#include "OSFile.h"
#include "msgtrans.h"

/* Local headers */
#include "FNCIconbar.h"
#include "Utils.h"
#include "ParseArgs.h"
#include "Scan.h"

/* ----------------------------------------------------------------------- */
/*                         Public functions                                */


void parse_arguments(int argc, char *argv[])
{
  /*
   * Interpret any command-line arguments
   */
  bool end_of_switches = false, quit_parse_cl = false;

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
        char const * const decimal = argv[++i];
        const size_t len = strlen(decimal);

        for (size_t c = 0; c < len; c++)
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
      OS_File_CatalogueInfo cat;

      end_of_switches = true;

      EF(os_file_read_cat_no_path(argv[i], &cat));

      if (cat.object_type == ObjectType_NotFound)
      {
        /* Object not found - generate appropriate error */
        EF(os_file_generate_error(
                          argv[i],
                          OS_File_GenerateError_FileNotFound));
      }
      else
      {
        /* Does filetype match any known Fednet type? */
        int const file_type = decode_load_exec(cat.load, cat.exec, NULL);
        if (!compressed_file_type(file_type))
          continue; /* not a compressed file */

        if (quit_parse_cl)
        {
          /* A multi-tasking decompression is incompatible with '-quit'.
             Load the whole file into memory before overwriting it. */
          void *buffer_anchor = NULL;
          if (load_file(argv[i], &buffer_anchor, copy_to_buf))
          {
            (void)save_file(argv[i], FileType_Data, &buffer_anchor, decomp_from_buf);
            if (buffer_anchor)
            {
              flex_free(&buffer_anchor);
            }
          }
        }
        else
        {
          /* Start a multi-tasking decompression operation */
          char *canonical_path;
          EF(canonicalise(&canonical_path, NULL, NULL, argv[i]));

          Scan_create(canonical_path, canonical_path, false, 0);
          free(canonical_path);
        }
      }
    }
  } /* next parameter */

  if (quit_parse_cl)
    exit(EXIT_SUCCESS);
}
