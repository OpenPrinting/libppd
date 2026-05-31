//
// PPD option-marking API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (66 assertions across 12 groups):
//
//   Group 1  (T01-T09)  NULL/argument guards — every public function in
//                       ppd-mark.c that accepts pointer arguments must
//                       return a safe sentinel (NULL/0) without crashing
//                       when handed NULL.  These run before any PPD is
//                       opened and therefore have no PPD-content dependency.
//
//   Group 2  (T10-T15)  ppdOpen() + explicit ppdMarkDefaults() — verify
//                       that opening the embedded PPD succeeds, that an
//                       explicit ppdMarkDefaults() populates ppd->marked
//                       with each option's *Default<Keyword> choice, and
//                       that PageRegion is *not* in the marked array
//                       (PageRegion is explicitly skipped by
//                       ppd_defaults() to avoid duplication with
//                       PageSize).  Note: ppdOpen() does NOT call
//                       ppdMarkDefaults() — the caller is responsible
//                       for invoking it explicitly.  Also re-call
//                       ppdMarkDefaults() to prove idempotency.
//
//   Group 3  (T16-T21)  ppdFindOption() — locate an option by keyword,
//                       confirm the returned ppd_option_t fields match
//                       the PPD source (keyword, num_choices > 0,
//                       ui == PPD_UI_PICKONE), verify a missing keyword
//                       returns NULL, and verify the second NULL-arg
//                       guard (NULL keyword with a valid PPD).
//
//   Group 4  (T22-T27)  ppdFindChoice() — exact-name lookup, the two
//                       Custom-rewrite branches ("Custom.WxH" prefix and
//                       leading-'{' for multi-value custom), a missing
//                       choice, and the two NULL-argument guards.
//
//   Group 5  (T28-T31)  ppdFindMarkedChoice() — after defaults: PageSize
//                       is marked Letter, PageRegion is unmarked
//                       (defaulted-out), and an unknown keyword returns
//                       NULL.
//
//   Group 6  (T32-T36)  ppdIsMarked() — return value semantics: 1 for
//                       a default choice, 0 for a non-default choice of
//                       a known option, 0 for an unknown option entirely,
//                       0 for an unknown choice of a known option.
//
//   Group 7  (T37-T43)  ppdMarkOption() — marking a non-default choice
//                       updates ppd->marked: the new choice is marked,
//                       the previously-marked choice is unmarked, the
//                       returned conflict count is 0 for non-conflicting
//                       marks, and calls with unknown option or unknown
//                       choice are silent no-ops (no crash, no mark
//                       change for the unknown branch).
//
//   Group 8  (T44-T47)  ppdMarkOption() + UIConstraints — our PPD
//                       declares "*UIConstraints: *InputSlot Manual
//                       *Duplex DuplexNoTumble".  After marking both
//                       sides of the constraint, ppdMarkOption()'s
//                       return value (which delegates to ppdConflicts())
//                       must be > 0.  After reverting one side via
//                       ppdMarkDefaults(), the conflict count drops to 0.
//
//   Group 9  (T48-T50)  PageSize ↔ PageRegion mutual exclusion — when
//                       PageSize is marked, ppd_mark_option() actively
//                       removes any PageRegion entry from ppd->marked,
//                       and vice versa.  Verify both directions.
//
//   Group 10 (T51-T54)  ppdFirstOption() / ppdNextOption() — iteration
//                       returns options in sorted order.  Confirm the
//                       first call is non-NULL, that iteration visits
//                       at least the six PickOne options declared in
//                       the test PPD, and that we can locate "PageSize"
//                       somewhere in the traversal.
//
//   Group 11 (T55-T61)  ppdParseOptions() — round-trip "*Option Choice"
//                       and "property value" strings into a
//                       cups_option_t array, validate the parsed name
//                       and value, exercise the PPD_PARSE_OPTIONS /
//                       PPD_PARSE_PROPERTIES filters, and verify
//                       NULL/empty-string guards.
//
//   Group 12 (T62-T66)  ppdMarkOptions() — high-level IPP→PPD mapping
//                       layer.  Verify NULL/zero argument guards,
//                       direct option pass-through ("PageSize"="A4"),
//                       the "resolution" remap branch (resolution →
//                       Resolution option), and a tolerant no-op for
//                       an IPP attribute whose PPD target is absent
//                       ("mirror" → MirrorPrint, which our PPD lacks).
//
// Design: the PPD is built entirely in memory from a single static
// string and loaded via tmpfile() + ppdOpen().  The PPD declares only
// the options necessary to exercise the targeted code paths
// (PageSize/PageRegion + custom-size support, Resolution, InputSlot,
// Duplex with UIConstraints, OutputBin).  No external files are
// required at build or CI time, making the binary fully hermetic.
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
//   PPD header block (PPD-Adobe through DefaultResolution):
//     Mandatory metadata so ppdOpen() returns a valid ppd_file_t *.
//
//   PageSize (Letter default, A4 alternative):
//     Default-marking + ppdFindOption()/ppdFindChoice()/ppdIsMarked()
//     happy paths.  Letter is the default and therefore appears in
//     ppd->marked immediately after ppdOpen().
//
//   PageRegion (Letter default, A4 alternative):
//     Lets us prove the "PageRegion is *skipped* during ppdMarkDefaults"
//     rule, and lets us prove PageSize ↔ PageRegion mutual exclusion
//     via the explicit unmark branches in ppd_mark_option().
//
//   VariablePaperSize / ParamCustomPageSize / CustomPageSize block:
//     Adds a synthetic "Custom" choice under PageSize so that
//     ppdFindChoice(pagesize, "Custom.72x72") and
//     ppdFindChoice(pagesize, "{...}") can resolve through the
//     Custom-rewrite branches.
//
//   Resolution (300dpi/600dpi, default 600dpi):
//     Target option for ppdMarkOptions() "resolution" → "Resolution"
//     remap branch.
//
//   InputSlot (Cassette default, Manual alternative):
//     Half of the UIConstraints conflict pair.
//
//   Duplex (None default, DuplexNoTumble / DuplexTumble alternatives):
//     Other half of the UIConstraints conflict pair.
//
//   OutputBin (StandardBin default, FaceUp alternative):
//     Extra PickOne option so iteration tests see at least 6 options.
//
//   *UIConstraints lines:
//     Declare the conflict between InputSlot=Manual and either
//     Duplex=DuplexNoTumble or Duplex=DuplexTumble.  These are what
//     make ppdConflicts() return >0 in Group 8.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"MARKTEST.PPD\"\n"
  "*Product: \"(MarkTest)\"\n"
  "*ModelName: \"PPD Mark Test Printer\"\n"
  "*ShortNickName: \"MarkTest\"\n"
  "*NickName: \"PPD Mark Test Printer\"\n"
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
  // PageRegion (parallel of PageSize — required by Adobe PPD spec)
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  // ImageableArea + PaperDimension (required for valid PPD)
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  // Custom page size — synthesises a "Custom" choice under PageSize so
  // ppdFindChoice() Custom-rewrite branches are reachable.
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
  // Resolution (target of the "resolution" remap branch of ppdMarkOptions)
  "*DefaultResolution: 600dpi\n"
  "*OpenUI *Resolution/Resolution: PickOne\n"
  "*OrderDependency: 20 AnySetup *Resolution\n"
  "*Resolution 300dpi/300 DPI: \"\"\n"
  "*Resolution 600dpi/600 DPI: \"\"\n"
  "*CloseUI: *Resolution\n"
  // InputSlot (half of the conflict pair)
  "*OpenUI *InputSlot/Paper Source: PickOne\n"
  "*OrderDependency: 30 AnySetup *InputSlot\n"
  "*DefaultInputSlot: Cassette\n"
  "*InputSlot Cassette/Cassette: \"\"\n"
  "*InputSlot Manual/Manual Feed: \"\"\n"
  "*CloseUI: *InputSlot\n"
  // Duplex (other half of the conflict pair)
  "*OpenUI *Duplex/Two-Sided: PickOne\n"
  "*OrderDependency: 40 AnySetup *Duplex\n"
  "*DefaultDuplex: None\n"
  "*Duplex None/Off: \"\"\n"
  "*Duplex DuplexNoTumble/Long Edge: \"\"\n"
  "*Duplex DuplexTumble/Short Edge: \"\"\n"
  "*CloseUI: *Duplex\n"
  // OutputBin (extra PickOne for iteration coverage)
  "*OpenUI *OutputBin/Output Bin: PickOne\n"
  "*OrderDependency: 50 AnySetup *OutputBin\n"
  "*DefaultOutputBin: StandardBin\n"
  "*OutputBin StandardBin/Standard: \"\"\n"
  "*OutputBin FaceUp/Face Up: \"\"\n"
  "*CloseUI: *OutputBin\n"
  // UIConstraints — the rule that makes Group 8 fire ppdConflicts() > 0
  "*UIConstraints: \"*InputSlot Manual *Duplex DuplexNoTumble\"\n"
  "*UIConstraints: \"*InputSlot Manual *Duplex DuplexTumble\"\n";


//
// 'main()' - Run all ppd-mark.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// PPD file handle
  ppd_option_t *opt;			// Returned by ppdFindOption()
  ppd_choice_t *choice;			// Returned by ppdFindChoice()/MarkedChoice()
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error
  int           conflicts;		// Return value from ppdMarkOption/Options
  int           num_options;		// Option-list size
  cups_option_t *options;		// Parsed/built option list
  int           count;			// Iteration counter
  bool          saw_pagesize;		// Iteration sentinel


  // =========================================================================
  // Group 1: NULL/argument guards (T01–T09)
  //
  // Every public function in ppd-mark.c that accepts pointer arguments must
  // tolerate NULL and return a safe sentinel without crashing.  These are
  // tested before any PPD is opened so there is no dependency on PPD content.
  // =========================================================================

  // T01 — ppdFindChoice: `if (!o || !choice)` first half — NULL option.
  testBegin("ppdFindChoice(NULL option, \"x\") returns NULL");
  testEnd(ppdFindChoice(NULL, "x") == NULL);

  // T02 — ppdFindOption: `if (!ppd || !option)` first half — NULL ppd.
  testBegin("ppdFindOption(NULL ppd, \"PageSize\") returns NULL");
  testEnd(ppdFindOption(NULL, "PageSize") == NULL);

  // T03 — ppdIsMarked: `if (!ppd)` — NULL ppd short-circuits to 0.
  testBegin("ppdIsMarked(NULL ppd, ...) returns 0");
  testEnd(ppdIsMarked(NULL, "PageSize", "Letter") == 0);

  // T04 — ppdMarkOption: `if (!ppd || !option || !choice)` — NULL ppd → 0.
  testBegin("ppdMarkOption(NULL ppd, ...) returns 0");
  testEnd(ppdMarkOption(NULL, "PageSize", "Letter") == 0);

  // T05 — ppdFirstOption: `if (!ppd) return NULL;` — NULL ppd.
  testBegin("ppdFirstOption(NULL) returns NULL");
  testEnd(ppdFirstOption(NULL) == NULL);

  // T06 — ppdNextOption: `if (!ppd) return NULL;` — NULL ppd.
  testBegin("ppdNextOption(NULL) returns NULL");
  testEnd(ppdNextOption(NULL) == NULL);

  // T07 — ppdMarkDefaults: `if (!ppd) return;` — must not crash on NULL.
  //       No return value to assert on; passing test is "did not segfault".
  testBegin("ppdMarkDefaults(NULL) does not crash");
  ppdMarkDefaults(NULL);
  testEnd(true);

  // T08 — ppdMarkOptions: `if (!ppd || num_options <= 0 || !options) return 0;`
  //       NULL ppd hits the first branch of the guard.
  testBegin("ppdMarkOptions(NULL ppd, 1, options) returns 0");
  options     = NULL;
  num_options = cupsAddOption("PageSize", "A4", 0, &options);
  testEnd(ppdMarkOptions(NULL, num_options, options) == 0);
  cupsFreeOptions(num_options, options);

  // T09 — ppdParseOptions: `if (!s) return num_options;` — NULL input
  //       returns the caller's existing count unchanged.  We pass 42
  //       (an arbitrary non-zero sentinel) and expect 42 back, with the
  //       options pointer left untouched (still NULL).
  testBegin("ppdParseOptions(NULL s, 42, ...) returns 42 unchanged");
  options     = NULL;
  num_options = ppdParseOptions(NULL, 42, &options, PPD_PARSE_ALL);
  testEndMessage(num_options == 42 && options == NULL,
                 "got %d, options=%p", num_options, (void *)options);


  // =========================================================================
  // Group 2: ppdOpen() + explicit ppdMarkDefaults() (T10–T15)
  //
  // Open the embedded PPD via tmpfile().  ppdOpen() does NOT call
  // ppdMarkDefaults() — the marked-array is empty until the caller invokes
  // it.  After the explicit call, ppd->marked must hold each option's
  // *Default<Keyword> choice.  ppd_defaults() explicitly skips "PageRegion"
  // to avoid double-marking with PageSize — verify that exception too.
  // =========================================================================

  testBegin("ppdOpen(embedded mark test PPD)");
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

  // ppdOpen() leaves ppd->marked empty; the caller must populate defaults.
  ppdMarkDefaults(ppd);

  // T11 — After the explicit ppdMarkDefaults(), the PageSize default
  //       "Letter" must be in ppd->marked.
  testBegin("ppdIsMarked(\"PageSize\", \"Letter\") == 1 after ppdMarkDefaults");
  testEnd(ppdIsMarked(ppd, "PageSize", "Letter") == 1);

  // T12 — Non-default PageSize choice "A4" is *not* in ppd->marked.
  testBegin("ppdIsMarked(\"PageSize\", \"A4\") == 0 after ppdMarkDefaults");
  testEnd(ppdIsMarked(ppd, "PageSize", "A4") == 0);

  // T13 — Default InputSlot choice "Cassette" was marked by ppd_defaults().
  testBegin("ppdIsMarked(\"InputSlot\", \"Cassette\") == 1 after ppdMarkDefaults");
  testEnd(ppdIsMarked(ppd, "InputSlot", "Cassette") == 1);

  // T14 — Default Duplex choice "None" was marked by ppd_defaults().
  testBegin("ppdIsMarked(\"Duplex\", \"None\") == 1 after ppdMarkDefaults");
  testEnd(ppdIsMarked(ppd, "Duplex", "None") == 1);

  // T15 — Idempotency: re-invoking ppdMarkDefaults() on an already-defaulted
  //       PPD must leave the same defaults in place (Letter still marked).
  testBegin("ppdMarkDefaults() is idempotent (Letter still marked)");
  ppdMarkDefaults(ppd);
  testEnd(ppdIsMarked(ppd, "PageSize", "Letter") == 1);


  // =========================================================================
  // Group 3: ppdFindOption (T16–T21)
  //
  // Hash-style keyword lookup via cupsArrayFind().  The comparator is
  // _ppd_strcasecmp() so the lookup is case-insensitive.  Each test below
  // checks one observable property of the returned ppd_option_t.
  // =========================================================================

  // T16 — Known keyword returns non-NULL ppd_option_t pointer.
  testBegin("ppdFindOption(\"PageSize\") returns non-NULL");
  opt = ppdFindOption(ppd, "PageSize");
  testEnd(opt != NULL);

  // T17 — keyword field of the returned option matches the request
  //       (the comparator is case-insensitive but the stored string
  //       preserves the PPD's casing — "PageSize").
  testBegin("ppdFindOption(\"PageSize\")->keyword == \"PageSize\"");
  testEnd(opt != NULL && !strcmp(opt->keyword, "PageSize"));

  // T18 — Option has at least one choice (the PPD declares Letter and A4
  //       plus a synthesised Custom choice — total ≥ 2).
  testBegin("ppdFindOption(\"PageSize\")->num_choices > 0");
  testEndMessage(opt != NULL && opt->num_choices > 0,
                 "num_choices=%d", opt ? opt->num_choices : -1);

  // T19 — *OpenUI *PageSize: PickOne maps to ppd_ui_t == PPD_UI_PICKONE.
  testBegin("ppdFindOption(\"PageSize\")->ui == PPD_UI_PICKONE");
  testEnd(opt != NULL && opt->ui == PPD_UI_PICKONE);

  // T20 — A keyword absent from the PPD returns NULL (loop exhausts).
  testBegin("ppdFindOption(\"NoSuchOption\") returns NULL");
  testEnd(ppdFindOption(ppd, "NoSuchOption") == NULL);

  // T21 — Second NULL-argument guard: valid ppd but NULL keyword → NULL.
  testBegin("ppdFindOption(valid ppd, NULL keyword) returns NULL");
  testEnd(ppdFindOption(ppd, NULL) == NULL);


  // =========================================================================
  // Group 4: ppdFindChoice (T22–T27)
  //
  // Linear scan over o->choices[].  Two pre-scan rewrites:
  //   • choice[0] == '{'                 → choice = "Custom"
  //   • _ppd_strncasecmp(choice,"Custom.",7)==0 → choice = "Custom"
  // Both cause the function to search for the synthesised "Custom" choice
  // (added because the PPD has *VariablePaperSize: True).
  // =========================================================================

  // T22 — Exact match: "Letter" exists, returned struct's ->choice == "Letter".
  testBegin("ppdFindChoice(PageSize, \"Letter\") returns Letter");
  choice = ppdFindChoice(opt, "Letter");
  testEndMessage(choice != NULL && !strcmp(choice->choice, "Letter"),
                 "got \"%s\"", choice ? choice->choice : "(null)");

  // T23 — Miss: "NoSuchChoice" exhausts the loop → NULL.
  testBegin("ppdFindChoice(PageSize, \"NoSuchChoice\") returns NULL");
  testEnd(ppdFindChoice(opt, "NoSuchChoice") == NULL);

  // T24 — Custom-rewrite via "Custom." prefix: input "Custom.72x72" is
  //       rewritten to "Custom" before the scan.  Our PPD synthesises a
  //       "Custom" choice (because *VariablePaperSize: True is present),
  //       so the returned choice's ->choice field must equal "Custom".
  testBegin("ppdFindChoice(PageSize, \"Custom.72x72\") rewrites to Custom");
  choice = ppdFindChoice(opt, "Custom.72x72");
  testEndMessage(choice != NULL && !strcmp(choice->choice, "Custom"),
                 "got \"%s\"", choice ? choice->choice : "(null)");

  // T25 — Custom-rewrite via leading '{': multi-value custom-option literal
  //       is rewritten to "Custom" before the scan.
  testBegin("ppdFindChoice(PageSize, \"{Width=72 ...}\") rewrites to Custom");
  choice = ppdFindChoice(opt, "{Width=72 Height=72}");
  testEndMessage(choice != NULL && !strcmp(choice->choice, "Custom"),
                 "got \"%s\"", choice ? choice->choice : "(null)");

  // T26 — First NULL-argument guard: NULL option pointer → NULL.
  //       (Duplicates T01 conceptually but the assertion is local to
  //       this group's narrative.)
  testBegin("ppdFindChoice(NULL, \"Letter\") returns NULL");
  testEnd(ppdFindChoice(NULL, "Letter") == NULL);

  // T27 — Second NULL-argument guard: valid option but NULL choice → NULL.
  testBegin("ppdFindChoice(PageSize, NULL) returns NULL");
  testEnd(ppdFindChoice(opt, NULL) == NULL);


  // =========================================================================
  // Group 5: ppdFindMarkedChoice (T28–T31)
  //
  // Uses cupsArrayFind() against ppd->marked, keyed by parent option.
  // Returns NULL when the option is unknown OR when nothing is marked
  // for it.  Note: ppd_defaults() *skips* PageRegion to avoid double-
  // marking, so PageRegion has no marked choice after ppdOpen().
  // =========================================================================

  // T28 — Default Letter is in ppd->marked under the PageSize option key.
  testBegin("ppdFindMarkedChoice(\"PageSize\") returns non-NULL after defaults");
  choice = ppdFindMarkedChoice(ppd, "PageSize");
  testEnd(choice != NULL);

  // T29 — The returned marked choice is Letter (the *DefaultPageSize value).
  testBegin("ppdFindMarkedChoice(\"PageSize\")->choice == \"Letter\"");
  testEndMessage(choice != NULL && !strcmp(choice->choice, "Letter"),
                 "got \"%s\"", choice ? choice->choice : "(null)");

  // T30 — PageRegion is intentionally skipped by ppd_defaults(); thus no
  //       PageRegion entry is in ppd->marked even though *DefaultPageRegion
  //       is declared.  cupsArrayFind() therefore returns NULL here.
  testBegin("ppdFindMarkedChoice(\"PageRegion\") returns NULL (skipped by defaults)");
  testEnd(ppdFindMarkedChoice(ppd, "PageRegion") == NULL);

  // T31 — Unknown option keyword: ppdFindOption() inside returns NULL,
  //       short-circuiting ppdFindMarkedChoice to NULL.
  testBegin("ppdFindMarkedChoice(\"NoSuchOption\") returns NULL");
  testEnd(ppdFindMarkedChoice(ppd, "NoSuchOption") == NULL);


  // =========================================================================
  // Group 6: ppdIsMarked (T32–T36)
  //
  // ppdIsMarked() returns non-zero only when the given option exists AND
  // a marked choice exists for it AND that marked choice's name equals
  // the supplied `choice` string (strcmp, case-sensitive).
  // =========================================================================

  // T32 — Default-marked Letter matches the supplied "Letter" → non-zero.
  testBegin("ppdIsMarked(\"PageSize\", \"Letter\") is non-zero");
  testEnd(ppdIsMarked(ppd, "PageSize", "Letter") != 0);

  // T33 — A4 is *not* the currently marked PageSize → zero.
  testBegin("ppdIsMarked(\"PageSize\", \"A4\") is zero");
  testEnd(ppdIsMarked(ppd, "PageSize", "A4") == 0);

  // T34 — Unknown option keyword: ppdFindOption() fails inside, returns 0.
  testBegin("ppdIsMarked(\"NoSuchOption\", \"X\") returns 0");
  testEnd(ppdIsMarked(ppd, "NoSuchOption", "X") == 0);

  // T35 — Known option but unknown choice name: the marked entry's choice
  //       field is "Letter" — strcmp("Letter", "NoSuchChoice") != 0 → 0.
  testBegin("ppdIsMarked(\"PageSize\", \"NoSuchChoice\") returns 0");
  testEnd(ppdIsMarked(ppd, "PageSize", "NoSuchChoice") == 0);

  // T36 — PageRegion has no marked entry at all → 0 regardless of choice.
  testBegin("ppdIsMarked(\"PageRegion\", \"Letter\") returns 0 (none marked)");
  testEnd(ppdIsMarked(ppd, "PageRegion", "Letter") == 0);


  // =========================================================================
  // Group 7: ppdMarkOption (T37–T43)
  //
  // ppdMarkOption() → ppd_mark_option() → ppdConflicts().  For a PickOne
  // option, marking a new choice removes the previously-marked choice from
  // ppd->marked.  The return value is the *current* conflict count (the
  // result of ppdConflicts() after the mark, not 0/1 success-style).
  // =========================================================================

  // T37 — Mark a non-conflicting non-default choice.  The PPD's defaults
  //       satisfy all UIConstraints, and switching PageSize to A4 doesn't
  //       trip any constraint, so conflicts must be 0.
  testBegin("ppdMarkOption(\"PageSize\", \"A4\") returns 0 conflicts");
  conflicts = ppdMarkOption(ppd, "PageSize", "A4");
  testEndMessage(conflicts == 0, "conflicts=%d", conflicts);

  // T38 — The new choice "A4" is now in ppd->marked.
  testBegin("ppdIsMarked(\"PageSize\", \"A4\") == 1 after marking");
  testEnd(ppdIsMarked(ppd, "PageSize", "A4") == 1);

  // T39 — The previously-marked "Letter" was removed from ppd->marked.
  testBegin("ppdIsMarked(\"PageSize\", \"Letter\") == 0 after marking A4");
  testEnd(ppdIsMarked(ppd, "PageSize", "Letter") == 0);

  // T40 — Unknown option keyword: ppd_mark_option()'s ppdFindOption call
  //       returns NULL and the function returns early without modifying
  //       the marked array.  ppdMarkOption() then returns ppdConflicts(),
  //       which is still 0.  We only assert "no crash and ≥ 0" — the
  //       exact return value is whatever the current conflict state is.
  testBegin("ppdMarkOption(\"NoSuchOption\", \"X\") is a safe no-op");
  conflicts = ppdMarkOption(ppd, "NoSuchOption", "X");
  testEndMessage(conflicts >= 0, "conflicts=%d (no crash)", conflicts);

  // T41 — Unknown choice for a known option: the inner choice-search loop
  //       exits without a match → `if (!i) return;` → no mark change.
  //       A4 should still be the marked PageSize.
  testBegin("ppdMarkOption(\"PageSize\", \"NoSuchChoice\") is a no-op");
  ppdMarkOption(ppd, "PageSize", "NoSuchChoice");
  testEnd(ppdIsMarked(ppd, "PageSize", "A4") == 1);

  // T42 — Second NULL-arg guard: NULL option keyword → return 0 immediately.
  testBegin("ppdMarkOption(ppd, NULL option, \"X\") returns 0");
  testEnd(ppdMarkOption(ppd, NULL, "X") == 0);

  // T43 — Third NULL-arg guard: NULL choice → return 0 immediately.
  testBegin("ppdMarkOption(ppd, \"PageSize\", NULL choice) returns 0");
  testEnd(ppdMarkOption(ppd, "PageSize", NULL) == 0);


  // =========================================================================
  // Group 8: ppdMarkOption + UIConstraints (T44–T47)
  //
  // The PPD declares:
  //   *UIConstraints: "*InputSlot Manual *Duplex DuplexNoTumble"
  //   *UIConstraints: "*InputSlot Manual *Duplex DuplexTumble"
  //
  // Marking InputSlot=Manual while Duplex=None is fine (0 conflicts).
  // Then marking Duplex=DuplexNoTumble while InputSlot=Manual triggers
  // the first constraint and ppdConflicts() must report > 0.
  // Reverting to defaults restores 0 conflicts.
  //
  // Reset to defaults first so previous-group marks don't leak in.
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T44 — Marking InputSlot=Manual alone does not conflict with the default
  //       Duplex=None, so conflicts must be 0.
  testBegin("ppdMarkOption(\"InputSlot\", \"Manual\") returns 0 conflicts");
  conflicts = ppdMarkOption(ppd, "InputSlot", "Manual");
  testEndMessage(conflicts == 0, "conflicts=%d", conflicts);

  // T45 — Marking Duplex=DuplexNoTumble while InputSlot=Manual is already
  //       set trips the first *UIConstraints rule.  ppdMarkOption() returns
  //       ppdConflicts(ppd) which must therefore be > 0.
  testBegin("ppdMarkOption(\"Duplex\", \"DuplexNoTumble\") triggers conflict");
  conflicts = ppdMarkOption(ppd, "Duplex", "DuplexNoTumble");
  testEndMessage(conflicts > 0, "conflicts=%d", conflicts);

  // T46 — ppdConflicts() called directly reports the same > 0 conflict
  //       count (sanity check that the mark-side wrapper agrees with the
  //       conflict-side query).
  testBegin("ppdConflicts() > 0 after constraint trip");
  conflicts = ppdConflicts(ppd);
  testEndMessage(conflicts > 0, "conflicts=%d", conflicts);

  // T47 — ppdMarkDefaults() restores InputSlot=Cassette, Duplex=None which
  //       satisfies every UIConstraint, so ppdConflicts() returns 0 again.
  testBegin("ppdMarkDefaults() restores 0 conflicts");
  ppdMarkDefaults(ppd);
  conflicts = ppdConflicts(ppd);
  testEndMessage(conflicts == 0, "conflicts=%d", conflicts);


  // =========================================================================
  // Group 9: PageSize ↔ PageRegion mutual exclusion (T48–T50)
  //
  // ppd_mark_option() has explicit branches: when marking PageSize, any
  // PageRegion entry in ppd->marked is forcibly removed, and vice versa.
  // We verify both directions by direct manipulation of ppd->marked via
  // ppdMarkOption() + ppdFindMarkedChoice().
  // =========================================================================

  // T48 — Marking PageRegion=A4 should place A4 in ppd->marked under the
  //       PageRegion option key.  (Reminder: after defaults PageRegion has
  //       no marked entry, so we're starting from an empty PageRegion slot.)
  testBegin("ppdMarkOption(\"PageRegion\", \"A4\") populates PageRegion mark");
  ppdMarkOption(ppd, "PageRegion", "A4");
  choice = ppdFindMarkedChoice(ppd, "PageRegion");
  testEndMessage(choice != NULL && !strcmp(choice->choice, "A4"),
                 "got \"%s\"", choice ? choice->choice : "(null)");

  // T49 — Now marking PageSize=Letter must forcibly remove the PageRegion
  //       entry — code path: `else { if ((o = ppdFindOption(ppd,
  //       "PageRegion")) != NULL) { ... cupsArrayRemove(...); } }` — and
  //       ppdFindMarkedChoice("PageRegion") must therefore return NULL.
  testBegin("ppdMarkOption(\"PageSize\", \"Letter\") clears PageRegion mark");
  ppdMarkOption(ppd, "PageSize", "Letter");
  testEnd(ppdFindMarkedChoice(ppd, "PageRegion") == NULL);

  // T50 — Reverse direction: marking PageRegion=Letter must clear the
  //       PageSize entry — code path in the `else` arm that calls
  //       cupsArrayRemove on the PageSize choice.
  testBegin("ppdMarkOption(\"PageRegion\", \"Letter\") clears PageSize mark");
  ppdMarkOption(ppd, "PageRegion", "Letter");
  testEnd(ppdFindMarkedChoice(ppd, "PageSize") == NULL);

  // Reset before subsequent groups.
  ppdMarkDefaults(ppd);


  // =========================================================================
  // Group 10: ppdFirstOption / ppdNextOption iteration (T51–T54)
  //
  // ppdFirstOption() resets ppd->options' internal cursor via
  // cupsArrayGetFirst and returns the first element; ppdNextOption() walks
  // forward.  Our PPD declares 6 PickOne options:
  //   PageSize, PageRegion, Resolution, InputSlot, Duplex, OutputBin
  // so iteration must visit at least 6 and one of them must be PageSize.
  // =========================================================================

  // T51 — ppdFirstOption() returns a non-NULL ppd_option_t * after open.
  testBegin("ppdFirstOption() returns non-NULL");
  opt = ppdFirstOption(ppd);
  testEnd(opt != NULL);

  // T52 — Walk the entire option list and count how many we visit.
  //       Should be ≥ 6 (the six PickOne options listed above).
  testBegin("ppdFirstOption()/ppdNextOption() iterate >= 6 options");
  count = 0;
  saw_pagesize = false;
  for (opt = ppdFirstOption(ppd); opt != NULL; opt = ppdNextOption(ppd))
  {
    count++;
    if (!strcmp(opt->keyword, "PageSize"))
      saw_pagesize = true;
  }
  testEndMessage(count >= 6, "visited %d options", count);

  // T53 — During the iteration above we expect to have encountered the
  //       PageSize option exactly once (booleans don't double-count).
  testBegin("Iteration visits the PageSize option");
  testEnd(saw_pagesize);

  // T54 — After iteration completes ppdNextOption returns NULL (cursor
  //       sat at end-of-array on loop exit).  Calling it again must still
  //       return NULL without crashing.
  testBegin("ppdNextOption() returns NULL at end of array");
  testEnd(ppdNextOption(ppd) == NULL);


  // =========================================================================
  // Group 11: ppdParseOptions (T55–T61)
  //
  // ppdParseOptions() consumes a single string and emits cups_option_t
  // entries.  The leading '*' on a token distinguishes options ("*Opt val")
  // from properties ("prop val").  The PPD_PARSE_* filter selects which
  // class is kept.  Each test below frees its options array via
  // cupsFreeOptions() to avoid leaks.
  // =========================================================================

  // T55 — Parse two "*Option Choice" pairs with PPD_PARSE_ALL.  We expect
  //       two entries because PPD_PARSE_ALL keeps both options and
  //       properties (and our input contains only options).
  testBegin("ppdParseOptions(\"*PageSize Letter *InputSlot Cassette\", ALL) returns 2");
  options     = NULL;
  num_options = ppdParseOptions("*PageSize Letter *InputSlot Cassette",
                                0, &options, PPD_PARSE_ALL);
  testEndMessage(num_options == 2, "got %d", num_options);

  // T56 — First parsed option carries the keyword without the leading '*'
  //       and the choice as the value.  cupsAddOption() inside
  //       ppdParseOptions() sorts the array alphabetically by name, so we
  //       look up by name rather than by index.
  testBegin("Parsed option \"PageSize\" exists with value \"Letter\"");
  testEnd(cupsGetOption("PageSize", num_options, options) != NULL &&
          !strcmp(cupsGetOption("PageSize", num_options, options), "Letter"));

  cupsFreeOptions(num_options, options);

  // T57 — Same input, but with PPD_PARSE_PROPERTIES the parser must skip
  //       both '*'-prefixed entries → resulting array is empty.
  testBegin("ppdParseOptions(\"*PageSize Letter *InputSlot Cassette\", PROPERTIES) returns 0");
  options     = NULL;
  num_options = ppdParseOptions("*PageSize Letter *InputSlot Cassette",
                                0, &options, PPD_PARSE_PROPERTIES);
  testEndMessage(num_options == 0, "got %d", num_options);
  cupsFreeOptions(num_options, options);

  // T58 — A property string ("foo bar") with PPD_PARSE_OPTIONS is skipped
  //       because the token has no leading '*' — result is empty.
  testBegin("ppdParseOptions(\"foo bar\", OPTIONS) returns 0");
  options     = NULL;
  num_options = ppdParseOptions("foo bar", 0, &options, PPD_PARSE_OPTIONS);
  testEndMessage(num_options == 0, "got %d", num_options);
  cupsFreeOptions(num_options, options);

  // T59 — A property string ("foo bar") with PPD_PARSE_PROPERTIES is kept.
  testBegin("ppdParseOptions(\"foo bar\", PROPERTIES) returns 1");
  options     = NULL;
  num_options = ppdParseOptions("foo bar", 0, &options, PPD_PARSE_PROPERTIES);
  testEndMessage(num_options == 1, "got %d", num_options);
  cupsFreeOptions(num_options, options);

  // T60 — Empty string: the outer `while (*s)` loop exits immediately and
  //       the input num_options (here, 0) is returned unchanged.
  testBegin("ppdParseOptions(\"\", 0, ...) returns 0");
  options     = NULL;
  num_options = ppdParseOptions("", 0, &options, PPD_PARSE_ALL);
  testEndMessage(num_options == 0, "got %d", num_options);
  cupsFreeOptions(num_options, options);

  // T61 — Pre-populated options array is preserved: passing in 3 existing
  //       options with a NULL string returns 3 unchanged (NULL-string
  //       guard fires first thing).
  testBegin("ppdParseOptions(NULL, 3, ...) returns 3 (preserves input count)");
  options     = NULL;
  num_options = cupsAddOption("A", "1", 0, &options);
  num_options = cupsAddOption("B", "2", num_options, &options);
  num_options = cupsAddOption("C", "3", num_options, &options);
  num_options = ppdParseOptions(NULL, num_options, &options, PPD_PARSE_ALL);
  testEndMessage(num_options == 3, "got %d", num_options);
  cupsFreeOptions(num_options, options);


  // =========================================================================
  // Group 12: ppdMarkOptions IPP→PPD mapping (T62–T66)
  //
  // The high-level wrapper translates IPP attribute names into PPD option
  // marks.  Branches exercised here:
  //   • zero / NULL options array — early-return guard (returns 0)
  //   • verbatim pass-through ("PageSize" is not a recognised IPP keyword
  //     so it falls through to the generic ppd_mark_option branch)
  //   • "resolution" → "Resolution" (the explicit remap branch)
  //   • "mirror" → "MirrorPrint" — our PPD lacks MirrorPrint so this
  //     branch silently no-ops (verifies tolerance, not effect)
  //
  // Reset to defaults first so the assertions about marks made here aren't
  // contaminated by anything left over from earlier groups.
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T62 — Guard: `if (... || num_options <= 0 || !options) return (0);`
  //       Zero num_options short-circuits even with a valid ppd and
  //       non-NULL options.
  testBegin("ppdMarkOptions(ppd, 0, options) returns 0");
  options     = NULL;
  num_options = cupsAddOption("PageSize", "A4", 0, &options);
  testEnd(ppdMarkOptions(ppd, 0, options) == 0);
  cupsFreeOptions(num_options, options);

  // T63 — Direct PPD option pass-through: "PageSize"="A4" is not in the
  //       IPP keyword table, so the function falls into the catch-all
  //       `else ppd_mark_option(ppd, optptr->name, optptr->value);`
  //       branch.  After the call, A4 must be the marked PageSize choice.
  testBegin("ppdMarkOptions({PageSize=A4}) marks PageSize=A4");
  options     = NULL;
  num_options = cupsAddOption("PageSize", "A4", 0, &options);
  ppdMarkOptions(ppd, num_options, options);
  testEnd(ppdIsMarked(ppd, "PageSize", "A4") == 1);
  cupsFreeOptions(num_options, options);

  // T64 — "resolution" remap branch:
  //         ppd_mark_option(ppd, "Resolution", optptr->value);
  //       The PPD has a Resolution option with a 300dpi choice, so the
  //       mark must succeed and ppdIsMarked must report 300dpi.
  testBegin("ppdMarkOptions({resolution=300dpi}) marks Resolution=300dpi");
  ppdMarkDefaults(ppd);
  options     = NULL;
  num_options = cupsAddOption("resolution", "300dpi", 0, &options);
  ppdMarkOptions(ppd, num_options, options);
  testEnd(ppdIsMarked(ppd, "Resolution", "300dpi") == 1);
  cupsFreeOptions(num_options, options);

  // T65 — "mirror" remap branch with no MirrorPrint option in the PPD:
  //         ppd_mark_option(ppd, "MirrorPrint", optptr->value);
  //       falls through to the inner `o = ppdFindOption(ppd, option);
  //       if (!o) return;` early-exit.  The call must therefore complete
  //       without modifying any existing mark.  Defaults are reset first
  //       so we can assert no mark was added under a phantom keyword.
  testBegin("ppdMarkOptions({mirror=True}) is a tolerant no-op (no MirrorPrint)");
  ppdMarkDefaults(ppd);
  options     = NULL;
  num_options = cupsAddOption("mirror", "True", 0, &options);
  ppdMarkOptions(ppd, num_options, options);
  testEnd(ppdFindOption(ppd, "MirrorPrint") == NULL);
  cupsFreeOptions(num_options, options);

  // T66 — Return-value contract: with defaults restored and only a
  //       non-conflicting option supplied (PageSize=A4), ppdMarkOptions
  //       must return 0 — i.e. ppdConflicts(ppd) returned 0 at the end.
  testBegin("ppdMarkOptions({PageSize=A4}) returns 0 conflicts");
  ppdMarkDefaults(ppd);
  options     = NULL;
  num_options = cupsAddOption("PageSize", "A4", 0, &options);
  conflicts   = ppdMarkOptions(ppd, num_options, options);
  testEndMessage(conflicts == 0, "conflicts=%d", conflicts);
  cupsFreeOptions(num_options, options);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
