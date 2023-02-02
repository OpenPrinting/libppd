#include <ppd/ppd.h>
#include <cups/array.h>
#include <ppd/array-private.h>

/*
 * 'main()' - Wrapper function for ppdTest().
 */

int main(int argc,	   /* I - Number of command-line args */
		 char *argv[]) /* I - Command-line arguments */
{

	int i, j, k, m, n; /* Looping vars */
	int verbose;	   /* Want verbose output? */
	int ignore_pc_filenames;  /* Whether to ignore filename */  
	int ignore_filters;  /* Whether to ignore filters */
	int ignore_profiles;  /* Whether to ignore profiles */
	int ignore_none;  /* Whether to ignore nothing */
	int ignore_all;  /* Whether to ignore everything */
	int root_present;  /* Whether root directory is specified */
	char *rootdir;  /* What is the root directory if mentioned */
	int warn_none;  /* Whether to warn about nothing */
	int warn_constraints;  /* Whether to warn about constraints */
	int warn_defaults;  /* Whether to warn about defaults */
	int warn_duplex;  /* Whether to warn about duplex */
	int warn_filters;  /* Whether to warn about filters */
	int warn_profiles;  /* Whether to warn about profiles */
	int warn_sizes;  /* Whether to warn about sizes */
	int warn_translations;  /* Whether to warn about translations */
	int warn_all;  /* Whether to warn about everything */
	int help;  /* Whether to run help dialog */
	char *opt; /* Option character */
	int q_with_v;  /* If q is used together with v in the command line */
	int v_with_q;  /* If v is used together with q in the command line */
	int relaxed;  /* If relaxed mode is to be used */
	cups_array_t *file_array;  /* Array consisting of filenames of the ppd files to be checked */
	cups_array_t *stdin_array;  /* Array consisting of when "-" is used in the command line without supplying further argument */
	int files;			   /* Number of files */

	verbose = 0;
	ignore_pc_filenames = 0;
	ignore_filters = 0;
	ignore_profiles = 0;
	ignore_none = 0;
	ignore_all = 0;
	root_present = 0;
	warn_none = 0;
	warn_constraints = 0;
	warn_defaults = 0;
	warn_duplex = 0;
	warn_filters = 0;
	warn_profiles = 0;
	warn_sizes = 0;
	warn_translations = 0;
	warn_all = 0;
	help = 0;
	relaxed = 0;
	q_with_v = 0;
	v_with_q = 0;
	files=0;

	for (i = 1; i < argc; i++)
		if (!strcmp(argv[i], "--help"))
			help = 1;
		else if (argv[i][0] == '-' && argv[i][1])
		{
			for (opt = argv[i] + 1; *opt; opt++)
				switch (*opt)
				{
				case 'I': /* Ignore errors */
					i++;

					if (i >= argc)
						help = 1;

					if (!strcmp(argv[i], "none"))
						ignore_none = 1;
					else if (!strcmp(argv[i], "filename"))
						ignore_pc_filenames = 1;
					else if (!strcmp(argv[i], "filters"))
						ignore_filters = 1;
					else if (!strcmp(argv[i], "profiles"))
						ignore_profiles = 1;
					else if (!strcmp(argv[i], "all"))
						ignore_all = 1;
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

				case 'W': /* Turn errors into warnings */
					i++;

					if (i >= argc)
						help = 1;

					if (!strcmp(argv[i], "none"))
						warn_none = 1;
					else if (!strcmp(argv[i], "constraints"))
						warn_constraints = 1;
					else if (!strcmp(argv[i], "defaults"))
						warn_defaults = 1;
					else if (!strcmp(argv[i], "duplex"))
						warn_duplex = 1;
					else if (!strcmp(argv[i], "filters"))
						warn_filters = 1;
					else if (!strcmp(argv[i], "profiles"))
						warn_profiles = 1;
					else if (!strcmp(argv[i], "sizes"))
						warn_sizes = 1;
					else if (!strcmp(argv[i], "translations"))
						warn_translations = 1;
					else if (!strcmp(argv[i], "all"))
						warn_all = 1;
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
				_ppdArrayAddStrings(stdin_array,argv[i]);
			}
			else
			{
				_ppdArrayAddStrings(file_array,argv[i]);
				_ppdArrayAddStrings(stdin_array,"");
			}
		}

	ppdTest(ignore_pc_filenames, ignore_filters,
			ignore_profiles, ignore_none, ignore_all, rootdir, warn_none, warn_constra, warn_defaults,
			warn_duplex, warn_filters, warn_profiles, warn_sizes, warn_translations,
			warn_all, help, verbose, relaxed, q_with_v, v_with_q, root_present,
			files, file_array, stdin_array);
} 
