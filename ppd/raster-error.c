//
// Raster error handling for libppd.
//
// Copyright © 2007-2018 by Apple Inc.
// Copyright © 2007 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

//
// Include necessary headers...
//

#include "raster-private.h"
#include "thread-private.h"
#include "debug-internal.h"

typedef struct _ppd_raster_error_s	// **** Error buffer structure ****
{
  char	*start,				// Start of buffer
	*current,			// Current position in buffer
	*end;				// End of buffer
} _ppd_raster_error_t;


//
// Local globals...
//

#ifdef HAVE_PTHREAD_H
static _ppd_threadkey_t	ppd_raster_error_key = _PPD_THREADKEY_INITIALIZER;
					// Thread local storage key
static pthread_once_t	ppd_raster_error_key_once = PTHREAD_ONCE_INIT;
					// One-time initialization object
#else
static _ppd_raster_error_t ppd_raster_error_buf = { NULL, NULL, NULL };
					// Error buffer
#endif // HAVE_PTHREAD_H


//
// Local functions...
//

static _ppd_raster_error_t	*ppd_raster_error_buffer(void);
#ifdef HAVE_PTHREAD_H
static void			ppd_raster_error_free(void *data);
static void			ppd_raster_error_init(void);
#endif // HAVE_PTHREAD_H


//
// '_ppdRasterAddError()' - Add an error message to the error buffer.
//

void
_ppdRasterAddError(const char *f,	// I - Printf-style error message
                    ...)		// I - Additional arguments as needed
{
  _ppd_raster_error_t	*buf = ppd_raster_error_buffer();
					// Error buffer for this thread
  va_list	ap;			// Pointer to additional arguments
  char		s[2048];		// Message string
  ssize_t	bytes;			// Bytes in message string


  DEBUG_printf(("_ppdRasterAddError(f=\"%s\", ...)", f));

  if (!buf)
    return;

  va_start(ap, f);
  bytes = vsnprintf(s, sizeof(s), f, ap);
  va_end(ap);

  if (bytes <= 0)
    return;

  DEBUG_printf(("1_ppdRasterAddError: %s", s));

  bytes ++;

  if ((size_t)bytes >= sizeof(s))
    return;

  if (bytes > (ssize_t)(buf->end - buf->current))
  {
    //
    // Allocate more memory...
    //

    char	*temp;			// New buffer
    size_t	size;			// Size of buffer


    size = (size_t)(buf->end - buf->start + 2 * bytes + 1024);

    if (buf->start)
      temp = realloc(buf->start, size);
    else
      temp = malloc(size);

    if (!temp)
      return;

    //
    // Update pointers...
    //

    buf->end     = temp + size;
    buf->current = temp + (buf->current - buf->start);
    buf->start   = temp;
  }

  //
  // Append the message to the end of the current string...
  //

  memcpy(buf->current, s, (size_t)bytes);
  buf->current += bytes - 1;
}


//
// '_ppdRasterClearError()' - Clear the error buffer.
//

void
_ppdRasterClearError(void)
{
  _ppd_raster_error_t	*buf = ppd_raster_error_buffer();
					// Error buffer for this thread


  if (!buf)
    return;

  buf->current = buf->start;

  if (buf->start)
    *(buf->start) = '\0';
}


//
// '_ppdRasterErrorString()' - Return the last error from a raster function.
//
// If there are no recent errors, NULL is returned.
//
// @since CUPS 1.3/macOS 10.5@
//

const char *				// O - Last error
_ppdRasterErrorString(void)
{
  _ppd_raster_error_t	*buf = ppd_raster_error_buffer();
					// Error buffer for this thread


  if (!buf || buf->current == buf->start)
    return (NULL);
  else
    return (buf->start);
}


//
// 'ppd_raster_error_buffer()' - Return the error buffer of the current thread.
//

static _ppd_raster_error_t *		// O - Error buffer or NULL
ppd_raster_error_buffer(void)
{
#ifdef HAVE_PTHREAD_H
  _ppd_raster_error_t	*buf;		// Error buffer


  //
  // Initialize the thread key exactly once...
  //

  pthread_once(&ppd_raster_error_key_once, ppd_raster_error_init);

  //
  // Allocate the buffer of this thread on first use...
  //

  if ((buf = (_ppd_raster_error_t *)
	     _ppdThreadGetData(ppd_raster_error_key)) == NULL)
  {
    if ((buf = calloc(1, sizeof(_ppd_raster_error_t))) != NULL)
      _ppdThreadSetData(ppd_raster_error_key, buf);
  }

  return (buf);

#else
  return (&ppd_raster_error_buf);
#endif // HAVE_PTHREAD_H
}


#ifdef HAVE_PTHREAD_H
//
// 'ppd_raster_error_free()' - Free the error buffer of a thread that exits.
//

static void
ppd_raster_error_free(void *data)	// I - Error buffer
{
  _ppd_raster_error_t	*buf = (_ppd_raster_error_t *)data;
					// Error buffer


  free(buf->start);
  free(buf);
}


//
// 'ppd_raster_error_init()' - Create the thread key for the error buffers.
//

static void
ppd_raster_error_init(void)
{
  pthread_key_create(&ppd_raster_error_key, ppd_raster_error_free);
}
#endif // HAVE_PTHREAD_H
