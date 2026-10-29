//
// Raster to PostScript filter unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests ppdFilterRasterToPS() with one-page raster streams written by
// libcups.  The output must end one page with "showpage", count it in the
// trailer, and hold Flate-encoded image data that inflates to the expected
// bytes:
//
//   a. An 8x2 1-bit RGB page, whose pixels the filter converts to one byte
//      per color.
//   b. A 5x3 8-bit grayscale page, whose pixels the filter passes on
//      unchanged.
//   c. A 2000000x2 8-bit grayscale page, whose 2,000,000 byte lines the
//      filter must encode without line-sized buffers on the stack.
//

#include <ppd/ppd.h>
#include <ppd/ppd-filter.h>
#include <ppd/libcups2-private.h>
#include "test-internal.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zlib.h>


//
// Local globals...
//

static int	log_errors = 0;		// Number of errors the filter logged
static char	log_message[1024] = ""; // Last error the filter logged


//
// Local functions...
//

static void	check_output(const unsigned char *output, size_t bytes,
			     const unsigned char *expected,
			     size_t expected_bytes);
static const unsigned char *find_string(const unsigned char *data,
					size_t bytes, const char *s);
static void	init_header(cups_page_header_t *header, unsigned width,
			    unsigned height, cups_cspace_t cspace,
			    unsigned bpc, unsigned ncolors, unsigned bpp);
static void	log_cb(void *data, cf_loglevel_t level, const char *message,
		       ...);
static unsigned char *run_filter(cups_page_header_t *header,
				 unsigned char *pixels, FILE *infile,
				 FILE *outfile, size_t *bytes);
static void	test_page(cups_page_header_t *header, unsigned char *pixels,
			  const unsigned char *expected,
			  size_t expected_bytes);


//
// 'main()' - Run the ppdFilterRasterToPS() tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  cups_page_header_t	header;		// Page header
  unsigned char		rgb[8] =	// 1-bit RGB data, 8x2 pixels
  {
    0x77, 0x00, 0x41, 0x26,
    0x88, 0xff, 0x5a, 0x13
  };
  static const unsigned char masks[6] = // Bits the filter keeps of each byte
  {
    0x40, 0x20, 0x10, 0x04, 0x02, 0x01
  };
  unsigned char		rgb_image[sizeof(rgb) * 6],
					// Image data for the RGB page
			gray[15],	// Grayscale data, 5x3 pixels
			*pixels;	// Grayscale data, 2000000x2 pixels
  size_t		i, j,		// Looping vars
			bytes;		// Bytes of raster data


  //
  // a. 1-bit RGB: each byte holds two 4-bit pixels whose top bit is unused,
  //    and the filter makes one byte, 0x00 or 0xFF, of each other bit.  The
  //    unused bit counts as a color so that cupsBitsPerPixel is
  //    cupsBitsPerColor * cupsNumColors, as cupsRasterReadHeader() checks
  //    since CUPS 2.4.17...
  //

  testBegin("ppdFilterRasterToPS(8x2 1-bit RGB)");
  for (i = 0; i < sizeof(rgb); i ++)
    for (j = 0; j < sizeof(masks); j ++)
      rgb_image[i * sizeof(masks) + j] = (rgb[i] & masks[j]) ? 0xff : 0x00;
  init_header(&header, 8, 2, CUPS_CSPACE_RGB, 1, 4, 4);
  test_page(&header, rgb, rgb_image, sizeof(rgb_image));

  //
  // b. 8-bit grayscale: the image data is the raster data...
  //

  testBegin("ppdFilterRasterToPS(5x3 8-bit grayscale)");
  for (i = 0; i < sizeof(gray); i ++)
    gray[i] = (unsigned char)(i * 37 + 11);
  init_header(&header, 5, 3, CUPS_CSPACE_K, 8, 1, 8);
  test_page(&header, gray, gray, sizeof(gray));

  //
  // c. 8-bit grayscale with 2,000,000 byte lines, which overflowed a default
  //    8 MiB stack when write_flate() kept them in stack arrays...
  //

  testBegin("ppdFilterRasterToPS(2000000x2 8-bit grayscale)");
  init_header(&header, 2000000, 2, CUPS_CSPACE_K, 8, 1, 8);
  bytes = (size_t)header.cupsBytesPerLine * header.cupsHeight;
  if ((pixels = malloc(bytes)) == NULL)
  {
    testEndMessage(false, "unable to allocate %u bytes", (unsigned)bytes);
  }
  else
  {
    for (i = 0; i < bytes; i ++)
      pixels[i] = (unsigned char)(i % 251);
    test_page(&header, pixels, pixels, bytes);
    free(pixels);
  }

  return (testsPassed ? 0 : 1);
}


//
// 'check_output()' - Check that the filter output ends one page, counts it
//                    in the trailer, and holds the expected image data.
//

static void
check_output(
    const unsigned char *output,	// I - Filter output
    size_t              bytes,		// I - Size of the output
    const unsigned char *expected,	// I - Expected image data
    size_t              expected_bytes) // I - Size of the expected image data
{
  const unsigned char	*start;		// Start of the image data
  unsigned char		*image;		// Inflated image data
  size_t		image_bytes;	// Size of the inflated image data
  z_stream		strm;		// Inflate state
  int			status;		// Inflate status
  static const char	image_op[] = ">> image\n";
					// Line before the image data


  if (!find_string(output, bytes, "\nshowpage\n"))
  {
    testEndMessage(false, "no \"showpage\" in the output");
    return;
  }

  if (!find_string(output, bytes, "\n%%Pages: 1\n"))
  {
    testEndMessage(false, "no \"%%%%Pages: 1\" in the output");
    return;
  }

  if ((start = find_string(output, bytes, image_op)) == NULL)
  {
    testEndMessage(false, "no image in the output");
    return;
  }

  start += strlen(image_op);

  //
  // Inflate into a buffer one byte larger than the expected data, so that
  // extra data shows...
  //

  if ((image = malloc(expected_bytes + 1)) == NULL)
  {
    testEndMessage(false, "unable to allocate %u bytes",
		   (unsigned)(expected_bytes + 1));
    return;
  }

  memset(&strm, 0, sizeof(strm));

  if (inflateInit(&strm) != Z_OK)
  {
    testEndMessage(false, "inflateInit() failed");
    free(image);
    return;
  }

  strm.next_in   = (Bytef *)start;
  strm.avail_in  = (uInt)(bytes - (size_t)(start - output));
  strm.next_out  = image;
  strm.avail_out = (uInt)(expected_bytes + 1);

  do
    status = inflate(&strm, Z_NO_FLUSH);
  while (status == Z_OK && strm.avail_out > 0);

  image_bytes = expected_bytes + 1 - strm.avail_out;

  inflateEnd(&strm);

  if (status == Z_OK && strm.avail_out == 0)
    testEndMessage(false, "image data inflates to more than %u bytes",
		   (unsigned)expected_bytes);
  else if (status != Z_STREAM_END)
    testEndMessage(false, "inflate() returned %d after %u bytes", status,
		   (unsigned)image_bytes);
  else if (image_bytes != expected_bytes)
    testEndMessage(false, "image data inflates to %u bytes, expected %u",
		   (unsigned)image_bytes, (unsigned)expected_bytes);
  else if (memcmp(image, expected, expected_bytes))
    testEndMessage(false, "image data differs from the raster data");
  else
    testEndMessage(true, "%u bytes of image data", (unsigned)image_bytes);

  free(image);
}


//
// 'find_string()' - Find a string in a buffer.
//

static const unsigned char *		// O - First match or NULL
find_string(const unsigned char *data,	// I - Buffer
	    size_t              bytes,	// I - Size of buffer
	    const char          *s)	// I - String to find
{
  size_t	len = strlen(s);	// Length of string
  size_t	i;			// Looping var


  for (i = 0; i + len <= bytes; i ++)
    if (!memcmp(data + i, s, len))
      return (data + i);

  return (NULL);
}


//
// 'init_header()' - Make a chunked 72dpi page header.
//

static void
init_header(cups_page_header_t *header, // O - Page header
	    unsigned           width,	// I - Width in pixels
	    unsigned           height,	// I - Height in pixels
	    cups_cspace_t      cspace,	// I - Color space
	    unsigned           bpc,	// I - Bits per color
	    unsigned           ncolors, // I - Number of colors
	    unsigned           bpp)	// I - Bits per pixel
{
  memset(header, 0, sizeof(cups_page_header_t));

  header->HWResolution[0]  = 72;
  header->HWResolution[1]  = 72;
  header->PageSize[0]      = width;
  header->PageSize[1]      = height;
  header->cupsWidth        = width;
  header->cupsHeight       = height;
  header->cupsBitsPerColor = bpc;
  header->cupsBitsPerPixel = bpp;
  header->cupsBytesPerLine = (width * bpp + 7) / 8;
  header->cupsColorOrder   = CUPS_ORDER_CHUNKED;
  header->cupsColorSpace   = cspace;
  header->cupsNumColors    = ncolors;
}


//
// 'log_cb()' - Record the errors the filter logs.
//

static void
log_cb(void          *data,		// I - Logging data (unused)
       cf_loglevel_t level,		// I - Log level
       const char    *message,		// I - printf-style message
       ...)				// I - Additional arguments
{
  va_list	ap;			// Pointer to additional arguments


  (void)data;

  if (level != CF_LOGLEVEL_ERROR && level != CF_LOGLEVEL_FATAL)
    return;

  va_start(ap, message);
  vsnprintf(log_message, sizeof(log_message), message, ap);
  va_end(ap);

  log_errors ++;
}


//
// 'run_filter()' - Write a one-page raster stream and run the filter on it.
//

static unsigned char *			// O - Filter output or NULL
run_filter(cups_page_header_t *header,	// I - Page header
	   unsigned char      *pixels,	// I - Raster data for the page
	   FILE               *infile,	// I - Temporary file for the raster
	   FILE               *outfile, // I - Temporary file for the output
	   size_t             *bytes)	// O - Size of the output
{
  cups_raster_t		*ras;		// Raster stream
  cf_filter_data_t	data;		// Filter data
  unsigned		y;		// Current line
  int			infd,		// Input for the filter
			outfd,		// Output for the filter
			status;		// Filter exit status
  long			length;		// Length of the output
  unsigned char		*output;	// Filter output


  //
  // Write the page...
  //

  if ((ras = cupsRasterOpen(fileno(infile),
			    CUPS_RASTER_WRITE_COMPRESSED)) == NULL)
  {
    testEndMessage(false, "cupsRasterOpen() failed");
    return (NULL);
  }

  if (!cupsRasterWriteHeader(ras, header))
  {
    cupsRasterClose(ras);
    testEndMessage(false, "cupsRasterWriteHeader() failed");
    return (NULL);
  }

  for (y = 0; y < header->cupsHeight; y ++)
  {
    if (cupsRasterWritePixels(ras, pixels, header->cupsBytesPerLine) !=
	header->cupsBytesPerLine)
    {
      cupsRasterClose(ras);
      testEndMessage(false, "cupsRasterWritePixels() failed");
      return (NULL);
    }

    pixels += header->cupsBytesPerLine;
  }

  cupsRasterClose(ras);

  //
  // Run the filter on copies of the file descriptors, as it closes them...
  //

  if (lseek(fileno(infile), 0, SEEK_SET) != 0)
  {
    testEndMessage(false, "lseek: %s", strerror(errno));
    return (NULL);
  }

  if ((infd = dup(fileno(infile))) < 0)
  {
    testEndMessage(false, "dup: %s", strerror(errno));
    return (NULL);
  }

  if ((outfd = dup(fileno(outfile))) < 0)
  {
    testEndMessage(false, "dup: %s", strerror(errno));
    close(infd);
    return (NULL);
  }

  memset(&data, 0, sizeof(data));
  data.logfunc = log_cb;

  log_errors     = 0;
  log_message[0] = '\0';

  if ((status = ppdFilterRasterToPS(infd, outfd, 0, &data, NULL)) != 0)
  {
    testEndMessage(false, "ppdFilterRasterToPS() returned %d", status);
    return (NULL);
  }

  if (log_errors)
  {
    testEndMessage(false, "%d errors logged, last \"%s\"", log_errors,
		   log_message);
    return (NULL);
  }

  //
  // Read the output back...
  //

  if (fseek(outfile, 0, SEEK_END) || (length = ftell(outfile)) <= 0)
  {
    testEndMessage(false, "no output");
    return (NULL);
  }

  rewind(outfile);

  if ((output = malloc((size_t)length)) == NULL)
  {
    testEndMessage(false, "unable to allocate %ld bytes", length);
    return (NULL);
  }

  if (fread(output, 1, (size_t)length, outfile) != (size_t)length)
  {
    testEndMessage(false, "unable to read the output");
    free(output);
    return (NULL);
  }

  *bytes = (size_t)length;

  return (output);
}


//
// 'test_page()' - Run the filter on one page and check its output.
//

static void
test_page(cups_page_header_t  *header,	// I - Page header
	  unsigned char       *pixels,	// I - Raster data for the page
	  const unsigned char *expected,// I - Expected image data
	  size_t              expected_bytes)
					// I - Size of the expected image data
{
  FILE			*infile,	// Temporary file for the raster
			*outfile;	// Temporary file for the output
  unsigned char		*output;	// Filter output
  size_t		bytes;		// Size of the output


  infile  = tmpfile();
  outfile = tmpfile();

  if (!infile || !outfile)
  {
    testEndMessage(false, "tmpfile: %s", strerror(errno));
  }
  else if ((output = run_filter(header, pixels, infile, outfile,
				&bytes)) != NULL)
  {
    check_output(output, bytes, expected, expected_bytes);
    free(output);
  }

  if (infile)
    fclose(infile);
  if (outfile)
    fclose(outfile);
}
