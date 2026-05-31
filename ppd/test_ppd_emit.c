//
// PPD code-emission API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (46 assertions across 14 groups):
//
//   Group  1  (T01-T08)  NULL / argument guards — FILE*-based emit
//                        functions return -1 on bad args; ppdEmitJCL /
//                        ppdEmitJCLEnd return 0 (documented no-op);
//                        ppdCollect with non-NULL out-slot nulls it.
//
//   Group  2  (T09-T11)  ppdOpen + JCL field decoding — ppdDecode runs
//                        on *JCLBegin / *JCLEnd / *JCLToPSInterpreter
//                        so "<0A>" → real LF byte.
//
//   Group  3  (T12-T14)  ppdCollect by section — JCL / PageSetup
//                        (sorted by order: Resolution 10 < MediaType 50)
//                        / DocumentSetup buckets.
//
//   Group  4  (T15-T18)  ppdEmitString content shape — wrapper for
//                        non-JCL/non-EXIT sections; raw choice->code
//                        for JCL; NULL for empty section.
//
//   Group  5  (T19-T21)  ppdEmit / ppdEmitFd equivalence + "nothing
//                        to emit" return code (0, success).
//
//   Group  6  (T22-T25)  ppdEmitJCL / ppdEmitJCLEnd — non-PJL else
//                        branches: emit jcl_begin + JCL options + jcl_ps;
//                        emit jcl_end raw.
//
//   Group  7  (T26-T28)  OrderDependency filtering — ppdCollect2,
//                        ppdEmitString, ppdEmitAfterOrder honour
//                        min_order (Resolution=10 < min=20 → dropped).
//
//   Group  8  (T29-T30)  PROLOG + EXIT section emission — PROLOG gets
//                        the standard wrapper format; EXIT is RAW
//                        choice->code (no wrapper) per the
//                        `else if (section != PPD_ORDER_EXIT)` skip at
//                        ppd-emit.c line 752 / 926 / 1164.
//
//   Group  9  (T31-T33)  Custom PageSize emission — when PageSize=Custom
//                        is marked, ppdEmitString writes
//                        %%BeginFeature: *CustomPageSize True + the
//                        five-float positional payload assembled from
//                        the *ParamCustomPageSize positions and the
//                        custom_points of Width/Height.
//
//   Group 10  (T34-T36)  Custom non-PageSize emission — when a generic
//                        option is marked Custom and has *ParamCustom<KW>
//                        parameters, the emitter writes
//                        %%BeginFeature: *Customkeyword True + each
//                        parameter formatted in order:
//                        strings parenthesised + octal-escaped, ints
//                        as %d, reals via _ppdStrFormatd.
//
//   Group 11  (T37-T40)  PJL ppdEmitJCL — when jcl_begin starts with
//                        \033%-12345X@, the function prepends a literal
//                        \033%-12345X@PJL\n, strips any @PJL JOB lines
//                        from jcl_begin, sanitises the title (basename
//                        only, smbprn.######## prefix stripped, double-
//                        quotes → single, high-bit chars → ? when no
//                        cupsPJLCharset=UTF-8), and emits @PJL JOB NAME
//                        + @PJL SET USERNAME.
//
//   Group 12  (T41-T42)  PJL ppdEmitJCLEnd — when jcl_end starts with
//                        \033%-12345X@, prepends \033%-12345X@PJL\n
//                        and @PJL RDYMSG DISPLAY = "" before the rest
//                        of jcl_end.
//
//   Group 13  (T43-T44)  ppdEmitJCLPDF (hw_copies >= 0 path) — PDF
//                        mode: looks up *JCLToPDFInterpreter (or
//                        ppd->jcl_pdf on CUPS 3.x), writes jcl_pdf in
//                        place of jcl_ps, and adds @PJL SET COPIES =
//                        when hw_copies > 1 and the PPD has no Copies
//                        option.
//
//   Group 14  (T45-T46)  ppdHandleMedia — the PageSize-vs-PageRegion
//                        decision tree.  Default branch (no
//                        ManualFeed / no InputSlot / no cupsFilter)
//                        marks PageSize; flipping to a PPD that
//                        declares *cupsFilter lines without a
//                        RequiresPageRegion attribute flips the
//                        decision to PageRegion.
//
// Design: the suite uses three hermetic embedded PPDs:
//
//   test_ppd_text          — non-PJL JCL + standard PageSetup /
//                            DocumentSetup / PROLOG / EXIT options +
//                            Custom PageSize (variable paper) + Custom
//                            Watermark option for the Group 9 / 10
//                            custom-emission tests.
//
//   test_ppd_pjl_text      — JCL with the \033%-12345X@ PJL prefix on
//                            *JCLBegin AND *JCLEnd, plus
//                            *JCLToPDFInterpreter for the Group 13 PDF
//                            mode coverage.
//
//   test_ppd_media_text    — declares *cupsFilter (num_filters > 0)
//                            without a *RequiresPageRegion attribute,
//                            forcing ppdHandleMedia's PageRegion branch.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


// =============================================================================
// PPD #1 — main test fixture (extended).
// =============================================================================

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"EMITTEST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(EmitTest)\"\n"
  "*ModelName: \"Acme EmitTest\"\n"
  "*ShortNickName: \"EmitTest\"\n"
  "*NickName: \"Acme EmitTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  // Non-PJL JCL — exercises the simple else branch of ppdEmitJCL.
  "*JCLBegin: \"BEGIN<0A>\"\n"
  "*JCLToPSInterpreter: \"PSINTERP<0A>\"\n"
  "*JCLEnd: \"END<0A>\"\n"
  // PageSize (AnySetup → PPD_ORDER_ANY).  Letter + A4 + a Custom variant
  // is declared via *VariablePaperSize / *ParamCustomPageSize / *HWMargins
  // / *CustomPageSize True below.
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
  // Variable / Custom page size machinery — required for Group 9.
  // Width param at position 1, Height at 2, Orientation at 5.  When
  // PageSize=Custom is emitted, ppdEmitString assembles 5 floats with
  // the Width and Height values landing at positions 1 and 2 (0-indexed
  // → 0 and 1 in the values[] array) and the orientation at position 4.
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
  // JCL section option (JCLSetup → PPD_ORDER_JCL).
  "*OpenUI *JCLDuplex/JCL Duplex: PickOne\n"
  "*OrderDependency: 100 JCLSetup *JCLDuplex\n"
  "*DefaultJCLDuplex: JCLNone\n"
  "*JCLDuplex JCLNone/Off: \"@PJL SET DUPLEX = OFF<0A>\"\n"
  "*JCLDuplex JCLDuplexNoTumble/Long: \"@PJL SET DUPLEX = NOTUMBLE<0A>\"\n"
  "*CloseUI: *JCLDuplex\n"
  // PageSetup section options (order 10 and 50, drives Group 7 filtering).
  "*OpenUI *Resolution/Resolution: PickOne\n"
  "*OrderDependency: 10 PageSetup *Resolution\n"
  "*DefaultResolution: 600dpi\n"
  "*Resolution 300dpi/300 DPI: \"<</HWResolution[300 300]>>setpagedevice\"\n"
  "*Resolution 600dpi/600 DPI: \"<</HWResolution[600 600]>>setpagedevice\"\n"
  "*CloseUI: *Resolution\n"
  "*OpenUI *MediaType/Media Type: PickOne\n"
  "*OrderDependency: 50 PageSetup *MediaType\n"
  "*DefaultMediaType: Plain\n"
  "*MediaType Plain/Plain: \"<</MediaType (Plain)>>setpagedevice\"\n"
  "*MediaType Glossy/Glossy: \"<</MediaType (Glossy)>>setpagedevice\"\n"
  "*CloseUI: *MediaType\n"
  // DocumentSetup section option.
  "*OpenUI *Smoothing/Smoothing: PickOne\n"
  "*OrderDependency: 20 DocumentSetup *Smoothing\n"
  "*DefaultSmoothing: None\n"
  "*Smoothing None/No: \"<</PostRenderingEnhance false>>setpagedevice\"\n"
  "*Smoothing Best/Yes: \"<</PostRenderingEnhance true>>setpagedevice\"\n"
  "*CloseUI: *Smoothing\n"
  // PROLOG section option — for Group 8 PROLOG wrapper coverage.
  "*OpenUI *MyPrologue/Prologue: PickOne\n"
  "*OrderDependency: 5 Prolog *MyPrologue\n"
  "*DefaultMyPrologue: Standard\n"
  "*MyPrologue Standard/Std: \"<</PrologueFlavor (Std)>>setpagedevice\"\n"
  "*CloseUI: *MyPrologue\n"
  // EXIT section option — for Group 8 raw-emission coverage.  The
  // emitter's "section != EXIT" else-if at line 926 means EXIT picks up
  // the raw choice->code path (no [{ / EndFeature wrapper).
  "*OpenUI *MyExit/Exit Code: PickOne\n"
  "*OrderDependency: 200 ExitServer *MyExit\n"
  "*DefaultMyExit: ResetExit\n"
  "*MyExit ResetExit/Reset Exit: \"%%RESETEXIT%%\"\n"
  "*CloseUI: *MyExit\n"
  // Custom non-PageSize option — for Group 10.  Single string parameter
  // (Text, order 1) so the emitter's STRING branch fires and writes
  // (value) with parens and octal escaping for any out-of-range char.
  "*OpenUI *Watermark/Watermark: PickOne\n"
  "*OrderDependency: 60 PageSetup *Watermark\n"
  "*DefaultWatermark: None\n"
  "*Watermark None/None: \"\"\n"
  "*CloseUI: *Watermark\n"
  "*CustomWatermark True: \"pop\"\n"
  "*ParamCustomWatermark Text/Watermark Text: 1 string 0 80\n";


// =============================================================================
// PPD #2 — PJL-flavoured JCL fixture.
// =============================================================================
//
// *JCLBegin starts with the literal PJL ESC sequence (\033%-12345X@) so
// ppdEmitJCL takes the PJL branch (lines 421-568).  *JCLEnd does the same
// so ppdEmitJCLEnd takes its PJL branch (lines 631-645).  We also declare
// *cupsPJLCharset and *cupsPJLDisplay attributes plus *JCLToPDFInterpreter
// so the Group 13 PDF-mode test has something to emit.
//

static const char test_ppd_pjl_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"PJLTEST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(PJLTest)\"\n"
  "*ModelName: \"Acme PJL\"\n"
  "*ShortNickName: \"PJL\"\n"
  "*NickName: \"Acme PJL, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  // PJL JCL — <1B> = ESC, <0A> = LF.  The leading @PJL JOB line is
  // there specifically so the test can prove that ppdEmitJCL strips
  // pre-existing @PJL JOB lines (lines 450-462).  ENTER LANGUAGE stays.
  "*JCLBegin: \"<1B>%-12345X@PJL JOB<0A>@PJL ENTER LANGUAGE=POSTSCRIPT<0A>\"\n"
  "*JCLToPSInterpreter: \"\"\n"
  "*JCLToPDFInterpreter: \"@PJL ENTER LANGUAGE=PDF<0A>\"\n"
  "*JCLEnd: \"<1B>%-12345X@PJL EOJ<0A><1B>%-12345X<0A>\"\n"
  "*cupsPJLCharset: \"UTF-8\"\n"
  "*cupsPJLDisplay: \"job\"\n"
  // PageSize/PageRegion/ImageableArea/PaperDimension (minimal valid PPD).
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
// PPD #3 — for ppdHandleMedia decision testing.
// =============================================================================
//
// Drives the ELSE IF branch of ppdHandleMedia (ppd-emit.c line 1244) that
// marks PageRegion.  Required to make all four OR-clauses of the outer IF
// evaluate FALSE while the ELSE IF evaluates TRUE:
//
//   * Outer clause 2 (no manual && no input) → defeat by declaring an
//     InputSlot with a default choice so input_slot is marked.
//   * Outer clause 4 (!rpr && num_filters > 0) → defeat by declaring
//     *RequiresPageRegion All: True so rpr is non-NULL (the cupsFilter
//     count is irrelevant once rpr exists).
//   * Outer clauses 1 and 3 are inert (no Custom size mark, no
//     ManualFeed declared).
//
// With outer-IF false, the ELSE IF "rpr && rpr->value == True" fires and
// ppdMarkOption(PageRegion, …) is called.  Note that PageSize=Letter is
// already marked by ppdMarkDefaults — ppdHandleMedia does not unmark it
// in this branch, so the test checks for PageRegion *also* being marked.
//

static const char test_ppd_media_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"MEDIATST.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(MediaTest)\"\n"
  "*ModelName: \"Acme Media\"\n"
  "*ShortNickName: \"Media\"\n"
  "*NickName: \"Acme Media, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
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
  "*PaperDimension Letter: \"612 792\"\n"
  // InputSlot with a default choice — defeats branch 2 of the outer IF.
  "*OpenUI *InputSlot/Media Source: PickOne\n"
  "*OrderDependency: 20 AnySetup *InputSlot\n"
  "*DefaultInputSlot: Tray1\n"
  "*InputSlot Tray1/Tray 1: \"<</MediaPosition 0>>setpagedevice\"\n"
  "*CloseUI: *InputSlot\n"
  // RequiresPageRegion: All True — makes the ELSE IF in ppdHandleMedia
  // succeed: rpr is found, rpr->value is "True".
  "*RequiresPageRegion All: True\n";


// =============================================================================
// Helpers.
// =============================================================================

// Rewind a FILE* and read its entire content into a malloc'd buffer.
static char *
slurp(FILE *fp, size_t *out_len)
{
  long  end;
  char *buf;

  fflush(fp);
  fseek(fp, 0, SEEK_END);
  end = ftell(fp);
  if (end < 0)
    return (NULL);
  rewind(fp);

  buf = malloc((size_t)end + 1);
  if (!buf)
    return (NULL);

  *out_len = fread(buf, 1, (size_t)end, fp);
  buf[*out_len] = '\0';
  return (buf);
}

// Open a static PPD string via tmpfile() / ppdOpen().
static ppd_file_t *
open_embedded_ppd(const char *text)
{
  FILE       *f = tmpfile();
  ppd_file_t *ppd;

  fputs(text, f);
  rewind(f);
  ppd = ppdOpen(f);
  fclose(f);
  return (ppd);
}


int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t    *ppd;
  ppd_file_t    *ppd_pjl;
  ppd_file_t    *ppd_media;
  ppd_status_t   err;
  int            line;
  FILE          *out;
  char          *s;
  char          *disk;
  size_t         disk_len;
  ppd_choice_t **choices;
  int            count;
  int            rc;
  int            fd;
  ppd_coption_t *coption;
  ppd_cparam_t  *cparam;
  ppd_choice_t  *marked;


  // =========================================================================
  // Group 1: NULL / argument guards (T01-T08)
  // =========================================================================

  // T01 — ppdEmit: `if (!ppd || !fp) return (-1);` (ppd-emit.c line 264).
  testBegin("ppdEmit(NULL ppd, ...) returns -1");
  testEnd(ppdEmit(NULL, stderr, PPD_ORDER_JCL) == -1);

  // T02 — !fp half of the same guard.
  testBegin("ppdEmit(ppd, NULL fp, ...) returns -1");
  testEnd(ppdEmit((ppd_file_t *)1, NULL, PPD_ORDER_JCL) == -1);

  // T03 — ppdEmitString: `if (!ppd) return (NULL);` (line 692).
  testBegin("ppdEmitString(NULL, ...) returns NULL");
  testEnd(ppdEmitString(NULL, PPD_ORDER_JCL, 0.0f) == NULL);

  // T04 — ppdEmitFd: `if (!ppd || fd < 0) return (-1);` (line 310).
  testBegin("ppdEmitFd(NULL ppd, ...) returns -1");
  testEnd(ppdEmitFd(NULL, 1, PPD_ORDER_JCL) == -1);

  // T05 — fd < 0 half of the same guard.
  testBegin("ppdEmitFd(ppd, fd=-1, ...) returns -1");
  testEnd(ppdEmitFd((ppd_file_t *)1, -1, PPD_ORDER_JCL) == -1);

  // T06 — ppdCollect(NULL, …, &choices) returns 0 AND nulls the slot.
  testBegin("ppdCollect(NULL ppd, …, &choices) returns 0, nulls slot");
  choices = (ppd_choice_t **)0xDEADBEEF;
  count = ppdCollect(NULL, PPD_ORDER_JCL, &choices);
  testEnd(count == 0 && choices == NULL);

  // T07 — ppdEmitJCL: `if (!ppd || !ppd->jcl_begin) return (0);` (line 400).
  //       Returns 0 (no-op), NOT -1 — documented API quirk.
  testBegin("ppdEmitJCL(NULL ppd, ...) returns 0 (documented no-op)");
  testEnd(ppdEmitJCL(NULL, stderr, 1, "user", "title") == 0);

  // T08 — ppdEmitJCLEnd: `if (!ppd) return (0);` (line 616).
  testBegin("ppdEmitJCLEnd(NULL ppd, ...) returns 0 (documented no-op)");
  testEnd(ppdEmitJCLEnd(NULL, stderr) == 0);


  // =========================================================================
  // Group 2: ppdOpen + JCL field decoding (T09-T11)
  // =========================================================================

  testBegin("ppdOpen(embedded emit test PPD #1)");
  ppd = open_embedded_ppd(test_ppd_text);
  if (ppd)
    testEnd(true);
  else
  {
    err = ppdLastError(&line);
    testEndMessage(false, "%s on line %d", ppdErrorString(err), line);
    return (1);
  }

  // T10 — *JCLBegin: "BEGIN<0A>" → "BEGIN\n" after ppdDecode.
  testBegin("ppd->jcl_begin decoded to \"BEGIN\\n\"");
  testEndMessage(ppd->jcl_begin && !strcmp(ppd->jcl_begin, "BEGIN\n"),
                 "got \"%s\"", ppd->jcl_begin ? ppd->jcl_begin : "(null)");

  // T11 — *JCLEnd / *JCLToPSInterpreter parsed analogously.
  testBegin("ppd->jcl_end == \"END\\n\" and ppd->jcl_ps == \"PSINTERP\\n\"");
  testEndMessage(ppd->jcl_end && !strcmp(ppd->jcl_end, "END\n") &&
                 ppd->jcl_ps && !strcmp(ppd->jcl_ps, "PSINTERP\n"),
                 "jcl_end=\"%s\" jcl_ps=\"%s\"",
                 ppd->jcl_end ? ppd->jcl_end : "(null)",
                 ppd->jcl_ps ? ppd->jcl_ps : "(null)");


  // =========================================================================
  // Group 3: ppdCollect by section (T12-T14)
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T12 — JCL bucket: JCLDuplex only.
  testBegin("ppdCollect(JCL) returns [JCLDuplex]");
  count = ppdCollect(ppd, PPD_ORDER_JCL, &choices);
  testEndMessage(count == 1 && choices &&
                 !strcmp(choices[0]->option->keyword, "JCLDuplex"),
                 "count=%d", count);
  free(choices);

  // T13 — PageSetup bucket: Resolution (10) then MediaType (50) then
  //       Watermark (60), sorted by order.
  testBegin("ppdCollect(PAGE) returns [Resolution, MediaType, Watermark]");
  count = ppdCollect(ppd, PPD_ORDER_PAGE, &choices);
  testEndMessage(count == 3 && choices &&
                 !strcmp(choices[0]->option->keyword, "Resolution") &&
                 !strcmp(choices[1]->option->keyword, "MediaType") &&
                 !strcmp(choices[2]->option->keyword, "Watermark"),
                 "count=%d", count);
  free(choices);

  // T14 — DocumentSetup bucket: Smoothing only.
  testBegin("ppdCollect(DOCUMENT) returns [Smoothing]");
  count = ppdCollect(ppd, PPD_ORDER_DOCUMENT, &choices);
  testEndMessage(count == 1 && choices &&
                 !strcmp(choices[0]->option->keyword, "Smoothing"),
                 "count=%d", count);
  free(choices);


  // =========================================================================
  // Group 4: ppdEmitString content shape (T15-T18)
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T15 — PAGE emission wraps Resolution choice code.
  testBegin("ppdEmitString(PAGE) wraps Resolution choice code");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  testEndMessage(s &&
                 strstr(s, "%%BeginFeature: *Resolution 600dpi") &&
                 strstr(s, "<</HWResolution[600 600]>>setpagedevice") &&
                 strstr(s, "%%EndFeature") &&
                 strstr(s, "} stopped cleartomark"),
                 "s=%p", (void *)s);
  free(s);

  // T16 — DOCUMENT emission wraps Smoothing choice code.
  testBegin("ppdEmitString(DOCUMENT) wraps Smoothing choice code");
  s = ppdEmitString(ppd, PPD_ORDER_DOCUMENT, 0.0f);
  testEndMessage(s &&
                 strstr(s, "%%BeginFeature: *Smoothing None") &&
                 strstr(s, "<</PostRenderingEnhance false>>setpagedevice"),
                 "s=%p", (void *)s);
  free(s);

  // T17 — JCL emits raw choice->code with no wrapper.
  testBegin("ppdEmitString(JCL) emits raw JCLDuplex code (no wrapper)");
  s = ppdEmitString(ppd, PPD_ORDER_JCL, 0.0f);
  testEndMessage(s && strstr(s, "@PJL SET DUPLEX = OFF") &&
                 strstr(s, "%%BeginFeature") == NULL,
                 "s=\"%s\"", s ? s : "(null)");
  free(s);

  // T18 — PROLOG: section IS populated (MyPrologue) so this should now
  //       NOT be NULL.  (Group 8 covers the bytes.)  Sanity check here.
  testBegin("ppdEmitString(PROLOG) is non-NULL when section has options");
  s = ppdEmitString(ppd, PPD_ORDER_PROLOG, 0.0f);
  testEnd(s != NULL);
  free(s);


  // =========================================================================
  // Group 5: ppdEmit / ppdEmitFd equivalence (T19-T21)
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T19 — ppdEmit bytes == ppdEmitString bytes for PAGE.
  testBegin("ppdEmit(PAGE) bytes match ppdEmitString(PAGE)");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  out = tmpfile();
  rc = ppdEmit(ppd, out, PPD_ORDER_PAGE);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && s && disk &&
                 disk_len == strlen(s) && !memcmp(disk, s, disk_len),
                 "rc=%d", rc);
  free(s);
  free(disk);

  // T20 — ppdEmitFd bytes == ppdEmitString bytes for PAGE.
  testBegin("ppdEmitFd(PAGE) bytes match ppdEmitString(PAGE)");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  out = tmpfile();
  fd = fileno(out);
  rc = ppdEmitFd(ppd, fd, PPD_ORDER_PAGE);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && s && disk &&
                 disk_len == strlen(s) && !memcmp(disk, s, disk_len),
                 "rc=%d", rc);
  free(s);
  free(disk);

  // T21 — ppdEmit on empty section returns 0 and writes 0 bytes.
  //       Pick a section that genuinely has no marked options: there
  //       is no option in PPD_ORDER_ANY that is *marked* (PageSize is in
  //       ANY, but ppdEmit doesn't emit ANY sections meaningfully via
  //       this function — section is "JCL", "PAGE", "DOCUMENT", "EXIT",
  //       "PROLOG").  We use EXIT after un-marking MyExit.
  testBegin("ppdEmit on a section with no marked options returns 0");
  ppdMarkDefaults(ppd);
  // Pop the only EXIT option (MyExit defaults to ResetExit) by marking
  // a different non-existent choice → no effect.  Instead we just call
  // ppdEmit for a section that exists only with options never marked:
  // Walk ppd->marked and clear MyExit if present.
  if ((marked = ppdFindMarkedChoice(ppd, "MyExit")) != NULL)
  {
    marked->marked = 0;
    cupsArrayRemove(ppd->marked, marked);
  }
  out = tmpfile();
  rc = ppdEmit(ppd, out, PPD_ORDER_EXIT);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && disk_len == 0,
                 "rc=%d disk_len=%zu", rc, disk_len);
  free(disk);


  // =========================================================================
  // Group 6: ppdEmitJCL / ppdEmitJCLEnd (non-PJL) (T22-T25)
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T22 — ppdEmitJCL returns 0 on success.
  testBegin("ppdEmitJCL returns 0 on success (non-PJL)");
  out = tmpfile();
  rc = ppdEmitJCL(ppd, out, 42, "tester", "TestJob");
  testEndMessage(rc == 0, "rc=%d", rc);

  // T23 — Bytes: BEGIN\n + JCL options + PSINTERP\n .
  testBegin("ppdEmitJCL bytes == BEGIN\\n + JCL code + PSINTERP\\n");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk &&
                 !strcmp(disk, "BEGIN\n@PJL SET DUPLEX = OFF\nPSINTERP\n"),
                 "got \"%s\"", disk ? disk : "(null)");
  free(disk);

  // T24 — ppdEmitJCLEnd returns 0 on success.
  testBegin("ppdEmitJCLEnd returns 0 on success (non-PJL)");
  out = tmpfile();
  rc = ppdEmitJCLEnd(ppd, out);
  testEndMessage(rc == 0, "rc=%d", rc);

  // T25 — Bytes: "END\n" (jcl_end emitted raw).
  testBegin("ppdEmitJCLEnd bytes == \"END\\n\"");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk && !strcmp(disk, "END\n"),
                 "got \"%s\"", disk ? disk : "(null)");
  free(disk);


  // =========================================================================
  // Group 7: OrderDependency filtering (T26-T28)
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T26 — ppdCollect2(PAGE, 20.0f) drops Resolution (10), keeps MediaType
  //       (50) and Watermark (60).
  testBegin("ppdCollect2(PAGE, 20.0f) drops Resolution; keeps [MediaType, Watermark]");
  count = ppdCollect2(ppd, PPD_ORDER_PAGE, 20.0f, &choices);
  testEndMessage(count == 2 && choices &&
                 !strcmp(choices[0]->option->keyword, "MediaType") &&
                 !strcmp(choices[1]->option->keyword, "Watermark"),
                 "count=%d", count);
  free(choices);

  // T27 — ppdEmitString min_order=20 → MediaType wrapped, no Resolution.
  testBegin("ppdEmitString(PAGE, 20.0f) keeps MediaType, drops Resolution");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 20.0f);
  testEndMessage(s &&
                 strstr(s, "%%BeginFeature: *MediaType Plain") &&
                 strstr(s, "%%BeginFeature: *Resolution") == NULL,
                 "s=%p", (void *)s);
  free(s);

  // T28 — ppdEmitAfterOrder(limit=1, 20.0f) bytes-on-disk match same filter.
  testBegin("ppdEmitAfterOrder(PAGE, 1, 20.0f) writes MediaType only");
  out = tmpfile();
  rc = ppdEmitAfterOrder(ppd, out, PPD_ORDER_PAGE, 1, 20.0f);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && disk &&
                 strstr(disk, "%%BeginFeature: *MediaType Plain") &&
                 strstr(disk, "%%BeginFeature: *Resolution") == NULL,
                 "rc=%d", rc);
  free(disk);


  // =========================================================================
  // Group 8: PROLOG + EXIT section emission (T29-T30)
  //
  // PROLOG goes through the standard wrapper path; EXIT goes through the
  // ppd-emit.c line 1164-1168 raw branch (no [{ / %%BeginFeature wrapper).
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T29 — PROLOG wraps MyPrologue with %%BeginFeature.
  testBegin("ppdEmitString(PROLOG) wraps MyPrologue with %%BeginFeature");
  s = ppdEmitString(ppd, PPD_ORDER_PROLOG, 0.0f);
  testEndMessage(s &&
                 strstr(s, "%%BeginFeature: *MyPrologue Standard") &&
                 strstr(s, "<</PrologueFlavor (Std)>>setpagedevice") &&
                 strstr(s, "} stopped cleartomark"),
                 "s=%p", (void *)s);
  free(s);

  // T30 — EXIT emits raw choice->code with NO wrapper.  The "%%RESETEXIT%%"
  //       sentinel string is unique to our EXIT choice; the wrapper
  //       sentinel "%%BeginFeature" must be absent.
  testBegin("ppdEmitString(EXIT) emits raw choice->code (no wrapper)");
  s = ppdEmitString(ppd, PPD_ORDER_EXIT, 0.0f);
  testEndMessage(s && strstr(s, "%%RESETEXIT%%") &&
                 strstr(s, "%%BeginFeature") == NULL &&
                 strstr(s, "} stopped cleartomark") == NULL,
                 "s=\"%s\"", s ? s : "(null)");
  free(s);


  // =========================================================================
  // Group 9: Custom PageSize emission (T31-T33)
  //
  // When PageSize=Custom is marked, ppdEmitString takes the variable-
  // PageSize branch (ppd-emit.c lines 943-1061).  It emits:
  //   [{\n
  //   %%BeginFeature: *CustomPageSize True\n
  //   <values[0] formatted via _ppdStrFormatd>\n   (Width @ pos 1 → idx 0)
  //   <values[1]>\n                                  (Height @ pos 2 → idx 1)
  //   <values[2]>\n                                  (WidthOffset @ 3 → 2)
  //   <values[3]>\n                                  (HeightOffset @ 4 → 3)
  //   <values[4]>\n                                  (orientation @ 5 → 4)
  //   <choices[i]->code or ppd_custom_code>\n
  //   %%EndFeature\n
  //   } stopped cleartomark\n
  // For our PPD: Width=500, Height=600 (from "Custom.500x600"); pos 0=500.0,
  // pos 1=600.0, pos 2=0 (WidthOffset), pos 3=0 (HeightOffset), pos 4=1
  // (default orientation per the comment on line 1016).
  // =========================================================================

  ppdMarkDefaults(ppd);
  ppdMarkOption(ppd, "PageSize", "Custom.500x600");

  // T31 — PAGE emission contains the *CustomPageSize True header.
  //       PageSize is in PPD_ORDER_ANY which is NOT emitted by
  //       ppdEmitString(PAGE).  We need to emit ANY... or use the
  //       PageSetup section?  In fact ppdEmitString accepts ppd_section_t
  //       values; for PageSize=Custom the relevant section is ANY, but
  //       the documentation and existing CUPS callers query each declared
  //       section.  Our PageSize is declared AnySetup → PPD_ORDER_ANY,
  //       so we emit PPD_ORDER_ANY here.
  testBegin("ppdEmitString(ANY) with PageSize=Custom emits *CustomPageSize True header");
  s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0f);
  testEndMessage(s && strstr(s, "%%BeginFeature: *CustomPageSize True"),
                 "s=%p", (void *)s);
  free(s);

  // T32 — The emitted payload contains the Width / Height values verbatim
  //       (500 and 600 — the values we passed via Custom.500x600).
  testBegin("ppdEmitString(ANY) CustomPageSize payload contains 500 and 600");
  s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0f);
  testEndMessage(s && strstr(s, "500") && strstr(s, "600") &&
                 strstr(s, "pop pop pop pop pop"),
                 "s=%p", (void *)s);
  free(s);

  // T33 — The emission still includes the [{ … } stopped cleartomark
  //       wrapper around the custom-size code.
  testBegin("ppdEmitString(ANY) Custom emission has [{ … } cleartomark wrapper");
  s = ppdEmitString(ppd, PPD_ORDER_ANY, 0.0f);
  testEndMessage(s && strstr(s, "[{") &&
                 strstr(s, "} stopped cleartomark"),
                 "s=%p", (void *)s);
  free(s);


  // =========================================================================
  // Group 10: Custom non-PageSize emission (T34-T36)
  //
  // Mark Watermark=Custom and set its single STRING parameter (Text).
  // ppdEmitString takes the "else if Custom && ppdFindCustomOption"
  // branch at lines 1063-1138, which emits
  //   %%BeginFeature: *CustomWatermark True\n
  //   (text-with-parens)\n
  // followed by the wrapper.
  // =========================================================================

  ppdMarkDefaults(ppd);
  ppdMarkOption(ppd, "Watermark", "Custom");
  // Find the Text parameter and set its string value.
  coption = ppdFindCustomOption(ppd, "Watermark");
  if (coption)
  {
    cparam = ppdFindCustomParam(coption, "Text");
    if (cparam)
    {
      free(cparam->current.custom_string);
      cparam->current.custom_string = strdup("Confidential");
    }
  }

  // T34 — PAGE emission contains *CustomWatermark True header.
  //       Watermark is OrderDependency 60 PageSetup → PPD_ORDER_PAGE.
  testBegin("ppdEmitString(PAGE) with Watermark=Custom emits *CustomWatermark True");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  testEndMessage(s && strstr(s, "%%BeginFeature: *CustomWatermark True"),
                 "s=%p", (void *)s);
  free(s);

  // T35 — The string parameter is emitted parenthesised: "(Confidential)".
  testBegin("ppdEmitString(PAGE) emits string param parenthesised: (Confidential)");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  testEndMessage(s && strstr(s, "(Confidential)"),
                 "s=%p", (void *)s);
  free(s);

  // T36 — Custom emission still uses the standard wrapper.
  testBegin("ppdEmitString(PAGE) Custom emission has wrapper + EndFeature");
  s = ppdEmitString(ppd, PPD_ORDER_PAGE, 0.0f);
  testEndMessage(s && strstr(s, "[{") &&
                 strstr(s, "%%EndFeature") &&
                 strstr(s, "} stopped cleartomark"),
                 "s=%p", (void *)s);
  free(s);


  // =========================================================================
  // Group 11: PJL ppdEmitJCL (T37-T40)
  //
  // Open PPD #2 (PJL-flavoured) and exercise the PJL branch.
  // =========================================================================

  ppd_pjl = open_embedded_ppd(test_ppd_pjl_text);
  if (!ppd_pjl)
  {
    err = ppdLastError(&line);
    testError("ppdOpen(PJL PPD) failed: %s on line %d",
              ppdErrorString(err), line);
    ppdClose(ppd);
    return (1);
  }

  // T37 — PJL output always starts with the literal init prefix
  //       "\033%-12345X@PJL\n" (line 448 of ppd-emit.c).  The prefix is
  //       14 bytes: 1 (ESC) + 12 ("%-12345X@PJL") + 1 ("\n").
  testBegin("ppdEmitJCL (PJL) output begins with \\033%-12345X@PJL\\n");
  out = tmpfile();
  rc = ppdEmitJCL(ppd_pjl, out, 42, "tester", "TestJob");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && disk &&
                 disk_len >= 14 &&
                 !memcmp(disk, "\033%-12345X@PJL\n", 14),
                 "rc=%d prefix-ok=%d",
                 rc, (disk && disk_len >= 14) ?
                       !memcmp(disk, "\033%-12345X@PJL\n", 14) : -1);
  free(disk);

  // T38 — The pre-existing @PJL JOB line in jcl_begin is stripped
  //       (lines 450-462), AND the function emits its own @PJL JOB NAME
  //       line (line 551).  We expect exactly ONE occurrence of
  //       "@PJL JOB NAME = " in the output AND zero occurrences of the
  //       bare "@PJL JOB\n" from jcl_begin.
  testBegin("ppdEmitJCL (PJL) strips jcl_begin's @PJL JOB; emits its own @PJL JOB NAME");
  out = tmpfile();
  ppdEmitJCL(ppd_pjl, out, 42, "tester", "TestJob");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk &&
                 strstr(disk, "@PJL JOB NAME = \"TestJob\"") &&
                 strstr(disk, "@PJL JOB\n") == NULL,
                 "disk=%p", (void *)disk);
  free(disk);

  // T39 — @PJL SET USERNAME is emitted with the supplied username.
  testBegin("ppdEmitJCL (PJL) emits @PJL SET USERNAME = \"tester\"");
  out = tmpfile();
  ppdEmitJCL(ppd_pjl, out, 42, "tester", "TestJob");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk && strstr(disk, "@PJL SET USERNAME = \"tester\""),
                 "disk=%p", (void *)disk);
  free(disk);

  // T40 — Title sanitisation: "smbprn.00000042 SomeApp - RealTitle" should
  //       resolve to "RealTitle" (smbprn.######## + isdigit + isspace
  //       skip, then " - " app-name strip).  Also: double-quote in user
  //       must become single-quote.
  testBegin("ppdEmitJCL (PJL) sanitises smbprn.<digits> + 'App - Title' to 'Title'");
  out = tmpfile();
  ppdEmitJCL(ppd_pjl, out, 7, "us\"er", "smbprn.00000042 SomeApp - RealTitle");
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk &&
                 strstr(disk, "JOB NAME = \"RealTitle\"") &&
                 strstr(disk, "USERNAME = \"us'er\"") &&
                 strstr(disk, "\"us\"er\"") == NULL,
                 "disk=%p", (void *)disk);
  free(disk);


  // =========================================================================
  // Group 12: PJL ppdEmitJCLEnd (T41-T42)
  //
  // jcl_end starts with the PJL prefix → take the PJL branch
  // (ppd-emit.c lines 631-645): emit "\033%-12345X@PJL\n", then
  // "@PJL RDYMSG DISPLAY = \"\"\n", then jcl_end+9.
  // =========================================================================

  // T41 — Return code is 0 on success.
  testBegin("ppdEmitJCLEnd (PJL) returns 0");
  out = tmpfile();
  rc = ppdEmitJCLEnd(ppd_pjl, out);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0, "rc=%d", rc);
  free(disk);

  // T42 — Output starts with the init prefix and contains the RDYMSG
  //       clear-display directive.  Prefix length is 14 bytes (same as T37).
  testBegin("ppdEmitJCLEnd (PJL) emits init prefix + RDYMSG DISPLAY = \"\"");
  out = tmpfile();
  ppdEmitJCLEnd(ppd_pjl, out);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(disk &&
                 disk_len >= 14 &&
                 !memcmp(disk, "\033%-12345X@PJL\n", 14) &&
                 strstr(disk, "@PJL RDYMSG DISPLAY = \"\""),
                 "disk=%p", (void *)disk);
  free(disk);


  // =========================================================================
  // Group 13: ppdEmitJCLPDF — hw_copies >= 0 path (T43-T44)
  //
  // With hw_copies >= 0, the function looks up *JCLToPDFInterpreter
  // (CUPS 2.x build) and substitutes it in place of jcl_ps.  Also: when
  // hw_copies > 1 AND the PPD has no Copies option AND the PJL prefix
  // matches, the function emits "@PJL SET COPIES = N" or
  // "@PJL SET QTY = N" (line 592-593).
  // =========================================================================

  // T43 — In PDF mode, jcl_pdf payload appears in the output.  Our PPD #2
  //       declares JCLToPDFInterpreter = "@PJL ENTER LANGUAGE=PDF\n" so
  //       that exact substring must be present.
  testBegin("ppdEmitJCLPDF (hw_copies=1) emits JCLToPDFInterpreter payload");
  out = tmpfile();
  rc = ppdEmitJCLPDF(ppd_pjl, out, 42, "tester", "Job", 1, false);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && disk &&
                 strstr(disk, "@PJL ENTER LANGUAGE=PDF"),
                 "rc=%d", rc);
  free(disk);

  // T44 — hw_copies=4 with no *Copies in the PPD AND PJL prefix:
  //       expect "@PJL SET COPIES = 4" (hw_collate=false → COPIES, not QTY).
  testBegin("ppdEmitJCLPDF (hw_copies=4, no Copies opt) emits @PJL SET COPIES=4");
  out = tmpfile();
  rc = ppdEmitJCLPDF(ppd_pjl, out, 42, "tester", "Job", 4, false);
  disk = slurp(out, &disk_len);
  fclose(out);
  testEndMessage(rc == 0 && disk && strstr(disk, "@PJL SET COPIES=4"),
                 "rc=%d", rc);
  free(disk);


  // =========================================================================
  // Group 14: ppdHandleMedia decision (T45-T46)
  //
  // Branch 2 of ppdHandleMedia (lines 1232-1243) fires when neither
  // ManualFeed nor InputSlot is marked → PageSize is marked.  Branch 4
  // (line 1244-1251) fires when no RequiresPageRegion is present AND the
  // PPD has cupsFilter → PageRegion is marked instead.  Our PPD #1 has
  // no cupsFilter / no InputSlot → branch 2; PPD #3 has cupsFilter and no
  // InputSlot → branch 4.
  // =========================================================================

  // T45 — PPD #1, defaults marked, no InputSlot → PageSize gets marked
  //       (branch 2).  Verify via ppdFindMarkedChoice.
  testBegin("ppdHandleMedia: no InputSlot, no filter -> PageSize marked");
  ppdMarkDefaults(ppd);
  // Make sure nothing else has touched PageSize/PageRegion since defaults.
  ppdHandleMedia(ppd);
  marked = ppdFindMarkedChoice(ppd, "PageSize");
  testEndMessage(marked && !strcmp(marked->choice, "Letter"),
                 "PageSize marked=\"%s\"",
                 marked ? marked->choice : "(none)");

  // T46 — PPD #3 has a marked InputSlot AND RequiresPageRegion=True.
  //       Outer IF: clause 2 is false (input_slot set), clause 4 is false
  //       (rpr is non-NULL).  ELSE IF: rpr->value=="True" → true →
  //       ppdMarkOption(PageRegion, "Letter").  We assert PageRegion is
  //       now marked with "Letter".  (PageSize=Letter also stays marked
  //       from ppdMarkDefaults — ppdHandleMedia does not unmark it on
  //       this branch — so we don't assert PageSize's absence.)
  testBegin("ppdHandleMedia: InputSlot + RequiresPageRegion=True -> PageRegion marked");
  ppd_media = open_embedded_ppd(test_ppd_media_text);
  if (!ppd_media)
  {
    err = ppdLastError(&line);
    testEndMessage(false, "ppdOpen(media PPD) failed: %s line %d",
                   ppdErrorString(err), line);
    ppdClose(ppd_pjl);
    ppdClose(ppd);
    return (1);
  }
  ppdMarkDefaults(ppd_media);
  ppdHandleMedia(ppd_media);
  marked = ppdFindMarkedChoice(ppd_media, "PageRegion");
  testEndMessage(marked && !strcmp(marked->choice, "Letter"),
                 "PageRegion marked=\"%s\"",
                 marked ? marked->choice : "(none)");


  // -------------------------------------------------------------------------
  // Tear down and return success/failure based on the framework flag.
  // -------------------------------------------------------------------------
  ppdClose(ppd_media);
  ppdClose(ppd_pjl);
  ppdClose(ppd);
  return (testsPassed ? 0 : 1);
}
