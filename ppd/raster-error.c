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
#include "debug-internal.h"

typedef struct _ppd_raster_error_s	// **** Error buffer structure ****
{
  char	*start,				// Start of buffer
	*current,			// Current position in buffer
	*end;				// End of buffer
} _ppd_raster_error_t;

static _ppd_raster_error_t	*buf = NULL;

//
// '_ppdRasterAddError()' - Add an error message to the error buffer.
//

void
_ppdRasterAddError(const char *f,	// I - Printf-style error message
                    ...)		// I - Additional arguments as needed
{
  va_list	ap;			// Pointer to additional arguments
  char		s[2048];		// Message string
  ssize_t	bytes;			// Bytes in message string


  DEBUG_printf(("_ppdRasterAddError(f=\"%s\", ...)", f));

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
  if (buf == NULL)
  {
    buf = malloc(sizeof(_ppd_raster_error_t));
    buf->start = NULL;
    buf->end = NULL;
    buf->current = NULL;
  }

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
  if (buf->current == buf->start)
    return (NULL);
  else
    return (buf->start);
}
