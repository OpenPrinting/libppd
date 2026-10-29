//
// Private image library definitions for libppd.
//
// Copyright © 2007-2019 by Apple Inc.
// Copyright © 1993-2006 by Easy Software Products.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.


#ifndef _PPD_RASTER_PRIVATE_H_
#  define _PPD_RASTER_PRIVATE_H_

//
// Include necessary headers...
//

#  include <cups/raster.h>
#  include "debug-private.h"
#  include "string-private.h"
#  ifdef _WIN32
#    include <io.h>
#    include <winsock2.h>		// for htonl() definition
#  else
#    include <unistd.h>
#    include <fcntl.h>
#  endif // _WIN32


//
// Longest raster line, the same limit libcups applies to raster streams...
//

#  define PPD_MAX_BYTES_PER_LINE	(16 * 1024 * 1024)

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//
// Prototypes...
//

extern void		_ppdRasterAddError(const char *f, ...);
extern void		_ppdRasterClearError(void);
extern const char	*_ppdRasterErrorString(void);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#endif // !_PPD_RASTER_PRIVATE_H_
