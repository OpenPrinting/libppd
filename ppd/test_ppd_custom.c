//
// PPD custom-option API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (24 assertions across 4 groups):
//
//   Group 1  (T01-T04)  NULL/argument guards — every public function in
//                       ppd-custom.c starts with an `if (!ppd)` or
//                       `if (!opt)` short-circuit and must return NULL
//                       without dereferencing the NULL pointer.  These run
//                       before any PPD is opened so there is no dependency
//                       on PPD content.
//
//   Group 2  (T05-T11)  ppdFindCustomOption() — open the embedded PPD and
//                       locate custom options by keyword.  The coptions
//                       array comparator is _ppd_strcasecmp(), so the
//                       lookup is case-insensitive; the stored keyword
//                       preserves the PPD's casing.  We verify the two
//                       custom options the PPD declares (PageSize and
//                       Watermark), case-insensitive lookup returning the
//                       same record, and that a plain option with no
//                       *ParamCustom*/*Custom*…True line (PageRegion) and a
//                       wholly unknown keyword both return NULL.
//
//   Group 3  (T12-T19)  ppdFindCustomParam() — linear, case-insensitive
//                       (_ppd_strcasecmp) scan over a custom option's
//                       parameter array.  We confirm the returned
//                       ppd_cparam_t fields match the *ParamCustom* lines
//                       in the PPD: name, type enum, and the parsed
//                       minimum/maximum union members for POINTS, INT and
//                       STRING parameter types, plus the human-readable
//                       text field and the unknown-parameter miss.
//
//   Group 4  (T20-T24)  ppdFirstCustomParam()/ppdNextCustomParam() — the
//                       parameter array is created with a NULL comparator
//                       in ppd_get_coption(), so parameters are stored in
//                       *insertion* (PPD declaration) order rather than by
//                       their numeric *order* field.  We verify the first
//                       parameter, the full count via First/Next walking,
//                       the NULL terminator at end-of-array, and a single-
//                       parameter option.
//
//                       NOTE: ppdFirstCustomParam(), ppdNextCustomParam()
//                       and ppdFindCustomParam() all drive the *same*
//                       cupsArray internal cursor (GetFirst/GetNext).  A
//                       ppdFindCustomParam() call therefore disturbs an
//                       in-progress First/Next walk, so the iteration tests
//                       below never interleave a Find between First and
//                       Next.
//
// Design: the PPD is built entirely in memory from a single static string
// and loaded via tmpfile() + ppdOpen().  It declares exactly the custom
// machinery needed to exercise every branch:
//
//   • A standard CustomPageSize block (*VariablePaperSize: True with five
//     *ParamCustomPageSize parameters of two different types) — the canonical
//     multi-parameter custom option.
//   • A second, non-PageSize custom option "Watermark" carrying a single
//     STRING parameter with a human-readable translation string — proves
//     ppdFindCustomOption() finds more than one record and exercises the
//     STRING type plus the param->text field.
//
// No external files are required at build or CI time, making the binary
// fully hermetic.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//
// Minimal self-contained PPD content.
//
// Sections and what they exercise:
//
//   PPD header block (PPD-Adobe through TTRasterizer):
//     Mandatory metadata so ppdOpen() returns a valid ppd_file_t *.
//
//   PageSize / PageRegion / ImageableArea / PaperDimension:
//     A valid, spec-conformant media option pair.  PageRegion is declared
//     *without* any custom variant so it can serve as the "real option but
//     no custom option" negative case in T10.
//
//   VariablePaperSize + ParamCustomPageSize (×5) + CustomPageSize True:
//     Synthesises the "PageSize" custom option with five parameters.  The
//     declaration order is Width, Height, WidthOffset, HeightOffset,
//     Orientation — and because the parameter array uses a NULL comparator
//     that is exactly the order First/Next iteration must observe.  Width is
//     POINTS with min 36 / max 1000; Orientation is INT with min 0 / max 3.
//
//   Watermark option + CustomWatermark True + ParamCustomWatermark Text:
//     A second custom option whose single parameter "Text" is of STRING
//     type (min 0 / max 80) and carries the translation "Watermark Text",
//     so we can assert the param->text field and single-element iteration.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"CUSTTEST.PPD\"\n"
  "*Product: \"(CustomTest)\"\n"
  "*ModelName: \"PPD Custom Test Printer\"\n"
  "*ShortNickName: \"CustomTest\"\n"
  "*NickName: \"PPD Custom Test Printer\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  // PageSize
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  // PageRegion (parallel of PageSize — required by Adobe PPD spec; declared
  // WITHOUT any custom variant so it is the "no custom option" negative case)
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  // ImageableArea + PaperDimension (required for a valid PPD)
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  // Custom page size — synthesises the "PageSize" custom option with five
  // parameters in this exact declaration order: Width, Height, WidthOffset,
  // HeightOffset, Orientation.
  "*VariablePaperSize: True\n"
  "*ParamCustomPageSize Width: 1 points 36 1000\n"
  "*ParamCustomPageSize Height: 2 points 36 1000\n"
  "*ParamCustomPageSize WidthOffset: 3 points 0 0\n"
  "*ParamCustomPageSize HeightOffset: 4 points 0 0\n"
  "*ParamCustomPageSize Orientation: 5 int 0 3\n"
  "*MaxMediaWidth: \"1000\"\n"
  "*MaxMediaHeight: \"1000\"\n"
  "*HWMargins: 18 18 18 18\n"
  "*CustomPageSize True: \"pop pop pop pop pop\"\n"
  // Watermark — a second custom option with a single STRING parameter that
  // carries a human-readable translation string ("Watermark Text").
  "*OpenUI *Watermark/Watermark: PickOne\n"
  "*OrderDependency: 60 AnySetup *Watermark\n"
  "*DefaultWatermark: None\n"
  "*Watermark None/None: \"\"\n"
  "*CloseUI: *Watermark\n"
  "*CustomWatermark True: \"pop\"\n"
  "*ParamCustomWatermark Text/Watermark Text: 1 string 0 80\n";


//
// 'main()' - Run all ppd-custom.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// PPD file handle
  ppd_coption_t *copt;			// Returned by ppdFindCustomOption()
  ppd_coption_t *copt2;			// Second lookup for pointer-identity test
  ppd_coption_t *wmark;			// The "Watermark" custom option
  ppd_cparam_t *param;			// Returned by ppdFind/First/NextCustomParam()
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error
  int           count;			// Iteration counter
  bool          saw_orientation;	// Iteration sentinel


  // =========================================================================
  // Group 1: NULL/argument guards (T01–T04)
  //
  // Each public function in ppd-custom.c opens with a NULL guard
  // (`if (!ppd)` for ppdFindCustomOption, `if (!opt)` for the other three)
  // and must return NULL without dereferencing the pointer.  These run
  // before any PPD is opened so there is no dependency on PPD content.
  // =========================================================================

  // T01 — ppdFindCustomOption: `if (!ppd) return (NULL);` — NULL PPD must
  //       short-circuit before strlcpy()/cupsArrayFind().
  testBegin("ppdFindCustomOption(NULL ppd, \"PageSize\") returns NULL");
  testEnd(ppdFindCustomOption(NULL, "PageSize") == NULL);

  // T02 — ppdFindCustomParam: `if (!opt) return (NULL);` — NULL option must
  //       short-circuit before iterating opt->params.
  testBegin("ppdFindCustomParam(NULL opt, \"Width\") returns NULL");
  testEnd(ppdFindCustomParam(NULL, "Width") == NULL);

  // T03 — ppdFirstCustomParam: `if (!opt) return (NULL);` — NULL option.
  testBegin("ppdFirstCustomParam(NULL) returns NULL");
  testEnd(ppdFirstCustomParam(NULL) == NULL);

  // T04 — ppdNextCustomParam: `if (!opt) return (NULL);` — NULL option.
  testBegin("ppdNextCustomParam(NULL) returns NULL");
  testEnd(ppdNextCustomParam(NULL) == NULL);


  // =========================================================================
  // Group 2: ppdFindCustomOption (T05–T11)
  //
  // Open the embedded PPD via tmpfile().  The custom options are built by
  // the parser as it processes *ParamCustom*/*Custom*…True lines and stored
  // in ppd->coptions, an array whose comparator is _ppd_strcasecmp() — so
  // lookups are case-insensitive while the stored keyword preserves casing.
  // =========================================================================

  testBegin("ppdOpen(embedded custom test PPD)");
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

  // T06 — The CustomPageSize block declared *ParamCustomPageSize, so the
  //       parser created a "PageSize" custom option record.
  testBegin("ppdFindCustomOption(\"PageSize\") returns non-NULL");
  copt = ppdFindCustomOption(ppd, "PageSize");
  testEnd(copt != NULL);

  // T07 — The stored keyword preserves the PPD's casing (the comparator is
  //       case-insensitive but strlcpy() copied the keyword verbatim).
  testBegin("ppdFindCustomOption(\"PageSize\")->keyword == \"PageSize\"");
  testEnd(copt != NULL && !strcmp(copt->keyword, "PageSize"));

  // T08 — Case-insensitive lookup: searching with all-lowercase "pagesize"
  //       resolves to the *same* record pointer as "PageSize" because the
  //       array comparator is _ppd_strcasecmp().
  testBegin("ppdFindCustomOption(\"pagesize\") finds the same record");
  copt2 = ppdFindCustomOption(ppd, "pagesize");
  testEndMessage(copt2 == copt, "%p vs %p", (void *)copt2, (void *)copt);

  // T09 — The PPD also declares *CustomWatermark/*ParamCustomWatermark, so a
  //       second, independent custom option "Watermark" must exist.
  testBegin("ppdFindCustomOption(\"Watermark\") returns non-NULL");
  wmark = ppdFindCustomOption(ppd, "Watermark");
  testEnd(wmark != NULL);

  // T10 — PageRegion is a real PPD option but carries NO *ParamCustom* or
  //       *CustomPageRegion…True line, so no custom option record was ever
  //       created for it → cupsArrayFind() returns NULL.
  testBegin("ppdFindCustomOption(\"PageRegion\") returns NULL (no custom variant)");
  testEnd(ppdFindCustomOption(ppd, "PageRegion") == NULL);

  // T11 — A keyword that appears nowhere in the PPD returns NULL.
  testBegin("ppdFindCustomOption(\"NoSuchOption\") returns NULL");
  testEnd(ppdFindCustomOption(ppd, "NoSuchOption") == NULL);


  // =========================================================================
  // Group 3: ppdFindCustomParam (T12–T19)
  //
  // ppdFindCustomParam() walks opt->params with cupsArrayGetFirst/GetNext
  // and matches on param->name using _ppd_strcasecmp().  Each test confirms
  // one observable field of the returned ppd_cparam_t against the
  // *ParamCustom* line that produced it.
  // =========================================================================

  // T12 — "Width" is the first parameter of the PageSize custom option.
  testBegin("ppdFindCustomParam(PageSize, \"Width\") returns non-NULL");
  param = ppdFindCustomParam(copt, "Width");
  testEnd(param != NULL);

  // T13 — The returned record's name field equals the requested name.
  testBegin("ppdFindCustomParam(PageSize, \"Width\")->name == \"Width\"");
  testEndMessage(param != NULL && !strcmp(param->name, "Width"),
                 "got \"%s\"", param ? param->name : "(null)");

  // T14 — "*ParamCustomPageSize Width: 1 points 36 1000" → the "points"
  //       data type maps to the PPD_CUSTOM_POINTS enum value.
  testBegin("ppdFindCustomParam(PageSize, \"Width\")->type == PPD_CUSTOM_POINTS");
  testEnd(param != NULL && param->type == PPD_CUSTOM_POINTS);

  // T15 — For a POINTS parameter the limits live in the custom_points union
  //       members; the PPD line declares minimum 36 and maximum 1000.
  testBegin("ppdFindCustomParam(PageSize, \"Width\") points limits 36..1000");
  testEndMessage(param != NULL &&
                 param->minimum.custom_points == 36.0f &&
                 param->maximum.custom_points == 1000.0f,
                 "min=%g max=%g",
                 param ? param->minimum.custom_points : -1.0,
                 param ? param->maximum.custom_points : -1.0);

  // T16 — Name matching is case-insensitive (_ppd_strcasecmp), so searching
  //       for lowercase "width" still resolves the "Width" parameter.
  testBegin("ppdFindCustomParam(PageSize, \"width\") matches case-insensitively");
  testEnd(ppdFindCustomParam(copt, "width") != NULL);

  // T17 — "*ParamCustomPageSize Orientation: 5 int 0 3" → INT type whose
  //       limits live in the custom_int union members (min 0, max 3).
  testBegin("ppdFindCustomParam(PageSize, \"Orientation\") is INT 0..3");
  param = ppdFindCustomParam(copt, "Orientation");
  testEndMessage(param != NULL &&
                 param->type == PPD_CUSTOM_INT &&
                 param->minimum.custom_int == 0 &&
                 param->maximum.custom_int == 3,
                 "type=%d min=%d max=%d",
                 param ? (int)param->type : -1,
                 param ? param->minimum.custom_int : -1,
                 param ? param->maximum.custom_int : -1);

  // T18 — A parameter name that does not exist exhausts the scan → NULL.
  testBegin("ppdFindCustomParam(PageSize, \"NoSuchParam\") returns NULL");
  testEnd(ppdFindCustomParam(copt, "NoSuchParam") == NULL);

  // T19 — The Watermark option's single parameter "Text" is STRING-typed
  //       (limits in custom_string: 0..80) and carries the translation text
  //       "Watermark Text" given after the slash in the *ParamCustom* line.
  testBegin("ppdFindCustomParam(Watermark, \"Text\") is STRING 0..80 with text");
  param = ppdFindCustomParam(wmark, "Text");
  testEndMessage(param != NULL &&
                 param->type == PPD_CUSTOM_STRING &&
                 param->minimum.custom_string == 0 &&
                 param->maximum.custom_string == 80 &&
                 !strcmp(param->text, "Watermark Text"),
                 "type=%d min=%d max=%d text=\"%s\"",
                 param ? (int)param->type : -1,
                 param ? param->minimum.custom_string : -1,
                 param ? param->maximum.custom_string : -1,
                 param ? param->text : "(null)");


  // =========================================================================
  // Group 4: ppdFirstCustomParam / ppdNextCustomParam iteration (T20–T24)
  //
  // The parameter array is created in ppd_get_coption() with a NULL
  // comparator, so cupsArrayAdd() appends in declaration order.  Iteration
  // therefore yields the parameters in the order the *ParamCustom* lines
  // appear in the PPD: Width, Height, WidthOffset, HeightOffset, Orientation.
  //
  // First/Next/Find all share the array's single internal cursor, so the
  // tests below perform a clean First-then-Next walk with NO interleaved
  // ppdFindCustomParam() calls (which would reset/advance that same cursor).
  // =========================================================================

  // T20 — ppdFirstCustomParam() resets the cursor to the head and returns
  //       the first element — non-NULL for the five-parameter PageSize.
  testBegin("ppdFirstCustomParam(PageSize) returns non-NULL");
  param = ppdFirstCustomParam(copt);
  testEnd(param != NULL);

  // T21 — Because the array preserves declaration order, the first parameter
  //       is "Width" (the first *ParamCustomPageSize line), NOT the param
  //       with the lowest numeric order value (they happen to coincide here,
  //       but the guarantee is insertion order).
  testBegin("ppdFirstCustomParam(PageSize)->name == \"Width\"");
  testEndMessage(param != NULL && !strcmp(param->name, "Width"),
                 "got \"%s\"", param ? param->name : "(null)");

  // T22 — Walk First()→Next()…→NULL and count the parameters.  The PPD
  //       declares exactly five PageSize parameters, and "Orientation" (the
  //       last one) must be encountered during the walk.
  testBegin("First/Next iterate exactly 5 PageSize parameters");
  count          = 0;
  saw_orientation = false;
  for (param = ppdFirstCustomParam(copt); param != NULL;
       param = ppdNextCustomParam(copt))
  {
    count++;
    if (!strcmp(param->name, "Orientation"))
      saw_orientation = true;
  }
  testEndMessage(count == 5 && saw_orientation,
                 "count=%d saw_orientation=%s",
                 count, saw_orientation ? "true" : "false");

  // T23 — After the loop above the cursor sits past the last element, so a
  //       further ppdNextCustomParam() returns NULL without crashing.
  testBegin("ppdNextCustomParam(PageSize) returns NULL at end of array");
  testEnd(ppdNextCustomParam(copt) == NULL);

  // T24 — The Watermark option has a single parameter, so ppdFirstCustomParam
  //       returns it (non-NULL) and the immediately following
  //       ppdNextCustomParam returns NULL (no second element).
  testBegin("ppdFirstCustomParam(Watermark) has exactly one parameter");
  param = ppdFirstCustomParam(wmark);
  testEnd(param != NULL && ppdNextCustomParam(wmark) == NULL);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
