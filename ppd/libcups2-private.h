//
// Libcups2 header file for libcupsfilters.
//
// Copyright 2020-2022 by Till Kamppeter.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//

#ifndef _LIBCUPS2_PRIVATE_H_
#  define _LIBCUPS2_PRIVATE_H_

#  include <config.h>

#  ifdef HAVE_LIBCUPS2 

//   These CUPS headers need to get applied before applying the
//   renaming "#define"s. Otherwise we get conflicting duplicate
//   declarations.

#    include "cups/http.h"
#    include "cups/array.h"
#    include "cups/cups.h"
#    include "cups/ipp.h"
#    include "cups/raster.h"
#    include "cups/language.h"

//
// C++ magic...
//

#  ifdef __cplusplus
extern "C" {
#  endif // __cplusplus


//   Constants renamed in libcups3

#    define CUPS_ENCODING_ISO8859_1     CUPS_ISO8859_1
#    define CUPS_ENCODING_ISO8859_2     CUPS_ISO8859_2
#    define CUPS_ENCODING_ISO8859_5     CUPS_ISO8859_5
#    define CUPS_ENCODING_JIS_X0213     CUPS_JIS_X0213
#    define CUPS_ENCODING_MAC_ROMAN     CUPS_MAC_ROMAN
#    define CUPS_ENCODING_WINDOWS_1252  CUPS_WINDOWS_1252
#    define CUPS_ENCODING_UTF_8         CUPS_UTF8

//   Functions renamed in libcups3

#    define cupsArrayGetCount      cupsArrayCount
#    define cupsArrayGetFirst      cupsArrayFirst
#    define cupsArrayGetNext       cupsArrayNext
#    define cupsArrayGetPrev       cupsArrayPrev
#    define cupsArrayGetLast       cupsArrayLast
#    define cupsArrayGetElement    cupsArrayIndex
#    define cupsArrayNew           cupsArrayNew3
#    define cupsGetDests           cupsGetDests2
#    define cupsGetError           cupsLastError
#    define cupsGetErrorString     cupsLastErrorString
#    define cupsRasterReadHeader   cupsRasterReadHeader2
#    define cupsRasterWriteHeader  cupsRasterWriteHeader2
#    define httpConnect            httpConnect2
#    define httpAddrGetPort        httpAddrPort
#    define ippGetFirstAttribute   ippFirstAttribute
#    define ippGetNextAttribute    ippNextAttribute
#    define ippGetLength           ippLength

//   Functions replaced by a different functions in libcups3

#    define cupsCreateTempFd(prefix,suffix,buffer,bufsize) cupsTempFd(buffer,bufsize)
#    define cupsCreateTempFile(prefix,suffix,buffer,bufsize) cupsTempFile2(buffer,bufsize)

//   Data types renamed in libcups3

#    define cups_acopy_cb_t       cups_acopy_func_t
#    define cups_ahash_cb_t       cups_ahash_func_t
#    define cups_afree_cb_t       cups_afree_func_t
#    define cups_array_cb_t       cups_array_func_t
#    define ipp_io_cb_t           ipp_iocb_t
#    define cups_page_header_t    cups_page_header2_t

//   For some functions' parameters in libcups3 size_t is used while
//   int was used in libcups2. We use this type in such a case.

#    define cups_len_t            int

//   For some functions' parameters in libcups3 bool is used while
//   int was used in libcups2. We use this type in such a case.

#    define cups_bool_t           int

//   Prototypes of functions equivalent to newly introduced ones in libcups3

const char *cupsLangGetName(cups_lang_t *lang);
const char *cupsLangGetString(cups_lang_t *lang, const char *message);

#  ifdef __cplusplus
}
#  endif // __cplusplus

#  else

#    define cups_len_t            size_t
#    define cups_utf8_t           char
#    define cups_bool_t           bool

#  endif // HAVE_LIBCUPS2

#endif // !_LIBCUPS2_PRIVATE_H_
