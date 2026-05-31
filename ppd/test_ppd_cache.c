//
// PPD cache API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (52 assertions across 10 groups):
//
//   Group 1  (T01-T07)  NULL-pc guard checks — every public function that
//                       accepts a ppd_cache_t* must return NULL/no-crash when
//                       pc is NULL.
//   Group 2  (T08-T12)  ppdPwgPpdizeName() — IPP dashed keyword →
//                       PPD CamelCase conversion, including guard conditions.
//   Group 3  (T13-T16)  ppdPwgUnppdizeName() — PPD CamelCase → IPP
//                       lowercase-dashed conversion, including the already-
//                       lowercase fast path and digit-boundary dash insertion.
//   Group 4  (T17-T22)  ppdCacheCreateWithPPD() happy path — cache is created
//                       and all four collection arrays are non-empty.
//   Group 5  (T23-T28)  NULL-keyword guards with a valid pc — the second NULL-
//                       arg branch of each lookup function.
//   Group 6  (T29-T33)  OutputBin bi-directional lookup:
//                         GetBin by PPD name, GetBin by PWG name, miss;
//                         GetOutputBin by PWG name, miss.
//   Group 7  (T34-T37)  InputSlot bi-directional lookup:
//                         GetSource PPD→PWG, PWG→PWG; miss; GetInputSlot
//                         PWG→PPD.
//   Group 8  (T38-T42)  MediaType bi-directional lookup:
//                         GetType PPD→PWG, PWG→PWG; miss; GetMediaType
//                         PWG→PPD; NULL-keyword guard.
//   Group 9  (T43-T46)  ppdCacheGetPageSize() — PPD name, exact flag, PWG
//                       name, and unknown-keyword miss.
//   Group 10 (T47-T52)  ppdCacheGetSize()/ppdCacheGetSize2() — PPD name,
//                       PWG name, custom-size in points, custom-size in
//                       inches, bare-"Custom" (no ppd_size → NULL), miss.
//
// Design: entirely hermetic — the PPD is loaded from a static string via
// tmpfile() + ppdOpen() so no external files are required at build or CI
// time.  Only the keywords needed to exercise the targeted code paths are
// included in the PPD text.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <string.h>


//
// Minimal self-contained PPD content.
//
// PageSize Letter + A4:
//   Populate pc->sizes[]; Letter gives us a known PPD name ("Letter") and
//   a known PWG name ("na_letter_8.5x11in") for exact-match lookups.
//
// VariablePaperSize / ParamCustomPageSize / HWMargins:
//   Enable ppd->variable_sizes so ppdCacheCreateWithPPD() fills
//   pc->custom_min/max_* — required for the Custom.WxH parsing paths in
//   ppdCacheGetSize2().  Range chosen: 36–1000 pt ≈ 1270–35277 in 2540ths,
//   so Custom.72x72 (72 pt = 1 inch = 2540 units) falls comfortably inside.
//
// InputSlot Cassette / Upper:
//   "Cassette" hits the hardcoded branch → PWG "main".
//   "Upper"    hits the hardcoded branch → PWG "top".
//
// MediaType Plain / Gloss:
//   "Plain" prefix-matches standard_types[5] → PWG "stationery".
//   "Gloss" prefix-matches standard_types[5] → PWG "photographic-glossy".
//
// OutputBin StandardBin / FaceUp:
//   Both fall through to ppdPwgUnppdizeName():
//     "StandardBin" → "standard-bin"   (CamelCase, two words)
//     "FaceUp"      → "face-up"        (CamelCase, two words)
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"CACHETEST.PPD\"\n"
  "*Product: \"(CacheTest)\"\n"
  "*ModelName: \"PPD Cache Test Printer\"\n"
  "*ShortNickName: \"CacheTest\"\n"
  "*NickName: \"PPD Cache Test Printer\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  "*DefaultResolution: 600dpi\n"
  // Page sizes
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
  // Custom size: 36–1000 pt range enables ppdCacheGetSize2("Custom.WxH") paths
  "*VariablePaperSize: True\n"
  "*ParamCustomPageSize Width: 1 points 36 1000\n"
  "*ParamCustomPageSize Height: 2 points 36 1000\n"
  "*ParamCustomPageSize WidthOffset: 3 points 0 0\n"
  "*ParamCustomPageSize HeightOffset: 4 points 0 0\n"
  "*ParamCustomPageSize Orientation: 5 int 0 3\n"
  "*HWMargins: 18 18 18 18\n"
  "*CustomPageSize True: \"pop\"\n"
  // Input slots — exercises the hardcoded PPD-choice→PWG keyword table
  "*OpenUI *InputSlot/Paper Source: PickOne\n"
  "*OrderDependency: 20 AnySetup *InputSlot\n"
  "*DefaultInputSlot: Cassette\n"
  "*InputSlot Cassette/Main Tray: \"\"\n"
  "*InputSlot Upper/Upper Tray: \"\"\n"
  "*CloseUI: *InputSlot\n"
  // Media types — exercises standard_types[] prefix-match table
  "*OpenUI *MediaType/Media Type: PickOne\n"
  "*OrderDependency: 30 AnySetup *MediaType\n"
  "*DefaultMediaType: Plain\n"
  "*MediaType Plain/Plain Paper: \"\"\n"
  "*MediaType Gloss/Glossy Photo: \"\"\n"
  "*CloseUI: *MediaType\n"
  // Output bins — exercises ppdPwgUnppdizeName() on choice names
  "*OpenUI *OutputBin/Output Bin: PickOne\n"
  "*OrderDependency: 40 AnySetup *OutputBin\n"
  "*DefaultOutputBin: StandardBin\n"
  "*OutputBin StandardBin/Standard Bin: \"\"\n"
  "*OutputBin FaceUp/Face Up: \"\"\n"
  "*CloseUI: *OutputBin\n";


//
// 'main()' - Run all PPD cache unit tests.
//

int
main(void)
{
  ppd_file_t   *ppd;		// PPD file handle
  ppd_cache_t  *pc;		// PPD cache
  FILE         *f;		// Temporary FILE for the in-memory PPD
  ppd_status_t  err;		// PPD parse error code
  int           line;		// Error line number
  char          name[64];	// Output buffer for name-conversion tests
  const char   *val;		// Return value from lookup functions
  pwg_size_t   *sz;		// Return value from size-record lookups
  int           exact;		// Exact-match flag for ppdCacheGetPageSize


  // =========================================================================
  // Group 1: NULL-pc guard checks (T01–T07)
  //
  // Every public function that takes ppd_cache_t* must safely return
  // NULL (or not crash) when that pointer is NULL.  These run before any
  // PPD is opened so there is no dependency on the test PPD content.
  // =========================================================================

  // T01 — ppdCacheDestroy must be a no-op on NULL, never dereference it.
  testBegin("ppdCacheDestroy(NULL pc) does not crash");
  ppdCacheDestroy(NULL);
  testEnd(true);

  // T02 — ppdCacheGetBin: guard `if (!pc || !output_bin)`
  testBegin("ppdCacheGetBin(NULL pc) returns NULL");
  testEnd(ppdCacheGetBin(NULL, "StandardBin") == NULL);

  // T03 — ppdCacheGetOutputBin: guard `if (!pc || !output_bin)`
  testBegin("ppdCacheGetOutputBin(NULL pc) returns NULL");
  testEnd(ppdCacheGetOutputBin(NULL, "standard-bin") == NULL);

  // T04 — ppdCacheGetSource: guard `if (!pc || !input_slot)`
  testBegin("ppdCacheGetSource(NULL pc) returns NULL");
  testEnd(ppdCacheGetSource(NULL, "Cassette") == NULL);

  // T05 — ppdCacheGetType: guard `if (!pc || !media_type)`
  testBegin("ppdCacheGetType(NULL pc) returns NULL");
  testEnd(ppdCacheGetType(NULL, "Plain") == NULL);

  // T06 — ppdCacheGetPageSize: guard `if (!pc || (!job && !keyword))`
  testBegin("ppdCacheGetPageSize(NULL pc) returns NULL");
  testEnd(ppdCacheGetPageSize(NULL, NULL, "Letter", NULL) == NULL);

  // T07 — ppdCacheGetSize: delegates to ppdCacheGetSize2; guard `if (!pc || !page_size)`
  testBegin("ppdCacheGetSize(NULL pc) returns NULL");
  testEnd(ppdCacheGetSize(NULL, "Letter") == NULL);


  // =========================================================================
  // Group 2: ppdPwgPpdizeName() (T08–T12)
  //
  // Converts a lowercase-dashed IPP keyword to CamelCase PPD keyword.
  // Guard: `if (!ipp || !_ppd_isalnum(*ipp)) { *name = '\0'; return; }`
  //   • NULL ipp  → short-circuit on `!ipp`
  //   • ""        → `_ppd_isalnum('\0') == 0`
  //   • "_..."    → `_ppd_isalnum('_') == 0`
  // Happy paths: first char capitalised, each char after '-' capitalised.
  // =========================================================================

  // T08 — NULL pointer: first half of `||` guard fires.
  testBegin("ppdPwgPpdizeName(NULL) produces empty string");
  ppdPwgPpdizeName(NULL, name, sizeof(name));
  testEnd(name[0] == '\0');

  // T09 — Empty string: `_ppd_isalnum('\0')` is 0, guard fires.
  testBegin("ppdPwgPpdizeName(\"\") produces empty string");
  ppdPwgPpdizeName("", name, sizeof(name));
  testEnd(name[0] == '\0');

  // T10 — Non-alnum first char: `_ppd_isalnum('_')` is 0, guard fires.
  testBegin("ppdPwgPpdizeName(\"_invalid\") produces empty string");
  ppdPwgPpdizeName("_invalid", name, sizeof(name));
  testEnd(name[0] == '\0');

  // T11 — Two-word IPP keyword: 'o' → 'O', then '-b' → 'B'.
  testBegin("ppdPwgPpdizeName(\"output-bin\") == \"OutputBin\"");
  ppdPwgPpdizeName("output-bin", name, sizeof(name));
  testEndMessage(!strcmp(name, "OutputBin"), "got \"%s\"", name);

  // T12 — Two-word IPP keyword: 'm' → 'M', then '-t' → 'T'.
  testBegin("ppdPwgPpdizeName(\"media-type\") == \"MediaType\"");
  ppdPwgPpdizeName("media-type", name, sizeof(name));
  testEndMessage(!strcmp(name, "MediaType"), "got \"%s\"", name);


  // =========================================================================
  // Group 3: ppdPwgUnppdizeName() (T13–T16)
  //
  // Converts a PPD CamelCase keyword to lowercase-dashed IPP keyword.
  // Fast path: if the input is already all-lowercase with no uppercase
  //   letters and no dashchars, strlcpy is used directly.
  // Dash-insertion rules:
  //   (a) lower-then-upper transition: "OutputBin" → 't'+'B' → "output-bin"
  //   (b) non-digit-then-digit transition: "Tray1" → 'y'+'1' → "tray-1"
  // =========================================================================

  // T13 — Already-lowercase single word: fast path triggered, returned as-is.
  testBegin("ppdPwgUnppdizeName(\"auto\") == \"auto\" (fast path)");
  ppdPwgUnppdizeName("auto", name, sizeof(name), NULL);
  testEndMessage(!strcmp(name, "auto"), "got \"%s\"", name);

  // T14 — CamelCase, lower-then-upper transition inserts dash.
  testBegin("ppdPwgUnppdizeName(\"OutputBin\") == \"output-bin\"");
  ppdPwgUnppdizeName("OutputBin", name, sizeof(name), NULL);
  testEndMessage(!strcmp(name, "output-bin"), "got \"%s\"", name);

  // T15 — CamelCase, two transitions: 'a'+'T' and 'e'+'p' (no extra dash).
  testBegin("ppdPwgUnppdizeName(\"MediaType\") == \"media-type\"");
  ppdPwgUnppdizeName("MediaType", name, sizeof(name), NULL);
  testEndMessage(!strcmp(name, "media-type"), "got \"%s\"", name);

  // T16 — Digit boundary: non-digit 'y' followed by digit '1' → dash.
  testBegin("ppdPwgUnppdizeName(\"Tray1\") == \"tray-1\"");
  ppdPwgUnppdizeName("Tray1", name, sizeof(name), NULL);
  testEndMessage(!strcmp(name, "tray-1"), "got \"%s\"", name);


  // =========================================================================
  // Group 4: ppdCacheCreateWithPPD() — setup + population checks (T17–T22)
  //
  // Open the in-memory PPD and build the cache.  Abort early if either
  // step fails because every subsequent group depends on a valid pc.
  // =========================================================================

  testBegin("ppdOpen(embedded cache test PPD)");
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

  // T18 — Cache creation succeeds.
  testBegin("ppdCacheCreateWithPPD() returns non-NULL");
  pc = ppdCacheCreateWithPPD(ppd);
  if (pc)
  {
    testEnd(true);
  }
  else
  {
    testEnd(false);
    ppdClose(ppd);
    return (1);
  }

  // T19 — OutputBin choices were mapped to pc->bins[].
  testBegin("ppdCacheCreateWithPPD() populates pc->bins (num_bins > 0)");
  testEndMessage(pc->num_bins > 0, "num_bins=%d", pc->num_bins);

  // T20 — InputSlot choices were mapped to pc->sources[].
  testBegin("ppdCacheCreateWithPPD() populates pc->sources (num_sources > 0)");
  testEndMessage(pc->num_sources > 0, "num_sources=%d", pc->num_sources);

  // T21 — MediaType choices were mapped to pc->types[].
  testBegin("ppdCacheCreateWithPPD() populates pc->types (num_types > 0)");
  testEndMessage(pc->num_types > 0, "num_types=%d", pc->num_types);

  // T22 — PageSize entries were converted to pc->sizes[].
  testBegin("ppdCacheCreateWithPPD() populates pc->sizes (num_sizes > 0)");
  testEndMessage(pc->num_sizes > 0, "num_sizes=%d", pc->num_sizes);


  // =========================================================================
  // Group 5: NULL-keyword guards with a valid pc (T23–T28)
  //
  // Covers the second NULL-argument branch of each guard expression, which
  // is distinct from the NULL-pc branch tested in Group 1.
  // =========================================================================

  // T23 — ppdCacheGetBin: `if (!pc || !output_bin)` — output_bin is NULL.
  testBegin("ppdCacheGetBin(valid pc, NULL keyword) returns NULL");
  testEnd(ppdCacheGetBin(pc, NULL) == NULL);

  // T24 — ppdCacheGetOutputBin: `if (!pc || !output_bin)` — output_bin is NULL.
  testBegin("ppdCacheGetOutputBin(valid pc, NULL keyword) returns NULL");
  testEnd(ppdCacheGetOutputBin(pc, NULL) == NULL);

  // T25 — ppdCacheGetSource: `if (!pc || !input_slot)` — input_slot is NULL.
  testBegin("ppdCacheGetSource(valid pc, NULL slot) returns NULL");
  testEnd(ppdCacheGetSource(pc, NULL) == NULL);

  // T26 — ppdCacheGetType: `if (!pc || !media_type)` — media_type is NULL.
  testBegin("ppdCacheGetType(valid pc, NULL type) returns NULL");
  testEnd(ppdCacheGetType(pc, NULL) == NULL);

  // T27 — ppdCacheGetPageSize: `if (!pc || (!job && !keyword))` — both NULL.
  testBegin("ppdCacheGetPageSize(valid pc, NULL job, NULL keyword) returns NULL");
  testEnd(ppdCacheGetPageSize(pc, NULL, NULL, NULL) == NULL);

  // T28 — ppdCacheGetSize2: `if (!pc || !page_size)` — page_size is NULL.
  testBegin("ppdCacheGetSize(valid pc, NULL page_size) returns NULL");
  testEnd(ppdCacheGetSize(pc, NULL) == NULL);


  // =========================================================================
  // Group 6: OutputBin bi-directional lookup (T29–T33)
  //
  // ppdCacheGetBin()       — searches bins[i].ppd OR bins[i].pwg,
  //                          returns bins[i].pwg (the PWG keyword).
  // ppdCacheGetOutputBin() — searches bins[i].pwg only,
  //                          returns bins[i].ppd (the PPD choice name).
  //
  // PPD has: StandardBin → ppdPwgUnppdizeName() → "standard-bin"
  //          FaceUp      → ppdPwgUnppdizeName() → "face-up"
  // =========================================================================

  // T29 — Lookup by PPD choice name; returns the PWG keyword.
  testBegin("ppdCacheGetBin(\"StandardBin\") returns non-NULL PWG name");
  val = ppdCacheGetBin(pc, "StandardBin");
  testEndMessage(val != NULL && !strcmp(val, "standard-bin"),
                 "got \"%s\"", val ? val : "(null)");

  // T30 — Lookup by PWG keyword (same function, different match arm).
  testBegin("ppdCacheGetBin(\"standard-bin\") returns \"standard-bin\"");
  val = ppdCacheGetBin(pc, "standard-bin");
  testEndMessage(val != NULL && !strcmp(val, "standard-bin"),
                 "got \"%s\"", val ? val : "(null)");

  // T31 — Unknown name exhausts the loop and returns NULL.
  testBegin("ppdCacheGetBin(\"NotARealBin\") returns NULL");
  testEnd(ppdCacheGetBin(pc, "NotARealBin") == NULL);

  // T32 — ppdCacheGetOutputBin: PWG keyword → PPD choice name.
  testBegin("ppdCacheGetOutputBin(\"standard-bin\") returns \"StandardBin\"");
  val = ppdCacheGetOutputBin(pc, "standard-bin");
  testEndMessage(val != NULL && !strcmp(val, "StandardBin"),
                 "got \"%s\"", val ? val : "(null)");

  // T33 — Unknown PWG name returns NULL.
  testBegin("ppdCacheGetOutputBin(\"not-a-real-bin\") returns NULL");
  testEnd(ppdCacheGetOutputBin(pc, "not-a-real-bin") == NULL);


  // =========================================================================
  // Group 7: InputSlot bi-directional lookup (T34–T37)
  //
  // ppdCacheGetSource()    — searches sources[i].ppd OR sources[i].pwg,
  //                          returns sources[i].pwg.
  // ppdCacheGetInputSlot() — calls ppd_inputslot_for_keyword() which
  //                          searches sources[i].pwg only,
  //                          returns sources[i].ppd.
  //
  // PPD has: Cassette → hardcoded → "main"
  //          Upper    → hardcoded → "top"
  // =========================================================================

  // T34 — PPD choice name → PWG keyword (ppd field match arm).
  testBegin("ppdCacheGetSource(\"Cassette\") returns \"main\"");
  val = ppdCacheGetSource(pc, "Cassette");
  testEndMessage(val != NULL && !strcmp(val, "main"),
                 "got \"%s\"", val ? val : "(null)");

  // T35 — PWG keyword passed as input → same PWG keyword returned (pwg field match arm).
  testBegin("ppdCacheGetSource(\"main\") returns \"main\"");
  val = ppdCacheGetSource(pc, "main");
  testEndMessage(val != NULL && !strcmp(val, "main"),
                 "got \"%s\"", val ? val : "(null)");

  // T36 — Unknown slot exhausts the loop and returns NULL.
  testBegin("ppdCacheGetSource(\"not-a-real-slot\") returns NULL");
  testEnd(ppdCacheGetSource(pc, "not-a-real-slot") == NULL);

  // T37 — ppdCacheGetInputSlot: PWG keyword → PPD InputSlot choice name.
  testBegin("ppdCacheGetInputSlot(NULL job, \"main\") returns \"Cassette\"");
  val = ppdCacheGetInputSlot(pc, NULL, "main");
  testEndMessage(val != NULL && !strcmp(val, "Cassette"),
                 "got \"%s\"", val ? val : "(null)");


  // =========================================================================
  // Group 8: MediaType bi-directional lookup (T38–T42)
  //
  // ppdCacheGetType()      — searches types[i].ppd OR types[i].pwg,
  //                          returns types[i].pwg.
  // ppdCacheGetMediaType() — searches types[i].pwg only (keyword path),
  //                          returns types[i].ppd.
  //
  // PPD has: Plain → standard_types prefix "Plain" → "stationery"
  //          Gloss → standard_types prefix "Gloss" → "photographic-glossy"
  // =========================================================================

  // T38 — PPD choice name → PWG keyword (ppd field match arm).
  testBegin("ppdCacheGetType(\"Plain\") returns \"stationery\"");
  val = ppdCacheGetType(pc, "Plain");
  testEndMessage(val != NULL && !strcmp(val, "stationery"),
                 "got \"%s\"", val ? val : "(null)");

  // T39 — PWG keyword passed as input → same PWG keyword returned (pwg field match arm).
  testBegin("ppdCacheGetType(\"stationery\") returns \"stationery\"");
  val = ppdCacheGetType(pc, "stationery");
  testEndMessage(val != NULL && !strcmp(val, "stationery"),
                 "got \"%s\"", val ? val : "(null)");

  // T40 — Unknown type exhausts the loop and returns NULL.
  testBegin("ppdCacheGetType(\"not-a-real-type\") returns NULL");
  testEnd(ppdCacheGetType(pc, "not-a-real-type") == NULL);

  // T41 — ppdCacheGetMediaType: PWG keyword → PPD MediaType choice name.
  testBegin("ppdCacheGetMediaType(NULL job, \"stationery\") returns \"Plain\"");
  val = ppdCacheGetMediaType(pc, NULL, "stationery");
  testEndMessage(val != NULL && !strcmp(val, "Plain"),
                 "got \"%s\"", val ? val : "(null)");

  // T42 — ppdCacheGetMediaType: no job + no keyword triggers the
  //       `(!job && !keyword)` guard branch.
  testBegin("ppdCacheGetMediaType(NULL job, NULL keyword) returns NULL");
  testEnd(ppdCacheGetMediaType(pc, NULL, NULL) == NULL);


  // =========================================================================
  // Group 9: ppdCacheGetPageSize() (T43–T46)
  //
  // Lookup order:
  //   1. Direct name scan: ppd_name matches sizes[i].map.ppd or .pwg.
  //   2. Dimension scan via pwgMediaForPWG/Legacy/PPD (keyword path).
  //   3. Custom size range check.
  //   4. Return NULL.
  //
  // The `exact` output flag is set to 1 on path (1) hits.
  // =========================================================================

  // T43 — PPD name direct match: "Letter" hits sizes[i].map.ppd.
  testBegin("ppdCacheGetPageSize(\"Letter\") returns \"Letter\"");
  val = ppdCacheGetPageSize(pc, NULL, "Letter", NULL);
  testEndMessage(val != NULL && !strcmp(val, "Letter"),
                 "got \"%s\"", val ? val : "(null)");

  // T44 — exact flag is set to 1 for a direct name match.
  testBegin("ppdCacheGetPageSize(\"Letter\", &exact) sets exact=1");
  exact = 0;
  val   = ppdCacheGetPageSize(pc, NULL, "Letter", &exact);
  testEndMessage(val != NULL && exact == 1,
                 "val=\"%s\" exact=%d", val ? val : "(null)", exact);

  // T45 — PWG name direct match: "na_letter_8.5x11in" hits sizes[i].map.pwg;
  //        the returned value is the PPD name "Letter".
  testBegin("ppdCacheGetPageSize(\"na_letter_8.5x11in\") returns \"Letter\"");
  val = ppdCacheGetPageSize(pc, NULL, "na_letter_8.5x11in", NULL);
  testEndMessage(val != NULL && !strcmp(val, "Letter"),
                 "got \"%s\"", val ? val : "(null)");

  // T46 — Unknown keyword: pwgMediaForPWG/Legacy/PPD all return NULL → NULL.
  testBegin("ppdCacheGetPageSize(\"NotARealSize\") returns NULL");
  testEnd(ppdCacheGetPageSize(pc, NULL, "NotARealSize", NULL) == NULL);


  // =========================================================================
  // Group 10: ppdCacheGetSize() / ppdCacheGetSize2() (T47–T52)
  //
  // ppdCacheGetSize() is a thin wrapper around ppdCacheGetSize2(pc, name, NULL).
  //
  // ppdCacheGetSize2() code paths:
  //   A. "Custom" or "Custom.*": parse dimensions from the name string,
  //      fill pc->custom_size, return &pc->custom_size.
  //      Bare "Custom" with ppd_size=NULL returns NULL (no dimensions).
  //   B. Scan pc->sizes[] by ppd or pwg name → return &sizes[i].
  //   C. pwgMediaForPPD/Legacy/PWG fallback → fill custom_size, return it.
  //   D. All fail → return NULL.
  // =========================================================================

  // T47 — PPD name in pc->sizes[]: path B, ppd match.
  testBegin("ppdCacheGetSize(\"Letter\") returns non-NULL");
  sz = ppdCacheGetSize(pc, "Letter");
  testEnd(sz != NULL);

  // T48 — PWG name in pc->sizes[]: path B, pwg match.
  testBegin("ppdCacheGetSize(\"na_letter_8.5x11in\") returns non-NULL");
  sz = ppdCacheGetSize(pc, "na_letter_8.5x11in");
  testEnd(sz != NULL);

  // T49 — Custom size in points: "Custom.72x72" → w=72*2540/72=2540 units,
  //        within the 36–1000 pt range set by ParamCustomPageSize.  Path A.
  testBegin("ppdCacheGetSize2(\"Custom.72x72\") returns non-NULL (points)");
  sz = ppdCacheGetSize2(pc, "Custom.72x72", NULL);
  testEnd(sz != NULL);

  // T50 — Custom size in inches: "Custom.1x1in" → 1 inch = 2540 units.  Path A.
  testBegin("ppdCacheGetSize2(\"Custom.1x1in\") returns non-NULL (inches)");
  sz = ppdCacheGetSize2(pc, "Custom.1x1in", NULL);
  testEnd(sz != NULL);

  // T51 — Bare "Custom" with ppd_size=NULL: page_size[6]=='\0' and no ppd_size
  //        to supply dimensions → the function returns NULL.  Path A error branch.
  testBegin("ppdCacheGetSize2(\"Custom\", NULL ppd_size) returns NULL");
  sz = ppdCacheGetSize2(pc, "Custom", NULL);
  testEnd(sz == NULL);

  // T52 — Completely unknown name: not in sizes[], not found by any
  //        pwgMediaFor*() call → path D → NULL.
  testBegin("ppdCacheGetSize(\"NotARealSize\") returns NULL");
  sz = ppdCacheGetSize(pc, "NotARealSize");
  testEnd(sz == NULL);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdCacheDestroy(pc);
  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
