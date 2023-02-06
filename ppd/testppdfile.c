/*
 * Wrapper function to check correctness of PPD files.
 *
 * Copyright © 2021-2022 by OpenPrinting
 *
 * Licensed under Apache License v2.0.  See the file "LICENSE" for more
 * information.
 */

/*
 * Include necessary headers...
 */

#include <ppd/ppd.h>
#include <cups/array.h>
#include <ppd/array-private.h>
#include <stdio.h>

/*
 * 'main()' - Wrapper function for ppdTest().
 */


int main(int argc,	   /* I - Number of command-line args */
		 char *argv[]) /* I - Command-line arguments */
{

  int i; /* Looping vars */
  int verbose;	   /* Want verbose output? */
  int root_present;  /* Whether root directory is specified */
  char *rootdir;  /* What is the root directory if mentioned */
  int help;  /* Whether to run help dialog */
  char *opt; /* Option character */
  int q_with_v;  /* If q is used together with v in the command line */
  int v_with_q;  /* If v is used together with q in the command line */
  int relaxed;  /* If relaxed mode is to be used */
  cups_array_t *file_array;  /* Array consisting of filenames of the ppd files to be checked */
  int files;			   /* Number of files */
  cups_array_t	*output;
  int len_output;  /* Length of the output array */
  char[256] txt;
  ignore_parameters_t ignore_params;
  warn_parameters_t warn_params;


  verbose = 0;
  root_present = 0;
  help = 0;
  relaxed = 0;
  q_with_v = 0;
  v_with_q = 0;
  files=0;
  ignore_params = {0,0,0,0,0};
  warn_params = {0,0,0,0,0,0,0,0,0};

  for (i = 1; i < argc; i++)
    if (!strcmp(argv[i], "--help"))
    help = 1;
  else if (argv[i][0] == '-' && argv[i][1])
  {
    for (opt = argv[i] + 1; *opt; opt++)
      switch (*opt)
      {
        case 'I': /* ignore_params errors */
            i++;

            if (i >= argc)
              help = 1;

            if (!strcmp(argv[i], "none"))
              ignore_params.none = 1;
            else if (!strcmp(argv[i], "filename"))
              ignore_params.pc_filenames = 1;
            else if (!strcmp(argv[i], "filters"))
              ignore_params.filters = 1;
            else if (!strcmp(argv[i], "profiles"))
              ignore_params.profiles = 1;
            else if (!strcmp(argv[i], "all"))
              ignore_params.all = 1;
            else
              help = 1;
            break;

        case 'R': /* Alternate root directory */
            i++;

            if (i >= argc)
              help = 1;

              rootdir = argv[i];
              root_present = 1;
              break;

        case 'W': /* Turn errors into warn_paramsings */
            i++;

            if (i >= argc)
              help = 1;

            if (!strcmp(argv[i], "none"))
              warn_params.none = 1;
            else if (!strcmp(argv[i], "constraints"))
              warn_params.constraints = 1;
            else if (!strcmp(argv[i], "defaults"))
              warn_params.defaults = 1;
            else if (!strcmp(argv[i], "duplex"))
              warn_params.duplex = 1;
            else if (!strcmp(argv[i], "filters"))
              warn_params.filters = 1;
            else if (!strcmp(argv[i], "profiles"))
              warn_params.profiles = 1;
            else if (!strcmp(argv[i], "sizes"))
              warn_params.sizes = 1;
            else if (!strcmp(argv[i], "translations"))
              warn_params.translations = 1;
            else if (!strcmp(argv[i], "all"))
              warn_params.all = 1;
            else
              help = 1;
            break;

        case 'q': /* Quiet mode */

            if (verbose > 0)
            {
              q_with_v = 1;
            }
            verbose--;
            break;

        case 'r': /* Relaxed mode */
            relaxed = 1

        case 'v': /* Verbose mode */
            if (verbose < 0)
            {
              v_with_q = 1
            }

            verbose++;
            break;

        default:
            help = 1;
      }
  }
  else
  {			
    files++;

    if (argv[i][0] == '-')
    {
      _ppdArrayAddStrings(file_array,"");
    }
    else
    {
      _ppdArrayAddStrings(file_array,argv[i]);
    }
  }

  output = ppdTest(ignore_params, warn_params, rootdir, help, verbose,
                   relaxed, q_with_v, v_with_q, root_present, files, file_array);

  len_output = cupsArrayCount(output);
	
  for (int j = 1, j<= len_output, j++)
  {
    txt = cupsArrayCurrent(output);
    puts(txt);
    cupsArrayNext(output);

  }
  txt = cupsArrayCurrent(output);
  puts(txt);

  return(0);
	
}
