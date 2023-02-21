// Wrapper function to check correctness of PPD files.
// Copyright © 2021-2022 by OpenPrinting
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
 

// Include necessary headers...
 

#include <ppd/ppd.h>
#include <string.h>
#include <cups/array.h>
#include <stdio.h>

      

// 'main()' - Wrapper function for ppdTest().
 


int main(int argc,          // I - Number of command-line args 
             char *argv[])  // I - Command-line arguments 
{

  int i;  // Looping vars 
  int verbose;         // Want verbose output? 
  int root_present;  // Whether root directory is specified 
  char *rootdir;  // What is the root directory if mentioned 
  int help;  // Whether to run help dialog 
  char *opt;  // Option character 
  int q_with_v;  // If q is used together with v in the command line 
  int v_with_q;  // If v is used together with q in the command line 
  int relaxed;  // If relaxed mode is to be used 
  cups_array_t *file_array;  // Array consisting of filenames of the ppd files to be checked 
  int files;                     // Number of files 
  char *line;   // Looping var for output array
  int warn;                      // Which errors to just warn about 
  int ignore;                     // Which errors to ignore 
  cups_array_t *report = NULL;   // Report variable
  int result;    // Whether PPD pased or not


  verbose = 0;
  root_present = 0;
  help = 0;
  relaxed = 0;
  q_with_v = 0;
  v_with_q = 0;
  files=0;
  warn = PPD_TEST_WARN_NONE;
  ignore = PPD_TEST_WARN_NONE;
  rootdir = "";
  file_array = cupsArrayNew(NULL,"");
  cupsArrayAdd(file_array," ");
  

  for (i = 1; i < argc; i++)
  if (!strcmp(argv[i], "--help"))
    help = 1;
  else if (argv[i][0] == '-' && argv[i][1])
  {
    for (opt = argv[i] + 1; *opt; opt++) 
    // puts is used to print the string char array
      
      switch (*opt)
      {
        case 'I':  // ignore_params errors 
            i++;

            if (i >= argc)
              help = 1;

            if (!strcmp(argv[i], "none"))
              ignore = PPD_TEST_WARN_NONE;
            else if (!strcmp(argv[i], "filename"))
              ignore |= PPD_TEST_WARN_FILENAME;
            else if (!strcmp(argv[i], "filters"))
              ignore |= PPD_TEST_WARN_FILTERS;
            else if (!strcmp(argv[i], "profiles"))
              ignore |= PPD_TEST_WARN_PROFILES;
            else if (!strcmp(argv[i], "all"))
              ignore = PPD_TEST_WARN_FILTERS | PPD_TEST_WARN_PROFILES;
            else
              help = 1;
            break;

        case 'R':  // Alternate root directory 
            i++;

            if (i >= argc)
              help = 1;

            rootdir = argv[i];
            root_present = 1;
            break;

        case 'W':  // Turn errors into warn_paramsings 
            i++;

            if (i >= argc)
              help = 1;

            if (!strcmp(argv[i], "none"))
              warn = PPD_TEST_WARN_NONE;
            else if (!strcmp(argv[i], "constraints"))
              warn |= PPD_TEST_WARN_CONSTRAINTS;
            else if (!strcmp(argv[i], "defaults"))
              warn |= PPD_TEST_WARN_DEFAULTS;
            else if (!strcmp(argv[i], "duplex"))
              warn |= PPD_TEST_WARN_DUPLEX;
            else if (!strcmp(argv[i], "filters"))
              warn |= PPD_TEST_WARN_FILTERS;
            else if (!strcmp(argv[i], "profiles"))
              warn |= PPD_TEST_WARN_PROFILES;
            else if (!strcmp(argv[i], "sizes"))
              warn |= PPD_TEST_WARN_SIZES;
            else if (!strcmp(argv[i], "translations"))
              warn |= PPD_TEST_WARN_TRANSLATIONS;
            else if (!strcmp(argv[i], "all"))
              warn = PPD_TEST_WARN_ALL;
            else
              help = 1;
            break;

        case 'q':  // Quiet mode 

            if (verbose > 0)
            {
              q_with_v = 1;
            }
            verbose--;
            break;

        case 'r':  // Relaxed mode 
            relaxed = 1;

        case 'v':  // Verbose mode 
            if (verbose < 0)
            {
              v_with_q = 1;
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
    cupsArrayAdd(file_array, argv[i]);
  }
  

  result = ppdTest(ignore, warn, rootdir, help, verbose,
                   relaxed, q_with_v, v_with_q, root_present, files,
		   file_array, &report, NULL, NULL);
  
  if (result == 1 && files > 0) puts("PPD PASSED");
  else if (result == 0) puts("PPD FAILED");
  else if (result == -1) puts("ERROR");

  if (report)
  {
    for (line = (char *)cupsArrayFirst(report); line; line = (char *)cupsArrayNext(report))
    {
      puts(line);
    }
  }
  return(0);
      
}
