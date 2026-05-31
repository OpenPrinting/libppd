//
// PPD IPP API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (69 assertions across 13 groups):
//
//   Group 1  (T01)        ppdLoadAttributes() NULL-pointer guard — the
//                         function's first defensive check.
//   Group 2  (T02-T03)    PPD setup + successful ppdLoadAttributes() —
//                         loads the embedded PPD and verifies an
//                         ipp_t* attribute set is returned.
//   Group 3  (T04-T08)    Scalar printer attributes — color-supported,
//                         copies-default/-supported, pages-per-minute,
//                         and absence of pages-per-minute-color when
//                         ColorDevice is False.
//   Group 4  (T09-T13)    Orientation, print-quality, and finishings
//                         enum defaults/supported sets.
//   Group 5  (T14-T17)    Booleans + color/content keywords —
//                         page-ranges-supported, print-color-mode-*,
//                         print-content-optimize-default.
//   Group 6  (T18)        overrides-supported static list.
//   Group 7  (T19-T26)    Media defaults, supported lists, size-supported
//                         collection, col-database, col-default — exercises
//                         create_media_col(), create_media_size(), and
//                         create_media_size_ranges() indirectly.
//   Group 8  (T27-T34)    Media sources / types / margins — verifies the
//                         PPD-to-PWG cache mappings and de-duplicated
//                         margin lists.
//   Group 9  (T35-T38)    Output bin and Sides — including the duplex
//                         option discovered by ppdCacheCreateWithPPD().
//   Group 10 (T39-T44)    Finishing-template, finishings-col-*, and
//                         print-rendering-intent-* default/supported.
//   Group 11 (T45-T54)    Printer identity — printer-info / -make-and-model
//                         / -device-id (MFG/MDL/CMD), printer-resolution-*,
//                         printer-input-tray, printer-output-tray, and
//                         document-format-supported synthesised from the
//                         PPD-lacking-filter fallback path.
//   Group 12 (T55-T56)    ppdGetOptions() — empty-input behaviour with
//                         and without a PPD.
//   Group 13 (T57-T69)    ppdGetOptions() — every option-extraction code
//                         path: media keyword, media-default fallback,
//                         media-col with size-name + source + type,
//                         media-col with media-size dimensions, output-bin,
//                         all three sides values, and the print-quality /
//                         finishings enum branches.
//
// Design: entirely hermetic.  A minimal static PPD with Letter+A4 sizes,
// a Custom paper range, InputSlot (Cassette/Upper), MediaType (Plain/Gloss),
// OutputBin (StandardBin/FaceUp) and Duplex (None/DuplexNoTumble/DuplexTumble)
// is loaded via tmpfile() + ppdOpen().  No external file dependencies and
// no locale assumptions.  ColorDevice is False so all color-related branches
// take the monochrome path deterministically.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//
// Minimal self-contained PPD content.
//
// Page sizes:
//   Letter + A4 give pc->num_sizes == 2.
//   VariablePaperSize/ParamCustomPageSize/HWMargins/CustomPageSize enable
//   ppd->variable_sizes, which adds the custom min/max keywords to
//   "media-supported" (+2 entries → count 4) and one custom range collection
//   to "media-size-supported" (+1 entry → count 3).
//
// InputSlot Cassette / Upper:
//   "Cassette" → PWG "main" via the hardcoded standard_sources table.
//   "Upper"    → PWG "top"  via the hardcoded standard_sources table.
//
// MediaType Plain / Gloss:
//   "Plain" → PWG "stationery"          via standard_types prefix match.
//   "Gloss" → PWG "photographic-glossy" via standard_types prefix match.
//
// OutputBin StandardBin / FaceUp:
//   Both fall through to ppdPwgUnppdizeName():
//     "StandardBin" → "standard-bin"
//     "FaceUp"      → "face-up"
//
// Duplex None / DuplexNoTumble / DuplexTumble:
//   Recognised by ppdCacheCreateWithPPD() so that:
//     pc->sides_option       == "Duplex"
//     pc->sides_1sided       == "None"
//     pc->sides_2sided_long  == "DuplexNoTumble"
//     pc->sides_2sided_short == "DuplexTumble"
//   DefaultDuplex is None so sides-default falls through to "one-sided".
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"IPPTEST.PPD\"\n"
  "*Manufacturer: \"TestCo\"\n"
  "*Product: \"(IppTest)\"\n"
  "*ModelName: \"IPP Test Printer\"\n"
  "*ShortNickName: \"IppTest\"\n"
  "*NickName: \"IPP Test Printer\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"20\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  "*DefaultResolution: 600dpi\n"
  // Page sizes — Letter + A4
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
  "*PaperDimension A4: \"595 842\"\n"
  // Custom paper range — enables ppd->variable_sizes
  "*VariablePaperSize: True\n"
  "*ParamCustomPageSize Width: 1 points 36 1000\n"
  "*ParamCustomPageSize Height: 2 points 36 1000\n"
  "*ParamCustomPageSize WidthOffset: 3 points 0 0\n"
  "*ParamCustomPageSize HeightOffset: 4 points 0 0\n"
  "*ParamCustomPageSize Orientation: 5 int 0 3\n"
  "*HWMargins: 18 18 18 18\n"
  "*CustomPageSize True: \"pop\"\n"
  // InputSlot — hardcoded PPD-choice → PWG keyword table
  "*OpenUI *InputSlot/Paper Source: PickOne\n"
  "*OrderDependency: 20 AnySetup *InputSlot\n"
  "*DefaultInputSlot: Cassette\n"
  "*InputSlot Cassette/Main Tray: \"\"\n"
  "*InputSlot Upper/Upper Tray: \"\"\n"
  "*CloseUI: *InputSlot\n"
  // MediaType — standard_types prefix-match table
  "*OpenUI *MediaType/Media Type: PickOne\n"
  "*OrderDependency: 30 AnySetup *MediaType\n"
  "*DefaultMediaType: Plain\n"
  "*MediaType Plain/Plain Paper: \"\"\n"
  "*MediaType Gloss/Glossy Photo: \"\"\n"
  "*CloseUI: *MediaType\n"
  // OutputBin — ppdPwgUnppdizeName() conversion
  "*OpenUI *OutputBin/Output Bin: PickOne\n"
  "*OrderDependency: 40 AnySetup *OutputBin\n"
  "*DefaultOutputBin: StandardBin\n"
  "*OutputBin StandardBin/Standard Bin: \"\"\n"
  "*OutputBin FaceUp/Face Up: \"\"\n"
  "*CloseUI: *OutputBin\n"
  // Duplex — fills pc->sides_option / _1sided / _2sided_long / _2sided_short
  "*OpenUI *Duplex/Two-Sided: PickOne\n"
  "*OrderDependency: 50 AnySetup *Duplex\n"
  "*DefaultDuplex: None\n"
  "*Duplex None/Off: \"<</Duplex false>>setpagedevice\"\n"
  "*Duplex DuplexNoTumble/Long Edge: \"<</Duplex true/Tumble false>>setpagedevice\"\n"
  "*Duplex DuplexTumble/Short Edge: \"<</Duplex true/Tumble true>>setpagedevice\"\n"
  "*CloseUI: *Duplex\n";


//
// 'load_test_ppd()' - Open the embedded PPD text via tmpfile() + ppdOpen().
//
// tmpfile() creates an anonymous, auto-deleted temporary file; we write the
// PPD text, rewind, and hand the FILE* straight to ppdOpen() so the test
// has no on-disk filesystem dependencies.  On parse failure the PPD
// error/line is forwarded to stderr via testError() and NULL is returned
// so main() can abort early.
//

static ppd_file_t *
load_test_ppd(void)
{
  FILE         *f;			// Temporary FILE
  ppd_file_t   *ppd;			// Parsed PPD handle
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Error line number

  f = tmpfile();
  fputs(test_ppd_text, f);
  rewind(f);
  ppd = ppdOpen(f);
  fclose(f);

  if (!ppd)
  {
    err = ppdLastError(&line);
    testError("ppdOpen failed: %s on line %d", ppdErrorString(err), line);
  }
  return (ppd);
}


//
// 'attr_has_value()' - Return 1 if the named keyword/string attribute of
//                      'ipp' contains 'value' at any index, 0 otherwise.
//
// Used to make multi-valued assertions (e.g. "media-supported contains
// na_letter_8.5x11in") position-independent so the test does not depend
// on the internal ordering of cups_array_t iterators.
//

static int
attr_has_value(ipp_t *ipp, const char *name, const char *value)
{
  ipp_attribute_t *attr;		// Located attribute
  int              i, count;		// Iteration variables
  const char      *s;			// Current string value

  attr = ippFindAttribute(ipp, name, IPP_TAG_ZERO);
  if (!attr)
    return (0);

  count = ippGetCount(attr);
  for (i = 0; i < count; i ++)
  {
    s = ippGetString(attr, i, NULL);
    if (s && !strcmp(s, value))
      return (1);
  }
  return (0);
}


//
// 'main()' - Run all ppd-ipp.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t      *ppd;			// PPD file handle
  ipp_t           *attrs;		// IPP attributes returned by ppdLoadAttributes()
  ipp_t           *job_attrs;		// IPP job attribute set built for ppdGetOptions()
  ipp_attribute_t *attr;		// Generic ipp attribute pointer
  cups_option_t   *options;		// Output option list from ppdGetOptions()
  int              num_options;		// Number of options returned
  const char      *val;			// Generic string value pointer


  // =========================================================================
  // Group 1: ppdLoadAttributes() NULL-pointer guard (T01)
  //
  // ppdLoadAttributes() begins with:
  //   if (ppd == NULL) return (NULL);
  // This single assertion exercises that early-exit guard before any
  // PPD-dependent state is constructed.
  // =========================================================================

  // T01 — Passing NULL must yield NULL with no side effects (no allocation,
  //       no crash, no global state mutation).
  testBegin("ppdLoadAttributes(NULL ppd) returns NULL");
  testEnd(ppdLoadAttributes(NULL) == NULL);


  // =========================================================================
  // Group 2: PPD setup + successful ppdLoadAttributes() (T02-T03)
  //
  // Load the embedded PPD and run ppdLoadAttributes() once.  All groups 3-11
  // below assert against the single ipp_t* it returns.  Abort early on any
  // failure here because the rest of the test cannot proceed.
  // =========================================================================

  // T02 — The embedded PPD parses cleanly.  Any parse error indicates a
  //       PPD-text bug in this test, not a libppd bug, so we surface the
  //       exact error string via testEndMessage / testError.
  testBegin("ppdOpen(embedded test PPD)");
  ppd = load_test_ppd();
  if (!ppd)
  {
    testEnd(false);
    return (1);
  }
  testEnd(true);

  // T03 — ppdLoadAttributes() returns a non-NULL ipp_t* and internally
  //       builds a ppd_cache_t via ppdCacheCreateWithPPD().  All subsequent
  //       group-3..11 assertions inspect the returned 'attrs' object.
  testBegin("ppdLoadAttributes(ppd) returns non-NULL");
  attrs = ppdLoadAttributes(ppd);
  if (!attrs)
  {
    testEnd(false);
    ppdClose(ppd);
    return (1);
  }
  testEnd(true);


  // =========================================================================
  // Group 3: Scalar printer attributes (T04-T08)
  //
  // Verifies the simple-typed printer attributes that are derived directly
  // from PPD scalar fields:
  //
  //   color-supported          ← ppd->color_device (False → 0)
  //   copies-default           ← DefaultCopies or 1 fallback
  //   copies-supported         ← range 1..pc->max_copies (or 999)
  //   pages-per-minute         ← ppd->throughput (we set Throughput "20")
  //   pages-per-minute-color   ← only added when ppd->color_device is True
  // =========================================================================

  // T04 — ColorDevice: False in the PPD → color-supported boolean is 0.
  testBegin("attrs contains color-supported == false");
  attr = ippFindAttribute(attrs, "color-supported", IPP_TAG_BOOLEAN);
  testEnd(attr != NULL && ippGetBoolean(attr, 0) == 0);

  // T05 — No DefaultCopies in the PPD and no marked Copies choice → the
  //       fallback `i = 1` branch is taken.
  testBegin("attrs contains copies-default == 1");
  attr = ippFindAttribute(attrs, "copies-default", IPP_TAG_INTEGER);
  testEndMessage(attr != NULL && ippGetInteger(attr, 0) == 1,
                 "got %d", attr ? ippGetInteger(attr, 0) : -1);

  // T06 — copies-supported is always a single ipp range (lower..upper).
  //       The ipp_attribute_t::count for a range is 1 even though the
  //       value encodes two integers.
  testBegin("attrs contains copies-supported range");
  attr = ippFindAttribute(attrs, "copies-supported", IPP_TAG_RANGE);
  testEnd(attr != NULL && ippGetCount(attr) == 1);

  // T07 — Throughput "20" in the PPD → ppd->throughput == 20 → the integer
  //       value is forwarded verbatim to pages-per-minute.
  testBegin("attrs contains pages-per-minute == 20");
  attr = ippFindAttribute(attrs, "pages-per-minute", IPP_TAG_INTEGER);
  testEndMessage(attr != NULL && ippGetInteger(attr, 0) == 20,
                 "got %d", attr ? ippGetInteger(attr, 0) : -1);

  // T08 — pages-per-minute-color is only added when ppd->color_device is
  //       non-zero.  Our PPD is ColorDevice: False, so the attribute must
  //       be absent (this exercises the `if (ppd->color_device)` branch
  //       not taken).
  testBegin("attrs does not contain pages-per-minute-color");
  attr = ippFindAttribute(attrs, "pages-per-minute-color", IPP_TAG_INTEGER);
  testEnd(attr == NULL);


  // =========================================================================
  // Group 4: Orientation, print-quality, and finishings defaults (T09-T13)
  //
  // These are hard-coded constant attributes in ppdLoadAttributes():
  //   orientation-requested-default       = IPP_ORIENT_PORTRAIT
  //   orientation-requested-supported     = {PORT, LAND, REV_LAND, REV_PORT}
  //   print-quality-default               = IPP_QUALITY_NORMAL
  //   print-quality-supported             = {DRAFT, NORMAL, HIGH}
  //   finishings-default                  = IPP_FINISHINGS_NONE
  // The PPD has no cupsIPPFinishings entries, so finishings-supported
  // contains only the synthetic NONE entry.
  // =========================================================================

  // T09 — Hard-coded default orientation.
  testBegin("attrs contains orientation-requested-default == IPP_ORIENT_PORTRAIT");
  attr = ippFindAttribute(attrs, "orientation-requested-default", IPP_TAG_ENUM);
  testEnd(attr != NULL && ippGetInteger(attr, 0) == IPP_ORIENT_PORTRAIT);

  // T10 — orientation-requested-supported is built from a static
  //       four-element array in ppd-ipp.c.
  testBegin("attrs contains orientation-requested-supported with 4 entries");
  attr = ippFindAttribute(attrs, "orientation-requested-supported", IPP_TAG_ENUM);
  testEndMessage(attr != NULL && ippGetCount(attr) == 4,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T11 — Hard-coded default print quality.
  testBegin("attrs contains print-quality-default == IPP_QUALITY_NORMAL");
  attr = ippFindAttribute(attrs, "print-quality-default", IPP_TAG_ENUM);
  testEnd(attr != NULL && ippGetInteger(attr, 0) == IPP_QUALITY_NORMAL);

  // T12 — print-quality-supported is a static three-element array.
  testBegin("attrs contains print-quality-supported with 3 entries");
  attr = ippFindAttribute(attrs, "print-quality-supported", IPP_TAG_ENUM);
  testEndMessage(attr != NULL && ippGetCount(attr) == 3,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T13 — finishings-default is hard-coded to IPP_FINISHINGS_NONE.
  testBegin("attrs contains finishings-default == IPP_FINISHINGS_NONE");
  attr = ippFindAttribute(attrs, "finishings-default", IPP_TAG_ENUM);
  testEnd(attr != NULL && ippGetInteger(attr, 0) == IPP_FINISHINGS_NONE);


  // =========================================================================
  // Group 5: Booleans, color-mode, content-optimize keywords (T14-T17)
  //
  // Covers:
  //   page-ranges-supported           = true (hard-coded)
  //   print-color-mode-default        = "monochrome" when ColorDevice is False
  //                                     (the `ppd->color_device && !mono`
  //                                      condition is false → fallback)
  //   print-color-mode-supported      = single-element {"monochrome"} when
  //                                     ColorDevice is False (the mono-only
  //                                     branch of the if/else)
  //   print-content-optimize-default  = "auto" (hard-coded)
  // =========================================================================

  // T14 — page-ranges-supported is unconditionally true.
  testBegin("attrs contains page-ranges-supported == true");
  attr = ippFindAttribute(attrs, "page-ranges-supported", IPP_TAG_BOOLEAN);
  testEnd(attr != NULL && ippGetBoolean(attr, 0) == 1);

  // T15 — Mono device → default is "monochrome".
  testBegin("attrs contains print-color-mode-default == monochrome");
  attr = ippFindAttribute(attrs, "print-color-mode-default", IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && !strcmp(val, "monochrome"),
                 "got \"%s\"", val ? val : "(null)");

  // T16 — The mono branch of print-color-mode-supported has exactly one
  //       entry; the color branch would have three (auto/color/monochrome).
  testBegin("attrs contains print-color-mode-supported with 1 entry");
  attr = ippFindAttribute(attrs, "print-color-mode-supported", IPP_TAG_KEYWORD);
  testEndMessage(attr != NULL && ippGetCount(attr) == 1,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T17 — print-content-optimize-default is the constant "auto".
  testBegin("attrs contains print-content-optimize-default == auto");
  attr = ippFindAttribute(attrs, "print-content-optimize-default",
                          IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && !strcmp(val, "auto"),
                 "got \"%s\"", val ? val : "(null)");


  // =========================================================================
  // Group 6: overrides-supported (T18)
  //
  // overrides-supported is generated from a five-element static
  // const char *const overrides_supported[] = {
  //   "document-numbers", "media", "media-col", "orientation-requested",
  //   "pages"
  // };
  // =========================================================================

  // T18 — Exactly 5 entries; this catches accidental truncation if the
  //       static array is shortened in the future.
  testBegin("attrs contains overrides-supported with 5 entries");
  attr = ippFindAttribute(attrs, "overrides-supported", IPP_TAG_KEYWORD);
  testEndMessage(attr != NULL && ippGetCount(attr) == 5,
                 "count=%d", attr ? ippGetCount(attr) : -1);


  // =========================================================================
  // Group 7: Media defaults, supported list, size-supported, col-database
  //          (T19-T26)
  //
  // Exercises the largest block of ppdLoadAttributes() and indirectly the
  // helpers create_media_col() and create_media_size_ranges().
  //
  // With our PPD:
  //   pc->num_sizes        == 2   (Letter, A4 — "Custom" is excluded)
  //   ppd->variable_sizes  == 1   (set by *CustomPageSize)
  //
  // Therefore:
  //   media-supported       count = pc->num_sizes + 2 = 4
  //                                 (Letter, A4, custom_min, custom_max)
  //   media-size-supported  count = pc->num_sizes + 1 = 3
  //                                 (Letter, A4, custom-range collection)
  //   media-col-database    count = pc->num_sizes     = 2  (no custom entry)
  //   media-default / media-ready = "na_letter_8.5x11in" (default size)
  // =========================================================================

  // T19 — Default size Letter → PWG mapping "na_letter_8.5x11in".
  testBegin("attrs contains media-default == na_letter_8.5x11in");
  attr = ippFindAttribute(attrs, "media-default", IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && !strcmp(val, "na_letter_8.5x11in"),
                 "got \"%s\"", val ? val : "(null)");

  // T20 — media-ready mirrors media-default in ppdLoadAttributes()
  //       when no custom default size is forced.
  testBegin("attrs contains media-ready == na_letter_8.5x11in");
  attr = ippFindAttribute(attrs, "media-ready", IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEnd(val != NULL && !strcmp(val, "na_letter_8.5x11in"));

  // T21 — Letter must be present in the media-supported list.
  testBegin("attrs contains media-supported with Letter");
  testEnd(attr_has_value(attrs, "media-supported", "na_letter_8.5x11in"));

  // T22 — A4 must also be present in the media-supported list.
  testBegin("attrs contains media-supported with A4");
  testEnd(attr_has_value(attrs, "media-supported", "iso_a4_210x297mm"));

  // T23 — Count is num_sizes (2) + 2 custom min/max keywords because
  //       ppd->variable_sizes is set by *CustomPageSize.
  testBegin("attrs contains media-supported count == 4");
  attr = ippFindAttribute(attrs, "media-supported", IPP_TAG_KEYWORD);
  testEndMessage(attr != NULL && ippGetCount(attr) == 4,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T24 — media-size-supported = pc->num_sizes (2) + 1 range collection
  //       built by create_media_size_ranges().
  testBegin("attrs contains media-size-supported count == 3");
  attr = ippFindAttribute(attrs, "media-size-supported",
                          IPP_TAG_BEGIN_COLLECTION);
  testEndMessage(attr != NULL && ippGetCount(attr) == 3,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T25 — media-col-database = pc->num_sizes (custom range is NOT added
  //       to media-col-database; only to media-size-supported).
  testBegin("attrs contains media-col-database count == 2");
  attr = ippFindAttribute(attrs, "media-col-database",
                          IPP_TAG_BEGIN_COLLECTION);
  testEndMessage(attr != NULL && ippGetCount(attr) == 2,
                 "count=%d", attr ? ippGetCount(attr) : -1);

  // T26 — media-col-default is a single collection holding the default
  //       size's media-col record built by create_media_col().
  testBegin("attrs contains media-col-default");
  attr = ippFindAttribute(attrs, "media-col-default",
                          IPP_TAG_BEGIN_COLLECTION);
  testEnd(attr != NULL);


  // =========================================================================
  // Group 8: Media sources, types, and margins (T27-T34)
  //
  // media-source-supported / media-type-supported come from the
  // ppd_cache_t's pwg_map_t arrays built by ppdCacheCreateWithPPD()
  // (Cassette→main, Plain→stationery, etc.).
  //
  // media-{bottom,left,right,top}-margin-supported are populated by four
  // separate de-duplicate-and-sort loops over pc->sizes[].  Since both
  // Letter and A4 share 18pt → ~635 unit margins in our PPD, each list
  // ends up with at least one entry (we just assert "non-empty" — the
  // exact deduplicated values depend on libppd's rounding).
  // =========================================================================

  // T27 — media-source-supported is built from pc->sources[].
  testBegin("attrs contains media-source-supported");
  attr = ippFindAttribute(attrs, "media-source-supported", IPP_TAG_KEYWORD);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  // T28 — "Cassette" maps to PWG "main" via the standard_sources table.
  testBegin("attrs contains media-source-supported with main");
  testEnd(attr_has_value(attrs, "media-source-supported", "main"));

  // T29 — media-type-supported is built from pc->types[].
  testBegin("attrs contains media-type-supported");
  attr = ippFindAttribute(attrs, "media-type-supported", IPP_TAG_KEYWORD);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  // T30 — "Plain" maps to PWG "stationery" via the standard_types table.
  testBegin("attrs contains media-type-supported with stationery");
  testEnd(attr_has_value(attrs, "media-type-supported", "stationery"));

  // T31..T34 — Each margin list is computed via a de-dup-and-sort loop.
  //            We only assert non-empty here because the exact value set
  //            depends on libppd's PWG margin rounding internals.

  testBegin("attrs contains media-bottom-margin-supported");
  attr = ippFindAttribute(attrs, "media-bottom-margin-supported",
                          IPP_TAG_INTEGER);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  testBegin("attrs contains media-left-margin-supported");
  attr = ippFindAttribute(attrs, "media-left-margin-supported",
                          IPP_TAG_INTEGER);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  testBegin("attrs contains media-right-margin-supported");
  attr = ippFindAttribute(attrs, "media-right-margin-supported",
                          IPP_TAG_INTEGER);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  testBegin("attrs contains media-top-margin-supported");
  attr = ippFindAttribute(attrs, "media-top-margin-supported",
                          IPP_TAG_INTEGER);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);


  // =========================================================================
  // Group 9: Output bin and Sides (T35-T38)
  //
  // Output bin:
  //   pc->num_bins > 0 → output-bin-supported is built from pc->bins[].
  //   "StandardBin" is converted by ppdPwgUnppdizeName() to "standard-bin"
  //   when no explicit mapping is given.
  //   output-bin-default is selected by walking pc->bins[] and tracking
  //   the entry whose PPD name matches default_output_bin OR has matching
  //   PageStackOrder; with our PPD the matched entry is StandardBin.
  //
  // Sides:
  //   pc->sides_option == "Duplex", pc->sides_2sided_long == "DuplexNoTumble",
  //   so the three-entry sides_supported branch is taken (count == 3).
  //   DefaultDuplex is "None" → ppdIsMarked(ppd, "Duplex", "DuplexNoTumble")
  //   is false → ppdIsMarked(ppd, "Duplex", "DuplexTumble") is false → the
  //   final `else` is taken → sides-default == "one-sided".
  // =========================================================================

  // T35 — output-bin-default is a single keyword; we only assert presence
  //       and non-NULL string because the exact value is determined by a
  //       comparatively long heuristic over pc->bins[] entries.
  testBegin("attrs contains output-bin-default");
  attr = ippFindAttribute(attrs, "output-bin-default", IPP_TAG_KEYWORD);
  testEnd(attr != NULL && ippGetString(attr, 0, NULL) != NULL);

  // T36 — "StandardBin" → ppdPwgUnppdizeName() → "standard-bin" must
  //       appear somewhere in output-bin-supported.
  testBegin("attrs contains output-bin-supported with standard-bin");
  testEnd(attr_has_value(attrs, "output-bin-supported", "standard-bin"));

  // T37 — DefaultDuplex: None means DuplexNoTumble is not the marked
  //       choice → sides-default falls through to "one-sided".
  testBegin("attrs contains sides-default == one-sided");
  attr = ippFindAttribute(attrs, "sides-default", IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && !strcmp(val, "one-sided"),
                 "got \"%s\"", val ? val : "(null)");

  // T38 — Because pc->sides_2sided_long is non-NULL, the full
  //       three-element sides_supported keyword list is added
  //       (one-sided, two-sided-long-edge, two-sided-short-edge).
  testBegin("attrs contains sides-supported with 3 entries");
  attr = ippFindAttribute(attrs, "sides-supported", IPP_TAG_KEYWORD);
  testEndMessage(attr != NULL && ippGetCount(attr) == 3,
                 "count=%d", attr ? ippGetCount(attr) : -1);


  // =========================================================================
  // Group 10: Finishing-template, finishings-col-*, print-rendering-intent
  //           (T39-T44)
  //
  // No cupsIPPFinishings entries in the PPD → pc->templates is empty and
  // pc->finishings contains only the implicit NONE entry.  Therefore:
  //   finishing-template-supported    = {"none"}
  //   finishings-col-default          = {{ finishing-template = "none" }}
  //   finishings-col-supported        = "finishing-template"
  //   finishings-supported[0]         = IPP_FINISHINGS_NONE
  //
  // No cupsRenderingIntent or print-rendering-intent option in the PPD →
  // both rendering-intent-default and -supported fall through to "auto".
  // =========================================================================

  // T39 — "none" is always inserted at index 0 of finishing-template-supported.
  testBegin("attrs contains finishing-template-supported with none");
  testEnd(attr_has_value(attrs, "finishing-template-supported", "none"));

  // T40 — finishings-supported[0] is always IPP_FINISHINGS_NONE.
  testBegin("attrs contains finishings-supported with NONE");
  attr = ippFindAttribute(attrs, "finishings-supported", IPP_TAG_ENUM);
  testEnd(attr != NULL && ippGetInteger(attr, 0) == IPP_FINISHINGS_NONE);

  // T41 — finishings-col-default is a single collection with finishing-template="none".
  testBegin("attrs contains finishings-col-default");
  attr = ippFindAttribute(attrs, "finishings-col-default",
                          IPP_TAG_BEGIN_COLLECTION);
  testEnd(attr != NULL);

  // T42 — finishings-col-supported is a constant single keyword.
  testBegin("attrs contains finishings-col-supported");
  attr = ippFindAttribute(attrs, "finishings-col-supported", IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEnd(val != NULL && !strcmp(val, "finishing-template"));

  // T43 — print-rendering-intent-default fallback when no marked choice exists.
  testBegin("attrs contains print-rendering-intent-default == auto");
  attr = ippFindAttribute(attrs, "print-rendering-intent-default",
                          IPP_TAG_KEYWORD);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEnd(val != NULL && !strcmp(val, "auto"));

  // T44 — print-rendering-intent-supported fallback is the single "auto"
  //       entry when no PPD option lists alternative choices.
  testBegin("attrs contains print-rendering-intent-supported");
  attr = ippFindAttribute(attrs, "print-rendering-intent-supported",
                          IPP_TAG_KEYWORD);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);


  // =========================================================================
  // Group 11: Printer identity, resolution, trays, document formats
  //           (T45-T54)
  //
  // printer-info             = ppd->nickname
  // printer-make-and-model   = output of cfIEEE1284NormalizeMakeModel() or
  //                            the nickname fallback
  // printer-device-id        = synthesised "MFG:%s;MDL:%s;%s" string
  //                            because the PPD does not define *1284DeviceId.
  //                            The synthesised CMD: token includes
  //                            "POSTSCRIPT,PS" because the PPD has no
  //                            cupsFilter entries so the synthetic
  //                            "CMD:POSTSCRIPT,PS;" path is taken.
  // printer-resolution-*     = single IPP_RES_PER_INCH resolution derived
  //                            from DefaultResolution "600dpi".
  // printer-input-tray       = OctetString attribute, count == pc->num_sources
  //                            (each entry stores a serialized tray descriptor).
  // printer-output-tray      = OctetString attribute, count == pc->num_bins.
  // document-format-supported = single-entry fallback
  //                            "application/vnd.cups-postscript" because the
  //                            PPD has zero cupsFilter lines.
  // =========================================================================

  // T45 — printer-info echoes the *NickName field of the PPD.
  testBegin("attrs contains printer-info matching nickname");
  attr = ippFindAttribute(attrs, "printer-info", IPP_TAG_TEXT);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && !strcmp(val, "IPP Test Printer"),
                 "got \"%s\"", val ? val : "(null)");

  // T46 — printer-make-and-model must exist with a non-empty string.
  testBegin("attrs contains printer-make-and-model");
  attr = ippFindAttribute(attrs, "printer-make-and-model", IPP_TAG_TEXT);
  testEnd(attr != NULL && ippGetString(attr, 0, NULL) != NULL);

  // T47 — Synthesised device ID always starts with "MFG:<manufacturer>".
  testBegin("attrs contains printer-device-id with MFG: prefix");
  attr = ippFindAttribute(attrs, "printer-device-id", IPP_TAG_TEXT);
  val  = attr ? ippGetString(attr, 0, NULL) : NULL;
  testEndMessage(val != NULL && strstr(val, "MFG:") != NULL,
                 "got \"%s\"", val ? val : "(null)");

  // T48 — Synthesised device ID also contains "MDL:<modelname>".
  testBegin("attrs contains printer-device-id with MDL: token");
  testEnd(val != NULL && strstr(val, "MDL:") != NULL);

  // T49 — No cupsFilter lines → fallback CMD: token is "CMD:POSTSCRIPT,PS;".
  testBegin("attrs contains printer-device-id with CMD: token");
  testEnd(val != NULL && strstr(val, "CMD:POSTSCRIPT") != NULL);

  // T50 — printer-resolution-default is a single IPP_RES_PER_INCH value.
  testBegin("attrs contains printer-resolution-default");
  attr = ippFindAttribute(attrs, "printer-resolution-default",
                          IPP_TAG_RESOLUTION);
  testEnd(attr != NULL);

  // T51 — printer-resolution-supported is a single IPP_RES_PER_INCH value
  //       (ppdLoadAttributes() emits the same value as -default).
  testBegin("attrs contains printer-resolution-supported");
  attr = ippFindAttribute(attrs, "printer-resolution-supported",
                          IPP_TAG_RESOLUTION);
  testEnd(attr != NULL);

  // T52 — printer-input-tray is an OctetString attribute (IPP_TAG_STRING),
  //       with one OctetString entry per pc->sources[].
  testBegin("attrs contains printer-input-tray");
  attr = ippFindAttribute(attrs, "printer-input-tray", IPP_TAG_STRING);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  // T53 — printer-output-tray is an OctetString attribute (IPP_TAG_STRING),
  //       with one OctetString entry per pc->bins[].
  testBegin("attrs contains printer-output-tray");
  attr = ippFindAttribute(attrs, "printer-output-tray", IPP_TAG_STRING);
  testEnd(attr != NULL && ippGetCount(attr) >= 1);

  // T54 — No cupsFilter in the PPD → the else branch adds the literal
  //       "application/vnd.cups-postscript" entry to docformats.
  testBegin("attrs contains document-format-supported with postscript");
  testEnd(attr_has_value(attrs, "document-format-supported",
                         "application/vnd.cups-postscript"));

  ippDelete(attrs);


  // =========================================================================
  // Group 12: ppdGetOptions() — empty-input behaviour (T55-T56)
  //
  // The function's first statement is `*options = NULL;` so even when no
  // attributes are passed the output pointer is reset.  All three lookup
  // chains then return NULL attributes and the function falls through to
  // `return (0);`.
  //
  // We seed `options = 0xdeadbeef` to confirm the function actively writes
  // NULL rather than leaving the caller-supplied value untouched.
  // =========================================================================

  // T55 — All inputs NULL → 0 options, *options reset to NULL.
  testBegin("ppdGetOptions(&options, NULL, NULL, NULL) returns 0");
  options     = (cups_option_t *)0xdeadbeef;
  num_options = ppdGetOptions(&options, NULL, NULL, NULL);
  testEndMessage(num_options == 0 && options == NULL,
                 "num=%d options=%p", num_options, (void *)options);

  // T56 — Valid ppd but no attrs → ppd block runs ppdMarkDefaults()
  //       and ppdMarkOptions(0, NULL); still returns 0.
  testBegin("ppdGetOptions(&options, NULL, NULL, ppd) with no attrs returns 0");
  options     = (cups_option_t *)0xdeadbeef;
  num_options = ppdGetOptions(&options, NULL, NULL, ppd);
  testEndMessage(num_options == 0 && options == NULL,
                 "num=%d options=%p", num_options, (void *)options);


  // =========================================================================
  // Group 13: ppdGetOptions() — full code-path coverage (T57-T69)
  //
  // The function consults attributes in the following order:
  //   1. job_attrs "media" or "media-col"; else
  //   2. printer_attrs "media-default" or "media-col-default".
  //
  // The discovered value is converted to a string by ippAttributeString()
  // and then either:
  //   - parsed as a brace-delimited collection via cupsParseOptions() if
  //     it begins with '{', or
  //   - inserted as media-size-name = "<value>" via cupsAddOption().
  //
  // After media handling, with a non-NULL ppd:
  //   - finishings        → ppdCacheGetFinishingOptions()  → option list
  //   - media-source      → ppdCacheGetInputSlot()         → InputSlot
  //   - media-type        → ppdCacheGetMediaType()         → MediaType
  //   - output-bin        → ppdCacheGetOutputBin()         → OutputBin
  //   - sides             → pc->sides_option / _1sided / _2sided_*
  //   - print-quality (+ optional print-color-mode) selects preset list
  //
  // The remainder of this group hits every one of those branches.
  // =========================================================================

  // T57 — Bare "media" keyword in job_attrs.  With ppd==NULL the function
  //       only emits the PageSize option → exactly 1 option.
  testBegin("ppdGetOptions(media=na_letter_8.5x11in) yields PageSize=Letter");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "media", NULL, "na_letter_8.5x11in");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, NULL);
  val = cupsGetOption("PageSize", num_options, options);
  testEndMessage(num_options == 1 && val != NULL && !strcmp(val, "Letter"),
                 "num=%d val=\"%s\"", num_options, val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T58 — Falls through the job_attrs chain (no "media", no "media-col"),
  //       then hits printer_attrs "media-default".  We pass the attribute
  //       set as the FIRST ipp_t* argument (printer_attrs) for this test.
  testBegin("ppdGetOptions(media-default=na_letter_8.5x11in via printer_attrs)");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "media-default", NULL, "na_letter_8.5x11in");
  options     = NULL;
  num_options = ppdGetOptions(&options, job_attrs, NULL, NULL);
  val = cupsGetOption("PageSize", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Letter"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T59..T61 — media-col with media-size-name + media-source + media-type.
  //            The string starts with '{' so cupsParseOptions() is used.
  //            With a non-NULL ppd, ppdGetOptions() resolves all three
  //            sub-values against the cache and emits PageSize, InputSlot,
  //            and MediaType options in one pass.  We reuse the result
  //            across three assertions for efficiency, then free.

  testBegin("ppdGetOptions(media-col with media-size-name) yields PageSize");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "media-col", NULL,
               "{media-size-name=na_letter_8.5x11in media-source=main "
               "media-type=stationery}");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("PageSize", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Letter"),
                 "val=\"%s\"", val ? val : "(null)");

  // T60 — The same call also emits InputSlot=Cassette because the
  //       media-source value "main" round-trips through
  //       ppdCacheGetInputSlot() back to the PPD choice "Cassette".
  testBegin("ppdGetOptions(media-col with media-source=main) yields InputSlot");
  val = cupsGetOption("InputSlot", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Cassette"),
                 "val=\"%s\"", val ? val : "(null)");

  // T61 — The same call also emits MediaType=Plain because the
  //       media-type value "stationery" round-trips through
  //       ppdCacheGetMediaType() back to the PPD choice "Plain".
  testBegin("ppdGetOptions(media-col with media-type=stationery) yields MediaType");
  val = cupsGetOption("MediaType", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Plain"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T62 — media-col with nested media-size={x-dimension=21590 y-dimension=27940}.
  //       21590 == 8.5 * 2540, 27940 == 11 * 2540 → exact Letter dimensions.
  //       This triggers the pwgMediaForSize() lookup branch (instead of the
  //       pwgMediaForPWG() name lookup branch).  cupsParseOptions() preserves
  //       the inner braces so the function recursively re-parses them.
  testBegin("ppdGetOptions(media-size x/y) maps via pwgMediaForSize");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "media-col", NULL,
               "{media-size={x-dimension=21590 y-dimension=27940}}");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("PageSize", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Letter"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T63 — output-bin keyword maps via ppdCacheGetOutputBin() back to the
  //       PPD choice name.
  testBegin("ppdGetOptions(output-bin=standard-bin) yields OutputBin");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "output-bin", NULL, "standard-bin");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("OutputBin", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "StandardBin"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T64..T66 — sides triggers the three-way string compare on the IPP
  //            keyword and emits the matching PPD duplex choice:
  //              one-sided            → pc->sides_1sided      → "None"
  //              two-sided-long-edge  → pc->sides_2sided_long → "DuplexNoTumble"
  //              two-sided-short-edge → pc->sides_2sided_short→ "DuplexTumble"

  testBegin("ppdGetOptions(sides=one-sided) yields Duplex=None");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "sides", NULL, "one-sided");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("Duplex", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "None"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  testBegin("ppdGetOptions(sides=two-sided-long-edge) yields Duplex=DuplexNoTumble");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "sides", NULL, "two-sided-long-edge");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("Duplex", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "DuplexNoTumble"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  testBegin("ppdGetOptions(sides=two-sided-short-edge) yields Duplex=DuplexTumble");
  job_attrs = ippNew();
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "sides", NULL, "two-sided-short-edge");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  val = cupsGetOption("Duplex", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "DuplexTumble"),
                 "val=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T67 — print-quality "draft" exercises the pq==0 branch.  Our PPD has
  //       no cupsCommandPreset entries, so num_presets is zero and no
  //       options are added — the test asserts num_options >= 0 and that
  //       the path runs cleanly without crashing.
  testBegin("ppdGetOptions(print-quality=draft) does not crash");
  job_attrs = ippNew();
  ippAddInteger(job_attrs, IPP_TAG_OPERATION, IPP_TAG_ENUM,
                "print-quality", IPP_QUALITY_DRAFT);
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  testEnd(num_options >= 0);
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T68 — print-quality "high" (pq=2) combined with print-color-mode
  //       "monochrome" (pcm=0) — exercises the most-specific preset
  //       lookup arm pc->presets[0][2].  Same no-crash assertion.
  testBegin("ppdGetOptions(print-quality=high, print-color-mode=monochrome)");
  job_attrs = ippNew();
  ippAddInteger(job_attrs, IPP_TAG_OPERATION, IPP_TAG_ENUM,
                "print-quality", IPP_QUALITY_HIGH);
  ippAddString(job_attrs, IPP_TAG_OPERATION, IPP_TAG_KEYWORD,
               "print-color-mode", NULL, "monochrome");
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  testEnd(num_options >= 0);
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);

  // T69 — finishings enum drives the strtol() parse loop in ppdGetOptions().
  //       IPP_FINISHINGS_NONE serializes to "3", which the loop consumes
  //       before reaching the comma terminator and exits cleanly.
  testBegin("ppdGetOptions(finishings=NONE) does not crash");
  job_attrs = ippNew();
  ippAddInteger(job_attrs, IPP_TAG_OPERATION, IPP_TAG_ENUM,
                "finishings", IPP_FINISHINGS_NONE);
  options     = NULL;
  num_options = ppdGetOptions(&options, NULL, job_attrs, ppd);
  testEnd(num_options >= 0);
  cupsFreeOptions(num_options, options);
  ippDelete(job_attrs);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
