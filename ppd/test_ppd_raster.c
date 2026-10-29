//
// PPD raster header unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests ppdRasterInterpretPPD() with an in-memory PPD for an 8-bit RGB
// raster printer that supports custom page sizes:
//
//   a. The defaults (Letter, 300dpi) give a 24-bit header whose
//      cupsBytesPerLine holds a whole line of pixels.
//   b. A custom size whose line is far over 16 MiB (and whose bits per line
//      overflow 32 bits) is refused.
//   c. A custom size whose width in pixels does not fit in an unsigned
//      integer is refused.
//   d. A custom size whose width in points does not fit in an unsigned
//      integer is refused.
//   e. A negative HWResolution is refused.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>


//
// 8-bit RGB raster PPD with a custom page size (1..1000 x 1..2000 points)...
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"RASTTEST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(RasterTest)\"\n"
  "*ModelName: \"Acme RasterTest\"\n"
  "*ShortNickName: \"RasterTest\"\n"
  "*NickName: \"Acme RasterTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  "*OpenUI *ColorModel/Color Mode: PickOne\n"
  "*OrderDependency: 10 AnySetup *ColorModel\n"
  "*DefaultColorModel: RGB\n"
  "*ColorModel RGB/Color: \"<</cupsColorSpace 1/cupsColorOrder 0"
  "/cupsBitsPerColor 8>>setpagedevice\"\n"
  "*CloseUI: *ColorModel\n"
  "*OpenUI *Resolution/Resolution: PickOne\n"
  "*OrderDependency: 20 AnySetup *Resolution\n"
  "*DefaultResolution: 300dpi\n"
  "*Resolution 300dpi/300 DPI: \"<</HWResolution[300 300]>>setpagedevice\"\n"
  "*Resolution bad/Bad: \"<</HWResolution[-300 300]>>setpagedevice\"\n"
  "*CloseUI: *Resolution\n"
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 30 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]/ImagingBBox null>>"
  "setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 30 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageSize[612 792]/ImagingBBox null>>"
  "setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*VariablePaperSize: True\n"
  "*ParamCustomPageSize Width: 1 points 1 1000\n"
  "*ParamCustomPageSize Height: 2 points 1 2000\n"
  "*ParamCustomPageSize WidthOffset: 3 points 0 0\n"
  "*ParamCustomPageSize HeightOffset: 4 points 0 0\n"
  "*ParamCustomPageSize Orientation: 5 int 0 3\n"
  "*MaxMediaWidth: \"1000\"\n"
  "*MaxMediaHeight: \"2000\"\n"
  "*HWMargins: 0 0 0 0\n"
  "*CustomPageSize True: \"pop pop pop <</PageSize[5 -2 roll]"
  "/ImagingBBox null>>setpagedevice\"\n";


//
// Options giving headers that ppdRasterInterpretPPD() must refuse...
//

static const struct
{
  const char	*option,		// Option to mark
		*choice,		// Choice to mark
		*why;			// What is wrong with the header
} refused[] =
{
  { "PageSize",   "Custom.100000000x1000",    "line over 16 MiB" },
  { "PageSize",   "Custom.2000000000x1000",   "cupsWidth out of range" },
  { "PageSize",   "Custom.100000000000x1000", "width out of range" },
  { "Resolution", "bad",                      "negative HWResolution" }
};


//
// Local functions...
//

static int	interpret(ppd_file_t *ppd, const char *option,
		          const char *choice, cups_page_header_t *header);


//
// 'main()' - Run the ppdRasterInterpretPPD() tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t		*ppd;		// PPD file
  cups_page_header_t	header;		// Page header
  FILE			*f;		// Temporary file for the PPD
  ppd_status_t		err;		// PPD parse error code
  int			line;		// Line number of any parse error
  size_t		i;		// Looping var


  testBegin("ppdOpen(embedded RGB raster test PPD)");
  f = tmpfile();
  if (!f)
  {
    testEndMessage(false, "tmpfile() failed");
    return (1);
  }
  fputs(test_ppd_text, f);
  rewind(f);
  ppd = ppdOpen(f);
  fclose(f);
  if (ppd)
  {
    testEnd(true);
  }
  else
  {
    err = ppdLastError(&line);
    testEndMessage(false, "%s on line %d", ppdErrorString(err), line);
    return (1);
  }

  //
  // a. Defaults: Letter, 300dpi, 8-bit RGB...
  //

  testBegin("ppdRasterInterpretPPD(defaults)");
  if (interpret(ppd, NULL, NULL, &header))
    testEndMessage(false, "returned -1");
  else
    testEndMessage(header.cupsBitsPerPixel == 24 &&
		   (unsigned long long)header.cupsBytesPerLine * 8 >=
		   (unsigned long long)header.cupsWidth *
		   header.cupsBitsPerPixel,
		   "cupsWidth=%u, cupsBitsPerPixel=%u, cupsBytesPerLine=%u",
		   header.cupsWidth, header.cupsBitsPerPixel,
		   header.cupsBytesPerLine);

  //
  // b-e. Headers that cannot be represented are refused...
  //

  for (i = 0; i < sizeof(refused) / sizeof(refused[0]); i ++)
  {
    testBegin("ppdRasterInterpretPPD(%s=%s) returns -1 (%s)",
	      refused[i].option, refused[i].choice, refused[i].why);
    if (interpret(ppd, refused[i].option, refused[i].choice, &header))
      testEnd(true);
    else
      testEndMessage(false, "returned 0, cupsWidth=%u, cupsBytesPerLine=%u",
		     header.cupsWidth, header.cupsBytesPerLine);
  }

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}


//
// 'interpret()' - Mark the defaults and one option, then build a page header.
//

static int				// O - ppdRasterInterpretPPD() status
interpret(ppd_file_t         *ppd,	// I - PPD file
	  const char         *option,	// I - Option to mark or NULL
	  const char         *choice,	// I - Choice to mark
	  cups_page_header_t *header)	// O - Page header
{
  ppdMarkDefaults(ppd);

  if (option)
    ppdMarkOption(ppd, option, choice);

  return (ppdRasterInterpretPPD(header, ppd, 0, NULL, NULL));
}
