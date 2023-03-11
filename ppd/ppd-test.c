// PPD tset program to check correctness of PPD Files.
// Copyright © 2021-2022 by OpenPrinting
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
// The PPD test is based on the cupstestppd utility for CUPS
// in the systemv/cupstestppd.c file.

//
// Include necessary headers...
//

#include <cups/dir.h>
#include <cups/raster.h>
#include <cups/file.h>
#include <ppd/string-private.h>
#include <ppd/array-private.h>
#include <ppd/raster-private.h>
#include <ppd/ppd.h>
#include <cups/array.h>
#include <cupsfilters/log.h>
#include <stdio.h>
#include <math.h>
#ifdef _WIN32
#  define X_OK 0
#endif // _WIN32


//
// Error codes...
//

enum
{
  ERROR_NONE = 0,
  ERROR_USAGE,
  ERROR_FILE_OPEN,
  ERROR_PPD_FORMAT,
  ERROR_CONFORMANCE
};

//
// Line endings...
//

enum
{
  EOL_NONE = 0,
  EOL_CR,
  EOL_LF,
  EOL_CRLF
};


//
// File permissions...
//

#define MODE_WRITE    0022       // Group/other write
#define MODE_MASK    0555        // Owner/group/other read+exec/search
#define MODE_DATAFILE    0444    // Owner/group/other read
#define MODE_DIRECTORY    0555   // Owner/group/other read+search
#define MODE_PROGRAM    0555     // Owner/group/other read+exec


//
// Local functions...
//

static void check_basics(const char *filename, cups_array_t **report,
			 cf_logfunc_t log, void *ld);
static int check_constraints(ppd_file_t *ppd, int errors, int verbose,
                             int warn, cups_array_t **report,
			     cf_logfunc_t log, void *ld);
static int check_case(ppd_file_t *ppd, int errors, int verbose,
		      cups_array_t **report, cf_logfunc_t log, void *ld);
static int check_defaults(ppd_file_t *ppd, int errors, int verbose,
                          int warn, cups_array_t **report,
			  cf_logfunc_t log, void *ld);
static int check_duplex(ppd_file_t *ppd, int errors, int verbose,
                        int warn, cups_array_t **report,
			cf_logfunc_t log, void *ld);
static int check_filters(ppd_file_t *ppd, const char *root, int errors,
                         int verbose, int warn, cups_array_t **report,
			 cf_logfunc_t log, void *ld);
static int check_profiles(ppd_file_t *ppd, const char *root, int errors,
                          int verbose, int warn, cups_array_t **report,
			  cf_logfunc_t log, void *ld);
static int check_sizes(ppd_file_t *ppd, int errors, int verbose, int warn,
		       cups_array_t **report, cf_logfunc_t log, void *ld);
static int check_translations(ppd_file_t *ppd, int errors, int verbose,
                              int warn, cups_array_t **report,
			      cf_logfunc_t log, void *ld);
static void show_conflicts(ppd_file_t *ppd, const char *prefix,
			   cups_array_t **report, cf_logfunc_t log, void *ld);
static int test_raster(ppd_file_t *ppd, int verbose, cups_array_t **report,
		       cf_logfunc_t log, void *ld);
static int valid_path(const char *keyword, const char *path, int errors,
                      int verbose, int warn, cups_array_t **report,
		      cf_logfunc_t log, void *ld);
static int valid_utf8(const char *s);


//
// 'ppdTest()' - Test the correctness of PPD files.
//

int ppdTest(int ignore,          // Which errors to ignore
            int warn,            //Which errors to just warn about
            char *rootdir,       // What is the root directory if mentioned
            int verbose,         // Want verbose output?
            int relaxed,         // If relaxed mode is to be used
            int root_present,    // Whether root directory is specified
            cups_array_t *file_array, // Array consisting of filenames of the
	                         // ppd files to be checked
            cups_array_t **report, // Report array
	    cf_logfunc_t log,    // I - Log function
	    void *ld)            // I - Log function data
{
  int i, j, k, m, n;        // Looping vars
  size_t len;               // Length of option name
  const char *ptr;          // Pointer into string
  cups_file_t *fp;          // PPD file
  int status;               // Exit status
  int errors;               // Number of conformance errors
  int ppdversion;           // PPD spec version in PPD file
  ppd_status_t error;       // Status of ppdOpen*()
  int line;                 // Line number for error
  char *file = NULL;        // File name in file_array
  char *root;               // Root directory
  char str_format[2048];    // Formatted string
  int xdpi,                 // X resolution
      ydpi;                 // Y resolution
  ppd_file_t *ppd;          // PPD file record
  ppd_attr_t *attr;         // PPD attribute
  ppd_size_t *size;         // Size record
  ppd_group_t *group;       // UI group
  ppd_option_t *option;     // Standard UI option
  ppd_group_t *group2;      // UI group
  ppd_option_t *option2;    // Standard UI option
  ppd_choice_t *choice;     // Standard UI option choice
  struct lconv *loc;        // Locale data
  static char *uis[] = {"BOOLEAN", "PICKONE", "PICKMANY"};
  static char *sections[] = {"ANY", "DOCUMENT", "EXIT",
                             "JCL", "PAGE", "PROLOG"};


  // Locale
  loc = localeconv();

  //
  // Display PPD files for each file listed on the command-line...
  //

  ppdSetConformance(PPD_CONFORM_STRICT);

  ppd = NULL;
  status = ERROR_NONE;
  root = rootdir;
  if (root == NULL)
    root = "";

  if (report && *report == NULL)
  {
    *report = cupsArrayNew3(NULL, NULL, NULL, 0,
                            (cups_acopy_func_t)_ppdStrAlloc,
                            (cups_afree_func_t)_ppdStrFree);
    if (*report == NULL)
    {
      if (log) log(ld, CF_LOGLEVEL_ERROR,
		   "ppdTest: Could not allocate memory.");
      return (-1);
    }
  }

  if (relaxed == 1)
    ppdSetConformance(PPD_CONFORM_RELAXED);

  //
  // Open the PPD file...
  //

  for (i = 0, file = (char *)cupsArrayFirst(file_array);
       file;
       i ++, file = (char *)cupsArrayNext(file_array))
  {
    if (strcmp(file, "-") == 0)
    {
      //
      // Read from stdin...
      //

      ppd = ppdOpenWithLocalization(cupsFileStdin(), PPD_LOCALIZATION_ALL);
      file = NULL;

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 "%s:", (ppd && ppd->pcfilename) ? ppd->pcfilename : "(stdin)");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      // Read from a file...
      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1, "\n%s:", file);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if ((fp = cupsFileOpen(file, "r")) != NULL)
      {
        ppd = ppdOpenWithLocalization(fp, PPD_LOCALIZATION_ALL);
        cupsFileClose(fp);
      }
      else
      {
        status = ERROR_FILE_OPEN;

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Unable to open PPD file - %s on "
		   "line %d.",strerror(errno), 0);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          continue;
        }
      }
    }

    if (ppd == NULL)
    {
      error = ppdLastError(&line);

      if (error <= PPD_ALLOC_ERROR)
      {
        status = ERROR_FILE_OPEN;

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG,
		       "ppdTest: %s", str_format);
          snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Unable to open PPD file - %s on "
		   "line %d.", strerror(errno), 0);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }
      }
      else
      {
        status = ERROR_PPD_FORMAT;
        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Unable to open PPD file - "
		   "%s on line %d.", ppdErrorString(error), line);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

          switch (error)
          {
            case PPD_MISSING_PPDADOBE4:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 42, section 5.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_MISSING_VALUE:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 20, section 3.4.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_BAD_OPEN_GROUP:
            case PPD_NESTED_OPEN_GROUP:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Pages 45-46, section 5.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_BAD_OPEN_UI:
            case PPD_NESTED_OPEN_UI:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Pages 42-45, section 5.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_BAD_ORDER_DEPENDENCY:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Pages 48-49, section 5.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_BAD_UI_CONSTRAINTS:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Pages 52-54, section 5.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_MISSING_ASTERISK:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 15, section 3.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_LINE_TOO_LONG:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 15, section 3.1.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_ILLEGAL_CHARACTER:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 15, section 3.1.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_ILLEGAL_MAIN_KEYWORD:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Pages 16-17, section 3.2.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_ILLEGAL_OPTION_KEYWORD:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 19, section 3.3.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            case PPD_ILLEGAL_TRANSLATION:
                snprintf(str_format, sizeof(str_format) - 1,
			 "                REF: Page 27, section 3.5.");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
                break;
            default:
                break;
          }

          check_basics(file, report, log, ld);
        }
      }

      continue;
    }

    //
    // Show the header and then perform basic conformance tests (limited
    // only by what the CUPS PPD functions actually load...)
    //

    errors = 0;
    ppdversion = 43;

    if (verbose > 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       "    DETAILED CONFORMANCE TEST RESULTS");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if ((attr = ppdFindAttr(ppd, "FormatVersion", NULL)) != NULL &&
	attr->value)
      ppdversion = (int)(10 * _ppdStrScand(attr->value, NULL, loc) + 0.5);

    if ((attr = ppdFindAttr(ppd, "cupsFilter2", NULL)) != NULL)
    {
      do
      {
        if (strstr(attr->value, "application/vnd.cups-raster"))
        {
          if (!test_raster(ppd, verbose, report, log, ld))
            errors ++;
          break;
        }
      }
      while ((attr = ppdFindNextAttr(ppd, "cupsFilter2", NULL)) != NULL);
    }
    else
    {
      for (j = 0; j < ppd->num_filters; j ++)
        if (strstr(ppd->filters[j], "application/vnd.cups-raster"))
        {
          if (!test_raster(ppd, verbose, report, log, ld))
            errors ++;
          break;
        }
    }

    //
    // Look for default keywords with no matching option...
    //

    if (!(warn & PPD_TEST_WARN_DEFAULTS))
      errors = check_defaults(ppd, errors, verbose, 0, report, log, ld);

    if ((attr = ppdFindAttr(ppd, "DefaultImageableArea", NULL)) == NULL)
    {
      if (verbose >= 0)
      {
        if (!errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }
        snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED DefaultImageableArea\n"
		 "                REF: Page 102, section 5.15.");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }
    else if (ppdPageSize(ppd, attr->value) == NULL &&
	     strcasecmp(attr->value, "Unknown"))
    {
      if (verbose >= 0)
      {
        if (!errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  Bad DefaultImageableArea %s\n"
		 "                REF: Page 102, section 5.15.",
		 attr->value);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }
    else
    {
      if (verbose > 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    DefaultImageableArea");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }

    if ((attr = ppdFindAttr(ppd, "DefaultPaperDimension", NULL)) == NULL)
    {
      if (verbose >= 0)
      {
        if (!errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED DefaultPaperDimension\n"
                 "                REF: Page 103, section 5.15.");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }
    else if (ppdPageSize(ppd, attr->value) == NULL &&
	     strcmp(attr->value, "Unknown"))
    {
      if (verbose >= 0)
      {
        if (!errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  Bad DefaultPaperDimension %s\n"
		 "                REF: Page 103, section 5.15.",
		 attr->value);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }
    else if (verbose > 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       "        PASS    DefaultPaperDimension");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    for (j = 0, group = ppd->groups; j < ppd->num_groups; j ++, group ++)
      for (k = 0, option = group->options;
	   k < group->num_options;
	   k ++, option ++)
      {
	//
	// Verify that we have a default choice...
	//

        if (option->defchoice[0])
        {
          if (ppdFindChoice(option, option->defchoice) == NULL &&
	      strcmp(option->defchoice, "Unknown"))
          {
            if (verbose >= 0)
            {
              if (!errors && !verbose)
              {
                snprintf(str_format, sizeof(str_format) - 1, " FAIL");
                if (*report)
                  cupsArrayAdd(*report, (void *)str_format);
                if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
              }

              snprintf(str_format, sizeof(str_format) - 1,
		       "      **FAIL**  Bad Default%s %s\n"
		       "                REF: Page 40, section 4.5.",
		       option->keyword, option->defchoice);
              if (*report)
                cupsArrayAdd(*report, (void *)str_format);
              if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

            }

            errors ++;
          }
          else if (verbose > 0)
          {
            snprintf(str_format, sizeof(str_format) - 1,
		     "        PASS    Default%s",
		     option->keyword);
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }
        }
        else
        {
          if (verbose >= 0)
          {
            if (!errors && !verbose)
            {
              snprintf(str_format, sizeof(str_format) - 1, " FAIL");
              if (*report)
                cupsArrayAdd(*report, (void *)str_format);
              if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
            }

            snprintf(str_format, sizeof(str_format) - 1,
		     "      **FAIL**  REQUIRED Default%s\n"
		     "                REF: Page 40, section 4.5.",
		     option->keyword);
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          errors ++;
        }
      }

    if ((attr = ppdFindAttr(ppd, "FileVersion", NULL)) != NULL)
    {
      for (ptr = attr->value; *ptr; ptr++)
	if (!isdigit(*ptr & 255) && *ptr != '.')
	  break;

      if (*ptr)
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }
	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad FileVersion \"%s\"\n"
		   "                REF: Page 56, section 5.3.",
		   attr->value);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}
	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    FileVersion");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED FileVersion\n"
                 "                REF: Page 56, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if ((attr = ppdFindAttr(ppd, "FormatVersion", NULL)) != NULL)
    {
      ptr = attr->value;
      if (*ptr == '4' && ptr[1] == '.')
      {
	for (ptr += 2; *ptr; ptr++)
	  if (!isdigit(*ptr & 255))
	    break;
      }

      if (*ptr)
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad FormatVersion \"%s\"\n"
		   "                REF: Page 56, section 5.3.",
		   attr->value);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    FormatVersion");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}
	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED FormatVersion\n"
                 "                REF: Page 56, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->lang_encoding != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    LanguageEncoding");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else if (ppdversion > 40)
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED LanguageEncoding\n"
                 "                REF: Pages 56-57, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->lang_version != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    LanguageVersion");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED LanguageVersion\n"
                 "                REF: Pages 57-58, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->manufacturer != NULL)
    {
      if (!_ppd_strncasecmp(ppd->manufacturer, "Hewlett-Packard", 15) ||
	  !_ppd_strncasecmp(ppd->manufacturer, "Hewlett Packard", 15))
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
                   "      **FAIL**  Bad Manufacturer (should be "
		   "\"%s\")\n"
		   "                REF: Page 211, table D.1.",
		   "HP");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (!_ppd_strncasecmp(ppd->manufacturer, "OkiData", 7) ||
	       !_ppd_strncasecmp(ppd->manufacturer, "Oki Data", 8))
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }
	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad Manufacturer (should be "
		   "\"%s\")\n"
		   "                REF: Page 211, table D.1.",
		   "Oki");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}
	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "         PASS    Manufacturer");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else if (ppdversion >= 43)
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED Manufacturer\n"
		 "                REF: Pages 58-59, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->modelname != NULL)
    {
      for (ptr = ppd->modelname; *ptr; ptr ++)
	if (!isalnum(*ptr & 255) && !strchr(" ./-+", *ptr))
	  break;

      if (*ptr)
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }
	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad ModelName - \"%c\" not "
		   "allowed in string.\n"
		   "                REF: Pages 59-60, section 5.3.",
		   *ptr);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    ModelName");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED ModelName\n"
                 "                REF: Pages 59-60, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->nickname != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    NickName");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}
	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED NickName\n"
                 "                REF: Page 60, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors++;
    }

    if (ppdFindOption(ppd, "PageSize") != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    PageSize");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED PageSize\n"
                 "                REF: Pages 99-100, section 5.14.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppdFindOption(ppd, "PageRegion") != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    PageRegion");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED PageRegion\n"
		 "                REF: Page 100, section 5.14.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->pcfilename != NULL)
    {
      if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    PCFileName");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else if (!(ignore & PPD_TEST_WARN_FILENAME))
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED PCFileName\n"
		 "                REF: Pages 61-62, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->product != NULL)
    {
      if (ppd->product[0] != '(' ||
	  ppd->product[strlen(ppd->product) - 1] != ')')
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad Product - not \"(string)\".\n"
                   "                REF: Page 62, section 5.3.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1, "        PASS    Product");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED Product\n"
                 "                REF: Page 62, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors++;
    }

    if ((attr = ppdFindAttr(ppd, "PSVersion", NULL)) != NULL &&
	attr->value != NULL)
    {
      char junkstr[255]; // Temp string
      int junkint;       // Temp integer
      if (sscanf(attr->value, "(%254[^)\n])%d", junkstr, &junkint) != 2)
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad PSVersion - not \"(string) "
                   "int\".\n"
                   "                REF: Pages 62-64, section 5.3.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    PSVersion");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED PSVersion\n"
		 "                REF: Pages 62-64, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->shortnickname != NULL)
    {
      if (strlen(ppd->shortnickname) > 31)
      {
	if (verbose >= 0)
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
		   "      **FAIL**  Bad ShortNickName - longer "
                   "than 31 chars.\n"
                   "                REF: Pages 64-65, section 5.3.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors ++;
      }
      else if (verbose > 0)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        PASS    ShortNickName");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }
    else if (ppdversion >= 43)
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED ShortNickName\n"
                 "                REF: Page 64-65, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    if (ppd->patches != NULL && strchr(ppd->patches, '\"') &&
	strstr(ppd->patches, "*End"))
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  Bad JobPatchFile attribute in file\n"
		 "                REF: Page 24, section 3.4.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    //
    // Check for page sizes without the corresponding ImageableArea or
    // PaperDimension values...
    //

    if (ppd->num_sizes == 0)
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  REQUIRED PageSize\n"
		 "                REF: Page 41, section 5.\n"
                 "                REF: Page 99, section 5.14.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }
    else
    {
      for (j = 0, size = ppd->sizes; j < ppd->num_sizes; j ++, size ++)
      {
	//
	// Don't check custom size...
        //

	if (!strcmp(size->name, "Custom"))
	  continue;

	//
	// Check for ImageableArea...
        //

	if (size->left == 0.0 && size->bottom == 0.0 &&
	    size->right == 0.0 && size->top == 0.0)
        {
	  if (verbose >= 0)
          {
	    if (!errors && !verbose)
            {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    snprintf(str_format, sizeof(str_format) - 1,
		     "      **FAIL**  REQUIRED ImageableArea for "
		     "PageSize %s\n"
		     "                REF: Page 41, section 5.\n"
		     "                REF: Page 102, section 5.15.",
		     size->name);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;
	}

        //
	// Check for PaperDimension...
        //

	if (size->width <= 0.0 && size->length <= 0.0)
        {
	  if (verbose >= 0)
          {
	    if (!errors && !verbose)
            {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    snprintf(str_format, sizeof(str_format) - 1,
		     "      **FAIL**  REQUIRED PaperDimension "
		     "for PageSize %s\n"
		     "                REF: Page 41, section 5.\n"
		     "                REF: Page 103, section 5.15.",
		     size->name);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;
	}
      }
    }

    //
    // Check for valid Resolution, JCLResolution, or SetResolution values...
    //

    if ((option = ppdFindOption(ppd, "Resolution")) == NULL)
      if ((option = ppdFindOption(ppd, "JCLResolution")) == NULL)
	option = ppdFindOption(ppd, "SetResolution");
    if (option != NULL)
    {
      for (j = option->num_choices, choice = option->choices;
	   j > 0;
	   j--, choice++)
      {
	//
	// Verify that all resolution options are of the form NNNdpi
	// or NNNxNNNdpi...
        //

	xdpi = strtol(choice->choice, (char **)&ptr, 10);
	if (ptr > choice->choice && xdpi > 0)
        {
	  if (*ptr == 'x')
	    ydpi = strtol(ptr + 1, (char **)&ptr, 10);
	  else
	    ydpi = xdpi;
	}
	else
	  ydpi = xdpi;

	if (xdpi <= 0 || xdpi > 99999 || ydpi <= 0 || ydpi > 99999 ||
	    strcmp(ptr, "dpi"))
        {
	  if (verbose >= 0)
          {
	    if (!errors && !verbose)
            {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    snprintf(str_format, sizeof(str_format) - 1,
		     "      **FAIL**  Bad option %s choice %s\n"
		     "                REF: Page 84, section 5.9",
		     option->keyword, choice->choice);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors++;
	}
      }
    }

    if ((attr = ppdFindAttr(ppd, "1284DeviceID", NULL)) &&
	strcmp(attr->name, "1284DeviceID"))
    {
      if (verbose >= 0)
      {
	if (!errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 "      **FAIL**  %s must be 1284DeviceID\n"
		 "                REF: Page 72, section 5.5",
		 attr->name);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      errors ++;
    }

    errors = check_case(ppd, errors, verbose, report, log, ld);

    if (!(warn & PPD_TEST_WARN_CONSTRAINTS))
      errors = check_constraints(ppd, errors, verbose, 0, report, log, ld);

    if (!(warn & PPD_TEST_WARN_FILTERS) && !(ignore & PPD_TEST_WARN_FILTERS))
      errors = check_filters(ppd, root, errors, verbose, 0, report, log, ld);

    if (!(warn & PPD_TEST_WARN_PROFILES) && !(ignore & PPD_TEST_WARN_PROFILES))
      errors = check_profiles(ppd, root, errors, verbose, 0, report, log, ld);

    if (!(warn & PPD_TEST_WARN_SIZES))
      errors = check_sizes(ppd, errors, verbose, 0, report, log, ld);

    if (!(warn & PPD_TEST_WARN_TRANSLATIONS))
      errors = check_translations(ppd, errors, verbose, 0, report, log, ld);

    if (!(warn & PPD_TEST_WARN_DUPLEX))
      errors = check_duplex(ppd, errors, verbose, 0, report, log, ld);

    if ((attr = ppdFindAttr(ppd, "cupsLanguages", NULL)) != NULL &&
	attr->value)
    {
      //
      // This file contains localizations, check for conformance of the
      // base translation...
      //

      if ((attr = ppdFindAttr(ppd, "LanguageEncoding", NULL)) != NULL)
      {
	if (!attr->value || strcmp(attr->value, "ISOLatin1"))
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
          {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      **FAIL**  Bad LanguageEncoding %s - "
		      "must be ISOLatin1."),
		     attr->value ? attr->value : "(null)");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;
	}

	if (!ppd->lang_version || strcmp(ppd->lang_version, "English"))
        {
	  if (!errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
          {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      **FAIL**  Bad LanguageVersion %s - "
		      "must be English."),
		     ppd->lang_version ? ppd->lang_version : "(null)");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;
	}

	//
	// Loop through all options and choices...
	//

	for (option = ppdFirstOption(ppd);
	     option;
	     option = ppdNextOption(ppd))
        {
	  //
	  // Check for special characters outside A0 to BF, F7, or F8
	  // that are used for languages other than English.
	  //

	  for (ptr = option->text; *ptr; ptr++)
	    if ((*ptr & 0x80) && (*ptr & 0xe0) != 0xa0 &&
		(*ptr & 0xff) != 0xf7 && (*ptr & 0xff) != 0xf8)
	      break;

	  if (*ptr)
          {
	    if (!errors && !verbose)
            {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    if (verbose >= 0)
            {
	      snprintf(str_format, sizeof(str_format) - 1,
		       ("      **FAIL**  Default translation "
                        "string for option %s contains 8-bit "
                        "characters."),
		       option->keyword);
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }
	    errors ++;
	  }

	  for (j = 0; j < option->num_choices; j ++)
          {
	    //
	    // Check for special characters outside A0 to BF, F7, or F8
	    // that are used for languages other than English.
	    //

	    for (ptr = option->choices[j].text; *ptr; ptr++)
              if ((*ptr & 0x80) && (*ptr & 0xe0) != 0xa0 &&
                  (*ptr & 0xff) != 0xf7 && (*ptr & 0xff) != 0xf8)
                break;

	    if (*ptr)
            {
	      if (!errors && !verbose)
              {
		snprintf(str_format, sizeof(str_format) - 1, " FAIL");
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	      }

	      if (verbose >= 0)
              {
		snprintf(str_format, sizeof(str_format) - 1,
			 ("      **FAIL**  Default translation "
                          "string for option %s choice %s contains "
                          "8-bit characters."),
			 option->keyword,
			 option->choices[j].choice);
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	      }

	      errors ++;
	    }
	  }
	}
      }
    }

    //
    // Final pass/fail notification...
    //

    if (errors)
      status = ERROR_CONFORMANCE;
    else if (!verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " PASS");
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }
    if (verbose >= 0)
    {
      check_basics(file, report, log, ld);

      if (warn & PPD_TEST_WARN_DEFAULTS)
	errors = check_defaults(ppd, errors, verbose, 1, report, log, ld);

      if (warn & PPD_TEST_WARN_CONSTRAINTS)
	errors = check_constraints(ppd, errors, verbose, 1, report, log, ld);

      if ((warn & PPD_TEST_WARN_FILTERS) && !(ignore & PPD_TEST_WARN_FILTERS))
	errors = check_filters(ppd, root, errors, verbose, 1, report, log, ld);

      if ((warn & PPD_TEST_WARN_PROFILES) && !(ignore & PPD_TEST_WARN_PROFILES))
	errors = check_profiles(ppd, root, errors, verbose, 1, report, log, ld);

      if (warn & PPD_TEST_WARN_SIZES)
	errors = check_sizes(ppd, errors, verbose, 1, report, log, ld);
      else
	errors = check_sizes(ppd, errors, verbose, 2, report, log, ld);

      if (warn & PPD_TEST_WARN_TRANSLATIONS)
	errors = check_translations(ppd, errors, verbose, 1, report, log, ld);

      if (warn & PPD_TEST_WARN_DUPLEX)
	errors = check_duplex(ppd, errors, verbose, 1, report, log, ld);

      //
      // Look for legacy duplex keywords...
      //

      if ((option = ppdFindOption(ppd, "JCLDuplex")) == NULL)
	if ((option = ppdFindOption(ppd, "EFDuplex")) == NULL)
	  option = ppdFindOption(ppd, "KD03Duplex");
      if (option)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 ("        WARN    Duplex option keyword %s may not "
		  "work as expected and should be named Duplex.\n"
		  "                REF: Page 122, section 5.17"),
		 option->keyword);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      //
      // Look for default keywords with no corresponding option...
      //

      for (j = 0; j < ppd->num_attrs; j ++)
      {
	attr = ppd->attrs[j];

	if (!strcmp(attr->name, "DefaultColorSpace") ||
	    !strcmp(attr->name, "DefaultColorSep") ||
	    !strcmp(attr->name, "DefaultFont") ||
	    !strcmp(attr->name, "DefaultHalftoneType") ||
	    !strcmp(attr->name, "DefaultImageableArea") ||
	    !strcmp(attr->name, "DefaultLeadingEdge") ||
	    !strcmp(attr->name, "DefaultOutputOrder") ||
	    !strcmp(attr->name, "DefaultPaperDimension") ||
	    !strcmp(attr->name, "DefaultResolution") ||
	    !strcmp(attr->name, "DefaultScreenProc") ||
	    !strcmp(attr->name, "DefaultTransfer"))
	  continue;

	if (!strncmp(attr->name, "Default", 7) &&
	    !ppdFindOption(ppd, attr->name + 7))
        {
	  snprintf(str_format, sizeof(str_format) - 1,
		   ("        WARN    %s has no corresponding "
		    "options."),
		   attr->name);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	}
      }

      if (ppdversion < 43)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 ("        WARN    Obsolete PPD version %.1f.\n"
		  "                REF: Page 42, section 5.2."),
		 0.1f * ppdversion);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      if (!ppd->lang_encoding && ppdversion < 41)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        WARN    LanguageEncoding required by PPD "
		 "4.3 spec.\n"
		 "                REF: Pages 56-57, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      if (!ppd->manufacturer && ppdversion < 43)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        WARN    Manufacturer required by PPD "
		 "4.3 spec.\n"
		 "                REF: Pages 58-59, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      //
      // Treat a PCFileName attribute longer than 12 characters as
      // a warning and not a hard error...
      //

      if (!(ignore & PPD_TEST_WARN_FILENAME) && ppd->pcfilename)
      {
	if (strlen(ppd->pcfilename) > 12)
        {
	  snprintf(str_format, sizeof(str_format) - 1,
		   "        WARN    PCFileName longer than 8.3 in "
		   "violation of PPD spec.\n"
		   "                REF: Pages 61-62, section "
		   "5.3.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	}

	if (!_ppd_strcasecmp(ppd->pcfilename, "unused.ppd"))
	{
	  snprintf(str_format, sizeof(str_format) - 1,
		   "        WARN    PCFileName should contain a "
		   "unique filename.\n"
		   "                REF: Pages 61-62, section "
		   "5.3.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	}
      }

      if (!ppd->shortnickname && ppdversion < 43)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        WARN    ShortNickName required by PPD "
		 "4.3 spec.\n"
		 "                REF: Pages 64-65, section 5.3.");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      //
      // Check the Protocols line and flag PJL + BCP since TBCP is
      // usually used with PJL...
      //

      if (ppd->protocols)
      {
	if (strstr(ppd->protocols, "PJL") &&
	    strstr(ppd->protocols, "BCP") &&
	    !strstr(ppd->protocols, "TBCP"))
	{
	  snprintf(str_format, sizeof(str_format) - 1,
		   "        WARN    Protocols contains both PJL "
		   "and BCP; expected TBCP.\n"
		   "                REF: Pages 78-79, section 5.7.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	}

	if (strstr(ppd->protocols, "PJL") &&
	    (!ppd->jcl_begin || !ppd->jcl_end || !ppd->jcl_ps))
	{
	  snprintf(str_format, sizeof(str_format) - 1,
		   "        WARN    Protocols contains PJL but JCL "
		   "attributes are not set.\n"
		   "                REF: Pages 78-79, section 5.7.");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	}
      }

      //
      // Check for options with a common prefix, e.g. Duplex and Duplexer,
      // which are errors according to the spec but won't cause problems
      // with CUPS specifically...
      //

      for (j = 0, group = ppd->groups; j < ppd->num_groups; j ++, group ++)
	for (k = 0, option = group->options;
	     k < group->num_options;
	     k ++, option ++)
	{
	  len = strlen(option->keyword);

	  for (m = 0, group2 = ppd->groups;
	       m < ppd->num_groups;
	       m ++, group2 ++)
	    for (n = 0, option2 = group2->options;
		 n < group2->num_options;
		 n ++, option2 ++)
	      if (option != option2 &&
		  len < strlen(option2->keyword) &&
		  !strncmp(option->keyword, option2->keyword, len))
	      {
		snprintf(str_format, sizeof(str_format) - 1,
			 ("        WARN    %s shares a common "
			  "prefix with %s\n"
			  "                REF: Page 15, section "
			  "3.2."),
			 option->keyword, option2->keyword);
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
	      }
	}
    }

    if (verbose > 0)
    {
      if (errors)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "    %d ERRORS FOUND", errors);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
      else
      {
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
    }

    //
    // Then list the options, if "-v" was provided...
    //

    if (verbose > 1)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       "\n"
	       "    language_level = %d\n"
	       "    color_device = %s\n"
	       "    variable_sizes = %s\n"
	       "    landscape = %d",
	       ppd->language_level,
	       ppd->color_device ? "TRUE" : "FALSE",
	       ppd->variable_sizes ? "TRUE" : "FALSE",
	       ppd->landscape);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      switch (ppd->colorspace)
      {
        case PPD_CS_CMYK:
            snprintf(str_format, sizeof(str_format) - 1,
		     "    colorspace = PPD_CS_CMYK");
            if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    break;
        case PPD_CS_CMY:
            snprintf(str_format, sizeof(str_format) - 1,
		     "    colorspace = PPD_CS_CMY");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    break;
        case PPD_CS_GRAY:
            snprintf(str_format, sizeof(str_format) - 1,
		     "    colorspace = PPD_CS_GRAY");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    break;
        case PPD_CS_RGB:
            snprintf(str_format, sizeof(str_format) - 1,
		     "    colorspace = PPD_CS_RGB");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    break;
        default:
            snprintf(str_format, sizeof(str_format) - 1,
		     "    colorspace = <unknown>");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    break;
      }
      puts("left switch");

      snprintf(str_format, sizeof(str_format) - 1, "    num_emulations = %d",
	       ppd->num_emulations);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      for (j = 0; j < ppd->num_emulations; j++)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        emulations[%d] = %s",
		 j, ppd->emulations[j].name);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
      }

      snprintf(str_format, sizeof(str_format) - 1, "    lang_encoding = %s",
	       ppd->lang_encoding);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1, "    lang_version = %s",
	       ppd->lang_version);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1,
	       "    modelname = %s", ppd->modelname);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1, "    ttrasterizer = %s",
	       ppd->ttrasterizer == NULL ? "None" : ppd->ttrasterizer);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1, "    manufacturer = %s",
	       ppd->manufacturer);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1,
	       "    product = %s", ppd->product);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1,
	       "    nickname = %s", ppd->nickname);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1, "    shortnickname = %s",
	       ppd->shortnickname);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1, "    patches = %d bytes",
	       ppd->patches == NULL ? 0 : (int)strlen(ppd->patches));
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      snprintf(str_format, sizeof(str_format) - 1,
	       "    num_groups = %d", ppd->num_groups);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      for (j = 0, group = ppd->groups; j < ppd->num_groups; j++, group++)
      {
	snprintf(str_format, sizeof(str_format) - 1, "        group[%d] = %s",
		 j, group->text);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

	for (k = 0, option = group->options; k < group->num_options;
	     k ++, option ++)
        {
	  snprintf(str_format, sizeof(str_format) - 1,
		   "            options[%d] = %s (%s) %s %s %.0f "
		   "(%d choices)",
		   k, option->keyword, option->text, uis[option->ui],
		   sections[option->section], option->order,
		   option->num_choices);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

	  if (!strcmp(option->keyword, "PageSize") ||
	      !strcmp(option->keyword, "PageRegion"))
          {
	    for (m = option->num_choices, choice = option->choices;
		 m > 0;
		 m --, choice ++)
            {
	      size = ppdPageSize(ppd, choice->choice);

	      if (size == NULL)
              {
		snprintf(str_format, sizeof(str_format) - 1,
                         "                %s (%s) = ERROR%s",
			 choice->choice, choice->text,
			 !strcmp(option->defchoice, choice->choice)
			 ? " *"
			 : "");
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	      }
              else
              {
		snprintf(str_format, sizeof(str_format) - 1,
			 "                %s (%s) = %.2fx%.2fin "
			 "(%.1f,%.1f,%.1f,%.1f)%s",
			 choice->choice, choice->text,
			 size->width / 72.0, size->length / 72.0,
			 size->left / 72.0, size->bottom / 72.0,
			 size->right / 72.0, size->top / 72.0,
			 !strcmp(option->defchoice, choice->choice)
			 ? " *"
			 : "");
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	      }
	    }
	  }
	  else
          {
	    for (m = option->num_choices, choice = option->choices;
		 m > 0;
		 m --, choice ++)
            {
	      snprintf(str_format, sizeof(str_format) - 1,
		       "                %s (%s)%s",
		       choice->choice, choice->text,
		       !strcmp(option->defchoice, choice->choice)
		       ? " *"
		       : "");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
	    }
	  }
	}
      }

      snprintf(str_format, sizeof(str_format) - 1, "    num_consts = %d",
	       ppd->num_consts);
      if (*report)
	cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

      for (j = 0; j < ppd->num_consts; j ++)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        consts[%d] = *%s %s *%s %s",
		 j, ppd->consts[j].option1, ppd->consts[j].choice1,
		 ppd->consts[j].option2, ppd->consts[j].choice2);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

	snprintf(str_format, sizeof(str_format) - 1, "    num_profiles = %d",
		 ppd->num_profiles);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
      }
      for (j = 0; j < ppd->num_profiles; j++)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        profiles[%d] = %s/%s %.3f %.3f "
		 "[ %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f %.3f ]",
		 j, ppd->profiles[j].resolution,
		 ppd->profiles[j].media_type,
		 ppd->profiles[j].gamma, ppd->profiles[j].density,
		 ppd->profiles[j].matrix[0][0],
		 ppd->profiles[j].matrix[0][1],
		 ppd->profiles[j].matrix[0][2],
		 ppd->profiles[j].matrix[1][0],
		 ppd->profiles[j].matrix[1][1],
		 ppd->profiles[j].matrix[1][2],
		 ppd->profiles[j].matrix[2][0],
		 ppd->profiles[j].matrix[2][1],
		 ppd->profiles[j].matrix[2][2]);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

	snprintf(str_format, sizeof(str_format) - 1,
		 "    num_fonts = %d", ppd->num_fonts);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
      }
      for (j = 0; j < ppd->num_fonts; j ++)
      {
	snprintf(str_format, sizeof(str_format) - 1, "        fonts[%d] = %s",
		 j, ppd->fonts[j]);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);

	snprintf(str_format, sizeof(str_format) - 1,
		 "    num_attrs = %d", ppd->num_attrs);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
      }
      for (j = 0; j < ppd->num_attrs; j ++)
      {
	snprintf(str_format, sizeof(str_format) - 1,
		 "        attrs[%d] = %s %s%s%s: \"%s\"", j,
		 ppd->attrs[j]->name, ppd->attrs[j]->spec,
		 ppd->attrs[j]->text[0] ? "/" : "",
		 ppd->attrs[j]->text,
		 ppd->attrs[j]->value ? ppd->attrs[j]->value : "(null)");
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_INFO, "ppdTest: %s", str_format);
      }
    }

    ppdClose(ppd);
  }

  if (!i)
  {
    if (log) log(ld, CF_LOGLEVEL_ERROR,
		 "ppdTest: No PPD file to be tested supplied.");
    return (-1);
  }

  return (!status);
}


//
// 'check_basics()' - Check for CR LF, mixed line endings, and blank lines.
//

static void
check_basics(const char *filename,  // I - PPD file to check
	     cups_array_t **report, // I - Report text
	     cf_logfunc_t log,      // I - Log function
	     void *ld)              // I - Log function data
{
  cups_file_t *fp;       // File pointer
  int ch;                // Current character
  int col,               // Current column
      whitespace;        // Only seen whitespace?
  int eol;               // Line endings
  int linenum;           // Line number
  int mixed;             // Mixed line endings?
  char str_format[2048]; // Formatted string

  if ((fp = cupsFileOpen(filename, "r")) == NULL)
    return;

  linenum = 1;
  col = 0;
  eol = EOL_NONE;
  mixed = 0;
  whitespace = 1;

  while ((ch = cupsFileGetChar(fp)) != EOF)
  {
    if (ch == '\r' || ch == '\n')
    {
      if (ch == '\n')
      {
        if (eol == EOL_NONE)
          eol = EOL_LF;
        else if (eol != EOL_LF)
          mixed = 1;
      }
      else if (ch == '\r')
      {
        if (cupsFilePeekChar(fp) == '\n')
        {
	  cupsFileGetChar(fp);

	  if (eol == EOL_NONE)
            eol = EOL_CRLF;
	  else if (eol != EOL_CRLF)
            mixed = 1;
        }
        else if (eol == EOL_NONE)
          eol = EOL_CR;
        else if (eol != EOL_CR)
          mixed = 1;
      }

      if (col > 0 && whitespace)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("        WARN    Line %d only contains whitespace."),
		 linenum);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
      }

      linenum ++;
      col = 0;
      whitespace = 1;
    }
    else
    {
      if (ch != ' ' && ch != '\t')
        whitespace = 0;
      col ++;
    }
  }

  if (mixed)
  {
    snprintf(str_format, sizeof(str_format) - 1,
	     "        WARN    File contains a mix of CR, LF, and "
	     "CR LF line endings.");
    if (*report)
      cupsArrayAdd(*report, (void *)str_format);
    if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
  }

  if (eol == EOL_CRLF)
  {
    snprintf(str_format, sizeof(str_format) - 1,
	     "        WARN    Non-Windows PPD files should use lines "
	     "ending with only LF, not CR LF.");
    if (*report)
      cupsArrayAdd(*report, (void *)str_format);
    if (log) log(ld, CF_LOGLEVEL_WARN, "ppdTest: %s", str_format);
  }

  cupsFileClose(fp);
}


//
// 'check_constraints()' - Check UIConstraints in the PPD file.
//

static int                               // O - Errors found
check_constraints(ppd_file_t *ppd,       // I - PPD file
                  int errors,            // I - Errors found
                  int verbose,           // I - Verbosity level
                  int warn,              // I - Warnings only?
                  cups_array_t **report, // I - Report text
		  cf_logfunc_t log,      // I - Log function
		  void *ld)              // I - Log function data
{
  int i;                     // Looping var
  const char *prefix;        // WARN/FAIL prefix
  ppd_const_t *c;            // Current UIConstraints data
  ppd_attr_t *constattr;     // Current cupsUIConstraints attribute
  const char *vptr;          // Pointer into constraint value
  char option[PPD_MAX_NAME], // Option name/MainKeyword
       choice[PPD_MAX_NAME], // Choice/OptionKeyword
       *ptr;                 // Pointer into option or choice
  int num_options;           // Number of options
  cups_option_t *options;    // Options
  ppd_option_t *o;           // PPD option
  char str_format[2048];     // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  //
  // See what kind of constraint data we have in the PPD...
  //

  if ((constattr = ppdFindAttr(ppd, "cupsUIConstraints", NULL)) != NULL)
  {
    //
    // Check new-style cupsUIConstraints data...
    //

    for (; constattr;
	 constattr = ppdFindNextAttr(ppd, "cupsUIConstraints", NULL))
    {
      if (!constattr->value)
      {
	if (!warn && !errors && !verbose)
	{
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Empty cupsUIConstraints %s"),
		 prefix, constattr->spec);
	if (*report)
	  cupsArrayAdd(*report, (void *)str_format);
	if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

	if (!warn)
          errors ++;

	continue;
      }

      for (i = 0, vptr = strchr(constattr->value, '*');
	   vptr;
	   i++, vptr = strchr(vptr + 1, '*'))

	if (i == 0)
	{
	  if (!warn && !errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad cupsUIConstraints %s: \"%s\""),
		   prefix, constattr->spec, constattr->value);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

	  if (!warn)
            errors ++;

	  continue;
	}

      cupsArraySave(ppd->sorted_attrs);

      if (constattr->spec[0] &&
          !ppdFindAttr(ppd, "cupsUIResolver", constattr->spec))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing cupsUIResolver %s"),
		 prefix, constattr->spec);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors ++;
      }

      cupsArrayRestore(ppd->sorted_attrs);

      num_options = 0;
      options = NULL;

      for (vptr = strchr(constattr->value, '*');
	   vptr;
	   vptr = strchr(vptr, '*'))
      {
        //
        // Extract "*Option Choice" or just "*Option"...
	//

        for (vptr++, ptr = option; *vptr && !isspace(*vptr & 255); vptr++)
          if (ptr < (option + sizeof(option) - 1))
            *ptr++ = *vptr;

        *ptr = '\0';

        while (isspace(*vptr & 255))
          vptr++;

        if (*vptr == '*')
          choice[0] = '\0';
        else
        {
          for (ptr = choice; *vptr && !isspace(*vptr & 255); vptr++)
            if (ptr < (choice + sizeof(choice) - 1))
	      *ptr++ = *vptr;

          *ptr = '\0';
        }

        if (!_ppd_strncasecmp(option, "Custom", 6) &&
	    !_ppd_strcasecmp(choice, "True"))
        {
          _ppd_strcpy(option, option + 6);
          strlcpy(choice, "Custom", sizeof(choice));
        }

        if ((o = ppdFindOption(ppd, option)) == NULL)
        {
          if (!warn && !errors && !verbose)
          {
            snprintf(str_format, sizeof(str_format) - 1, " FAIL");
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          snprintf(str_format, sizeof(str_format) - 1,
                  ("      %s  Missing option %s in "
		   "cupsUIConstraints %s: \"%s\""),
		   prefix, option, constattr->spec, constattr->value);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

          if (!warn)
	    errors ++;

          continue;
        }

        if (choice[0] && !ppdFindChoice(o, choice))
        {
          if (!warn && !errors && !verbose)
          {
            snprintf(str_format, sizeof(str_format) - 1, " FAIL");
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Missing choice *%s %s in "
                    "cupsUIConstraints %s: \"%s\""),
		   prefix, option, choice, constattr->spec,
		   constattr->value);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

          if (!warn)
            errors ++;

          continue;
        }

        if (choice[0])
	  num_options = cupsAddOption(option, choice, num_options, &options);
        else
        {
          for (i = 0; i < o->num_choices; i++)
	    if (_ppd_strcasecmp(o->choices[i].choice, "None") &&
		_ppd_strcasecmp(o->choices[i].choice, "Off") &&
		_ppd_strcasecmp(o->choices[i].choice, "False"))
	    {
	      num_options = cupsAddOption(option, o->choices[i].choice,
					  num_options, &options);
	      break;
	    }
        }
      }

      //
      // Resolvers must list at least two options...
      //

      if (num_options < 2)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  cupsUIResolver %s does not list at least "
		  "two different options."),
		 prefix, constattr->spec);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors ++;
      }

      //
      // Test the resolver...
      //

      if (!ppdResolveConflicts(ppd, NULL, NULL, &num_options, &options))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  cupsUIResolver %s causes a loop."),
		 prefix, constattr->spec);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors ++;
      }

      cupsFreeOptions(num_options, options);
    }
  }
  else
  {
    //
    // Check old-style [Non]UIConstraints data...
    //

    for (i = ppd->num_consts, c = ppd->consts; i > 0; i --, c ++)
    {
      if (!_ppd_strncasecmp(c->option1, "Custom", 6) &&
	  !_ppd_strcasecmp(c->choice1, "True"))
      {
        strlcpy(option, c->option1 + 6, sizeof(option));
        strlcpy(choice, "Custom", sizeof(choice));
      }
      else
      {
        strlcpy(option, c->option1, sizeof(option));
        strlcpy(choice, c->choice1, sizeof(choice));
      }

      if ((o = ppdFindOption(ppd, option)) == NULL)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing option %s in "
		  "UIConstraints \"*%s %s *%s %s\"."),
		 prefix, c->option1,
		 c->option1, c->choice1, c->option2, c->choice2);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors ++;
      }
      else if (choice[0] && !ppdFindChoice(o, choice))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing choice *%s %s in "
		  "UIConstraints \"*%s %s *%s %s\"."),
		 prefix, c->option1, c->choice1,
		 c->option1, c->choice1, c->option2, c->choice2);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors ++;
      }

      if (!_ppd_strncasecmp(c->option2, "Custom", 6) &&
	  !_ppd_strcasecmp(c->choice2, "True"))
      {
        strlcpy(option, c->option2 + 6, sizeof(option));
        strlcpy(choice, "Custom", sizeof(choice));
      }
      else
      {
        strlcpy(option, c->option2, sizeof(option));
        strlcpy(choice, c->choice2, sizeof(choice));
      }

      if ((o = ppdFindOption(ppd, option)) == NULL)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing option %s in "
		  "UIConstraints \"*%s %s *%s %s\"."),
		 prefix, c->option2,
		 c->option1, c->choice1, c->option2, c->choice2);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
          errors++;
      }
      else if (choice[0] && !ppdFindChoice(o, choice))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing choice *%s %s in "
		  "UIConstraints \"*%s %s *%s %s\"."),
		 prefix, c->option2, c->choice2,
		 c->option1, c->choice1, c->option2, c->choice2);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);

        if (!warn)
	  errors ++;
      }
    }
  }

  return (errors);
}


//
// 'check_case()' - Check that there are no duplicate groups, options,
//                  or choices that differ only by case.
//

static int                        // O - Errors found
check_case(ppd_file_t *ppd,       // I - PPD file
           int errors,            // I - Errors found
           int verbose,           // I - Verbosity level
           cups_array_t **report, // I - Report text
	   cf_logfunc_t log,      // I - Log function
	   void *ld)              // I - Log function data
{
  int i, j;              // Looping vars
  ppd_group_t *groupa,   // First group
        *groupb;         // Second group
  ppd_option_t *optiona, // First option
        *optionb;        // Second option
  ppd_choice_t *choicea, // First choice
        *choiceb;        // Second choice
  char str_format[2048]; // Formatted string

  //
  // Check that the groups do not have any duplicate names...
  //

  for (i = ppd->num_groups, groupa = ppd->groups; i > 1; i --, groupa ++)
    for (j = i - 1, groupb = groupa + 1; j > 0; j --, groupb ++)
      if (!_ppd_strcasecmp(groupa->name, groupb->name))
      {
        if (!errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      **FAIL**  Group names %s and %s differ only "
		    "by case."),
		   groupa->name, groupb->name);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        errors ++;
      }

  //
  // Check that the options do not have any duplicate names...
  //

  for (optiona = ppdFirstOption(ppd); optiona; optiona = ppdNextOption(ppd))
  {
    cupsArraySave(ppd->options);
    for (optionb = ppdNextOption(ppd); optionb; optionb = ppdNextOption(ppd))
      if (!_ppd_strcasecmp(optiona->keyword, optionb->keyword))
      {
	if (!errors && !verbose)
	{
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	if (verbose >= 0)
	{
	  snprintf(str_format, sizeof(str_format) - 1,
		   ("      **FAIL**  Option names %s and %s differ only "
		    "by case."),
		   optiona->keyword, optionb->keyword);
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	}

	errors++;
      }

    cupsArrayRestore(ppd->options);

    //
    // Then the choices...
    //

    for (i = optiona->num_choices, choicea = optiona->choices;
	 i > 1;
	 i --, choicea ++)
      for (j = i - 1, choiceb = choicea + 1; j > 0; j --, choiceb ++)
	if (!strcmp(choicea->choice, choiceb->choice))
	{
	  if (!errors && !verbose)
	  {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
	  {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      **FAIL**  Multiple occurrences of "
		      "option %s choice name %s."),
		     optiona->keyword, choicea->choice);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;

	  choicea ++;
	  i --;
	  break;
	}
	else if (!_ppd_strcasecmp(choicea->choice, choiceb->choice))
	{
	  if (!errors && !verbose)
	    {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	  if (verbose >= 0)
	  {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      **FAIL**  Option %s choice names %s and "
		      "%s differ only by case."),
		     optiona->keyword, choicea->choice, choiceb->choice);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  errors ++;
	}
  }

  //
  // Return the number of errors found...
  //

  return (errors);
}


//
// 'check_defaults()' - Check default option keywords in the PPD file.
//

static int                            // O - Errors found
check_defaults(ppd_file_t *ppd,       // I - PPD file
               int errors,            // I - Errors found
               int verbose,           // I - Verbosity level
               int warn,              // I - Warnings only?
               cups_array_t **report, // I - Report text
	       cf_logfunc_t log,      // I - Log function
	       void *ld)              // I - Log function data
{
  int j, k;                // Looping vars
  ppd_attr_t *attr;        // PPD attribute
  ppd_option_t *option;    // Standard UI option
  const char *prefix;      // WARN/FAIL prefix
  char str_format[2048];   // Formatted string

  prefix = warn ? "  WARN  " : "**FAIL**";

  ppdMarkDefaults(ppd);
  if (ppdConflicts(ppd))
  {
    if (!warn && !errors && !verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (verbose >= 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      %s  Default choices conflicting."), prefix);
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    show_conflicts(ppd, prefix, report, log, ld);

    if (!warn)
      errors ++;
  }

  for (j = 0; j < ppd->num_attrs; j ++)
  {
    attr = ppd->attrs[j];

    if (!strcmp(attr->name, "DefaultColorSpace") ||
        !strcmp(attr->name, "DefaultFont") ||
        !strcmp(attr->name, "DefaultHalftoneType") ||
        !strcmp(attr->name, "DefaultImageableArea") ||
        !strcmp(attr->name, "DefaultLeadingEdge") ||
        !strcmp(attr->name, "DefaultOutputOrder") ||
        !strcmp(attr->name, "DefaultPaperDimension") ||
        !strcmp(attr->name, "DefaultResolution") ||
        !strcmp(attr->name, "DefaultTransfer"))
      continue;

    if (!strncmp(attr->name, "Default", 7))
    {
      if ((option = ppdFindOption(ppd, attr->name + 7)) != NULL &&
          strcmp(attr->value, "Unknown"))
      {
        //
        // Check that the default option value matches a choice...
	//

        for (k = 0; k < option->num_choices; k ++)
	  if (!strcmp(option->choices[k].choice, attr->value))
            break;

        if (k >= option->num_choices)
        {
          if (!warn && !errors && !verbose)
	  {
            snprintf(str_format, sizeof(str_format) - 1, " FAIL");
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          if (verbose >= 0)
          {
            snprintf(str_format, sizeof(str_format) - 1,
		     ("      %s  %s %s does not exist."),
		     prefix, attr->name, attr->value);
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          if (!warn)
	    errors ++;
        }
      }
    }
  }

  return (errors);
}


//
// 'check_duplex()' - Check duplex keywords in the PPD file.
//

static int                          // O - Errors found
check_duplex(ppd_file_t *ppd,       // I - PPD file
             int errors,            // I - Error found
             int verbose,           // I - Verbosity level
             int warn,              // I - Warnings only?
             cups_array_t **report, // I - Report text
	     cf_logfunc_t log,      // I - Log function
	     void *ld)              // I - Log function data
{
  int i;                 // Looping var
  ppd_option_t *option;  // PPD option
  ppd_choice_t *choice;  // Current choice
  const char *prefix;    // Message prefix
  char str_format[2048]; // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  //
  // Check for a duplex option, and for standard values...
  //

  if ((option = ppdFindOption(ppd, "Duplex")) != NULL)
  {
    if (!ppdFindChoice(option, "None"))
    {
      if (verbose >= 0)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  REQUIRED %s does not define "
		  "choice None.\n"
		  "                REF: Page 122, section 5.17"),
		 prefix, option->keyword);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    for (i = option->num_choices, choice = option->choices;
         i > 0;
         i --, choice ++)
      if (strcmp(choice->choice, "None") &&
          strcmp(choice->choice, "DuplexNoTumble") &&
          strcmp(choice->choice, "DuplexTumble") &&
          strcmp(choice->choice, "SimplexTumble"))
      {
        if (verbose >= 0)
        {
          if (!warn && !errors && !verbose)
          {
            snprintf(str_format, sizeof(str_format) - 1, " FAIL");
            if (*report)
              cupsArrayAdd(*report, (void *)str_format);
            if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
          }

          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad %s choice %s.\n"
                    "                REF: Page 122, section 5.17"),
		   prefix, option->keyword, choice->choice);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
  }
  return (errors);
}


//
// 'check_filters()' - Check filters in the PPD file.
//

static int                           // O - Errors found
check_filters(ppd_file_t *ppd,       // I - PPD file
              const char *root,      // I - Root directory
              int errors,            // I - Errors found
              int verbose,           // I - Verbosity level
              int warn,              // I - Warnings only?
              cups_array_t **report, // I - Report text
	      cf_logfunc_t log,      // I - Log function
	      void *ld)              // I - Log function data
{
  ppd_attr_t *attr;      // PPD attribute
  const char *ptr;       // Pointer into string
  char super[16],        // Super-type for filter
    type[256],           // Type for filter
    dstsuper[16],        // Destination super-type for filter
    dsttype[256],        // Destination type for filter
    program[128],        // Program/filter name
    pathprog[1024];      // Complete path to program/filter
  int cost;              // Cost of filter
  const char *prefix;    // WARN/FAIL prefix
  struct stat fileinfo;  // File information
  char str_format[2048]; // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  //
  // cupsFilter
  //

  for (attr = ppdFindAttr(ppd, "cupsFilter", NULL);
       attr;
       attr = ppdFindNextAttr(ppd, "cupsFilter", NULL))
  {
    if (strcmp(attr->name, "cupsFilter"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "cupsFilter");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    if (!attr->value ||
	sscanf(attr->value, "%15[^/]/%255s%d%*[ \t]%1023[^\n]", super, type,
	       &cost, program) != 4)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad cupsFilter value \"%s\"."),
		 prefix, attr->value);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;

      continue;
    }

    if (!strncmp(program, "maxsize(", 8))
    {
      char *mptr; // Pointer into maxsize(nnnn) program

      strtoll(program + 8, &mptr, 10);

      if (*mptr != ')')
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
	  snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad cupsFilter value \"%s\"."),
		   prefix, attr->value);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
	  errors ++;

        continue;
      }

      mptr ++;
      while (_ppd_isspace(*mptr))
        mptr ++;

      _ppd_strcpy(program, mptr);
    }

    if (strcmp(program, "-"))
    {
      if (program[0] == '/')
        snprintf(pathprog, sizeof(pathprog), "%s%s", root, program);
      else
      {
        if ((ptr = getenv("CUPS_SERVERBIN")) == NULL)
          ptr = CUPS_SERVERBIN;

        if (*ptr == '/' || !*root)
          snprintf(pathprog, sizeof(pathprog), "%s%s/filter/%s", root, ptr,
		   program);
        else
          snprintf(pathprog, sizeof(pathprog), "%s/%s/filter/%s", root, ptr,
		   program);
      }

      if (stat(pathprog, &fileinfo))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Missing %s file \"%s\"."),
		   prefix, "cupsFilter", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else if (fileinfo.st_uid != 0 ||
               (fileinfo.st_mode & MODE_WRITE) ||
               (fileinfo.st_mode & MODE_MASK) != MODE_PROGRAM)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad permissions on %s file \"%s\"."),
		   prefix, "cupsFilter", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else
        errors = valid_path("cupsFilter", pathprog, errors, verbose, warn,
			    report, log, ld);
    }
  }

  //
  // cupsFilter2
  //

  for (attr = ppdFindAttr(ppd, "cupsFilter2", NULL);
       attr;
       attr = ppdFindNextAttr(ppd, "cupsFilter2", NULL))
  {
    if (strcmp(attr->name, "cupsFilter2"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "cupsFilter2");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }
      if (!warn)
        errors++;
    }

    if (!attr->value ||
        sscanf(attr->value,
	       "%15[^/]/%255s%*[ \t]%15[^/]/%255s%d%*[ \t]%1023[^\n]",
	       super, type, dstsuper, dsttype, &cost, program) != 6)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad cupsFilter2 value \"%s\"."),
		 prefix, attr->value);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;

      continue;
    }

    if (!strncmp(program, "maxsize(", 8))
    {
      char *mptr; // Pointer into maxsize(nnnn) program

      strtoll(program + 8, &mptr, 10);

      if (*mptr != ')')
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad cupsFilter2 value \"%s\"."),
		   prefix, attr->value);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;

        continue;
      }

      mptr ++;
      while (_ppd_isspace(*mptr))
        mptr ++;

      _ppd_strcpy(program, mptr);
    }

    if (strcmp(program, "-"))
    {
      if (strncmp(program, "maxsize(", 8) &&
          (ptr = strchr(program + 8, ')')) != NULL)
      {
        ptr ++;
        while (_ppd_isspace(*ptr))
	  ptr ++;

        _ppd_strcpy(program, ptr);
      }

      if (program[0] == '/')
        snprintf(pathprog, sizeof(pathprog), "%s%s", root, program);
      else
      {
        if ((ptr = getenv("CUPS_SERVERBIN")) == NULL)
          ptr = CUPS_SERVERBIN;

        if (*ptr == '/' || !*root)
          snprintf(pathprog, sizeof(pathprog), "%s%s/filter/%s", root, ptr,
		   program);
        else
          snprintf(pathprog, sizeof(pathprog), "%s/%s/filter/%s", root, ptr,
		   program);
      }

      if (stat(pathprog, &fileinfo))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Missing %s file \"%s\"."),
		   prefix, "cupsFilter2", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else if (fileinfo.st_uid != 0 ||
               (fileinfo.st_mode & MODE_WRITE) ||
               (fileinfo.st_mode & MODE_MASK) != MODE_PROGRAM)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad permissions on %s file \"%s\"."),
		   prefix, "cupsFilter2", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else
        errors = valid_path("cupsFilter2", pathprog, errors, verbose, warn,
			    report, log, ld);
    }
  }

  //
  // cupsPreFilter
  //

  for (attr = ppdFindAttr(ppd, "cupsPreFilter", NULL);
       attr;
       attr = ppdFindNextAttr(ppd, "cupsPreFilter", NULL))
  {
    if (strcmp(attr->name, "cupsPreFilter"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "cupsPreFilter");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    if (!attr->value ||
        sscanf(attr->value, "%15[^/]/%255s%d%*[ \t]%1023[^\n]", super, type,
	       &cost, program) != 4)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad cupsPreFilter value \"%s\"."),
		 prefix, attr->value ? attr->value : "");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (strcmp(program, "-"))
    {
      if (program[0] == '/')
        snprintf(pathprog, sizeof(pathprog), "%s%s", root, program);
      else
      {
        if ((ptr = getenv("CUPS_SERVERBIN")) == NULL)
          ptr = CUPS_SERVERBIN;

        if (*ptr == '/' || !*root)
          snprintf(pathprog, sizeof(pathprog), "%s%s/filter/%s", root, ptr,
		   program);
        else
          snprintf(pathprog, sizeof(pathprog), "%s/%s/filter/%s", root, ptr,
		   program);
      }

      if (stat(pathprog, &fileinfo))
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Missing %s file \"%s\"."),
		   prefix, "cupsPreFilter", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else if (fileinfo.st_uid != 0 ||
               (fileinfo.st_mode & MODE_WRITE) ||
               (fileinfo.st_mode & MODE_MASK) != MODE_PROGRAM)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad permissions on %s file \"%s\"."),
		   prefix, "cupsPreFilter", pathprog);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
      else
        errors = valid_path("cupsPreFilter", pathprog, errors, verbose, warn,
			    report, log, ld);
    }
  }

  #ifdef __APPLE__
  //
  // APDialogExtension
  //

  for (attr = ppdFindAttr(ppd, "APDialogExtension", NULL);
       attr != NULL;
       attr = ppdFindNextAttr(ppd, "APDialogExtension", NULL))
  {
    if (strcmp(attr->name, "APDialogExtension"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
                    ("      %s  Bad spelling of %s - should be %s."),
                    prefix, attr->name, "APDialogExtension");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    snprintf(pathprog, sizeof(pathprog), "%s%s", root,
	     attr->value ? attr->value : "(null)");

    if (!attr->value || stat(pathprog, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "APDialogExtension", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
            (fileinfo.st_mode & MODE_WRITE) ||
            (fileinfo.st_mode & MODE_MASK) != MODE_DIRECTORY)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "APDialogExtension", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("APDialogExtension", pathprog, errors, verbose,
                          warn, report, log, ld);
  }

  //
  // APPrinterIconPath
  //

  if ((attr = ppdFindAttr(ppd, "APPrinterIconPath", NULL)) != NULL)
  {
    if (strcmp(attr->name, "APPrinterIconPath"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "APPrinterIconPath");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    snprintf(pathprog, sizeof(pathprog), "%s%s", root,
	     attr->value ? attr->value : "(null)");

    if (!attr->value || stat(pathprog, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "APPrinterIconPath", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
	     (fileinfo.st_mode & MODE_WRITE) ||
	     (fileinfo.st_mode & MODE_MASK) != MODE_DATAFILE)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "APPrinterIconPath", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("APPrinterIconPath", pathprog, errors, verbose,
                          warn, report, log, ld);
  }

  //
  // APPrinterLowInkTool
  //

  if ((attr = ppdFindAttr(ppd, "APPrinterLowInkTool", NULL)) != NULL)
  {
    if (strcmp(attr->name, "APPrinterLowInkTool"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "APPrinterLowInkTool");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    snprintf(pathprog, sizeof(pathprog), "%s%s", root,
	     attr->value ? attr->value : "(null)");

    if (!attr->value || stat(pathprog, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "APPrinterLowInkTool", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
            (fileinfo.st_mode & MODE_WRITE) ||
            (fileinfo.st_mode & MODE_MASK) != MODE_DIRECTORY)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "APPrinterLowInkTool", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("APPrinterLowInkTool", pathprog, errors, verbose,
                          warn, report, log, ld);
  }

  //
  // APPrinterUtilityPath
  //

  if ((attr = ppdFindAttr(ppd, "APPrinterUtilityPath", NULL)) != NULL)
  {
    if (strcmp(attr->name, "APPrinterUtilityPath"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "APPrinterUtilityPath");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    snprintf(pathprog, sizeof(pathprog), "%s%s", root,
	     attr->value ? attr->value : "(null)");

    if (!attr->value || stat(pathprog, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "APPrinterUtilityPath", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
            (fileinfo.st_mode & MODE_WRITE) ||
            (fileinfo.st_mode & MODE_MASK) != MODE_DIRECTORY)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "APPrinterUtilityPath", pathprog);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("APPrinterUtilityPath", pathprog, errors, verbose,
                          warn, report, log, ld);
  }

  //
  // APScanAppBundleID and APScanAppPath
  //

  if ((attr = ppdFindAttr(ppd, "APScanAppPath", NULL)) != NULL)
  {
    if (strcmp(attr->name, "APScanAppPath"))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad spelling of %s - should be %s."),
		 prefix, attr->name, "APScanAppPath");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    if (!attr->value || stat(attr->value, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "APScanAppPath",
		 attr->value ? attr->value : "<NULL>");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
            (fileinfo.st_mode & MODE_WRITE) ||
            (fileinfo.st_mode & MODE_MASK) != MODE_DIRECTORY)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "APScanAppPath", attr->value);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("APScanAppPath", attr->value, errors, verbose,
                          warn, report, log, ld);

    if (ppdFindAttr(ppd, "APScanAppBundleID", NULL))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Cannot provide both "
		  "APScanAppPath and APScanAppBundleID."),
		 prefix);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
  }
  #endif // __APPLE__

  return (errors);
}


//
// 'check_profiles()' - Check ICC color profiles in the PPD file.
//

static int                            // O - Errors found
check_profiles(ppd_file_t *ppd,       // I - PPD file
               const char *root,      // I - Root directory
               int errors,            // I - Errors found
               int verbose,           // I - Verbosity level
               int warn,              // I - Warnings only?
               cups_array_t **report, // I - Report text
	       cf_logfunc_t log,      // I - Log function
	       void *ld)              // I - Log function data
{
  int i;                   // Looping var
  ppd_attr_t *attr;        // PPD attribute
  const char *ptr;         // Pointer into string
  const char *prefix;      // WARN/FAIL prefix
  char filename[1024];     // Profile filename
  struct stat fileinfo;    // File information
  int num_profiles = 0;    // Number of profiles
  unsigned hash,           // Current hash value
      hashes[1000];        // Hash values of profile names
  const char *specs[1000]; // Specifiers for profiles
  char str_format[2048];   // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  for (attr = ppdFindAttr(ppd, "cupsICCProfile", NULL);
       attr;
       attr = ppdFindNextAttr(ppd, "cupsICCProfile", NULL))
  {
    //
    // Check for valid selector...
    //

    for (i = 0, ptr = strchr(attr->spec, '.'); ptr; ptr = strchr(ptr + 1, '.'))
      i ++;

    if (!attr->value || i < 2)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad cupsICCProfile %s."),
		 prefix, attr->spec);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;

      continue;
    }

    //
    // Check for valid profile filename...
    //

    if (attr->value[0] == '/')
      snprintf(filename, sizeof(filename), "%s%s", root, attr->value);
    else
    {
      if ((ptr = getenv("CUPS_DATADIR")) == NULL)
        ptr = CUPS_DATADIR;

      if (*ptr == '/' || !*root)
        snprintf(filename, sizeof(filename), "%s%s/profiles/%s", root, ptr,
		 attr->value);
      else
        snprintf(filename, sizeof(filename), "%s/%s/profiles/%s", root, ptr,
		 attr->value);
    }

    if (stat(filename, &fileinfo))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Missing %s file \"%s\"."),
		 prefix, "cupsICCProfile", filename);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (fileinfo.st_uid != 0 ||
            (fileinfo.st_mode & MODE_WRITE) ||
            (fileinfo.st_mode & MODE_MASK) != MODE_DATAFILE)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Bad permissions on %s file \"%s\"."),
		 prefix, "cupsICCProfile", filename);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else
      errors = valid_path("cupsICCProfile", filename, errors, verbose, warn,
			  report, log, ld);

    //
    // Check for hash collisions...
    //

    hash = ppdHashName(attr->spec);

    if (num_profiles > 0)
    {
      for (i = 0; i < num_profiles; i++)
        if (hashes[i] == hash)
	  break;

      if (i < num_profiles)
      {
        if (!warn && !errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  cupsICCProfile %s hash value "
		    "collides with %s."),
		   prefix, attr->spec,
		   specs[i]);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
    }

    //
    // Remember up to 1000 profiles...
    //

    if (num_profiles < 1000)
    {
      hashes[num_profiles] = hash;
      specs[num_profiles] = attr->spec;
      num_profiles++;
    }
  }

  return (errors);
}


//
// 'check_sizes()' - Check media sizes in the PPD file.
//

static int                         // O - Errors found
check_sizes(ppd_file_t *ppd,       // I - PPD file
            int errors,            // I - Errors found
            int verbose,           // I - Verbosity level
            int warn,              // I - Warnings only?
            cups_array_t **report, // I - Report text
	    cf_logfunc_t log,      // I - Log function
	    void *ld)              // I - Log function data
{
  int i;                     // Looping var
  ppd_size_t *size;          // Current size
  int width,                 // Custom width
      length;                // Custom length
  const char *prefix;        // WARN/FAIL prefix
  ppd_option_t *page_size,   // PageSize option
               *page_region; // PageRegion option
  pwg_media_t *pwg_media;    // PWG media
  char buf[PPD_MAX_NAME];    // PapeSize name that is supposed to be
  const char *ptr;           // Pointer into string
  int width_2540ths,         // PageSize width in 2540ths
      length_2540ths;        // PageSize length in 2540ths
  int is_ok;                 // Flag for PageSize name verification
  double width_tmp,          // Width after rounded up
         length_tmp,         // Length after rounded up
         width_inch,         // Width in inches
         length_inch,        // Length in inches
         width_mm,           // Width in millimeters
         length_mm;          // Length in millimeters
  char str_format[2048];     // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  if ((page_size = ppdFindOption(ppd, "PageSize")) == NULL && warn != 2)
  {
    if (!warn && !errors && !verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (verbose >= 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      %s  Missing REQUIRED PageSize option.\n"
		"                REF: Page 99, section 5.14."),
	       prefix);
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (!warn)
      errors ++;
  }

  if ((page_region = ppdFindOption(ppd, "PageRegion")) == NULL && warn != 2)
  {
    if (!warn && !errors && !verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (verbose >= 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      %s  Missing REQUIRED PageRegion option.\n"
		"                REF: Page 100, section 5.14."),
	       prefix);
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (!warn)
      errors ++;
  }

  for (i = ppd->num_sizes, size = ppd->sizes; i > 0; i --, size ++)
  {
    //
    // Check that the size name is standard...
    //

    if (!strcmp(size->name, "Custom"))
    {
      //
      // Skip custom page size...
      //

      continue;
    }

    if (warn != 2 && size->name[0] == 'w' &&
	sscanf(size->name, "w%dh%d", &width, &length) == 2)
    {
      //
      // Validate device-specific size wNNNhNNN should have proper width and
      // length...
      //

      if (fabs(width - size->width) >= 1.0 ||
          fabs(length - size->length) >= 1.0)
      {
        if (!warn && !errors && !verbose)
        {
	  snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	  if (*report)
	    cupsArrayAdd(*report, (void *)str_format);
	  if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
	  snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Size \"%s\" has unexpected dimensions "
		    "(%gx%g)."),
		   prefix, size->name, size->width, size->length);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;
      }
    }

    //
    // Verify that the size is defined for both PageSize and PageRegion...
    //

    if (warn != 2 && !ppdFindChoice(page_size, size->name))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Size \"%s\" defined for %s but not for "
		  "%s."),
		 prefix, size->name, "PageRegion", "PageSize");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }
    else if (warn != 2 && !ppdFindChoice(page_region, size->name))
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  Size \"%s\" defined for %s but not for "
		  "%s."),
		 prefix, size->name, "PageSize", "PageRegion");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;
    }

    //
    // Verify that the size name is Adobe standard name if it's a standard size
    // and the dimensional name if it's not a standard size.  Suffix should be
    // .Fullbleed, etc., or numeric, e.g., Letter, Letter.Fullbleed,
    // Letter.Transverse, Letter1, Letter2, 4x8, 55x91mm, 55x91mm.Fullbleed,
    // etc.
    //

    if (warn != 0)
    {
      is_ok = 1;
      width_2540ths = (size->length > size->width) ?
	               PWG_FROM_POINTS(size->width) :
	               PWG_FROM_POINTS(size->length);
      length_2540ths = (size->length > size->width) ?
	                PWG_FROM_POINTS(size->length) :
	                PWG_FROM_POINTS(size->width);
      pwg_media = pwgMediaForSize(width_2540ths, length_2540ths);

      if (pwg_media &&
          (abs(pwg_media->width - width_2540ths) > 34 ||
           abs(pwg_media->length - length_2540ths) > 34))
        pwg_media = NULL; // Only flag matches within a point

      if (pwg_media && pwg_media->ppd &&
          (pwg_media->ppd[0] < 'a' || pwg_media->ppd[0] > 'z'))
      {
        size_t ppdlen = strlen(pwg_media->ppd); // Length of standard PPD name

        strlcpy(buf, pwg_media->ppd, sizeof(buf));

        if (strcmp(size->name, buf) && size->width > size->length)
        {
          if (!strcmp(pwg_media->ppd, "DoublePostcardRotated"))
            strlcpy(buf, "DoublePostcard", sizeof(buf));
          else if (strstr(size->name, ".Transverse"))
            snprintf(buf, sizeof(buf), "%s.Transverse", pwg_media->ppd);
          else
            snprintf(buf, sizeof(buf), "%sRotated", pwg_media->ppd);

          ppdlen = strlen(buf);
        }

        if (size->left == 0 && size->bottom == 0 &&
            size->right == size->width && size->top == size->length)
        {
          strlcat(buf, ".Fullbleed", sizeof(buf) - strlen(buf));
          if (_ppd_strcasecmp(size->name, buf))
          {
            //
            // Allow an additional qualifier such as ".WithTab"...
	    //

            size_t buflen = strlen(buf); // Length of full bleed name

            if (_ppd_strncasecmp(size->name, buf, buflen) ||
                size->name[buflen] != '.')
              is_ok = 0;
          }
        }
        else if (!strncmp(size->name, pwg_media->ppd, ppdlen))
        {
	  //
	  // Check for a proper qualifier (number, "Small", or .something)...
          //

          ptr = size->name + ppdlen;

          if (isdigit(*ptr & 255))
          {
            for (ptr ++; *ptr; ptr ++)
            {
              if (!isdigit(*ptr & 255))
              {
                is_ok = 0;
                break;
              }
            }
          }
          else if (*ptr != '.' && *ptr && strcmp(ptr, "Small"))
            is_ok = 0;
        }
        else
        {
	  //
          // Check for EnvSizeName as well...
	  //

          if (strncmp(pwg_media->ppd, "Env", 3) &&
	      !strncmp(size->name, "Env", 3))
            snprintf(buf, sizeof(buf), "Env%s", pwg_media->ppd);

          if (strcmp(size->name, buf))
            is_ok = 0;
        }

        if (!is_ok)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Size \"%s\" should be the Adobe "
		    "standard name \"%s\"."),
		   prefix, size->name, buf);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }
      }
      else
      {
        width_tmp = (fabs(size->width - ceil(size->width)) < 0.1) ?
	  ceil(size->width) : size->width;
        length_tmp = (fabs(size->length - ceil(size->length)) < 0.1) ?
	  ceil(size->length) : size->length;

        if (fmod(width_tmp, 9.0) == 0.0 && fmod(length_tmp, 9.0) == 0.0)
        {
          width_inch = width_tmp / 72.0;
          length_inch = length_tmp / 72.0;

          snprintf(buf, sizeof(buf), "%gx%g", width_inch, length_inch);
        }
        else
        {
          width_mm = size->width / 72 * 25.4;
          length_mm = size->length / 72 * 25.4;

          snprintf(buf, sizeof(buf), "%.0fx%.0fmm", width_mm, length_mm);
        }

        if (size->left == 0 && size->bottom == 0 &&
            size->right == size->width && size->top == size->length)
          strlcat(buf, ".Fullbleed", sizeof(buf));
        else if (size->width > size->length)
          strlcat(buf, ".Transverse", sizeof(buf));

        if (_ppd_strcasecmp(size->name, buf))
        {
          size_t buflen = strlen(buf); // Length of proposed name

          if (_ppd_strncasecmp(size->name, buf, buflen) ||
	      (strcmp(size->name + buflen, "in") &&
	       size->name[buflen] != '.'))
          {
            char altbuf[PPD_MAX_NAME];
            // Alternate "wNNNhNNN" name
            size_t altlen; // Length of alternate name

            snprintf(altbuf, sizeof(altbuf), "w%.0fh%.0f", size->width,
		     size->length);
            altlen = strlen(altbuf);
            if (_ppd_strncasecmp(size->name, altbuf, altlen) ||
                (size->name[altlen] && size->name[altlen] != '.'))
            {
              snprintf(str_format, sizeof(str_format) - 1,
		       ("      %s  Size \"%s\" should be \"%s\"."),
		       prefix, size->name, buf);
              if (*report)
                cupsArrayAdd(*report, (void *)str_format);
              if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
            }
          }
        }
      }
    }
  }

  return (errors);
}


//
// 'check_translations()' - Check translations in the PPD file.
//

static int                                // O - Errors found
check_translations(ppd_file_t *ppd,       // I - PPD file
                   int errors,            // I - Errors found
                   int verbose,           // I - Verbosity level
                   int warn,              // I - Warnings only?
                   cups_array_t **report, // I - Report text
		   cf_logfunc_t log,      // I - Log function
		   void *ld)              // I - Log function data
{
  int j;                       // Looping var
  ppd_attr_t *attr;            // PPD attribute
  cups_array_t *languages;     // Array of languages
  int langlen;                 // Length of language
  char *language,              // Current language
      keyword[PPD_MAX_NAME],   // Localization keyword (full)
      llkeyword[PPD_MAX_NAME], // Localization keyword (base)
      ckeyword[PPD_MAX_NAME],  // Custom option keyword (full)
      cllkeyword[PPD_MAX_NAME];
  // Custom option keyword (base)
  ppd_option_t *option;        // Standard UI option
  ppd_coption_t *coption;      // Custom option
  ppd_cparam_t *cparam;        // Custom parameter
  char ll[3];                  // Base language
  const char *prefix;          // WARN/FAIL prefix
  const char *text;            // Pointer into UI text
  char str_format[2048];       // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  if ((languages = ppdGetLanguages(ppd)) != NULL)
  {
    //
    // This file contains localizations, check them...
    //

    for (language = (char *)cupsArrayFirst(languages);
         language;
         language = (char *)cupsArrayNext(languages))
    {
      langlen = (int)strlen(language);
      if (langlen != 2 && langlen != 5)
      {
        if (!warn && !errors && !verbose)
        {
          snprintf(str_format, sizeof(str_format) - 1, " FAIL");
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (verbose >= 0)
        {
          snprintf(str_format, sizeof(str_format) - 1,
		   ("      %s  Bad language \"%s\"."),
		   prefix, language);
          if (*report)
            cupsArrayAdd(*report, (void *)str_format);
          if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
        }

        if (!warn)
          errors ++;

	continue;
      }

      if (!strcmp(language, "en"))
	continue;

      strlcpy(ll, language, sizeof(ll));

      //
      // Loop through all options and choices...
      //

      for (option = ppdFirstOption(ppd);
	   option;
	   option = ppdNextOption(ppd))
      {
	if (!strcmp(option->keyword, "PageRegion"))
	  continue;

	snprintf(keyword, sizeof(keyword), "%s.Translation", language);
	snprintf(llkeyword, sizeof(llkeyword), "%s.Translation", ll);

	if ((attr = ppdFindAttr(ppd, keyword, option->keyword)) == NULL &&
            (attr = ppdFindAttr(ppd, llkeyword, option->keyword)) == NULL)
	{
	  if (!warn && !errors && !verbose)
	  {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
          {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      %s  Missing \"%s\" translation "
		      "string for option %s."),
		     prefix, language, option->keyword);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (!warn)
	    errors ++;
	}
	else if (!valid_utf8(attr->text))
	{
	  if (!warn && !errors && !verbose)
          {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
          {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      %s  Bad UTF-8 \"%s\" translation "
		      "string for option %s."),
		     prefix, language, option->keyword);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (!warn)
	    errors ++;
	}

	snprintf(keyword, sizeof(keyword), "%s.%.37s", language,
		 option->keyword);
	snprintf(llkeyword, sizeof(llkeyword), "%s.%.37s", ll,
		 option->keyword);

	for (j = 0; j < option->num_choices; j++)
	{
	  //
	  // First see if this choice is a number; if so, don't require
	  // translation...
	  //

	  for (text = option->choices[j].text; *text; text++)
	    if (!strchr("0123456789-+.", *text))
	      break;

	  if (!*text)
	    continue;

	  //
	  // Check custom choices differently...
	  //

	  if (!_ppd_strcasecmp(option->choices[j].choice, "Custom") &&
              (coption = ppdFindCustomOption(ppd,
					     option->keyword)) != NULL)
	  {
	    snprintf(ckeyword, sizeof(ckeyword), "%s.Custom%.33s",
		     language, option->keyword);

	    if ((attr = ppdFindAttr(ppd, ckeyword, "True")) != NULL &&
		!valid_utf8(attr->text))
            {
	      if (!warn && !errors && !verbose)
	      {
		snprintf(str_format, sizeof(str_format) - 1, " FAIL");
		if (*report)
                cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	      }

	      if (verbose >= 0)
	      {
		snprintf(str_format, sizeof(str_format) - 1,
			 ("      %s  Bad UTF-8 \"%s\" "
			  "translation string for option %s, "
			  "choice %s."),
			 prefix, language,
			 ckeyword + 1 + strlen(language),
			 "True");
		if (*report)
		  cupsArrayAdd(*report, (void *)str_format);
		if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	      }

	      if (!warn)
		errors ++;
	    }

	    if (_ppd_strcasecmp(option->keyword, "PageSize"))
            {
	      for (cparam = (ppd_cparam_t *)cupsArrayFirst(coption->params);
		   cparam;
		   cparam = (ppd_cparam_t *)cupsArrayNext(coption->params))
	      {
		snprintf(ckeyword, sizeof(ckeyword), "%s.ParamCustom%.28s",
                         language, option->keyword);
		snprintf(cllkeyword, sizeof(cllkeyword), "%s.ParamCustom%.26s",
			 ll, option->keyword);

		if ((attr = ppdFindAttr(ppd, ckeyword,
					cparam->name)) == NULL &&
		    (attr = ppdFindAttr(ppd, cllkeyword,
					cparam->name)) == NULL)
		{
		  if (!warn && !errors && !verbose)
		  {
		    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
		    if (*report)
		      cupsArrayAdd(*report, (void *)str_format);
		    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s",
				 str_format);
		  }

		  if (verbose >= 0)
		  {
		    snprintf(str_format, sizeof(str_format) - 1,
			     ("      %s  Missing \"%s\" "
			      "translation string for option %s, "
			      "choice %s."),
			     prefix, language,
			     ckeyword + 1 + strlen(language),
			     cparam->name);
		    if (*report)
		      cupsArrayAdd(*report, (void *)str_format);
		    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s",
				 str_format);
		  }
		  if (!warn)
		    errors ++;
		}
		else if (!valid_utf8(attr->text))
		{
		  if (!warn && !errors && !verbose)
		  {
		    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
		    if (*report)
		      cupsArrayAdd(*report, (void *)str_format);
		    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s",
				 str_format);
		  }

		  if (verbose >= 0)
                  {
		    snprintf(str_format, sizeof(str_format) - 1,
			     ("      %s  Bad UTF-8 \"%s\" "
			      "translation string for option %s, "
			      "choice %s."),
			     prefix, language,
			     ckeyword + 1 + strlen(language),
			     cparam->name);
		    if (*report)
		      cupsArrayAdd(*report, (void *)str_format);
		    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s",
				 str_format);
		  }

		  if (!warn)
		    errors ++;
		}
	      }
	    }
	  }
	  else if ((attr = ppdFindAttr(ppd, keyword,
				       option->choices[j].choice)) == NULL &&
		   (attr = ppdFindAttr(ppd, llkeyword,
				       option->choices[j].choice)) == NULL)
	  {
	    if (!warn && !errors && !verbose)
	    {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    if (verbose >= 0)
	    {
	      snprintf(str_format, sizeof(str_format) - 1,
		       ("      %s  Missing \"%s\" "
			"translation string for option %s, "
                        "choice %s."),
		       prefix, language, option->keyword,
		       option->choices[j].choice);
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    if (!warn)
            errors ++;
	  }
	  else if (!valid_utf8(attr->text))
	  {
	    if (!warn && !errors && !verbose)
	    {
	      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    if (verbose >= 0)
	    {
	      snprintf(str_format, sizeof(str_format) - 1,
		       ("      %s  Bad UTF-8 \"%s\" "
			"translation string for option %s, "
			"choice %s."),
		       prefix, language, option->keyword,
		       option->choices[j].choice);
	      if (*report)
		cupsArrayAdd(*report, (void *)str_format);
	      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	    }

	    if (!warn)
	      errors ++;
	  }
	}
      }
    }

    //
    // Verify that we have the base language for each localized one...
    //

    for (language = (char *)cupsArrayFirst(languages);
	 language;
	 language = (char *)cupsArrayNext(languages))
      if (language[2])
      {
	//
	// Lookup the base language...
	//

	cupsArraySave(languages);

	strlcpy(ll, language, sizeof(ll));

	if (!cupsArrayFind(languages, ll) &&
            strcmp(ll, "zh") && strcmp(ll, "en"))
	{
	  if (!warn && !errors && !verbose)
	  {
	    snprintf(str_format, sizeof(str_format) - 1, " FAIL");
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (verbose >= 0)
	  {
	    snprintf(str_format, sizeof(str_format) - 1,
		     ("      %s  No base translation \"%s\" "
		      "is included in file."),
		     prefix, ll);
	    if (*report)
	      cupsArrayAdd(*report, (void *)str_format);
	    if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
	  }

	  if (!warn)
	    errors ++;
	}

	cupsArrayRestore(languages);
      }

    //
    // Free memory used for the languages...
    //

    ppdFreeLanguages(languages);
  }

  return (errors);
}

//
// 'show_conflicts()' - Show option conflicts in a PPD file.
//

static void
show_conflicts(ppd_file_t *ppd,       // I - PPD to check
               const char *prefix,    // I - Prefix string
               cups_array_t **report, // I - Report text
	       cf_logfunc_t log,      // I - Log function
	       void *ld)              // I - Log function data
{
  int i, j;              // Looping variables
  ppd_const_t *c;        // Current constraint
  ppd_option_t *o1, *o2; // Options
  ppd_choice_t *c1, *c2; // Choices
  char str_format[2048]; // Formatted string

  //
  // Loop through all of the UI constraints and report any options
  // that conflict...
  //

  for (i = ppd->num_consts, c = ppd->consts; i > 0; i --, c ++)
  {
    //
    // Grab pointers to the first option...
    //

    o1 = ppdFindOption(ppd, c->option1);

    if (o1 == NULL)
      continue;
    else if (c->choice1[0] != '\0')
    {
      //
      // This constraint maps to a specific choice.
      //

      c1 = ppdFindChoice(o1, c->choice1);
    }
    else
    {
      //
      // This constraint applies to any choice for this option.
      //

      for (j = o1->num_choices, c1 = o1->choices; j > 0; j --, c1 ++)
        if (c1->marked)
	  break;

      if (j == 0 ||
          !_ppd_strcasecmp(c1->choice, "None") ||
          !_ppd_strcasecmp(c1->choice, "Off") ||
          !_ppd_strcasecmp(c1->choice, "False"))
        c1 = NULL;
    }

    //
    // Grab pointers to the second option...
    //

    o2 = ppdFindOption(ppd, c->option2);

    if (o2 == NULL)
      continue;
    else if (c->choice2[0] != '\0')
    {
      //
      // This constraint maps to a specific choice.
      //

      c2 = ppdFindChoice(o2, c->choice2);
    }
    else
    {
      //
      // This constraint applies to any choice for this option.
      //

      for (j = o2->num_choices, c2 = o2->choices; j > 0; j --, c2 ++)
        if (c2->marked)
          break;

      if (j == 0 ||
          !_ppd_strcasecmp(c2->choice, "None") ||
          !_ppd_strcasecmp(c2->choice, "Off") ||
          !_ppd_strcasecmp(c2->choice, "False"))
        c2 = NULL;
    }

    //
    // If both options are marked then there is a conflict...
    //

    if (c1 != NULL && c1->marked && c2 != NULL && c2->marked)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      %s  \"%s %s\" conflicts with \"%s %s\"\n"
		"                (constraint=\"%s %s %s %s\")."),
	       prefix, o1->keyword, c1->choice, o2->keyword, c2->choice,
	       c->option1, c->choice1, c->option2, c->choice2);
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }
  }
}


//
// 'test_raster()' - Test PostScript commands for raster printers.
//

static int                         // O - 1 on success, 0 on failure
test_raster(ppd_file_t *ppd,       // I - PPD file
            int verbose,           // I - Verbosity
            cups_array_t **report, // I - Report text
	    cf_logfunc_t log,      // I - Log function
	    void *ld)              // I - Log function data
{
  char str_format[2048];      // Formatted string
  cups_page_header2_t header; // Page header


  ppdMarkDefaults(ppd);
  if (ppdRasterInterpretPPD(&header, ppd, 0, NULL, 0))
  {
    if (!verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (verbose >= 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      **FAIL**  Default option code cannot be "
		"interpreted: %s"),
	       _ppdRasterErrorString());
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    return (0);
  }

  //
  // Try a test of custom page size code, if available...
  //

  if (!ppdPageSize(ppd, "Custom.612x792"))
    return (1);

  ppdMarkOption(ppd, "PageSize", "Custom.612x792");

  if (ppdRasterInterpretPPD(&header, ppd, 0, NULL, 0))
  {
    if (!verbose)
    {
      snprintf(str_format, sizeof(str_format) - 1, " FAIL");
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    if (verbose >= 0)
    {
      snprintf(str_format, sizeof(str_format) - 1,
	       ("      **FAIL**  Default option code cannot be "
		"interpreted: %s"),
	       _ppdRasterErrorString());
      if (*report)
        cupsArrayAdd(*report, (void *)str_format);
      if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
    }

    return (0);
  }

  return (1);
}


//
// 'valid_path()' - Check whether a path has the correct capitalization.
//

static int                        // O - Errors found
valid_path(const char *keyword,   // I - Keyword using path
           const char *path,      // I - Path to check
           int errors,            // I - Errors found
           int verbose,           // I - Verbosity level
           int warn,              // I - Warnings only?
           cups_array_t **report, // I - Report text
	   cf_logfunc_t log,      // I - Log function
	   void *ld)              // I - Log function data
{
  cups_dir_t *dir;       // Current directory
  cups_dentry_t *dentry; // Current directory entry
  char temp[1024],       // Temporary path
      *ptr;              // Pointer into temporary path
  const char *prefix;    // WARN/FAIL prefix
  char str_format[2048]; // Formatted string


  prefix = warn ? "  WARN  " : "**FAIL**";

  //
  // Loop over the components of the path, checking that the entry exists with
  // the same capitalization...
  //

  strlcpy(temp, path, sizeof(temp));

  while ((ptr = strrchr(temp, '/')) != NULL)
  {
    //
    // Chop off the trailing component so temp == dirname and ptr == basename.
    //

    *ptr++ = '\0';

    //
    // Try opening the directory containing the base name...
    //

    if (temp[0])
      dir = cupsDirOpen(temp);
    else
      dir = cupsDirOpen("/");

    if (!dir)
      dentry = NULL;
    else
    {
      while ((dentry = cupsDirRead(dir)) != NULL)
      {
        if (!strcmp(dentry->filename, ptr))
          break;
      }

      cupsDirClose(dir);
    }

    //
    // Display an error if the filename doesn't exist with the same
    // capitalization...
    //

    if (!dentry)
    {
      if (!warn && !errors && !verbose)
      {
        snprintf(str_format, sizeof(str_format) - 1, " FAIL");
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (verbose >= 0)
      {
        snprintf(str_format, sizeof(str_format) - 1,
		 ("      %s  %s file \"%s\" has the wrong "
		  "capitalization."),
		 prefix, keyword, path);
        if (*report)
          cupsArrayAdd(*report, (void *)str_format);
        if (log) log(ld, CF_LOGLEVEL_DEBUG, "ppdTest: %s", str_format);
      }

      if (!warn)
        errors ++;

      break;
    }
  }

  return (errors);
}

//
// 'valid_utf8()' - Check whether a string contains valid UTF-8 text.
//

static int                // O - 1 if valid, 0 if not
valid_utf8(const char *s) // I - String to check
{
  while (*s)
  {
    if (*s & 0x80)
    {
      //
      // Check for valid UTF-8 sequence...
      //

      if ((*s & 0xc0) == 0x80)
        return (0); // Illegal suffix byte
      else if ((*s & 0xe0) == 0xc0)
      {
        //
	// 2-byte sequence...
	//

        s++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte
      }
      else if ((*s & 0xf0) == 0xe0)
      {
        //
        // 3-byte sequence...
	//

        s ++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte

        s ++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte
      }
      else if ((*s & 0xf8) == 0xf0)
      {
        //
	// 4-byte sequence...
	//

        s ++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte

        s ++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte

        s ++;

        if ((*s & 0xc0) != 0x80)
          return (0); // Missing suffix byte
      }
      else
        return (0); // Bad sequence
    }

    s ++;
  }

  return (1);
}
