//
// PPD color-profile loader API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (31 assertions across 5 groups):
//
//   Group 1  (T01-T05)  ppdFindColorAttr() argument guards — the function
//                       opens with a single combined range check:
//                       `!ppd || !name || !colormodel || !media ||
//                        !resolution || !spec || specsize < IPP_MAX_NAME`.
//                       These run before any PPD is opened.
//
//   Group 2  (T06-T14)  ppdFindColorAttr() lookup cascade — the function
//                       tries up to SEVEN spec keys in order of decreasing
//                       specificity:
//                         1. ColorModel.Media.Resolution
//                         2. ColorModel.Resolution
//                         3. ColorModel
//                         4. Media.Resolution
//                         5. Media
//                         6. Resolution
//                         7. ""            (empty spec)
//                       For each call it writes the chosen key into the
//                       caller's `spec` buffer and returns the matching
//                       ppd_attr_t (only when attr->value is non-NULL).
//                       Our fixture declares seven attributes with
//                       *distinct names* — each present at exactly ONE
//                       level — so we can drive every cascade rung in
//                       isolation and verify both the returned value and
//                       the spec written out.
//
//   Group 3  (T15-T19)  ppdLutLoad() — composes name "cups<Ink>Dither",
//                       falls back to "cupsAllDither" if not found, and
//                       returns a cf_lut_t built from up to 3 floats in
//                       attr->value.  We verify NULL guards, the ink-
//                       specific hit, the cupsAllDither fallback, and a
//                       complete miss returning NULL.
//
//   Group 4  (T20-T24)  ppdRGBLoad() — reads cupsRGBProfile header
//                       ("cube_size num_channels num_samples"), validates
//                       it (cube∈[2,16], chans∈[1,CF_MAX_RGB], samples
//                       == cube^3), then iterates that many cupsRGBSample
//                       lines with the SAME spec via ppdFindNextAttr().
//                       We verify: NULL ppd → NULL, success producing a
//                       cf_rgb_t whose cube_size/num_channels are echoed
//                       back, a malformed header → NULL, a count mismatch
//                       → NULL, and an out-of-range cube_size → NULL.
//
//   Group 5  (T25-T31)  ppdCMYKLoad() — requires cupsInkChannels; rejects
//                       num_channels < 1, > 7, or == 5 (5 is a forbidden
//                       value per the source's explicit guard); otherwise
//                       constructs a cf_cmyk_t whose ->num_channels is
//                       the inspected channel count.  We verify NULL ppd,
//                       missing cupsInkChannels, the three rejected
//                       channel counts (0, 5, 8), and two success cases
//                       (CMYK=4 and Gray=1).
//
// Design: the PPD is built entirely in memory from a single static string
// and loaded via tmpfile() + ppdOpen(), so the binary is fully hermetic —
// no external files are needed at build or CI time.  It declares:
//
//   • Standard header attributes (*Manufacturer, *ModelName, …) and a
//     valid media set (PageSize/PageRegion/ImageableArea/PaperDimension)
//     so ppdOpen() accepts the file.
//   • Seven cascade-level marker attributes (*cupsTestL1..L7) each at a
//     different specifier level, used by the Group 2 cascade tests.
//   • One ink-specific *cupsBlackDither and one *cupsAllDither for the
//     Group 3 LUT loader tests.
//   • A valid 2×2×2/3-channel/8-sample RGB profile plus three malformed
//     RGB headers under distinct colormodels for the Group 4 RGB loader
//     tests.
//   • Five *cupsInkChannels entries under distinct colormodels (CMYK=4,
//     Gray=1, FiveChan=5, ZeroChan=0, EightChan=8) for the Group 5 CMYK
//     loader tests.
//
// No external cupsfilters log callback is used — all `log` arguments are
// NULL, which the loader code explicitly guards (`if (log) …`).
//

#include <ppd/ppd.h>
#include <cupsfilters/driver.h>
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
//     A valid, spec-conformant media option set required for a parseable PPD.
//
//   cupsTestL1 .. cupsTestL7:
//     Seven distinctly-named cascade markers, one per ppdFindColorAttr
//     fallback level.  Calling ppdFindColorAttr(ppd, "cupsTestLn",
//     "RGB", "Plain", "600dpi", ...) lands on exactly level n and writes
//     the corresponding spec back to the caller.
//
//   cupsBlackDither / cupsAllDither (RGB.Plain.600dpi):
//     Dither lookup-table fixtures for ppdLutLoad.  The Black entry is
//     ink-specific; the All entry is the fallback used when the
//     "cups<Ink>Dither" lookup misses.
//
//   cupsRGBProfile / cupsRGBSample (RGB.Plain.600dpi):
//     A valid 2-on-a-side colour cube → 8 samples.  Each sample line has
//     6 floats: three RGB inputs followed by three colour-channel outputs
//     (matching num_channels = 3).
//
//   cupsRGBProfile (BadCount/BadFmt/BigCube.Plain.600dpi):
//     Three negative RGB headers — sample count mismatch, non-numeric
//     header, and cube_size exceeding CF_MAX_RGB-domain limits — each
//     under a distinct colormodel so it can be addressed in isolation.
//
//   cupsInkChannels under five distinct colormodels:
//     CMYK→"4", Gray→"1", FiveChan→"5", ZeroChan→"0", EightChan→"8".
//     Drives the ppdCMYKLoad channel-count validation and success paths.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"PROFTEST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(ProfTest)\"\n"
  "*ModelName: \"Acme ProfTest\"\n"
  "*ShortNickName: \"ProfTest\"\n"
  "*NickName: \"Acme ProfTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
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
  // PageRegion (parallel of PageSize — required by the Adobe PPD spec)
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageRegion[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageRegion[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  // ImageableArea + PaperDimension (required for a valid PPD)
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  // Cascade-level markers — exactly one attribute per ppdFindColorAttr
  // fallback level.  Distinct names mean each call isolates one rung.
  "*cupsTestL1 RGB.Plain.600dpi/Full: \"level1\"\n"
  "*cupsTestL2 RGB.600dpi/CM+Res: \"level2\"\n"
  "*cupsTestL3 RGB/CM only: \"level3\"\n"
  "*cupsTestL4 Plain.600dpi/M+Res: \"level4\"\n"
  "*cupsTestL5 Plain/Media: \"level5\"\n"
  "*cupsTestL6 600dpi/Res: \"level6\"\n"
  "*cupsTestL7: \"level7\"\n"
  // LUT loader fixtures
  "*cupsBlackDither RGB.Plain.600dpi: \"1.0 2.0 3.0\"\n"
  "*cupsAllDither RGB.Plain.600dpi: \"5.0 6.0 7.0\"\n"
  // Negative cupsRGBProfile headers come FIRST so the success entry (RGB)
  // ends up LAST in the cupsRGBProfile sort group.  libppd sorts the attrs
  // array by name only (ppd_compare_attrs in ppd.c), keeping same-name
  // entries in source order.  ppdRGBLoad relies on ppdFindNextAttr
  // landing on cupsRGBSample on its very first step after the cupsRGBProfile
  // match (ppdFindNextAttr returns NULL the moment it crosses a name
  // boundary).  So the matching cupsRGBProfile MUST be the last cupsRGBProfile
  // in source order — anything after it in the same name group would cause
  // ppdRGBLoad to find zero samples and return NULL.
  //
  //   BadCount: num_samples != cube^3 (2^3 = 8, not 9)
  //   BadFmt:   non-numeric value, sscanf returns 0, header rejected
  //   BigCube:  cube_size 17 exceeds the [2,16] bound
  "*cupsRGBProfile BadCount.Plain.600dpi: \"2 3 9\"\n"
  "*cupsRGBProfile BadFmt.Plain.600dpi: \"abc\"\n"
  "*cupsRGBProfile BigCube.Plain.600dpi: \"17 3 4913\"\n"
  // RGB loader fixture — cube_size = 2, num_channels = 3, num_samples = 8
  // (the 8 corners of the unit cube; each sample line is "Rr Gg Bb C M Y").
  // This entry must remain the LAST cupsRGBProfile in source order; see the
  // ordering note above.
  "*cupsRGBProfile RGB.Plain.600dpi: \"2 3 8\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"0.0 0.0 0.0 0.0 0.0 0.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"1.0 0.0 0.0 1.0 0.0 0.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"0.0 1.0 0.0 0.0 1.0 0.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"1.0 1.0 0.0 1.0 1.0 0.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"0.0 0.0 1.0 0.0 0.0 1.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"1.0 0.0 1.0 1.0 0.0 1.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"0.0 1.0 1.0 0.0 1.0 1.0\"\n"
  "*cupsRGBSample RGB.Plain.600dpi: \"1.0 1.0 1.0 1.0 1.0 1.0\"\n"
  // CMYK channel-count fixtures.  Distinct colormodels select which value
  // ppdFindColorAttr resolves for ppdCMYKLoad.
  "*cupsInkChannels CMYK.Plain.600dpi: \"4\"\n"
  "*cupsInkChannels Gray.Plain.600dpi: \"1\"\n"
  "*cupsInkChannels FiveChan.Plain.600dpi: \"5\"\n"
  "*cupsInkChannels ZeroChan.Plain.600dpi: \"0\"\n"
  "*cupsInkChannels EightChan.Plain.600dpi: \"8\"\n";


//
// 'main()' - Run all ppd-load-profile.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// PPD file handle
  ppd_attr_t   *attr;			// Returned by ppdFindColorAttr()
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error
  char          spec[PPD_MAX_LINE];	// Out-buffer for the chosen spec key
  char          smallbuf[8];		// Deliberately undersized spec buffer
  cf_lut_t     *lut;			// Returned LUT
  cf_rgb_t     *rgb;			// Returned RGB profile
  cf_cmyk_t    *cmyk;			// Returned CMYK profile


  // =========================================================================
  // Group 1: ppdFindColorAttr() argument guards (T01-T05)
  //
  // The single range check at the top of the function is:
  //   `if (!ppd || !name || !colormodel || !media || !resolution ||
  //        !spec || specsize < IPP_MAX_NAME) return (NULL);`
  // Each of these tests trips one clause of that disjunction.  They need
  // no PPD content and run before ppdOpen().
  // =========================================================================

  // T01 — NULL ppd trips the first clause.  All other arguments are valid
  //       (PPD_MAX_LINE is well above IPP_MAX_NAME) so only the !ppd guard
  //       can cause the return.
  testBegin("ppdFindColorAttr(NULL ppd, ...) returns NULL");
  testEnd(ppdFindColorAttr(NULL, "cupsTestL1", "RGB", "Plain", "600dpi",
                           spec, sizeof(spec), NULL, NULL) == NULL);

  // T02 — NULL name trips the !name guard.  We pass a non-NULL ppd-shaped
  //       pointer; the !name short-circuit fires before any dereference.
  //       Cast a dummy pointer (never dereferenced) to satisfy the type.
  testBegin("ppdFindColorAttr(ppd, NULL name, ...) returns NULL");
  testEnd(ppdFindColorAttr((ppd_file_t *)1, NULL, "RGB", "Plain", "600dpi",
                           spec, sizeof(spec), NULL, NULL) == NULL);

  // T03 — NULL colormodel trips the !colormodel guard.
  testBegin("ppdFindColorAttr(.., NULL colormodel, ..) returns NULL");
  testEnd(ppdFindColorAttr((ppd_file_t *)1, "cupsTestL1", NULL, "Plain",
                           "600dpi", spec, sizeof(spec), NULL, NULL)
          == NULL);

  // T04 — NULL spec output buffer trips the !spec guard.
  testBegin("ppdFindColorAttr(.., NULL spec out-buffer, ..) returns NULL");
  testEnd(ppdFindColorAttr((ppd_file_t *)1, "cupsTestL1", "RGB", "Plain",
                           "600dpi", NULL, sizeof(spec), NULL, NULL)
          == NULL);

  // T05 — specsize < IPP_MAX_NAME trips the size guard.  A 7-character
  //       buffer is far smaller than IPP_MAX_NAME (256 on every supported
  //       cups build), so the guard always fires.
  testBegin("ppdFindColorAttr(.., specsize < IPP_MAX_NAME) returns NULL");
  testEnd(ppdFindColorAttr((ppd_file_t *)1, "cupsTestL1", "RGB", "Plain",
                           "600dpi", smallbuf, (int)sizeof(smallbuf),
                           NULL, NULL) == NULL);


  // =========================================================================
  // Group 2: ppdFindColorAttr() cascade (T06-T14)
  //
  // Open the embedded PPD, then for each of the seven cascade levels
  // call the function with a name that exists at *exactly* that level.
  // Verify both the value (proves we got the right attr) and the spec
  // out-buffer (proves the cascade stopped at the expected rung).
  // =========================================================================

  testBegin("ppdOpen(embedded profile test PPD)");
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

  // T07 — Level 1: full key "ColorModel.Media.Resolution".
  //       cupsTestL1 exists with spec "RGB.Plain.600dpi" → caller's spec
  //       buffer receives that exact string and the returned attr's value
  //       is "level1" (the dequoted body of the *cupsTestL1 line).
  testBegin("ppdFindColorAttr: level 1 (ColorModel.Media.Resolution)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL1", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level1") &&
                 !strcmp(spec, "RGB.Plain.600dpi"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T08 — Level 2: "ColorModel.Resolution".  cupsTestL2 only exists with
  //       spec "RGB.600dpi", so level 1 (RGB.Plain.600dpi) misses and the
  //       cascade falls through to level 2.
  testBegin("ppdFindColorAttr: level 2 (ColorModel.Resolution)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL2", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level2") &&
                 !strcmp(spec, "RGB.600dpi"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T09 — Level 3: "ColorModel".  cupsTestL3 has spec "RGB"; levels 1-2
  //       miss and we land on the bare ColorModel rung.
  testBegin("ppdFindColorAttr: level 3 (ColorModel)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL3", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level3") &&
                 !strcmp(spec, "RGB"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T10 — Level 4: "Media.Resolution".  cupsTestL4 has spec
  //       "Plain.600dpi"; the ColorModel-prefixed rungs (1-3) all miss
  //       because no cupsTestL4 entry carries an RGB-anchored spec.
  testBegin("ppdFindColorAttr: level 4 (Media.Resolution)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL4", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level4") &&
                 !strcmp(spec, "Plain.600dpi"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T11 — Level 5: "Media".  cupsTestL5 has spec "Plain"; rungs 1-4 miss.
  testBegin("ppdFindColorAttr: level 5 (Media)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL5", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level5") &&
                 !strcmp(spec, "Plain"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T12 — Level 6: "Resolution".  cupsTestL6 has spec "600dpi"; rungs 1-5
  //       miss.
  testBegin("ppdFindColorAttr: level 6 (Resolution)");
  spec[0] = '\0';
  attr = ppdFindColorAttr(ppd, "cupsTestL6", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level6") &&
                 !strcmp(spec, "600dpi"),
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T13 — Level 7: empty spec.  cupsTestL7 is declared without an option
  //       word (`*cupsTestL7: "level7"`), so its attr->spec is "".  Levels
  //       1-6 all miss; the loader writes `spec[0] = '\0'` and looks up
  //       the bare attribute.
  testBegin("ppdFindColorAttr: level 7 (empty spec)");
  spec[0] = 'X';
  attr = ppdFindColorAttr(ppd, "cupsTestL7", "RGB", "Plain", "600dpi",
                          spec, sizeof(spec), NULL, NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "level7") && spec[0] == '\0',
                 "spec=\"%s\" value=\"%s\"",
                 spec, (attr && attr->value) ? attr->value : "(null)");

  // T14 — No match anywhere.  cupsTestL1 only has the spec
  //       "RGB.Plain.600dpi"; with (CMYK, Glossy, 1200dpi) every cascade
  //       rung misses and the function returns NULL.
  testBegin("ppdFindColorAttr: no match across all 7 rungs returns NULL");
  testEnd(ppdFindColorAttr(ppd, "cupsTestL1", "CMYK", "Glossy", "1200dpi",
                           spec, sizeof(spec), NULL, NULL) == NULL);


  // =========================================================================
  // Group 3: ppdLutLoad() (T15-T19)
  //
  // ppdLutLoad's name lookup is "cups<Ink>Dither" with a fallback to
  // "cupsAllDither" if the ink-specific name misses; both are then run
  // through ppdFindColorAttr's cascade.  The returned cf_lut_t is built
  // by cfLutNew() from up to 3 floats parsed from attr->value (plus a
  // zero head element, so nvals = sscanf_count + 1).
  // =========================================================================

  // T15 — NULL ppd trips ppdLutLoad's own range check.
  testBegin("ppdLutLoad(NULL ppd, ...) returns NULL");
  testEnd(ppdLutLoad(NULL, "RGB", "Plain", "600dpi", "Black", NULL, NULL)
          == NULL);

  // T16 — NULL ink trips the same range check.
  testBegin("ppdLutLoad(ppd, .., NULL ink, ..) returns NULL");
  testEnd(ppdLutLoad(ppd, "RGB", "Plain", "600dpi", NULL, NULL, NULL)
          == NULL);

  // T17 — Ink-specific hit.  Ink "Black" composes the name
  //       "cupsBlackDither"; our fixture declares one with value
  //       "1.0 2.0 3.0" at spec "RGB.Plain.600dpi", which the cascade
  //       finds at level 1.  cfLutNew returns a non-NULL allocation.
  testBegin("ppdLutLoad(ppd, RGB, Plain, 600dpi, Black) returns non-NULL");
  lut = ppdLutLoad(ppd, "RGB", "Plain", "600dpi", "Black", NULL, NULL);
  testEnd(lut != NULL);
  if (lut) cfLutDelete(lut);

  // T18 — cupsAllDither fallback.  Ink "Cyan" composes "cupsCyanDither",
  //       which is NOT in the PPD.  ppdLutLoad then retries with the
  //       literal name "cupsAllDither", which IS present, and returns a
  //       LUT built from its "5.0 6.0 7.0" value.
  testBegin("ppdLutLoad(.., \"Cyan\") falls back to cupsAllDither");
  lut = ppdLutLoad(ppd, "RGB", "Plain", "600dpi", "Cyan", NULL, NULL);
  testEnd(lut != NULL);
  if (lut) cfLutDelete(lut);

  // T19 — Total miss.  No dither attrs at all for (CMYK, Glossy, 1200dpi):
  //       cupsCyanDither absent and cupsAllDither absent at this spec
  //       → ppdLutLoad returns NULL.
  testBegin("ppdLutLoad: no dither attrs anywhere returns NULL");
  testEnd(ppdLutLoad(ppd, "CMYK", "Glossy", "1200dpi", "Cyan", NULL, NULL)
          == NULL);


  // =========================================================================
  // Group 4: ppdRGBLoad() (T20-T24)
  //
  // ppdRGBLoad reads "cupsRGBProfile" then walks "cupsRGBSample" with
  // ppdFindNextAttr() using the spec that the profile lookup resolved.
  // It rejects: missing profile attr, sscanf failing to read 3 ints,
  // cube_size ∉ [2,16], num_channels ∉ [1, CF_MAX_RGB], and num_samples
  // ≠ cube_size³.  On success it returns a cf_rgb_t whose ->cube_size
  // and ->num_channels echo the values from the header.
  // =========================================================================

  // T20 — NULL ppd.  ppdRGBLoad has no explicit !ppd guard, but the
  //       first thing it does is call ppdFindColorAttr, whose !ppd guard
  //       returns NULL; ppdRGBLoad therefore returns NULL too.
  testBegin("ppdRGBLoad(NULL ppd, ...) returns NULL");
  testEnd(ppdRGBLoad(NULL, "RGB", "Plain", "600dpi", NULL, NULL) == NULL);

  // T21 — Success.  cupsRGBProfile "2 3 8" + eight valid cupsRGBSample
  //       lines produce a non-NULL cf_rgb_t.  The struct is *not* opaque
  //       (driver.h exposes cube_size and num_channels), so we verify
  //       both fields echo what the header asked for.
  testBegin("ppdRGBLoad(ppd, RGB, Plain, 600dpi) returns valid profile");
  rgb = ppdRGBLoad(ppd, "RGB", "Plain", "600dpi", NULL, NULL);
  testEndMessage(rgb != NULL && rgb->cube_size == 2 &&
                 rgb->num_channels == 3,
                 "cube_size=%d num_channels=%d",
                 rgb ? rgb->cube_size : -1,
                 rgb ? rgb->num_channels : -1);
  if (rgb) cfRGBDelete(rgb);

  // T22 — Malformed header.  cupsRGBProfile BadFmt.Plain.600dpi: "abc".
  //       sscanf("%d%d%d") returns 0 — fewer than 3 ints — so the header
  //       is rejected and ppdRGBLoad returns NULL.
  testBegin("ppdRGBLoad: malformed cupsRGBProfile header returns NULL");
  testEnd(ppdRGBLoad(ppd, "BadFmt", "Plain", "600dpi", NULL, NULL) == NULL);

  // T23 — Sample-count mismatch.  cupsRGBProfile BadCount.Plain.600dpi:
  //       "2 3 9"; the validator requires num_samples == cube_size³, i.e.
  //       8.  9 ≠ 8 → rejected → NULL.
  testBegin("ppdRGBLoad: num_samples ≠ cube_size^3 returns NULL");
  testEnd(ppdRGBLoad(ppd, "BadCount", "Plain", "600dpi", NULL, NULL)
          == NULL);

  // T24 — cube_size out of range.  cupsRGBProfile BigCube.Plain.600dpi:
  //       "17 3 4913".  cube_size must be in [2, 16]; 17 fails → NULL.
  testBegin("ppdRGBLoad: cube_size > 16 returns NULL");
  testEnd(ppdRGBLoad(ppd, "BigCube", "Plain", "600dpi", NULL, NULL)
          == NULL);


  // =========================================================================
  // Group 5: ppdCMYKLoad() (T25-T31)
  //
  // ppdCMYKLoad requires cupsInkChannels (parsed with atoi); rejects
  // num_channels < 1, > 7, or == 5; and then allocates a cf_cmyk_t via
  // cfCMYKNew().  All other curve attributes are optional and silently
  // skipped if absent, so a PPD with just cupsInkChannels suffices for
  // the success cases.
  // =========================================================================

  // T25 — NULL ppd trips the explicit range check at the top of the
  //       function (`ppd == NULL || colormodel == NULL || ...`).
  testBegin("ppdCMYKLoad(NULL ppd, ...) returns NULL");
  testEnd(ppdCMYKLoad(NULL, "CMYK", "Plain", "600dpi", NULL, NULL) == NULL);

  // T26 — No cupsInkChannels for the given (cm, m, r).  ppdFindColorAttr
  //       walks all seven cascade rungs and returns NULL, so ppdCMYKLoad
  //       returns NULL before ever calling cfCMYKNew.
  testBegin("ppdCMYKLoad: missing cupsInkChannels returns NULL");
  testEnd(ppdCMYKLoad(ppd, "NoSuchCM", "NoSuchMedia", "NoSuchRes",
                      NULL, NULL) == NULL);

  // T27 — num_channels == 5 is explicitly rejected by the source guard
  //       (`num_channels == 5`).  cupsInkChannels FiveChan.Plain.600dpi:
  //       "5" resolves at level 1, atoi yields 5, and the function bails.
  testBegin("ppdCMYKLoad: num_channels == 5 returns NULL");
  testEnd(ppdCMYKLoad(ppd, "FiveChan", "Plain", "600dpi", NULL, NULL)
          == NULL);

  // T28 — num_channels < 1 (here, 0) is rejected.  cupsInkChannels
  //       ZeroChan.Plain.600dpi: "0".
  testBegin("ppdCMYKLoad: num_channels == 0 returns NULL");
  testEnd(ppdCMYKLoad(ppd, "ZeroChan", "Plain", "600dpi", NULL, NULL)
          == NULL);

  // T29 — num_channels > 7 is rejected.  cupsInkChannels
  //       EightChan.Plain.600dpi: "8".
  testBegin("ppdCMYKLoad: num_channels == 8 returns NULL");
  testEnd(ppdCMYKLoad(ppd, "EightChan", "Plain", "600dpi", NULL, NULL)
          == NULL);

  // T30 — Success with CMYK (num_channels = 4).  cfCMYKNew allocates a
  //       struct whose public num_channels field echoes the input — we
  //       verify both non-NULL and that field.
  testBegin("ppdCMYKLoad(ppd, CMYK, Plain, 600dpi) returns 4-channel profile");
  cmyk = ppdCMYKLoad(ppd, "CMYK", "Plain", "600dpi", NULL, NULL);
  testEndMessage(cmyk != NULL && cmyk->num_channels == 4,
                 "num_channels=%d", cmyk ? cmyk->num_channels : -1);
  if (cmyk) cfCMYKDelete(cmyk);

  // T31 — Success with Gray (num_channels = 1).  Same shape — non-NULL
  //       result and num_channels == 1.
  testBegin("ppdCMYKLoad(ppd, Gray, Plain, 600dpi) returns 1-channel profile");
  cmyk = ppdCMYKLoad(ppd, "Gray", "Plain", "600dpi", NULL, NULL);
  testEndMessage(cmyk != NULL && cmyk->num_channels == 1,
                 "num_channels=%d", cmyk ? cmyk->num_channels : -1);
  if (cmyk) cfCMYKDelete(cmyk);


  // -------------------------------------------------------------------------
  // Tear down and return success/failure based on the framework flag.
  // -------------------------------------------------------------------------
  ppdClose(ppd);
  return (testsPassed ? 0 : 1);
}
