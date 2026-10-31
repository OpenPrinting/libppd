//
// Thread-safety unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Printer applications call libppd from several threads at once (one per
// job or printer), so state the library keeps between calls must be kept
// per thread or locked.  Each group below checks one piece of that state.
//
// Tests covered:
//
//   Group 1  ppdOpen() status (ppdLastError()): a ppdOpen() of a valid PPD
//            in another thread leaves the error of a failed ppdOpen() in
//            the main thread untouched.
//
//   Group 2  String pool (_ppdStrAlloc(), _ppdStrRetain(), _ppdStrFree()):
//            several threads allocate, retain and free a shared string and
//            strings of their own in a loop; every string must read back
//            right and the pool must hold as many references afterwards as
//            before.
//
//   Group 3  Raster error buffer (_ppdRasterAddError(), _ppdRasterClearError()
//            and _ppdRasterErrorString(), used by ppdRasterInterpretPPD() and
//            ppdRasterExecPS()):
//            - ppdRasterExecPS() as the first raster call of the process
//              reports an unknown operator (-1 and its message) although no
//              ppdRasterInterpretPPD() has cleared the buffer before, and
//              ppdRasterInterpretPPD(NULL header) sets its error message;
//            - a new thread starts with an empty buffer, and its errors
//              leave the error message of the main thread untouched;
//            - several threads run ppdRasterInterpretPPD() (which clears the
//              buffer) and ppdRasterExecPS() (which adds an error for an
//              unknown operator and one per object left on the stack) in a
//              loop, each with its own code, and must read back exactly the
//              message the same code gives when run alone.
//
// The per-thread checks of groups 1 and 3 fail deterministically when the
// state is shared by all threads.  The concurrent checks may crash or pass
// by luck then; build with -fsanitize=thread to see every data race they run
// into.
//

#include <ppd/ppd.h>
#include <ppd/raster-private.h>
#include <ppd/string-private.h>
#include "test-internal.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//
// Constants...
//

#define NUM_THREADS	8		// Threads in the concurrent tests
#define NUM_LOOPS	2000		// Iterations per thread


//
// Local types...
//

typedef struct thread_data_s		// Per-thread test data
{
  int		num;			// Thread number
  char		code[4096];		// PostScript code to run
  char		*expected;		// Error message the code gives alone
  int		loops,			// Iterations done
		mismatches;		// Iterations with a wrong result
} thread_data_t;


//
// Minimal valid PPD for group 1.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"THREAD.PPD\"\n"
  "*Product: \"(Test)\"\n"
  "*ModelName: \"Thread Test Printer\"\n"
  "*ShortNickName: \"ThreadTest\"\n"
  "*NickName: \"Thread Test Printer\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n";


//
// Local functions...
//

static int		join_threads(pthread_t *threads, int started,
			             thread_data_t *data, int *loops);
static ppd_file_t	*open_ppd(const char *text);
static void		*run_open_ppd(void *arg);
static void		*run_raster_concurrent(void *arg);
static void		*run_raster_new_thread(void *arg);
static void		*run_string_pool(void *arg);
static void		set_code(thread_data_t *data, int num);
static int		set_expected(thread_data_t *data);
static int		start_threads(pthread_t *threads, thread_data_t *data,
			              void *(*func)(void *));
static void		test_ppd_status(void);
static void		test_raster_error(void);
static void		test_string_pool(void);


//
// 'main()' - Run the thread-safety tests.
//

int					// O - Exit status
main(void)
{
  test_ppd_status();
  test_string_pool();
  test_raster_error();

  return (testsPassed ? 0 : 1);
}


//
// 'join_threads()' - Wait for the threads and add up their results.
//

static int				// O - Mismatches seen
join_threads(pthread_t     *threads,	// I - Threads
             int           started,	// I - Threads started
             thread_data_t *data,	// I - Per-thread test data
             int           *loops)	// O - Iterations done
{
  int	i,				// Looping var
	mismatches;			// Mismatches seen


  for (i = 0, *loops = 0, mismatches = 0; i < started; i ++)
  {
    pthread_join(threads[i], NULL);
    *loops     += data[i].loops;
    mismatches += data[i].mismatches;
  }

  return (mismatches);
}


//
// 'open_ppd()' - Open a PPD from a string.
//

static ppd_file_t *			// O - PPD file or NULL
open_ppd(const char *text)		// I - PPD text
{
  FILE		*fp;			// Temporary file
  ppd_file_t	*ppd;			// PPD file


  if ((fp = tmpfile()) == NULL)
    return (NULL);

  fputs(text, fp);
  rewind(fp);

  ppd = ppdOpen(fp);

  fclose(fp);

  return (ppd);
}


//
// 'run_open_ppd()' - Open a valid PPD and check its status.
//

static void *				// O - Thread exit value
run_open_ppd(void *arg)			// I - Thread data
{
  thread_data_t	*data = (thread_data_t *)arg;
					// Thread data
  ppd_file_t	*ppd;			// PPD file
  int		line;			// Line number


  data->loops ++;

  if ((ppd = open_ppd(test_ppd_text)) == NULL ||
      ppdLastError(&line) != PPD_OK)
    data->mismatches ++;

  ppdClose(ppd);

  return (NULL);
}


//
// 'run_raster_concurrent()' - Clear and add raster errors in a loop.
//

static void *				// O - Thread exit value
run_raster_concurrent(void *arg)	// I - Thread data
{
  thread_data_t		*data = (thread_data_t *)arg;
					// Thread data
  cups_page_header_t	header;		// Page header
  int			preferred_bits;	// Preferred bits per color
  const char		*message;	// Error message


  for (data->loops = 0; data->loops < NUM_LOOPS; data->loops ++)
  {
    if (ppdRasterInterpretPPD(&header, NULL, 0, NULL, NULL) ||
        _ppdRasterErrorString() != NULL)
    {
      data->mismatches ++;
      continue;
    }

    preferred_bits = 0;

    if (ppdRasterExecPS(&header, &preferred_bits, data->code) != -1 ||
        (message = _ppdRasterErrorString()) == NULL || !data->expected ||
        strcmp(message, data->expected))
      data->mismatches ++;
  }

  return (NULL);
}


//
// 'run_raster_new_thread()' - Add a raster error as a thread's first call.
//

static void *				// O - Thread exit value
run_raster_new_thread(void *arg)	// I - Thread data
{
  thread_data_t		*data = (thread_data_t *)arg;
					// Thread data
  cups_page_header_t	header;		// Page header
  int			preferred_bits;	// Preferred bits per color
  const char		*message;	// Error message
  char			expected[64];	// Start of the expected message


  snprintf(expected, sizeof(expected), "Unknown operator \"bogus%d\"",
           data->num);

  // The buffer of a new thread is empty...
  data->loops ++;
  if (_ppdRasterErrorString() != NULL)
    data->mismatches ++;

  // ...and holds only this thread's error after ppdRasterExecPS()
  memset(&header, 0, sizeof(header));
  preferred_bits = 0;

  data->loops ++;
  if (ppdRasterExecPS(&header, &preferred_bits, data->code) != -1 ||
      (message = _ppdRasterErrorString()) == NULL ||
      strncmp(message, expected, strlen(expected)))
    data->mismatches ++;

  // ppdRasterInterpretPPD() clears this thread's buffer
  data->loops ++;
  if (ppdRasterInterpretPPD(&header, NULL, 0, NULL, NULL) ||
      _ppdRasterErrorString() != NULL)
    data->mismatches ++;

  return (NULL);
}


//
// 'run_string_pool()' - Allocate, retain and free pooled strings in a loop.
//

static void *				// O - Thread exit value
run_string_pool(void *arg)		// I - Thread data
{
  thread_data_t	*data = (thread_data_t *)arg;
					// Thread data
  char		own[64];		// String of this thread
  char		*shared,		// Pooled shared string
		*mine,			// Pooled string of this thread
		*retained;		// Retained reference to it


  for (data->loops = 0; data->loops < NUM_LOOPS; data->loops ++)
  {
    snprintf(own, sizeof(own), "libppd thread %d string %d", data->num,
             data->loops % 16);

    shared   = _ppdStrAlloc("libppd thread test shared string");
    mine     = _ppdStrAlloc(own);
    retained = _ppdStrRetain(mine);

    if (!shared || strcmp(shared, "libppd thread test shared string") ||
        !mine || strcmp(mine, own) || retained != mine)
      data->mismatches ++;

    _ppdStrFree(retained);
    _ppdStrFree(mine);
    _ppdStrFree(shared);
  }

  return (NULL);
}


//
// 'set_code()' - Set a thread's PostScript code.
//
// The code leaves a name, a number and 40 strings on the stack before an
// unknown operator, so each run adds over 40 errors (about 2k of text) and
// grows the buffer.
//

static void
set_code(thread_data_t *data,		// I - Thread data
         int           num)		// I - Thread number
{
  char	filler[49],			// String value
	*ptr;				// Pointer into code
  int	i;				// Looping var


  memset(filler, 'a' + num, sizeof(filler) - 1);
  filler[sizeof(filler) - 1] = '\0';

  data->num        = num;
  data->loops      = 0;
  data->mismatches = 0;

  snprintf(data->code, sizeof(data->code), "/Thread%d %d", num, num);
  for (i = 0, ptr = data->code + strlen(data->code); i < 40; i ++)
  {
    snprintf(ptr, sizeof(data->code) - (size_t)(ptr - data->code), " (%s)",
             filler);
    ptr += strlen(ptr);
  }
  snprintf(ptr, sizeof(data->code) - (size_t)(ptr - data->code), " bogus%d",
           num);
}


//
// 'set_expected()' - Run a thread's code alone and keep its error message.
//

static int				// O - 1 on success, 0 on failure
set_expected(thread_data_t *data)	// I - Thread data
{
  cups_page_header_t	header;		// Page header
  int			preferred_bits;	// Preferred bits per color
  const char		*message;	// Error message
  char			expected[64];	// Start of the expected message


  snprintf(expected, sizeof(expected), "Unknown operator \"bogus%d\"",
           data->num);

  ppdRasterInterpretPPD(&header, NULL, 0, NULL, NULL);
  preferred_bits = 0;

  if (ppdRasterExecPS(&header, &preferred_bits, data->code) != -1 ||
      (message = _ppdRasterErrorString()) == NULL ||
      strncmp(message, expected, strlen(expected)))
    return (0);

  free(data->expected);
  data->expected = strdup(message);

  return (data->expected != NULL);
}


//
// 'start_threads()' - Start one thread per test data record.
//

static int				// O - Threads started
start_threads(pthread_t     *threads,	// I - Threads
              thread_data_t *data,	// I - Per-thread test data
              void          *(*func)(void *))
					// I - Thread function
{
  int	started;			// Threads started


  for (started = 0; started < NUM_THREADS; started ++)
    if (pthread_create(threads + started, NULL, func, data + started))
      break;

  return (started);
}


//
// 'test_ppd_status()' - Test the status of ppdOpen() (group 1).
//

static void
test_ppd_status(void)
{
  pthread_t	thread;			// Thread
  thread_data_t	data;			// Thread data
  ppd_file_t	*ppd;			// PPD file
  ppd_status_t	status,			// Status of failed ppdOpen()
		current;		// Status now
  int		line,			// Line of failed ppdOpen()
		current_line;		// Line now


  testBegin("ppdOpen(not a PPD file) sets ppdLastError()");
  ppd    = open_ppd("This is not a PPD file.\n");
  status = ppdLastError(&line);
  testEndMessage(!ppd && status != PPD_OK, "status=%d (%s), line=%d",
                 status, ppdErrorString(status), line);
  ppdClose(ppd);

  memset(&data, 0, sizeof(data));

  testBegin("ppdOpen(valid PPD) in a new thread");
  if (pthread_create(&thread, NULL, run_open_ppd, &data))
  {
    testEndMessage(false, "unable to create thread");
  }
  else
  {
    pthread_join(thread, NULL);
    testEnd(data.mismatches == 0);
  }

  testBegin("ppdLastError() unchanged by the other thread");
  current = ppdLastError(&current_line);
  testEndMessage(current == status && current_line == line,
                 "status=%d (%s), line=%d", current, ppdErrorString(current),
                 current_line);
}


//
// 'test_raster_error()' - Test the raster error buffer (group 3).
//

static void
test_raster_error(void)
{
  cups_page_header_t	header;		// Page header
  int			preferred_bits;	// Preferred bits per color
  int			status;		// Function status
  const char		*message;	// Error message
  char			*saved;		// Copy of an error message
  pthread_t		threads[NUM_THREADS];
					// Threads
  thread_data_t		data[NUM_THREADS];
					// Per-thread test data
  int			i,		// Looping var
			started,	// Threads started
			loops,		// Iterations done
			mismatches;	// Wrong messages seen


  //
  // ppdRasterExecPS() before any ppdRasterInterpretPPD() (the groups before
  // make no raster calls)...
  //

  testBegin("ppdRasterExecPS(unknown operator) as the first call");
  memset(&header, 0, sizeof(header));
  preferred_bits = 0;
  status  = ppdRasterExecPS(&header, &preferred_bits, "bogus");
  message = _ppdRasterErrorString();
  testEndMessage(status == -1 && message &&
                 !strncmp(message, "Unknown operator \"bogus\"", 24),
                 "status=%d, message=\"%s\"", status,
                 message ? message : "(null)");

  testBegin("ppdRasterInterpretPPD(NULL header) sets an error");
  status  = ppdRasterInterpretPPD(NULL, NULL, 0, NULL, NULL);
  message = _ppdRasterErrorString();
  testEndMessage(status == -1 && message &&
                 !strcmp(message, "Page header cannot be NULL!\n"),
                 "status=%d, message=\"%s\"", status,
                 message ? message : "(null)");

  saved = strdup(message ? message : "");

  //
  // Errors are kept per thread...
  //

  memset(data, 0, sizeof(data));
  set_code(data + 0, 0);

  testBegin("ppdRasterExecPS(unknown operator) in a new thread");
  if (pthread_create(threads + 0, NULL, run_raster_new_thread, data + 0))
  {
    testEndMessage(false, "unable to create thread");
  }
  else
  {
    pthread_join(threads[0], NULL);
    testEndMessage(data[0].mismatches == 0, "%d of %d checks failed",
                   data[0].mismatches, data[0].loops);
  }

  testBegin("_ppdRasterErrorString() unchanged by the other thread");
  message = _ppdRasterErrorString();
  testEndMessage(message && saved && !strcmp(message, saved),
                 "message=\"%s\"", message ? message : "(null)");

  free(saved);

  //
  // Concurrent clear/add in several threads...
  //

  testBegin("Raster error messages of each code run alone");
  for (i = 0, mismatches = 0; i < NUM_THREADS; i ++)
  {
    set_code(data + i, i);

    if (!set_expected(data + i))
      mismatches ++;
  }
  testEndMessage(mismatches == 0, "%d of %d codes without the expected error",
                 mismatches, NUM_THREADS);

  testBegin("ppdRasterInterpretPPD/ppdRasterExecPS in %d threads x %d loops",
            NUM_THREADS, NUM_LOOPS);
  started    = start_threads(threads, data, run_raster_concurrent);
  mismatches = join_threads(threads, started, data, &loops);
  testEndMessage(started == NUM_THREADS && mismatches == 0,
                 "%d threads, %d of %d messages wrong", started, mismatches,
                 loops);

  for (i = 0; i < NUM_THREADS; i ++)
    free(data[i].expected);
}


//
// 'test_string_pool()' - Test the string pool (group 2).
//

static void
test_string_pool(void)
{
  pthread_t	threads[NUM_THREADS];	// Threads
  thread_data_t	data[NUM_THREADS];	// Per-thread test data
  int		i,			// Looping var
		started,		// Threads started
		loops,			// Iterations done
		mismatches;		// Mismatches seen
  size_t	before,			// Strings in the pool before
		after;			// Strings in the pool after


  memset(data, 0, sizeof(data));
  for (i = 0; i < NUM_THREADS; i ++)
    data[i].num = i;

  before = _ppdStrStatistics(NULL, NULL);

  testBegin("_ppdStrAlloc/_ppdStrRetain/_ppdStrFree in %d threads x %d loops",
            NUM_THREADS, NUM_LOOPS);
  started    = start_threads(threads, data, run_string_pool);
  mismatches = join_threads(threads, started, data, &loops);
  after      = _ppdStrStatistics(NULL, NULL);
  testEndMessage(started == NUM_THREADS && mismatches == 0 && after == before,
                 "%d threads, %d of %d strings wrong, %u references in the "
                 "pool before and %u after", started, mismatches, loops,
                 (unsigned)before, (unsigned)after);
}
