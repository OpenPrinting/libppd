//
// PPD page-size API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (35 assertions across 6 groups):
//
//   Group 1  (T01-T04)  NULL/pointer guards — every public function in
//                       ppd-page.c range-checks its PPD pointer.  ppdPageSize()
//                       returns NULL, ppdPageWidth()/ppdPageLength() return
//                       0.0, and ppdPageSizeLimits() returns 0 (zeroing both
//                       output records) when handed a NULL ppd.  These run
//                       before any PPD is opened.
//
//   Group 2  (T05-T12)  ppdPageSize() lookup by name — open the embedded PPD
//                       and resolve standard sizes.  Name matching uses
//                       _ppd_strcasecmp() (case-insensitive) and the returned
//                       record carries the geometry parsed from
//                       *PaperDimension (width/length) and *ImageableArea
//                       (left/bottom/right/top).  We also cover the unknown-
//                       name miss and the NULL-name "default" branch, which
//                       returns the currently-marked size — NULL before
//                       ppdMarkDefaults(), the default (Letter) after.
//
//   Group 3  (T13-T17)  ppdPageWidth()/ppdPageLength() — thin wrappers over
//                       ppdPageSize() that return size->width / size->length
//                       or 0.0 when the size is not found.  We confirm the
//                       Letter and A4 values, the not-found 0.0, and the
//                       NULL-name default (Letter) width.
//
//   Group 4  (T18-T25)  Custom.WIDTHxLENGTH parsing — when variable sizes are
//                       supported and the name begins "Custom.", ppdPageSize()
//                       parses WIDTHxLENGTH with an optional unit suffix
//                       (pt/in/ft/cm/mm/m), scales to points, applies
//                       ppd->custom_margins, and returns the shared "Custom"
//                       size record.  We verify points (no suffix), inches
//                       (exact ×72), millimetres (×72/25.4, tolerance check),
//                       the width/length wrappers, and two negative cases:
//                       a string with no 'x' separator, and a lowercase
//                       "custom." prefix (the prefix test is case-sensitive,
//                       so it falls through to a failing name lookup).
//
//   Group 5  (T26-T31)  ppdPageSizeLimits() with variable sizes — returns 1
//                       and fills "minimum"/"maximum" from ppd->custom_min /
//                       ppd->custom_max (set by *ParamCustomPageSize) with the
//                       printable margins derived from ppd->custom_margins
//                       (right = width - margins[2], top = length -
//                       margins[3]).  We check both records' width/length and
//                       all four margins, plus the NULL-minimum range-check
//                       returning 0 while zeroing the maximum record.
//
//   Group 6  (T32-T35)  No-variable-size PPD (edge cases) — open a second PPD
//                       that declares no custom page size.  Here
//                       ppd->variable_sizes is 0, so ppdPageSizeLimits()
//                       returns 0 (zeroing both records) and a "Custom.WxH"
//                       lookup misses (NULL), while standard named lookups
//                       still work.
//
// Design: two PPDs are built entirely in memory from static strings and
// loaded via tmpfile() + ppdOpen(), so the binary is fully hermetic — no
// external files are needed at build or CI time.
//
//   • test_ppd_text (variable sizes): three standard sizes (Letter, A4,
//     Legal) with distinct *PaperDimension/*ImageableArea geometry, plus the
//     full custom-size machinery — *ParamCustomPageSize Width (36..1000 pt) /
//     Height (72..2000 pt) feeding custom_min/custom_max, *HWMargins
//     "10 20 30 40" feeding custom_margins, and *CustomPageSize True enabling
//     variable_sizes and the "Custom" size record.
//   • test_ppd_novar (no variable sizes): the same header and Letter/A4
//     geometry but with none of the custom machinery, so variable_sizes is 0.
//
// Expected geometry (points), read straight from the PPD lines below:
//   Letter: 612 × 792, imageable 18 18 594 774
//   A4    : 595 × 842, imageable 18 18 577 824
//   Legal : 612 × 1008, imageable 18 18 594 990
//   Custom margins: left 10, bottom 20, right(margin) 30, top(margin) 40
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>


//
// Variable-size PPD.
//
// Sections and what they exercise:
//
//   PPD header block (PPD-Adobe through TTRasterizer):
//     Mandatory metadata so ppdOpen() returns a valid ppd_file_t *.
//
//   PageSize / PageRegion:
//     Three media choices (Letter, A4, Legal) with Letter as the default —
//     names looked up by ppdPageSize() and marked by ppdMarkDefaults().
//
//   ImageableArea / PaperDimension (Letter/A4/Legal):
//     The per-size geometry: PaperDimension → size->width/length,
//     ImageableArea → size->left/bottom/right/top.
//
//   VariablePaperSize + ParamCustomPageSize (×5) + HWMargins +
//   CustomPageSize True:
//     The custom-size machinery.  ParamCustomPageSize Width/Height set
//     custom_min/custom_max (36..1000 and 72..2000 points); HWMargins sets
//     custom_margins to {10,20,30,40}; *CustomPageSize True sets
//     ppd->variable_sizes = 1 and adds the "Custom" size record that
//     ppdPageSize() rewrites on each Custom.WxH request.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"PAGETEST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(PageTest)\"\n"
  "*ModelName: \"Acme PageTest\"\n"
  "*ShortNickName: \"PageTest\"\n"
  "*NickName: \"Acme PageTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  // PageSize — Letter is the default; three sizes total.
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*PageSize Legal/US Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  // PageRegion (parallel of PageSize — required by the Adobe PPD spec)
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*PageRegion Legal: \"<</PageSize[612 1008]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  // ImageableArea — printable rectangle (left bottom right top) per size.
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*ImageableArea Legal: \"18 18 594 990\"\n"
  // PaperDimension — physical size (width length) per size.
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  "*PaperDimension Legal: \"612 1008\"\n"
  // Custom page size — Width 36..1000 pt, Height 72..2000 pt feed
  // custom_min/custom_max; HWMargins feed custom_margins {10,20,30,40};
  // *CustomPageSize True enables variable_sizes and the "Custom" size record.
  "*VariablePaperSize: True\n"
  "*ParamCustomPageSize Width: 1 points 36 1000\n"
  "*ParamCustomPageSize Height: 2 points 72 2000\n"
  "*ParamCustomPageSize WidthOffset: 3 points 0 0\n"
  "*ParamCustomPageSize HeightOffset: 4 points 0 0\n"
  "*ParamCustomPageSize Orientation: 5 int 0 3\n"
  "*MaxMediaWidth: \"1000\"\n"
  "*MaxMediaHeight: \"2000\"\n"
  "*HWMargins: 10 20 30 40\n"
  "*CustomPageSize True: \"pop pop pop pop pop\"\n";


//
// No-variable-size PPD — identical header and Letter/A4 geometry, but with
// none of the *VariablePaperSize/*ParamCustomPageSize/*CustomPageSize lines,
// so ppd->variable_sizes stays 0.
//

static const char test_ppd_novar[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"NOVARTST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(NoVarTest)\"\n"
  "*ModelName: \"Acme NoVarTest\"\n"
  "*ShortNickName: \"NoVarTest\"\n"
  "*NickName: \"Acme NoVarTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n";


//
// 'main()' - Run all ppd-page.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// Variable-size PPD handle
  ppd_file_t   *ppd2;			// No-variable-size PPD handle
  ppd_size_t   *size;			// Returned by ppdPageSize()
  ppd_size_t   *size2;			// Second lookup for pointer identity
  ppd_size_t    minsize;		// Minimum custom size (output)
  ppd_size_t    maxsize;		// Maximum custom size (output)
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error
  int           ret;			// Return value of ppdPageSizeLimits()
  float         mm_w, mm_l;		// Expected mm→points conversions


  // =========================================================================
  // Group 1: NULL/pointer guards (T01–T04)
  //
  // Each function range-checks its PPD pointer before any dereference.  These
  // need no PPD content and run first.
  // =========================================================================

  // T01 — ppdPageSize: `if (!ppd) return (NULL);` — a NULL PPD short-circuits
  //       before touching ppd->sizes.
  testBegin("ppdPageSize(NULL ppd, \"Letter\") returns NULL");
  testEnd(ppdPageSize(NULL, "Letter") == NULL);

  // T02 — ppdPageWidth: forwards to ppdPageSize(), which returns NULL for a
  //       NULL ppd, so the wrapper returns 0.0.
  testBegin("ppdPageWidth(NULL ppd, \"Letter\") returns 0.0");
  testEnd(ppdPageWidth(NULL, "Letter") == 0.0f);

  // T03 — ppdPageLength: same path as T02 → 0.0.
  testBegin("ppdPageLength(NULL ppd, \"Letter\") returns 0.0");
  testEnd(ppdPageLength(NULL, "Letter") == 0.0f);

  // T04 — ppdPageSizeLimits: `if (!ppd ...)` returns 0 and zeroes both output
  //       records.  We pre-dirty the records to prove they are cleared.
  testBegin("ppdPageSizeLimits(NULL ppd, &min, &max) returns 0 and zeroes both");
  minsize.width = 123.0f;
  maxsize.width = 456.0f;
  ret = ppdPageSizeLimits(NULL, &minsize, &maxsize);
  testEnd(ret == 0 && minsize.width == 0.0f && maxsize.width == 0.0f);


  // =========================================================================
  // Group 2: ppdPageSize() lookup by name (T05–T12)
  //
  // Open the variable-size PPD.  Named lookups are case-insensitive and the
  // returned record carries PaperDimension (width/length) and ImageableArea
  // (left/bottom/right/top) geometry.  The NULL-name branch returns the
  // currently-marked size.
  // =========================================================================

  testBegin("ppdOpen(embedded variable-size test PPD)");
  f = tmpfile();
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

  // T06 — "Letter" is declared, so ppdPageSize() returns its record and the
  //       record's name field equals "Letter".
  testBegin("ppdPageSize(ppd, \"Letter\") returns the Letter record");
  size = ppdPageSize(ppd, "Letter");
  testEndMessage(size != NULL && !strcmp(size->name, "Letter"),
                 "name=\"%s\"", size ? size->name : "(null)");

  // T07 — Letter geometry: PaperDimension "612 792" → width/length;
  //       ImageableArea "18 18 594 774" → left/bottom/right/top.
  testBegin("ppdPageSize(ppd, \"Letter\") geometry 612x792, area 18/18/594/774");
  testEndMessage(size != NULL &&
                 size->width == 612.0f && size->length == 792.0f &&
                 size->left == 18.0f && size->bottom == 18.0f &&
                 size->right == 594.0f && size->top == 774.0f,
                 "w=%g l=%g L=%g B=%g R=%g T=%g",
                 size ? size->width : -1, size ? size->length : -1,
                 size ? size->left : -1, size ? size->bottom : -1,
                 size ? size->right : -1, size ? size->top : -1);

  // T08 — A4: PaperDimension "595 842" → width 595, length 842.
  testBegin("ppdPageSize(ppd, \"A4\") geometry 595x842");
  size = ppdPageSize(ppd, "A4");
  testEndMessage(size != NULL && size->width == 595.0f && size->length == 842.0f,
                 "w=%g l=%g", size ? size->width : -1, size ? size->length : -1);

  // T09 — Name matching is case-insensitive (_ppd_strcasecmp), so "letter"
  //       resolves to the *same* record pointer as "Letter".
  testBegin("ppdPageSize(ppd, \"letter\") finds the same record as \"Letter\"");
  size  = ppdPageSize(ppd, "Letter");
  size2 = ppdPageSize(ppd, "letter");
  testEndMessage(size2 != NULL && size2 == size, "%p vs %p",
                 (void *)size2, (void *)size);

  // T10 — A name absent from the PPD returns NULL.
  testBegin("ppdPageSize(ppd, \"Tabloid\") returns NULL (not declared)");
  testEnd(ppdPageSize(ppd, "Tabloid") == NULL);

  // T11 — NULL name asks for the *marked* size.  ppdOpen() marks nothing, so
  //       before ppdMarkDefaults() no size is marked → NULL.
  testBegin("ppdPageSize(ppd, NULL) returns NULL before marking defaults");
  testEnd(ppdPageSize(ppd, NULL) == NULL);

  // T12 — After ppdMarkDefaults() the default PageSize (Letter) is marked, so
  //       the NULL-name "default" lookup returns the Letter record.
  testBegin("ppdPageSize(ppd, NULL) returns Letter after ppdMarkDefaults()");
  ppdMarkDefaults(ppd);
  size = ppdPageSize(ppd, NULL);
  testEndMessage(size != NULL && !strcmp(size->name, "Letter"),
                 "name=\"%s\"", size ? size->name : "(null)");


  // =========================================================================
  // Group 3: ppdPageWidth() / ppdPageLength() (T13–T17)
  //
  // Wrappers returning size->width / size->length, or 0.0 when ppdPageSize()
  // misses.  (Defaults are now marked from Group 2, so the NULL-name wrapper
  // resolves to Letter.)
  // =========================================================================

  // T13 — ppdPageWidth("Letter") → Letter width 612.
  testBegin("ppdPageWidth(ppd, \"Letter\") == 612");
  testEndMessage(ppdPageWidth(ppd, "Letter") == 612.0f,
                 "got %g", ppdPageWidth(ppd, "Letter"));

  // T14 — ppdPageLength("Letter") → Letter length 792.
  testBegin("ppdPageLength(ppd, \"Letter\") == 792");
  testEndMessage(ppdPageLength(ppd, "Letter") == 792.0f,
                 "got %g", ppdPageLength(ppd, "Letter"));

  // T15 — ppdPageWidth("A4") → A4 width 595.
  testBegin("ppdPageWidth(ppd, \"A4\") == 595");
  testEndMessage(ppdPageWidth(ppd, "A4") == 595.0f,
                 "got %g", ppdPageWidth(ppd, "A4"));

  // T16 — A not-found size yields 0.0 from the wrapper (ppdPageSize → NULL).
  testBegin("ppdPageLength(ppd, \"NoSuchSize\") == 0.0");
  testEndMessage(ppdPageLength(ppd, "NoSuchSize") == 0.0f,
                 "got %g", ppdPageLength(ppd, "NoSuchSize"));

  // T17 — NULL name resolves to the marked default (Letter), so the width
  //       wrapper returns 612.
  testBegin("ppdPageWidth(ppd, NULL) == 612 (marked default Letter)");
  testEndMessage(ppdPageWidth(ppd, NULL) == 612.0f,
                 "got %g", ppdPageWidth(ppd, NULL));


  // =========================================================================
  // Group 4: Custom.WIDTHxLENGTH parsing (T18–T25)
  //
  // With variable_sizes set and a "Custom." prefix, ppdPageSize() parses
  // WIDTHxLENGTH plus an optional unit, scales to points, applies
  // custom_margins {10,20,30,40} (right = w - margins[2], top = l -
  // margins[3]), and returns the shared "Custom" record.
  // =========================================================================

  // T18 — "Custom.500x600" (no unit → points) returns the "Custom" record.
  testBegin("ppdPageSize(ppd, \"Custom.500x600\") returns the \"Custom\" record");
  size = ppdPageSize(ppd, "Custom.500x600");
  testEndMessage(size != NULL && !strcmp(size->name, "Custom"),
                 "name=\"%s\"", size ? size->name : "(null)");

  // T19 — Points width/length come straight from the parsed numbers: 500x600.
  testBegin("ppdPageSize(ppd, \"Custom.500x600\") width/length 500x600");
  testEndMessage(size != NULL && size->width == 500.0f && size->length == 600.0f,
                 "w=%g l=%g", size ? size->width : -1, size ? size->length : -1);

  // T20 — Margins applied from custom_margins {10,20,30,40}: left 10,
  //       bottom 20, right = 500 - 30 = 470, top = 600 - 40 = 560.
  testBegin("ppdPageSize(ppd, \"Custom.500x600\") margins 10/20/470/560");
  testEndMessage(size != NULL &&
                 size->left == 10.0f && size->bottom == 20.0f &&
                 size->right == 470.0f && size->top == 560.0f,
                 "L=%g B=%g R=%g T=%g",
                 size ? size->left : -1, size ? size->bottom : -1,
                 size ? size->right : -1, size ? size->top : -1);

  // T21 — Inches suffix scales ×72 exactly: 8.5in = 612, 11in = 792.
  testBegin("ppdPageSize(ppd, \"Custom.8.5x11in\") width/length 612x792");
  size = ppdPageSize(ppd, "Custom.8.5x11in");
  testEndMessage(size != NULL && size->width == 612.0f && size->length == 792.0f,
                 "w=%g l=%g", size ? size->width : -1, size ? size->length : -1);

  // T22 — Millimetre suffix scales ×72/25.4.  Float rounding makes exact
  //       equality fragile, so we compare against the same computation with a
  //       small tolerance: 210mm ≈ 595.28 pt, 297mm ≈ 841.89 pt.
  testBegin("ppdPageSize(ppd, \"Custom.210x297mm\") width/length ~595.28x841.89");
  size = ppdPageSize(ppd, "Custom.210x297mm");
  mm_w = (float)(210.0 * 72.0 / 25.4);
  mm_l = (float)(297.0 * 72.0 / 25.4);
  testEndMessage(size != NULL &&
                 fabsf(size->width - mm_w) < 0.01f &&
                 fabsf(size->length - mm_l) < 0.01f,
                 "w=%g (exp %g) l=%g (exp %g)",
                 size ? size->width : -1, mm_w,
                 size ? size->length : -1, mm_l);

  // T23 — The width/length wrappers also drive the Custom path: 400x500 pt.
  testBegin("ppdPageWidth/Length(ppd, \"Custom.400x500\") == 400 / 500");
  testEndMessage(ppdPageWidth(ppd, "Custom.400x500") == 400.0f &&
                 ppdPageLength(ppd, "Custom.400x500") == 500.0f,
                 "w=%g l=%g", ppdPageWidth(ppd, "Custom.400x500"),
                 ppdPageLength(ppd, "Custom.400x500"));

  // T24 — A custom string with no 'x' separator fails the `*nameptr != 'x'`
  //       check and returns NULL.
  testBegin("ppdPageSize(ppd, \"Custom.500\") returns NULL (no 'x' separator)");
  testEnd(ppdPageSize(ppd, "Custom.500") == NULL);

  // T25 — The "Custom." prefix test is case-sensitive (strncmp), so a lower-
  //       case "custom.500x600" is NOT treated as a custom size; it falls
  //       through to a name lookup that finds nothing → NULL.
  testBegin("ppdPageSize(ppd, \"custom.500x600\") returns NULL (case-sensitive prefix)");
  testEnd(ppdPageSize(ppd, "custom.500x600") == NULL);


  // =========================================================================
  // Group 5: ppdPageSizeLimits() with variable sizes (T26–T31)
  //
  // Returns 1 and fills min/max from custom_min {36,72} and custom_max
  // {1000,2000}, with margins from custom_margins {10,20,30,40}
  // (right = w - 30, top = l - 40).  No cupsMediaQualifier2 attribute is
  // present, so the qualifier branches are skipped and the base values are
  // used directly.
  // =========================================================================

  // T26 — A variable-size PPD with valid output pointers returns 1.
  testBegin("ppdPageSizeLimits(ppd, &min, &max) returns 1");
  ret = ppdPageSizeLimits(ppd, &minsize, &maxsize);
  testEnd(ret == 1);

  // T27 — Minimum width/length come from custom_min: 36 × 72.
  testBegin("ppdPageSizeLimits min width/length 36x72");
  testEndMessage(minsize.width == 36.0f && minsize.length == 72.0f,
                 "w=%g l=%g", minsize.width, minsize.length);

  // T28 — Minimum margins: left 10, bottom 20, right = 36 - 30 = 6,
  //       top = 72 - 40 = 32.
  testBegin("ppdPageSizeLimits min margins 10/20/6/32");
  testEndMessage(minsize.left == 10.0f && minsize.bottom == 20.0f &&
                 minsize.right == 6.0f && minsize.top == 32.0f,
                 "L=%g B=%g R=%g T=%g",
                 minsize.left, minsize.bottom, minsize.right, minsize.top);

  // T29 — Maximum width/length come from custom_max: 1000 × 2000.
  testBegin("ppdPageSizeLimits max width/length 1000x2000");
  testEndMessage(maxsize.width == 1000.0f && maxsize.length == 2000.0f,
                 "w=%g l=%g", maxsize.width, maxsize.length);

  // T30 — Maximum margins: left 10, bottom 20, right = 1000 - 30 = 970,
  //       top = 2000 - 40 = 1960.
  testBegin("ppdPageSizeLimits max margins 10/20/970/1960");
  testEndMessage(maxsize.left == 10.0f && maxsize.bottom == 20.0f &&
                 maxsize.right == 970.0f && maxsize.top == 1960.0f,
                 "L=%g B=%g R=%g T=%g",
                 maxsize.left, maxsize.bottom, maxsize.right, maxsize.top);

  // T31 — A NULL minimum pointer fails the range check: the function returns
  //       0 and zeroes the (non-NULL) maximum record.
  testBegin("ppdPageSizeLimits(ppd, NULL, &max) returns 0 and zeroes max");
  maxsize.width = 999.0f;
  ret = ppdPageSizeLimits(ppd, NULL, &maxsize);
  testEnd(ret == 0 && maxsize.width == 0.0f);


  // =========================================================================
  // Group 6: no-variable-size PPD edge cases (T32–T35)
  //
  // Open a PPD with no custom page size.  variable_sizes is 0, so the limits
  // query returns 0 (zeroing both records) and Custom.WxH misses, while
  // standard named lookups still resolve.
  // =========================================================================

  testBegin("ppdOpen(embedded no-variable-size test PPD)");
  f = tmpfile();
  fputs(test_ppd_novar, f);
  rewind(f);
  ppd2 = ppdOpen(f);
  fclose(f);
  if (ppd2)
  {
    testEnd(true);
  }
  else
  {
    err = ppdLastError(&line);
    testEndMessage(false, "%s on line %d", ppdErrorString(err), line);
    ppdClose(ppd);
    return (1);
  }

  // T33 — !ppd->variable_sizes fails the range check: returns 0 and zeroes
  //       both output records.
  testBegin("ppdPageSizeLimits(ppd2, &min, &max) returns 0 and zeroes both");
  minsize.width = 11.0f;
  maxsize.width = 22.0f;
  ret = ppdPageSizeLimits(ppd2, &minsize, &maxsize);
  testEnd(ret == 0 && minsize.width == 0.0f && maxsize.width == 0.0f);

  // T34 — With variable_sizes 0 the "Custom." branch is skipped; the name
  //       lookup finds no "Custom.500x600" size → NULL.
  testBegin("ppdPageSize(ppd2, \"Custom.500x600\") returns NULL (no variable sizes)");
  testEnd(ppdPageSize(ppd2, "Custom.500x600") == NULL);

  // T35 — Standard named lookups still work in the no-variable PPD: Letter
  //       width is 612.
  testBegin("ppdPageSize(ppd2, \"Letter\")->width == 612");
  size = ppdPageSize(ppd2, "Letter");
  testEndMessage(size != NULL && size->width == 612.0f,
                 "w=%g", size ? size->width : -1);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdClose(ppd2);
  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
