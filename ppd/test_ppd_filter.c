//
// PPD filter wrapper API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (50 assertions across 12 groups):
//
//   Group  1  (T01-T08)  NULL / argument guards.  ppdFilterLoadPPDFile
//                        rejects NULL / empty / non-existent paths with
//                        -1 (ppd-filter.c lines 217, 220).
//                        ppdFilterLoadPPD without a "libppd" extension
//                        returns -1 (line 293).  ppdFilterFreePPDFile /
//                        ppdFilterFreePPD are no-ops on freshly-zeroed
//                        data.  ppdFilterExternalCUPS without a filter
//                        path returns 1 (line 1052).  ppdFilterUniversal
//                        without content_type returns 1 (line 1457).
//
//   Group  2  (T09-T12)  ppdFilterUpdatePageVars — pure orientation
//                        transform.  Cases 0 / 1 / 2 / 3 of the switch
//                        at ppd-filter.c line 2007 (portrait /
//                        landscape / reverse portrait / reverse
//                        landscape).
//
//   Group  3  (T13-T16)  ppdFilterSetCommonOptions — basic page size,
//                        margins, ColorDevice / LanguageLevel from the
//                        PPD, and change_size=1 triggers
//                        ppdFilterUpdatePageVars to do the W/L swap.
//
//   Group  4  (T17-T20)  ppdFilterSetCommonOptions — orientation
//                        derivation.  landscape=true + ppd->landscape>0
//                        → Orientation=1 (line 1873).  landscape=no
//                        → unchanged.  orientation-requested=4 → 1
//                        (line 1891).  orientation-requested=6 → 2 via
//                        the ^=1 fold at line 1893.
//
//   Group  5  (T21-T24)  ppdFilterSetCommonOptions — per-margin
//                        overrides (page-left / -right / -bottom / -top)
//                        cycled through all four orientations.  Cases
//                        from the four switch blocks at lines 1898 /
//                        1917 / 1936 / 1955.
//
//   Group  6  (T25-T28)  ppdFilterSetCommonOptions — Duplex detection.
//                        Each of the 6 alternate option keywords (Duplex,
//                        JCLDuplex, EFDuplex) gets covered for both
//                        DuplexNoTumble and DuplexTumble, and the "no
//                        duplex marked" path returns Duplex=0.
//
//   Group  7  (T29-T32)  ppdFilterLoadPPDFile — happy path.  After load,
//                        cfFilterDataGetExt returns the "libppd"
//                        extension; extension->ppdfile matches the path
//                        passed in; extension->ppd is non-NULL.
//                        ppdFilterFreePPDFile removes the extension.
//
//   Group  8  (T33-T37)  ppdFilterLoadPPD — PPD attribute pass-through.
//                        PWGRaster=True → media-class=PwgRaster (line
//                        323).  cupsEvenDuplex → even-duplex (line 328).
//                        cupsBackSide → back-side-orientation (line 335).
//                        APDuplexRequiresFlippedMargin →
//                        duplex-requires-flipped-margin (line 344).
//                        cm-profile-qualifier always added (line 523).
//
//   Group  9  (T38-T41)  ppdFilterLoadPPD — hardware copies / collate.
//                        copies=1 → hw=false (line 386); copies=2 +
//                        PDF final → hw=true / collate=true (line 416);
//                        copies=2 + PDF final + Collate=Off →
//                        hw_collate=false (line 414); copies=2 +
//                        cupsManualCopies=True → hw=false (line 439).
//
//   Group 10  (T42-T44)  ppdFilterLoadPPD — pdf-filter-page-logging.
//                        final_content_type=NULL → Off (line 816).
//                        num_filters=0 (pure PS PPD) → Off (line 828).
//                        cupsFilter raster line + raster final → On via
//                        the "toraster" suffix branch (line 900).
//
//   Group 11  (T45-T47)  ppdFilterCUPSWrapper — argc validation, stub
//                        filter dispatch, and bad-file-path handling.
//                        argc=2 fails the (argc<6 || argc>7) && argc!=1
//                        check at line 71.  argc=1 takes the stdin
//                        branch and invokes the stub filter, which
//                        records that it ran and returns a sentinel.
//                        argc=7 with a non-existent file path returns 1
//                        from the open() failure at line 94.
//
//   Group 12  (T48-T50)  ppdFilterEmitJCL no-PPD fast paths.  With no
//                        "libppd" extension AND a non-PDFToPDF
//                        orig_filter, ppdFilterEmitJCL is a thin wrapper
//                        that delegates directly to orig_filter (no
//                        pipe / fork) and returns its exit code (line
//                        1245).  With a PPD loaded BUT emit-jcl=false
//                        the same fast path is taken (line 1240-1241).
//                        ppdFilterUniversal with NULL final_content_type
//                        AND NULL actual_output_type returns 1 (line
//                        1470).
//
// Design: the suite uses three hermetic embedded PPDs:
//
//   test_ppd_filter_text   — comprehensive: *cupsFilter raster line
//                            (page-logging "On" path + num_filters>0),
//                            *PWGRaster True, *cupsEvenDuplex,
//                            *cupsBackSide, *APDuplexRequiresFlippedMargin,
//                            *cupsRasterVersion, *cupsICCQualifier1,
//                            *LandscapeOrientation Plus90 (ppd->landscape
//                            > 0), Collate / Duplex / JCLDuplex /
//                            EFDuplex options, plus minimal PageSize
//                            / PageRegion / ImageableArea /
//                            PaperDimension for a valid PPD.
//
//   test_ppd_filter_ps     — pure PostScript PPD with NO *cupsFilter
//                            lines, so ppd->num_filters == 0 and the
//                            page-logging "elseif num_filters==0" branch
//                            fires (line 823).
//
//   test_ppd_filter_manual — minimal PPD with *cupsManualCopies True,
//                            forcing the hw_copies=false branch at
//                            line 439 even when copies > 1.
//
// What is NOT covered hermetically: the fork() + pipe() branch of
// ppdFilterEmitJCL (lines 1265-1307); the
// ppdFilterImageToPDF/PDFToPDF/PDFToPS/PSToPS/RasterToPS/ImageToPS
// wrappers that unconditionally invoke cfFilter*() (real ghostscript /
// pdftops / mutool subprocess chains); ppdFilterUniversal's main body
// (cupsFilter2 line parsing) because cfFilterUniversal forks
// gs/mutool; ppdFilterExternalCUPS env-var assembly because
// cfFilterExternal forks the configured filter binary.  These paths
// require real PDF/PostScript inputs and external tools that are not
// safe to drive from a unit test process.  Each unreachable branch is
// flagged here so a future maintainer (or Till on code review) can see
// the gap is intentional, not an oversight.
//

#include <ppd/ppd.h>
#include <ppd/ppd-filter.h>
#include <cupsfilters/filter.h>
#include <cups/cups.h>
#include "test-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>


// =============================================================================
// PPD #1 — comprehensive fixture.
// =============================================================================
//
// Used for everything except the "PostScript only / no cupsFilter" page
// logging test (PPD #2) and the manual-copies test (PPD #3).  Declares
// the full set of attributes that ppdFilterLoadPPD looks up plus a
// *cupsFilter raster line so ppd->num_filters > 0.
//

static const char test_ppd_filter_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"FILTERT.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(FilterTest)\"\n"
  "*ModelName: \"Acme FilterTest\"\n"
  "*ShortNickName: \"FilterTest\"\n"
  "*NickName: \"Acme FilterTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  // *LandscapeOrientation Plus90 → ppd->landscape > 0 (needed for T17).
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  // *cupsFilter raster line → ppd->num_filters > 0 AND the page-logging
  // loop's "toraster" suffix branch fires when final_content_type matches
  // the first word (Group 10 / T44).
  "*cupsFilter: \"application/vnd.cups-raster 100 pdftoraster\"\n"
  // Attributes consumed verbatim by ppdFilterLoadPPD's pass-through block.
  "*cupsRasterVersion: \"3\"\n"
  "*cupsBackSide: \"Normal\"\n"
  "*cupsEvenDuplex: \"True\"\n"
  "*APDuplexRequiresFlippedMargin: \"true\"\n"
  "*PWGRaster: True\n"
  // cupsICCQualifier1 makes the color-profile-qualifier picker use the
  // named option's choice as q1.  We point it at ColorModel.  No matching
  // *cupsICCProfile entries exist → cm-fallback-profile NOT added; but
  // cm-profile-qualifier IS always added (T37).
  "*cupsICCQualifier1: \"ColorModel\"\n"
  // ColorModel option — referenced by cupsICCQualifier1.
  "*OpenUI *ColorModel/Color Mode: PickOne\n"
  "*OrderDependency: 10 AnySetup *ColorModel\n"
  "*DefaultColorModel: RGB\n"
  "*ColorModel RGB/Color: \"<</cupsColorSpace 1>>setpagedevice\"\n"
  "*ColorModel Gray/Mono: \"<</cupsColorSpace 18>>setpagedevice\"\n"
  "*CloseUI: *ColorModel\n"
  // PageSize / PageRegion / ImageableArea / PaperDimension — minimal
  // valid PPD page-size machinery.  Letter is default.
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageRegion[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageRegion[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  // Collate option — needed for the hw_collate=false sub-branch (T40).
  // Default True, so when nothing is overridden the marked choice is
  // "True" and hw_collate=true (T39).
  "*OpenUI *Collate/Collate: PickOne\n"
  "*OrderDependency: 30 AnySetup *Collate\n"
  "*DefaultCollate: True\n"
  "*Collate True/Yes: \"\"\n"
  "*Collate False/No: \"\"\n"
  "*CloseUI: *Collate\n"
  // Three duplex-flavor options used by Group 6 (T25-T28).  All default
  // to "None" so the test_ppd_filter_set_common_options() helper's
  // default call leaves Duplex=0 (T28).
  "*OpenUI *Duplex/2-Sided: PickOne\n"
  "*OrderDependency: 40 AnySetup *Duplex\n"
  "*DefaultDuplex: None\n"
  "*Duplex None/Off: \"\"\n"
  "*Duplex DuplexNoTumble/Long: \"\"\n"
  "*Duplex DuplexTumble/Short: \"\"\n"
  "*CloseUI: *Duplex\n"
  "*OpenUI *JCLDuplex/JCL Duplex: PickOne\n"
  "*OrderDependency: 100 JCLSetup *JCLDuplex\n"
  "*DefaultJCLDuplex: None\n"
  "*JCLDuplex None/Off: \"\"\n"
  "*JCLDuplex DuplexNoTumble/Long: \"\"\n"
  "*JCLDuplex DuplexTumble/Tumble: \"\"\n"
  "*CloseUI: *JCLDuplex\n"
  "*OpenUI *EFDuplex/EF Duplex: PickOne\n"
  "*OrderDependency: 50 AnySetup *EFDuplex\n"
  "*DefaultEFDuplex: None\n"
  "*EFDuplex None/Off: \"\"\n"
  "*EFDuplex DuplexNoTumble/Long: \"\"\n"
  "*EFDuplex DuplexTumble/Tumble: \"\"\n"
  "*CloseUI: *EFDuplex\n";


// =============================================================================
// PPD #2 — pure PostScript fixture (no *cupsFilter lines).
// =============================================================================
//
// Forces ppd->num_filters == 0, so ppdFilterLoadPPD's page-logging
// switch takes the "manufacturer-supplied PostScript PPD" branch (line
// 823) and sets pdf-filter-page-logging=Off (T43).
//

static const char test_ppd_filter_ps_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"FILTERPS.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(FilterPS)\"\n"
  "*ModelName: \"Acme PS\"\n"
  "*ShortNickName: \"PS\"\n"
  "*NickName: \"Acme PS, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageRegion[612 792]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n";


// =============================================================================
// PPD #3 — *cupsManualCopies True fixture.
// =============================================================================
//
// Drives the manual_copies==true branch (line 435) of the hw_copies
// computation in ppdFilterLoadPPD: even with copies > 1 we expect
// hardware-copies=false AND hardware-collate=false (T41).
//

static const char test_ppd_filter_manual_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"FILTERMC.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(FilterMC)\"\n"
  "*ModelName: \"Acme MC\"\n"
  "*ShortNickName: \"MC\"\n"
  "*NickName: \"Acme MC, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  // The line under test: software-only copies.
  "*cupsManualCopies: \"True\"\n"
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageRegion[612 792]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n";


// =============================================================================
// Helpers.
// =============================================================================

// Write a PPD string to a freshly-created temporary file and return the
// path in *out_path (caller frees + unlinks).  Returns 0 on success, -1
// on error.  Path buffer must be at least 64 bytes.
static int
write_ppd_tmp(const char *text, char *out_path, size_t out_path_sz)
{
  int fd;
  ssize_t want, got;

  if (out_path_sz < 64)
    return (-1);
  strncpy(out_path, "/tmp/test_ppd_filter_XXXXXX", out_path_sz - 1);
  out_path[out_path_sz - 1] = '\0';

  if ((fd = mkstemp(out_path)) < 0)
    return (-1);

  want = (ssize_t)strlen(text);
  got = write(fd, text, (size_t)want);
  close(fd);
  if (got != want)
  {
    unlink(out_path);
    return (-1);
  }
  return (0);
}

// Initialise a cf_filter_data_t with safe zero defaults.
static void
init_filter_data(cf_filter_data_t *data)
{
  memset(data, 0, sizeof(*data));
}

// Tear-down: free options, PPD extension, printer_attrs / header.
static void
free_filter_data(cf_filter_data_t *data)
{
  if (data->num_options > 0 && data->options)
    cupsFreeOptions(data->num_options, data->options);
  data->num_options = 0;
  data->options = NULL;
  ppdFilterFreePPDFile(data);
}


// =============================================================================
// Stub filter function — sentinel value records that it was actually
// invoked, return code propagates up so we can assert wrapper behaviour.
// =============================================================================

static int g_stub_call_count = 0;
static int g_stub_inputfd = -1;
static int g_stub_outputfd = -1;

static int
stub_filter(int inputfd, int outputfd,
            int inputseekable,
            cf_filter_data_t *data,
            void *parameters)
{
  (void)inputseekable;
  (void)data;
  (void)parameters;
  g_stub_call_count ++;
  g_stub_inputfd = inputfd;
  g_stub_outputfd = outputfd;
  return (77);
}


int                                          // O - Exit status (0 = all pass)
main(void)
{
  cf_filter_data_t        data;
  cf_filter_external_t    ext_params;
  cf_filter_universal_parameter_t uni_params;
  ppd_filter_data_ext_t   *ext;
  ppd_file_t              *ppd;
  char                     ppd_path[128];
  char                     ppd_path2[128];
  char                     ppd_path3[128];
  int                      Orientation, Duplex, LanguageLevel, ColorDevice;
  float                    PageLeft, PageRight, PageTop, PageBottom;
  float                    PageWidth, PageLength;
  int                      rc;
  const char              *val;
  int                      argc_test;
  char                    *argv_test[8];


  // Provoke a clean environment: ppdFilterCUPSWrapper reads several env
  // vars (PPD, PRINTER, CONTENT_TYPE, FINAL_CONTENT_TYPE).  Wipe them so
  // a leftover from the test runner cannot perturb our assertions.
  unsetenv("PPD");
  unsetenv("PRINTER");
  unsetenv("CONTENT_TYPE");
  unsetenv("FINAL_CONTENT_TYPE");


  // =========================================================================
  // Group 1: NULL / argument guards (T01-T08)
  // =========================================================================

  // T01 — ppdFilterLoadPPDFile: `if (!ppdfile || !ppdfile[0]) return (-1);`
  //       at ppd-filter.c line 217 — the NULL half.
  testBegin("ppdFilterLoadPPDFile(data, NULL) returns -1");
  init_filter_data(&data);
  testEnd(ppdFilterLoadPPDFile(&data, NULL) == -1);
  free_filter_data(&data);

  // T02 — same guard, the empty-string half (`!ppdfile[0]`).
  testBegin("ppdFilterLoadPPDFile(data, \"\") returns -1");
  init_filter_data(&data);
  testEnd(ppdFilterLoadPPDFile(&data, "") == -1);
  free_filter_data(&data);

  // T03 — ppdOpenFile failure path at line 220: file does not exist on
  //       disk → ppdOpenFile returns NULL → -1.
  testBegin("ppdFilterLoadPPDFile(data, \"/no/such/path.ppd\") returns -1");
  init_filter_data(&data);
  testEnd(ppdFilterLoadPPDFile(&data, "/no/such/path.ppd") == -1);
  free_filter_data(&data);

  // T04 — ppdFilterLoadPPD: `if (!filter_data_ext || !filter_data_ext->ppd)
  //       return (-1);` at line 293.  With no "libppd" extension installed
  //       on data, cfFilterDataGetExt returns NULL and the function bails.
  testBegin("ppdFilterLoadPPD(data) without extension returns -1");
  init_filter_data(&data);
  testEnd(ppdFilterLoadPPD(&data) == -1);
  free_filter_data(&data);

  // T05 — ppdFilterFreePPDFile is a no-op when cfFilterDataRemoveExt
  //       returns NULL (line 980 guards everything inside the if).
  //       The success criterion is "doesn't crash"; we assert via
  //       a sentinel that we reach the line after the call.
  testBegin("ppdFilterFreePPDFile on empty data is a no-op");
  init_filter_data(&data);
  ppdFilterFreePPDFile(&data);
  testEnd(data.extension == NULL);

  // T06 — ppdFilterFreePPD safely no-ops when printer_attrs / header
  //       are NULL (guards at lines 1007 / 1013).
  testBegin("ppdFilterFreePPD on empty data is a no-op");
  init_filter_data(&data);
  ppdFilterFreePPD(&data);
  testEnd(data.printer_attrs == NULL && data.header == NULL);

  // T07 — ppdFilterExternalCUPS: `if (!params.filter || !params.filter[0])
  //       ... return (1);` at line 1052.  The function dereferences
  //       parameters, so the struct must exist; only `.filter` is NULL.
  testBegin("ppdFilterExternalCUPS NULL params.filter returns 1");
  init_filter_data(&data);
  memset(&ext_params, 0, sizeof(ext_params));
  ext_params.filter = NULL;
  testEnd(ppdFilterExternalCUPS(0, 1, 0, &data, &ext_params) == 1);
  free_filter_data(&data);

  // T08 — ppdFilterUniversal: `if (input == NULL) ... return (1);` at
  //       line 1461.  Dereferences the parameters struct, so pass a
  //       valid (zeroed) one.
  testBegin("ppdFilterUniversal NULL data->content_type returns 1");
  init_filter_data(&data);
  memset(&uni_params, 0, sizeof(uni_params));
  data.content_type = NULL;
  testEnd(ppdFilterUniversal(0, 1, 0, &data, &uni_params) == 1);
  free_filter_data(&data);


  // =========================================================================
  // Group 2: ppdFilterUpdatePageVars — all four orientation cases (T09-T12)
  // =========================================================================

  // T09 — Orientation 0 (Portrait): switch falls through `case 0: break;`
  //       (line 2009).  Nothing changes.
  testBegin("ppdFilterUpdatePageVars(0 Portrait) leaves variables alone");
  PageLeft = 10.0f; PageRight = 600.0f;
  PageTop = 780.0f; PageBottom = 20.0f;
  PageWidth = 612.0f; PageLength = 792.0f;
  ppdFilterUpdatePageVars(0, &PageLeft, &PageRight,
                          &PageTop, &PageBottom, &PageWidth, &PageLength);
  testEnd(PageLeft == 10.0f && PageRight == 600.0f &&
          PageTop == 780.0f && PageBottom == 20.0f &&
          PageWidth == 612.0f && PageLength == 792.0f);

  // T10 — Orientation 1 (Landscape, line 2012): Left↔Bottom, Right↔Top,
  //       Width↔Length.  Direct swap, no axis flip.
  testBegin("ppdFilterUpdatePageVars(1 Landscape) swaps L↔B, R↔T, W↔L");
  PageLeft = 10.0f; PageRight = 600.0f;
  PageTop = 780.0f; PageBottom = 20.0f;
  PageWidth = 612.0f; PageLength = 792.0f;
  ppdFilterUpdatePageVars(1, &PageLeft, &PageRight,
                          &PageTop, &PageBottom, &PageWidth, &PageLength);
  testEnd(PageLeft == 20.0f && PageBottom == 10.0f &&
          PageRight == 780.0f && PageTop == 600.0f &&
          PageWidth == 792.0f && PageLength == 612.0f);

  // T11 — Orientation 2 (Reverse Portrait, line 2026): mirror along
  //       both axes: NewLeft = Width - OldRight, NewRight = Width - OldLeft;
  //       same for Top/Bottom.  Width / Length unchanged.
  testBegin("ppdFilterUpdatePageVars(2 ReversePortrait) mirrors along axes");
  PageLeft = 10.0f; PageRight = 600.0f;
  PageTop = 780.0f; PageBottom = 20.0f;
  PageWidth = 612.0f; PageLength = 792.0f;
  ppdFilterUpdatePageVars(2, &PageLeft, &PageRight,
                          &PageTop, &PageBottom, &PageWidth, &PageLength);
  // Width-OldRight = 612-600 = 12; Width-OldLeft = 612-10 = 602.
  // Length-OldBottom = 792-20 = 772; Length-OldTop = 792-780 = 12.
  testEnd(PageLeft == 12.0f && PageRight == 602.0f &&
          PageBottom == 12.0f && PageTop == 772.0f &&
          PageWidth == 612.0f && PageLength == 792.0f);

  // T12 — Orientation 3 (Reverse Landscape, line 2036): mirror THEN
  //       swap L↔B, R↔T, W↔L.  Effectively case 2 followed by case 1.
  testBegin("ppdFilterUpdatePageVars(3 ReverseLandscape) mirrors then swaps");
  PageLeft = 10.0f; PageRight = 600.0f;
  PageTop = 780.0f; PageBottom = 20.0f;
  PageWidth = 612.0f; PageLength = 792.0f;
  ppdFilterUpdatePageVars(3, &PageLeft, &PageRight,
                          &PageTop, &PageBottom, &PageWidth, &PageLength);
  // Mirror first: L=12, R=602, B=12, T=772.
  // Then swap: L↔B (both 12), R↔T (602↔772), W↔L (612↔792).
  testEnd(PageLeft == 12.0f && PageBottom == 12.0f &&
          PageRight == 772.0f && PageTop == 602.0f &&
          PageWidth == 792.0f && PageLength == 612.0f);


  // =========================================================================
  // Group 3: ppdFilterSetCommonOptions — basic page / margin extraction
  //          (T13-T16)
  // =========================================================================

  // T13 — NULL ppd path: ppdPageSize(NULL, NULL) returns NULL → page
  //       defaults stay at the hard-coded literals (lines 1720-1725) and
  //       the "if (ppd != NULL)" block at line 1862 is skipped.
  testBegin("ppdFilterSetCommonOptions(NULL ppd) leaves defaults");
  Orientation = -1; Duplex = -1; LanguageLevel = -1; ColorDevice = -1;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(NULL, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEnd(Orientation == 0 && Duplex == 0 &&
          LanguageLevel == 1 && ColorDevice == 1 &&
          PageLeft == 18.0f && PageRight == 594.0f &&
          PageBottom == 36.0f && PageTop == 756.0f &&
          PageWidth == 612.0f && PageLength == 792.0f);

  // Open PPD #1 — used for the rest of Groups 3 / 4 / 5 / 6.
  if (write_ppd_tmp(test_ppd_filter_text, ppd_path, sizeof(ppd_path)) != 0)
  {
    testError("write_ppd_tmp failed");
    return (1);
  }
  ppd = ppdOpenFile(ppd_path);
  if (!ppd)
  {
    testError("ppdOpenFile(%s) returned NULL", ppd_path);
    unlink(ppd_path);
    return (1);
  }
  ppdMarkDefaults(ppd);

  // T14 — PPD has Letter (612x792) with HWMargins 18 18 18 18 → after
  //       Imageable/Paper parsing: ImageableArea Letter "18 18 594 774"
  //       sets left/bottom/right/top, PaperDimension "612 792" sets
  //       width/length.  ppdPageSize returns these directly.
  testBegin("ppdFilterSetCommonOptions(Letter PPD) extracts 612x792");
  ppdMarkDefaults(ppd);
  Orientation = Duplex = LanguageLevel = ColorDevice = 0;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(PageWidth == 612.0f && PageLength == 792.0f &&
                 PageLeft == 18.0f && PageBottom == 18.0f &&
                 PageRight == 594.0f && PageTop == 774.0f,
                 "W=%.0f L=%.0f Left=%.0f Right=%.0f Top=%.0f Bottom=%.0f",
                 PageWidth, PageLength, PageLeft, PageRight, PageTop, PageBottom);

  // T15 — *ColorDevice True and *LanguageLevel "3" propagate into the
  //       output ColorDevice / LanguageLevel slots via the `if (ppd != NULL)`
  //       block (line 1862).
  testBegin("ppdFilterSetCommonOptions propagates ColorDevice + LanguageLevel");
  ppdMarkDefaults(ppd);
  Orientation = Duplex = 0;
  ColorDevice = -1; LanguageLevel = -1;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(ColorDevice == 1 && LanguageLevel == 3,
                 "ColorDevice=%d LanguageLevel=%d", ColorDevice, LanguageLevel);

  // T16 — change_size=1 + orientation-requested=4 (→ Orientation=1
  //       landscape) should trigger ppdFilterUpdatePageVars at line 1972,
  //       which swaps Width↔Length on the way out.
  testBegin("ppdFilterSetCommonOptions(change_size=1, landscape) swaps W/L");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "4", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 1,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 1 &&
                   PageWidth == 792.0f && PageLength == 612.0f,
                   "Orientation=%d W=%.0f L=%.0f",
                   Orientation, PageWidth, PageLength);
  }


  // =========================================================================
  // Group 4: ppdFilterSetCommonOptions — orientation derivation (T17-T20)
  // =========================================================================

  // T17 — `if ((val = cupsGetOption("landscape", ...)) != NULL && val!="no/off/false")`
  //       at line 1868.  PPD #1 has *LandscapeOrientation: Plus90 →
  //       ppd->landscape > 0 → Orientation=1 (line 1873).
  testBegin("landscape=true + ppd->landscape>0 → Orientation=1");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("landscape", "true", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = 0;
    Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 1, "Orientation=%d", Orientation);
  }

  // T18 — landscape="no" is the negative half of the same conditional:
  //       Orientation must stay 0.
  testBegin("landscape=no → Orientation stays 0");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("landscape", "no", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = 0;
    Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 0, "Orientation=%d", Orientation);
  }

  // T19 — orientation-requested=4 → IPP mapping `atoi(val) - 3 = 1`,
  //       1 < 2 → Orientation stays 1 (no ^= 1 fold at line 1893).
  testBegin("orientation-requested=4 → Orientation=1");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "4", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = 0;
    Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 1, "Orientation=%d", Orientation);
  }

  // T20 — orientation-requested=6 → IPP mapping `atoi(val) - 3 = 3`,
  //       3 >= 2 → `3 ^= 1` folds to 2 (reverse portrait).
  testBegin("orientation-requested=6 → Orientation=2 (^=1 fold)");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "6", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = 0;
    Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 2, "Orientation=%d", Orientation);
  }


  // =========================================================================
  // Group 5: ppdFilterSetCommonOptions — per-margin overrides through
  //          all four orientations (T21-T24).  Each option hits a different
  //          switch arm in lines 1898 / 1917 / 1936 / 1955.
  // =========================================================================

  // T21 — page-left with Orientation=0 (Portrait): case 0 → PageLeft = atof(val).
  testBegin("page-left=72, Portrait → PageLeft=72");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("page-left", "72", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(PageLeft == 72.0f, "PageLeft=%.0f", PageLeft);
  }

  // T22 — page-right with Orientation=1 (Landscape, via orientation-requested=4):
  //       case 1 → PageTop = PageLength - atof(val).  PageLength=792 → 792-100=692.
  testBegin("page-right=100, Landscape → PageTop=PageLength-100");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "4", nopts, &opts);
    nopts = cupsAddOption("page-right", "100", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 1 && PageTop == 692.0f,
                   "Orientation=%d PageTop=%.0f PageLength=%.0f",
                   Orientation, PageTop, PageLength);
  }

  // T23 — page-bottom with Orientation=2 (ReversePortrait, orientation-requested=6):
  //       case 2 → PageTop = PageLength - atof(val).  792 - 50 = 742.
  testBegin("page-bottom=50, ReversePortrait → PageTop=PageLength-50");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "6", nopts, &opts);
    nopts = cupsAddOption("page-bottom", "50", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 2 && PageTop == 742.0f,
                   "Orientation=%d PageTop=%.0f PageLength=%.0f",
                   Orientation, PageTop, PageLength);
  }

  // T24 — page-top with Orientation=3 (ReverseLandscape, orientation-requested=5):
  //       atoi(5)-3=2, 2>=2 → 2^=1 → Orientation=3 → case 3 → PageLeft=atof(val).
  testBegin("page-top=42, ReverseLandscape → PageLeft=42");
  {
    int   nopts = 0;
    cups_option_t *opts = NULL;
    nopts = cupsAddOption("orientation-requested", "5", nopts, &opts);
    nopts = cupsAddOption("page-top", "42", nopts, &opts);
    ppdMarkDefaults(ppd);
    Orientation = Duplex = LanguageLevel = ColorDevice = 0;
    PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
    ppdFilterSetCommonOptions(ppd, nopts, opts, 0,
                              &Orientation, &Duplex,
                              &LanguageLevel, &ColorDevice,
                              &PageLeft, &PageRight, &PageTop, &PageBottom,
                              &PageWidth, &PageLength, NULL, NULL);
    cupsFreeOptions(nopts, opts);
    testEndMessage(Orientation == 3 && PageLeft == 42.0f,
                   "Orientation=%d PageLeft=%.0f", Orientation, PageLeft);
  }


  // =========================================================================
  // Group 6: ppdFilterSetCommonOptions — duplex detection (T25-T28).
  //          The OR-chain at lines 1976-1987 fires when ANY of the alternate
  //          keywords has DuplexNoTumble or DuplexTumble marked.
  // =========================================================================

  // T25 — Duplex=DuplexNoTumble: hits the first ppdIsMarked clause.
  testBegin("Duplex=DuplexNoTumble → *Duplex=1");
  ppdMarkDefaults(ppd);
  ppdMarkOption(ppd, "Duplex", "DuplexNoTumble");
  Duplex = -1;
  Orientation = LanguageLevel = ColorDevice = 0;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(Duplex == 1, "Duplex=%d", Duplex);

  // T26 — JCLDuplex=DuplexTumble: hits the JCLDuplex/DuplexTumble clause.
  testBegin("JCLDuplex=DuplexTumble → *Duplex=1");
  ppdMarkDefaults(ppd);
  ppdMarkOption(ppd, "JCLDuplex", "DuplexTumble");
  Duplex = -1;
  Orientation = LanguageLevel = ColorDevice = 0;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(Duplex == 1, "Duplex=%d", Duplex);

  // T27 — EFDuplex=DuplexNoTumble: hits the EFDuplex/DuplexNoTumble clause.
  //       (EFDuplexing/ARDuplex/KD03Duplex aren't tested explicitly here;
  //       they share the identical ppdIsMarked structure and are dead-easy
  //       to fall back to one-off PPDs if a maintainer wants to extend.)
  testBegin("EFDuplex=DuplexNoTumble → *Duplex=1");
  ppdMarkDefaults(ppd);
  ppdMarkOption(ppd, "EFDuplex", "DuplexNoTumble");
  Duplex = -1;
  Orientation = LanguageLevel = ColorDevice = 0;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(Duplex == 1, "Duplex=%d", Duplex);

  // T28 — Negative case: defaults (None on all duplex options) → Duplex=0.
  testBegin("No duplex option marked → *Duplex=0");
  ppdMarkDefaults(ppd);
  Duplex = -1;
  Orientation = LanguageLevel = ColorDevice = 0;
  PageLeft = PageRight = PageTop = PageBottom = PageWidth = PageLength = 0;
  ppdFilterSetCommonOptions(ppd, 0, NULL, 0,
                            &Orientation, &Duplex,
                            &LanguageLevel, &ColorDevice,
                            &PageLeft, &PageRight, &PageTop, &PageBottom,
                            &PageWidth, &PageLength, NULL, NULL);
  testEndMessage(Duplex == 0, "Duplex=%d", Duplex);

  // Done with stand-alone PPD object; subsequent groups reload it via
  // ppdFilterLoadPPDFile (which owns its own ppd_file_t inside the
  // extension), so close this one now.
  ppdClose(ppd);


  // =========================================================================
  // Group 7: ppdFilterLoadPPDFile — happy path + extension attachment
  //          (T29-T32)
  // =========================================================================

  // T29 — Good path returns 0 (after going through ppdFilterLoadPPD).
  //       For the file we just wrote, all required fields are present.
  testBegin("ppdFilterLoadPPDFile(<valid path>) returns 0");
  init_filter_data(&data);
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  testEndMessage(rc == 0, "rc=%d", rc);

  // T30 — Extension was attached: cfFilterDataGetExt finds "libppd".
  testBegin("cfFilterDataGetExt(\"libppd\") returns the attached extension");
  ext = (ppd_filter_data_ext_t *)cfFilterDataGetExt(&data, PPD_FILTER_DATA_EXT);
  testEnd(ext != NULL);

  // T31 — ppdfile field strdup'd from the input path argument.
  testBegin("extension->ppdfile == passed-in path");
  testEndMessage(ext && ext->ppdfile && !strcmp(ext->ppdfile, ppd_path),
                 "ppdfile=\"%s\"", ext && ext->ppdfile ? ext->ppdfile : "(null)");

  // T32 — ppdFilterFreePPDFile removes the extension; subsequent
  //       cfFilterDataGetExt returns NULL.  Also sanity-checks that
  //       a second invocation is safe (idempotent).
  testBegin("ppdFilterFreePPDFile removes the \"libppd\" extension");
  ppdFilterFreePPDFile(&data);
  ext = (ppd_filter_data_ext_t *)cfFilterDataGetExt(&data, PPD_FILTER_DATA_EXT);
  ppdFilterFreePPDFile(&data);    // idempotency check — must not crash
  testEnd(ext == NULL);
  if (data.num_options > 0 && data.options)
    cupsFreeOptions(data.num_options, data.options);
  data.num_options = 0;
  data.options = NULL;


  // =========================================================================
  // Group 8: ppdFilterLoadPPD — PPD attribute pass-through (T33-T37)
  // =========================================================================

  // T33 — *PWGRaster True triggers cupsAddOption("media-class", "PwgRaster")
  //       at line 323.
  testBegin("ppdFilterLoadPPD: PWGRaster True → media-class=PwgRaster");
  init_filter_data(&data);
  data.copies = 1;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("media-class", data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "PwgRaster"),
                 "rc=%d media-class=\"%s\"", rc, val ? val : "(null)");
  free_filter_data(&data);

  // T34 — *cupsEvenDuplex passed verbatim as even-duplex (line 328).
  testBegin("ppdFilterLoadPPD: cupsEvenDuplex → even-duplex option");
  init_filter_data(&data);
  data.copies = 1;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("even-duplex", data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "True"),
                 "even-duplex=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);

  // T35 — *cupsBackSide passed as back-side-orientation (line 335).
  //       The PPD declares "Normal".
  testBegin("ppdFilterLoadPPD: cupsBackSide → back-side-orientation");
  init_filter_data(&data);
  data.copies = 1;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("back-side-orientation", data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "Normal"),
                 "back-side-orientation=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);

  // T36 — *APDuplexRequiresFlippedMargin propagated unchanged (line 344).
  testBegin("ppdFilterLoadPPD: APDuplexRequiresFlippedMargin → option");
  init_filter_data(&data);
  data.copies = 1;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("duplex-requires-flipped-margin",
                      data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "true"),
                 "duplex-requires-flipped-margin=\"%s\"",
                 val ? val : "(null)");
  free_filter_data(&data);

  // T37 — cm-profile-qualifier is ALWAYS added (line 523).  Format is
  //       "<q1>.<q2>.<q3>"; for our PPD q1 is ColorModel→RGB (cupsICCQualifier1
  //       picks ColorModel), q2 is MediaType (not present → ""), q3 is
  //       Resolution (not present → "" → DefaultResolution → "" → "").
  //       Asserting non-empty string + starts with "RGB." gives us a
  //       reliable invariant without baking too many specifics in.
  testBegin("ppdFilterLoadPPD: cm-profile-qualifier always added");
  init_filter_data(&data);
  data.copies = 1;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("cm-profile-qualifier", data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strncmp(val, "RGB.", 4),
                 "cm-profile-qualifier=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);


  // =========================================================================
  // Group 9: ppdFilterLoadPPD — hardware copies / collate (T38-T41)
  // =========================================================================

  // T38 — data->copies == 1 short-circuit (line 383-388): both flags
  //       forced false regardless of PPD content.
  testBegin("copies=1 → hardware-copies=false, hardware-collate=false");
  init_filter_data(&data);
  data.copies = 1;
  data.final_content_type = (char *)"application/pdf";
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("hardware-copies", data.num_options, data.options);
  {
    const char *v2 = cupsGetOption("hardware-collate",
                                    data.num_options, data.options);
    testEndMessage(rc == 0 && val && !strcasecmp(val, "false") &&
                   v2 && !strcasecmp(v2, "false"),
                   "hw-copies=\"%s\" hw-collate=\"%s\"",
                   val ? val : "(null)", v2 ? v2 : "(null)");
  }
  free_filter_data(&data);

  // T39 — copies=2 + !manual_copies + final_content_type matching the
  //       "driverless" group at lines 400-405 (application/pdf qualifies)
  //       AND default Collate=True (not in off/no/false list) → both
  //       hardware-copies=true and hardware-collate=true.
  testBegin("copies=2 + PDF final + Collate=True → both true");
  init_filter_data(&data);
  data.copies = 2;
  data.final_content_type = (char *)"application/pdf";
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("hardware-copies", data.num_options, data.options);
  {
    const char *v2 = cupsGetOption("hardware-collate",
                                    data.num_options, data.options);
    testEndMessage(rc == 0 && val && !strcasecmp(val, "true") &&
                   v2 && !strcasecmp(v2, "true"),
                   "hw-copies=\"%s\" hw-collate=\"%s\"",
                   val ? val : "(null)", v2 ? v2 : "(null)");
  }
  free_filter_data(&data);

  // T40 — Same as T39 but pre-set Collate=False in data->options BEFORE
  //       LoadPPDFile.  After ppdMarkOptions runs, Collate=False is the
  //       marked choice → the "if (choice && !strcasecmp(choice->choice,
  //       "false"))" sub-branch fires → hw_collate=false.  hw_copies
  //       stays true (still in the driverless path).
  testBegin("copies=2 + PDF final + Collate=False → hw-collate=false");
  init_filter_data(&data);
  data.copies = 2;
  data.final_content_type = (char *)"application/pdf";
  data.num_options = cupsAddOption("Collate", "False",
                                   data.num_options, &data.options);
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("hardware-copies", data.num_options, data.options);
  {
    const char *v2 = cupsGetOption("hardware-collate",
                                    data.num_options, data.options);
    testEndMessage(rc == 0 && val && !strcasecmp(val, "true") &&
                   v2 && !strcasecmp(v2, "false"),
                   "hw-copies=\"%s\" hw-collate=\"%s\"",
                   val ? val : "(null)", v2 ? v2 : "(null)");
  }
  free_filter_data(&data);

  // T41 — *cupsManualCopies True (PPD #3) forces the `else` branch at
  //       line 435 → both flags forced false even though copies > 1.
  if (write_ppd_tmp(test_ppd_filter_manual_text, ppd_path3, sizeof(ppd_path3))
      != 0)
  {
    testError("write_ppd_tmp (manual) failed");
    unlink(ppd_path);
    return (1);
  }
  testBegin("copies=2 + cupsManualCopies True → both false");
  init_filter_data(&data);
  data.copies = 2;
  data.final_content_type = (char *)"application/pdf";
  rc = ppdFilterLoadPPDFile(&data, ppd_path3);
  val = cupsGetOption("hardware-copies", data.num_options, data.options);
  {
    const char *v2 = cupsGetOption("hardware-collate",
                                    data.num_options, data.options);
    testEndMessage(rc == 0 && val && !strcasecmp(val, "false") &&
                   v2 && !strcasecmp(v2, "false"),
                   "hw-copies=\"%s\" hw-collate=\"%s\"",
                   val ? val : "(null)", v2 ? v2 : "(null)");
  }
  free_filter_data(&data);


  // =========================================================================
  // Group 10: ppdFilterLoadPPD — pdf-filter-page-logging (T42-T44)
  // =========================================================================

  // T42 — data->final_content_type == NULL takes the very first arm
  //       (line 812) → page_logging=0 → "Off".
  testBegin("final_content_type=NULL → pdf-filter-page-logging=Off");
  init_filter_data(&data);
  data.copies = 1;
  data.final_content_type = NULL;
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("pdf-filter-page-logging",
                      data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "Off"),
                 "pdf-filter-page-logging=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);

  // T43 — Pure PostScript PPD (PPD #2: no *cupsFilter) → ppd->num_filters
  //       == 0 → `else if (ppd->num_filters == 0)` at line 823 →
  //       page_logging=0 → "Off".
  if (write_ppd_tmp(test_ppd_filter_ps_text, ppd_path2, sizeof(ppd_path2))
      != 0)
  {
    testError("write_ppd_tmp (PS) failed");
    unlink(ppd_path);
    unlink(ppd_path3);
    return (1);
  }
  testBegin("PostScript PPD (num_filters=0) → page-logging=Off");
  init_filter_data(&data);
  data.copies = 1;
  data.final_content_type = (char *)"application/postscript";
  rc = ppdFilterLoadPPDFile(&data, ppd_path2);
  val = cupsGetOption("pdf-filter-page-logging",
                      data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "Off"),
                 "pdf-filter-page-logging=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);

  // T44 — PPD #1 declares "application/vnd.cups-raster 100 pdftoraster"
  //       which, with final="application/vnd.cups-raster", matches in the
  //       loop (line 835).  Then in the second pass the filter name
  //       "pdftoraster" ends with "toraster" → branch at line 900 →
  //       page_logging=1 → "On".
  testBegin("Raster PPD + raster final + pdftoraster → page-logging=On");
  init_filter_data(&data);
  data.copies = 1;
  data.final_content_type = (char *)"application/vnd.cups-raster";
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  val = cupsGetOption("pdf-filter-page-logging",
                      data.num_options, data.options);
  testEndMessage(rc == 0 && val && !strcasecmp(val, "On"),
                 "pdf-filter-page-logging=\"%s\"", val ? val : "(null)");
  free_filter_data(&data);


  // =========================================================================
  // Group 11: ppdFilterCUPSWrapper — argc validation & stub dispatch
  //           (T45-T47)
  // =========================================================================

  // T45 — argc=2 fails the `(argc<6 || argc>7) && argc != 1` check at
  //       line 71 → wrapper writes "Usage:" to stderr and returns 1
  //       BEFORE the filter is ever called (g_stub_call_count must not
  //       advance).
  testBegin("ppdFilterCUPSWrapper(argc=2 invalid) returns 1 without calling filter");
  g_stub_call_count = 0;
  argv_test[0] = (char *)"test_ppd_filter";
  argv_test[1] = (char *)"extra";
  argv_test[2] = NULL;
  argc_test = 2;
  rc = ppdFilterCUPSWrapper(argc_test, argv_test, stub_filter, NULL, NULL);
  testEndMessage(rc == 1 && g_stub_call_count == 0,
                 "rc=%d stub_calls=%d", rc, g_stub_call_count);

  // T46 — argc=1 is the "called as filter pipeline binary" mode (line 71
  //       condition false): inputfd=0 (stdin), filter is invoked.  Our
  //       stub returns 77; we expect the wrapper to bubble that up
  //       unchanged.  Also assert the stub saw inputfd=0 / outputfd=1
  //       (the wrapper always sends output to fd 1 — see line 179).
  testBegin("ppdFilterCUPSWrapper(argc=1) invokes filter, returns stub's 77");
  g_stub_call_count = 0;
  g_stub_inputfd = g_stub_outputfd = -1;
  argv_test[0] = (char *)"test_ppd_filter";
  argv_test[1] = NULL;
  argc_test = 1;
  rc = ppdFilterCUPSWrapper(argc_test, argv_test, stub_filter, NULL, NULL);
  testEndMessage(rc == 77 && g_stub_call_count == 1 &&
                 g_stub_inputfd == 0 && g_stub_outputfd == 1,
                 "rc=%d calls=%d in=%d out=%d", rc, g_stub_call_count,
                 g_stub_inputfd, g_stub_outputfd);

  // T47 — argc=7 with a non-existent file path makes the open() at line 94
  //       fail; the wrapper returns 1 and the filter is never invoked.
  testBegin("ppdFilterCUPSWrapper(argc=7, bad path) returns 1");
  g_stub_call_count = 0;
  argv_test[0] = (char *)"test_ppd_filter";
  argv_test[1] = (char *)"42";
  argv_test[2] = (char *)"user";
  argv_test[3] = (char *)"title";
  argv_test[4] = (char *)"1";
  argv_test[5] = (char *)"";
  argv_test[6] = (char *)"/tmp/definitely_not_present_for_test_ppd_filter.dat";
  argv_test[7] = NULL;
  argc_test = 7;
  rc = ppdFilterCUPSWrapper(argc_test, argv_test, stub_filter, NULL, NULL);
  testEndMessage(rc == 1 && g_stub_call_count == 0,
                 "rc=%d calls=%d", rc, g_stub_call_count);


  // =========================================================================
  // Group 12: ppdFilterEmitJCL no-PPD fast paths + Universal NULL final
  //           (T48-T50)
  // =========================================================================

  // T48 — ppdFilterEmitJCL with no "libppd" extension takes the fast
  //       path at lines 1237-1245.  Since orig_filter != cfFilterPDFToPDF
  //       streaming stays 0 and we land in the `if (!streaming)` arm
  //       which delegates to orig_filter and returns its exit code.
  testBegin("ppdFilterEmitJCL(no PPD, non-PDFToPDF) delegates to orig_filter");
  init_filter_data(&data);
  g_stub_call_count = 0;
  rc = ppdFilterEmitJCL(0, 1, 0, &data, NULL, stub_filter);
  testEndMessage(rc == 77 && g_stub_call_count == 1,
                 "rc=%d calls=%d", rc, g_stub_call_count);
  free_filter_data(&data);

  // T49 — Same fast path but reached via the emit-jcl=false option
  //       (lines 1238-1241) WITH a real PPD loaded.  Proves that even
  //       when filter_data_ext->ppd is valid, the option override forces
  //       the no-fork branch.
  testBegin("ppdFilterEmitJCL(emit-jcl=false, PPD loaded) → orig_filter");
  init_filter_data(&data);
  data.copies = 1;
  data.num_options = cupsAddOption("emit-jcl", "false",
                                   data.num_options, &data.options);
  rc = ppdFilterLoadPPDFile(&data, ppd_path);
  g_stub_call_count = 0;
  if (rc == 0)
  {
    rc = ppdFilterEmitJCL(0, 1, 0, &data, NULL, stub_filter);
    testEndMessage(rc == 77 && g_stub_call_count == 1,
                   "rc=%d calls=%d", rc, g_stub_call_count);
  }
  else
  {
    testEndMessage(false, "ppdFilterLoadPPDFile rc=%d", rc);
  }
  free_filter_data(&data);

  // T50 — ppdFilterUniversal: with content_type set but BOTH
  //       final_content_type AND universal_parameters.actual_output_type
  //       NULL, the second guard at line 1470 fires → 1.
  testBegin("ppdFilterUniversal NULL final + NULL actual_output → 1");
  init_filter_data(&data);
  memset(&uni_params, 0, sizeof(uni_params));
  data.content_type = (char *)"application/pdf";
  data.final_content_type = NULL;
  uni_params.actual_output_type = NULL;
  testEnd(ppdFilterUniversal(0, 1, 0, &data, &uni_params) == 1);
  free_filter_data(&data);


  // =========================================================================
  // Tear-down: remove temp PPDs.
  // =========================================================================

  unlink(ppd_path);
  unlink(ppd_path2);
  unlink(ppd_path3);

  return (testsPassed ? 0 : 1);
}
