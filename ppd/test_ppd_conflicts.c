//
// PPD constraint / conflict API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (28 assertions across 7 groups):
//
//   Group 1  (T01-T08)  NULL/argument guards — every public function in
//                       ppd-conflicts.c short-circuits to a safe sentinel
//                       (0) when handed a NULL pointer.  ppdGetConflicts()
//                       also writes `*options = NULL` before its own
//                       range check, which means callers that pass a
//                       valid `options` slot get it nulled out even when
//                       the call fails — that quirk is exercised here.
//                       ppdResolveConflicts() has an extra symmetry
//                       guard: `(option == NULL) != (choice == NULL)`
//                       fails (i.e. mixing NULL/non-NULL across the two
//                       arguments returns 0).
//
//   Group 2  (T09-T11)  ppdOpen() + ppdMarkDefaults() — opening the
//                       embedded PPD #1 (non-installable constraints
//                       only) succeeds, marking the defaults produces a
//                       conflict-free state (ppdConflicts() == 0), and
//                       ppdInstallableConflict() on a PPD with no
//                       InstallableOptions group always returns 0
//                       (ppd_test_constraints skips non-installable
//                       constraints when which == _PPD_INSTALLABLE_
//                       CONSTRAINTS).
//
//   Group 3  (T12-T15)  ppdConflicts() with marked UIConstraints — our
//                       PPD declares the mirrored pair
//                         *UIConstraints "*Duplex DuplexNoTumble *OutputBin Bottom"
//                         *UIConstraints "*OutputBin Bottom *Duplex DuplexNoTumble"
//                       which ppd_load_constraints() collapses to ONE
//                       active constraint via its consecutive-duplicate
//                       check.  After marking both halves we therefore
//                       see exactly 1 conflict and both option records
//                       carry option->conflicted == 1.  Switching one
//                       half back to a non-conflicting choice clears
//                       both flags and returns ppdConflicts() == 0.
//
//   Group 4  (T16-T19)  ppdGetConflicts() — given a proposed (option,
//                       choice), reports which currently-marked options
//                       would conflict.  We exercise: a proposal that
//                       does not conflict with current marks (0 returned,
//                       *options nulled); a proposal that does conflict
//                       (1 returned with the conflicting option listed
//                       by name and choice); a proposal that matches the
//                       defaults exactly (0 returned).
//
//   Group 5  (T20-T22)  *NonUIConstraints — same engine, declared with
//                       the *NonUIConstraints keyword instead of
//                       *UIConstraints.  We pair InputSlot Manual with
//                       PageSize A4 so the UI/Non-UI distinction doesn't
//                       overlap our Group 3 fixture.
//
//   Group 6  (T23-T25)  ppdResolveConflicts() — starting from a
//                       conflict-free marked state, hand the resolver a
//                       proposed (Duplex, DuplexNoTumble) plus a pending
//                       OutputBin=Bottom in the options array.  The
//                       resolver cannot change the user's recent option
//                       (Duplex) so it switches OutputBin to its default
//                       choice (Top); the call returns 1 and the rewritten
//                       option list now carries Duplex=DuplexNoTumble and
//                       OutputBin=Top.
//
//   Group 7  (T26-T28)  ppdInstallableConflict() — uses a separate PPD
//                       #2 that declares an InstallableOptions group with
//                       OptionDuplexer and a *UIConstraints pair tying
//                       OptionDuplexer=False to Duplex=DuplexNoTumble.
//                       With OptionDuplexer marked False (default), a
//                       proposed Duplex=DuplexNoTumble triggers an
//                       installable conflict (returns 1).  Marking
//                       OptionDuplexer=True clears the constraint and
//                       the same call returns 0.
//
// Design: the suite is fully hermetic.  Two static PPD strings are
// embedded and loaded via tmpfile() + ppdOpen() — no external files are
// required at build or CI time.  PPD #1 (`test_ppd_text`) declares only
// non-installable options so its conflict counts are deterministic;
// PPD #2 (`test_ppd_installable_text`) adds the InstallableOptions
// group and the installable-flavoured UIConstraints used by Group 7.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


//
// PPD #1 — non-installable constraints only.
//
// Option set:
//   PageSize    Letter (default) / A4 — half of the NonUIConstraints pair
//   InputSlot   Auto (default) / Tray1 / Manual — half of the NonUIConstraints pair
//   Duplex      None (default) / DuplexNoTumble — half of the UIConstraints pair
//   OutputBin   Top (default) / Bottom — half of the UIConstraints pair
//
// Constraints:
//   *UIConstraints (mirrored): Duplex=DuplexNoTumble ↔ OutputBin=Bottom
//   *NonUIConstraints (mirrored): InputSlot=Manual ↔ PageSize=A4
//
// The mirrored pairs satisfy the Adobe PPD requirement to declare both
// directions; ppd_load_constraints() collapses each pair into a single
// active constraint via its consecutive-duplicate weeding (see
// ppd-conflicts.c lines 749-755), so ppdConflicts() returns 1 (not 2)
// when both halves are simultaneously marked.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"CONFLICT.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(ConflictTest)\"\n"
  "*ModelName: \"Acme ConflictTest\"\n"
  "*ShortNickName: \"ConflictTest\"\n"
  "*NickName: \"Acme ConflictTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  // PageSize — half of the NonUIConstraints pair (A4 vs Letter default).
  "*OpenUI *PageSize/Media Size: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageSize\n"
  "*DefaultPageSize: Letter\n"
  "*PageSize Letter/US Letter: \"<</PageSize[612 792]>>setpagedevice\"\n"
  "*PageSize A4/A4: \"<</PageSize[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageSize\n"
  // PageRegion — required parallel of PageSize per the Adobe spec.
  "*OpenUI *PageRegion: PickOne\n"
  "*OrderDependency: 10 AnySetup *PageRegion\n"
  "*DefaultPageRegion: Letter\n"
  "*PageRegion Letter: \"<</PageRegion[612 792]>>setpagedevice\"\n"
  "*PageRegion A4: \"<</PageRegion[595 842]>>setpagedevice\"\n"
  "*CloseUI: *PageRegion\n"
  // ImageableArea + PaperDimension (required for a valid PPD).
  "*DefaultImageableArea: Letter\n"
  "*ImageableArea Letter: \"18 18 594 774\"\n"
  "*ImageableArea A4: \"18 18 577 824\"\n"
  "*DefaultPaperDimension: Letter\n"
  "*PaperDimension Letter: \"612 792\"\n"
  "*PaperDimension A4: \"595 842\"\n"
  // InputSlot — half of the NonUIConstraints pair (Manual choice).
  "*OpenUI *InputSlot/Media Source: PickOne\n"
  "*OrderDependency: 20 AnySetup *InputSlot\n"
  "*DefaultInputSlot: Auto\n"
  "*InputSlot Auto/Auto Select: \"\"\n"
  "*InputSlot Tray1/Tray 1: \"\"\n"
  "*InputSlot Manual/Manual Feed: \"\"\n"
  "*CloseUI: *InputSlot\n"
  // Duplex — half of the UIConstraints pair (DuplexNoTumble choice).
  "*OpenUI *Duplex/2-Sided Printing: PickOne\n"
  "*OrderDependency: 30 AnySetup *Duplex\n"
  "*DefaultDuplex: None\n"
  "*Duplex None/Off: \"\"\n"
  "*Duplex DuplexNoTumble/Long Edge: \"\"\n"
  "*CloseUI: *Duplex\n"
  // OutputBin — half of the UIConstraints pair (Bottom choice).
  "*OpenUI *OutputBin/Output Bin: PickOne\n"
  "*OrderDependency: 40 AnySetup *OutputBin\n"
  "*DefaultOutputBin: Top\n"
  "*OutputBin Top/Top Bin: \"\"\n"
  "*OutputBin Bottom/Bottom Bin: \"\"\n"
  "*CloseUI: *OutputBin\n"
  // Constraints — both mirrored pairs collapse to ONE active constraint
  // each via the consecutive-duplicate dedup in ppd_load_constraints().
  "*UIConstraints: \"*Duplex DuplexNoTumble *OutputBin Bottom\"\n"
  "*UIConstraints: \"*OutputBin Bottom *Duplex DuplexNoTumble\"\n"
  "*NonUIConstraints: \"*InputSlot Manual *PageSize A4\"\n"
  "*NonUIConstraints: \"*PageSize A4 *InputSlot Manual\"\n";


//
// PPD #2 — adds an InstallableOptions group + an installable-flavoured
// UIConstraints pair.  Used by Group 7 to drive ppdInstallableConflict().
//
// Option set is the minimum required for a valid PPD plus:
//   *OpenGroup: InstallableOptions
//     OptionDuplexer  False (default) / True   ← installable
//   *CloseGroup: InstallableOptions
//   Duplex            None (default) / DuplexNoTumble
//
// Constraint:
//   *UIConstraints (mirrored): OptionDuplexer=False ↔ Duplex=DuplexNoTumble
//
// Because at least one constrained option lives in the InstallableOptions
// group, ppd_load_constraints sets consts->installable = 1 on the
// resulting record.  ppd_test_constraints with which =
// _PPD_INSTALLABLE_CONSTRAINTS therefore EVALUATES this constraint (and
// skips the non-installable ones from PPD #1, but PPD #2 declares only
// this single constraint, so the point is moot here).
//

static const char test_ppd_installable_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"INSTALL.PPD\"\n"
  "*Manufacturer: \"Acme\"\n"
  "*Product: \"(InstallTest)\"\n"
  "*ModelName: \"Acme InstallTest\"\n"
  "*ShortNickName: \"InstallTest\"\n"
  "*NickName: \"Acme InstallTest, 1.0\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: True\n"
  "*DefaultColorSpace: RGB\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
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
  // Duplex (non-installable) — one half of the constraint pair.
  "*OpenUI *Duplex/2-Sided Printing: PickOne\n"
  "*OrderDependency: 30 AnySetup *Duplex\n"
  "*DefaultDuplex: None\n"
  "*Duplex None/Off: \"\"\n"
  "*Duplex DuplexNoTumble/Long Edge: \"\"\n"
  "*CloseUI: *Duplex\n"
  // Installable hardware option — group name MUST be "InstallableOptions"
  // so ppd_load_constraints' group lookup at lines 730-737 finds it and
  // marks OptionDuplexer as installable.
  "*OpenGroup: InstallableOptions/Options Installed\n"
  "*OpenUI *OptionDuplexer/Duplexer Installed: Boolean\n"
  "*DefaultOptionDuplexer: False\n"
  "*OptionDuplexer False/Not Installed: \"\"\n"
  "*OptionDuplexer True/Installed: \"\"\n"
  "*CloseUI: *OptionDuplexer\n"
  "*CloseGroup: InstallableOptions\n"
  // Constraint between the installable option and Duplex.  Because
  // OptionDuplexer is in InstallableOptions, the parsed constraint has
  // consts->installable == 1 — i.e. it is evaluated by
  // ppdInstallableConflict() but skipped by ppdGetConflicts() with
  // which == _PPD_OPTION_CONSTRAINTS (not applicable here since
  // ppdGetConflicts uses _PPD_ALL_CONSTRAINTS).
  "*UIConstraints: \"*OptionDuplexer False *Duplex DuplexNoTumble\"\n"
  "*UIConstraints: \"*Duplex DuplexNoTumble *OptionDuplexer False\"\n";


//
// 'main()' - Run all ppd-conflicts.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t      *ppd;			// PPD #1 handle
  ppd_file_t      *ppd2;		// PPD #2 handle (installable)
  ppd_option_t    *opt;			// Returned by ppdFindOption()
  FILE            *f;			// Temporary FILE for in-memory PPD
  ppd_status_t     err;			// PPD parse error code
  int              line;		// Line number of any parse error
  int              n;			// Conflict count
  int              num_options;		// Option-list size
  cups_option_t   *options;		// Parsed/built option list
  const char      *val;			// Option-value probe
  int              rc;			// Return value from resolver


  // =========================================================================
  // Group 1: NULL/argument guards (T01-T08)
  //
  // Every public function in ppd-conflicts.c opens with a range check.
  // None of these need a parsed PPD — they run before ppdOpen().  We pass
  // dummy non-NULL pointers (cast to the expected types) where required;
  // the early-return guards fire before any dereference, so this is safe.
  // =========================================================================

  // T01 — ppdConflicts(NULL) — `if (!ppd) return (0);` (line 583-584).
  testBegin("ppdConflicts(NULL) returns 0");
  testEnd(ppdConflicts(NULL) == 0);

  // T02 — ppdGetConflicts(NULL ppd, ...) — `!ppd` clause of the combined
  //       guard at line 87.  Note: the function FIRST nulls *options
  //       (line 84-85), so we pass a real cups_option_t pointer to
  //       exercise that side effect as well.
  testBegin("ppdGetConflicts(NULL ppd, ...) returns 0 and nulls *options");
  options = (cups_option_t *)0xDEADBEEF;   // sentinel — must be cleared
  n = ppdGetConflicts(NULL, "Duplex", "DuplexNoTumble", &options);
  testEnd(n == 0 && options == NULL);

  // T03 — ppdGetConflicts(ppd, NULL option, ...) — `!option` clause.
  testBegin("ppdGetConflicts(ppd, NULL option, ...) returns 0");
  options = NULL;
  testEnd(ppdGetConflicts((ppd_file_t *)1, NULL, "x", &options) == 0);

  // T04 — ppdGetConflicts(ppd, opt, NULL choice, ...) — `!choice` clause.
  testBegin("ppdGetConflicts(ppd, opt, NULL choice, ...) returns 0");
  options = NULL;
  testEnd(ppdGetConflicts((ppd_file_t *)1, "Duplex", NULL, &options) == 0);

  // T05 — ppdGetConflicts(ppd, opt, choice, NULL **options) — `!options`
  //       clause.  Note: because *options would be dereferenced earlier
  //       to null it, the function tests `if (options)` BEFORE the
  //       null-check (line 84) — so passing NULL for the slot is safe and
  //       still returns 0.
  testBegin("ppdGetConflicts(ppd, opt, choice, NULL slot) returns 0");
  testEnd(ppdGetConflicts((ppd_file_t *)1, "Duplex", "DuplexNoTumble", NULL)
          == 0);

  // T06 — ppdInstallableConflict(NULL, ...) — `!ppd` (line 656-657).
  testBegin("ppdInstallableConflict(NULL, ...) returns 0");
  testEnd(ppdInstallableConflict(NULL, "Duplex", "DuplexNoTumble") == 0);

  // T07 — ppdInstallableConflict(ppd, NULL option, ...) — `!option`.
  testBegin("ppdInstallableConflict(ppd, NULL, ...) returns 0");
  testEnd(ppdInstallableConflict((ppd_file_t *)1, NULL, "x") == 0);

  // T08 — ppdResolveConflicts(NULL, ...) — `!ppd` (line 202-203).  The
  //       same line also enforces the symmetry rule `(option == NULL) !=
  //       (choice == NULL)` — i.e. mixing NULL and non-NULL across the
  //       two arguments must fail.  Both halves are covered by passing
  //       NULL/non-NULL with a NULL ppd; the failing guard returns first.
  testBegin("ppdResolveConflicts(NULL, ...) returns 0");
  num_options = 0;
  options     = NULL;
  testEnd(ppdResolveConflicts(NULL, "Duplex", "DuplexNoTumble",
                              &num_options, &options) == 0);


  // =========================================================================
  // Group 2: ppdOpen() + ppdMarkDefaults() (T09-T11)
  //
  // Open PPD #1, mark its defaults explicitly (ppdOpen() does NOT mark
  // defaults — that is the caller's responsibility), and verify the
  // resulting state has no conflicts.  Also verify that
  // ppdInstallableConflict() on a PPD with no InstallableOptions group
  // always returns 0 — ppd_test_constraints with which =
  // _PPD_INSTALLABLE_CONSTRAINTS skips every non-installable constraint,
  // and PPD #1 has only non-installable ones.
  // =========================================================================

  testBegin("ppdOpen(embedded conflicts test PPD #1)");
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

  // T10 — After ppdMarkDefaults() every option carries its declared
  //       default (Duplex=None, OutputBin=Top, InputSlot=Auto,
  //       PageSize=Letter).  None of those choices appear on the
  //       constrained side of any UIConstraints/NonUIConstraints pair,
  //       so ppdConflicts() must report 0.
  testBegin("ppdMarkDefaults; ppdConflicts() == 0 on clean default state");
  ppdMarkDefaults(ppd);
  testEndMessage(ppdConflicts(ppd) == 0, "got %d", ppdConflicts(ppd));

  // T11 — PPD #1 has no InstallableOptions group, so every parsed
  //       constraint has consts->installable == 0.  ppdInstallableConflict()
  //       runs ppd_test_constraints with which = _PPD_INSTALLABLE_
  //       CONSTRAINTS, which skips every non-installable constraint —
  //       therefore the return is unconditionally 0 even for a known
  //       option/choice pair that would otherwise conflict.
  testBegin("ppdInstallableConflict on PPD with no Installable group is 0");
  testEnd(ppdInstallableConflict(ppd, "Duplex", "DuplexNoTumble") == 0);


  // =========================================================================
  // Group 3: ppdConflicts() with marked UIConstraints (T12-T15)
  //
  // The mirrored UIConstraints pair
  //   *Duplex DuplexNoTumble *OutputBin Bottom
  //   *OutputBin Bottom *Duplex DuplexNoTumble
  // collapses to ONE active constraint via ppd_load_constraints' adjacent-
  // mirror dedup.  Marking both halves activates that constraint; both
  // constrained options' ->conflicted flags are then set by ppdConflicts.
  // =========================================================================

  // T12 — Mark both halves of the constrained pair.  Exactly 1 constraint
  //       is active (the dedupped pair), so ppdConflicts() returns 1.
  testBegin("Mark Duplex=DuplexNoTumble + OutputBin=Bottom; ppdConflicts() == 1");
  ppdMarkOption(ppd, "Duplex",    "DuplexNoTumble");
  ppdMarkOption(ppd, "OutputBin", "Bottom");
  n = ppdConflicts(ppd);
  testEndMessage(n == 1, "got %d", n);

  // T13 — ppdConflicts() walks the active constraints and sets ->conflicted
  //       = 1 on every constrained option's ppd_option_t.  Verify Duplex's
  //       flag is set.
  testBegin("Duplex option->conflicted == 1 after ppdConflicts()");
  opt = ppdFindOption(ppd, "Duplex");
  testEnd(opt != NULL && opt->conflicted == 1);

  // T14 — Same as T13 but for OutputBin (the other side of the pair).
  testBegin("OutputBin option->conflicted == 1 after ppdConflicts()");
  opt = ppdFindOption(ppd, "OutputBin");
  testEnd(opt != NULL && opt->conflicted == 1);

  // T15 — Switching OutputBin back to its default (Top) breaks the
  //       both-halves-marked condition; the constraint is no longer
  //       active and ppdConflicts() returns 0 again.
  testBegin("Mark OutputBin=Top; ppdConflicts() returns to 0");
  ppdMarkOption(ppd, "OutputBin", "Top");
  n = ppdConflicts(ppd);
  testEndMessage(n == 0, "got %d", n);


  // =========================================================================
  // Group 4: ppdGetConflicts() (T16-T19)
  //
  // ppdGetConflicts(ppd, option, choice, **options) tests "what would
  // conflict if I marked (option, choice) on top of the currently-marked
  // state".  The proposed (option, choice) acts as an override for that
  // option only; every OTHER option keeps its current marked value.
  // Returns the count of OTHER options that conflict, and fills *options
  // with their (keyword, choice) pairs.
  // =========================================================================

  // Reset to defaults so each test starts from a known state.
  ppdMarkDefaults(ppd);

  // T16 — Proposed Duplex=DuplexNoTumble, OutputBin still Top (default).
  //       Constraint requires BOTH DuplexNoTumble AND Bottom; OutputBin
  //       is Top → not active → 0 conflicts, *options nulled.
  testBegin("ppdGetConflicts(Duplex,DuplexNoTumble) with default marks: 0");
  options = (cups_option_t *)0xDEADBEEF;
  n = ppdGetConflicts(ppd, "Duplex", "DuplexNoTumble", &options);
  testEndMessage(n == 0 && options == NULL, "n=%d options=%p", n,
                 (void *)options);

  // T17 — Mark OutputBin=Bottom (no conflict yet — Duplex still None).
  //       Now propose Duplex=DuplexNoTumble: both halves match → constraint
  //       active → 1 conflict.  The conflicting option listed is OutputBin
  //       (the OTHER option of the constraint, not the proposed one).
  testBegin("Mark OutputBin=Bottom; ppdGetConflicts(Duplex,DuplexNoTumble) == 1");
  ppdMarkOption(ppd, "OutputBin", "Bottom");
  options = NULL;
  n = ppdGetConflicts(ppd, "Duplex", "DuplexNoTumble", &options);
  testEndMessage(n == 1, "n=%d", n);

  // T18 — The reported option list must carry OutputBin=Bottom (taken
  //       from the constraint's recorded choice — see ppd-conflicts.c
  //       lines 110-117).
  testBegin("ppdGetConflicts options[] contains OutputBin=Bottom");
  val = cupsGetOption("OutputBin", n, options);
  testEndMessage(val != NULL && !strcmp(val, "Bottom"),
                 "OutputBin=\"%s\"", val ? val : "(null)");
  cupsFreeOptions(n, options);

  // T19 — Reset; propose Duplex=None (the default).  None doesn't appear
  //       in any constraint, so no constraint can fire → 0 conflicts.
  testBegin("ppdGetConflicts(Duplex,None) on default marks returns 0");
  ppdMarkDefaults(ppd);
  options = NULL;
  n = ppdGetConflicts(ppd, "Duplex", "None", &options);
  testEndMessage(n == 0 && options == NULL, "n=%d options=%p", n,
                 (void *)options);


  // =========================================================================
  // Group 5: *NonUIConstraints (T20-T22)
  //
  // *NonUIConstraints uses the same parser/storage and the same
  // constraint engine as *UIConstraints — only the keyword differs.  We
  // exercise the second mirrored pair (InputSlot=Manual ↔ PageSize=A4)
  // so that NonUIConstraints' code path is independently confirmed.
  // =========================================================================

  ppdMarkDefaults(ppd);

  // T20 — Mark both halves of the NonUIConstraints pair.  Same dedup
  //       logic → 1 active constraint → ppdConflicts() == 1.
  testBegin("Mark InputSlot=Manual + PageSize=A4; ppdConflicts() == 1");
  ppdMarkOption(ppd, "InputSlot", "Manual");
  ppdMarkOption(ppd, "PageSize",  "A4");
  n = ppdConflicts(ppd);
  testEndMessage(n == 1, "got %d", n);

  // T21 — Switch PageSize back to Letter (its default) — the both-halves
  //       condition breaks and the constraint deactivates.
  testBegin("Mark PageSize=Letter; NonUIConstraints conflict clears");
  ppdMarkOption(ppd, "PageSize", "Letter");
  n = ppdConflicts(ppd);
  testEndMessage(n == 0, "got %d", n);

  // T22 — ppdGetConflicts() honours NonUIConstraints identically.  Re-mark
  //       PageSize=A4 (InputSlot is still Manual from T20→T21), then
  //       propose InputSlot=Manual again to force the constraint to fire.
  testBegin("ppdGetConflicts on NonUIConstraints: PageSize=A4 marked → 1");
  ppdMarkOption(ppd, "PageSize", "A4");
  options = NULL;
  n = ppdGetConflicts(ppd, "InputSlot", "Manual", &options);
  testEndMessage(n == 1, "n=%d", n);
  cupsFreeOptions(n, options);


  // =========================================================================
  // Group 6: ppdResolveConflicts() (T23-T25)
  //
  // ppdResolveConflicts() builds a shadow option list from the caller's
  // (num_options, options) plus the proposed (option, choice), then
  // iterates over active constraints trying to switch one of the
  // constrained options to a non-conflicting choice.  Per the source
  // (lines 390-396), it will NEVER switch the user's own most-recent
  // option — so for a Duplex×OutputBin conflict where the user proposes
  // Duplex=DuplexNoTumble, the resolver MUST change OutputBin.  Default
  // first, then iterate choices.
  // =========================================================================

  ppdMarkDefaults(ppd);

  // Pre-stage a pending OutputBin=Bottom in the options list.  This
  // is the state ppdResolveConflicts() sees on entry — Duplex hasn't
  // been added yet; the resolver will append it via the (option, choice)
  // argument internally (line 215-216).
  num_options = 0;
  options     = NULL;
  num_options = cupsAddOption("OutputBin", "Bottom", num_options, &options);

  // T23 — Call the resolver with proposed Duplex=DuplexNoTumble.  Shadow
  //       state inside: OutputBin=Bottom, Duplex=DuplexNoTumble → constraint
  //       fires.  The resolver skips Duplex (the user's option) and tries
  //       OutputBin's default (Top); Duplex=DuplexNoTumble + OutputBin=Top
  //       has no constraint → resolved → returns 1.
  testBegin("ppdResolveConflicts(Duplex=DuplexNoTumble, OutputBin=Bottom) -> 1");
  rc = ppdResolveConflicts(ppd, "Duplex", "DuplexNoTumble",
                           &num_options, &options);
  testEndMessage(rc == 1, "rc=%d", rc);

  // T24 — The rewritten option list must now carry OutputBin=Top (the
  //       resolver swapped it from Bottom to its default).
  testBegin("ppdResolveConflicts rewrites OutputBin=Top");
  val = cupsGetOption("OutputBin", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "Top"),
                 "OutputBin=\"%s\"", val ? val : "(null)");

  // T25 — The proposed (option, choice) is preserved in the rewritten
  //       list (it was added by the resolver via cupsAddOption at line
  //       215-216 of ppd-conflicts.c).
  testBegin("ppdResolveConflicts preserves Duplex=DuplexNoTumble");
  val = cupsGetOption("Duplex", num_options, options);
  testEndMessage(val != NULL && !strcmp(val, "DuplexNoTumble"),
                 "Duplex=\"%s\"", val ? val : "(null)");

  cupsFreeOptions(num_options, options);


  // =========================================================================
  // Group 7: ppdInstallableConflict() via PPD #2 (T26-T28)
  //
  // PPD #2 declares an InstallableOptions group containing OptionDuplexer
  // and a UIConstraints pair tying OptionDuplexer=False to
  // Duplex=DuplexNoTumble.  Because at least one side is installable,
  // ppd_load_constraints sets consts->installable = 1 — so
  // ppdInstallableConflict() (which restricts to installable-flagged
  // constraints) sees this one.
  // =========================================================================

  testBegin("ppdOpen(embedded conflicts test PPD #2 — installable group)");
  f = tmpfile();
  fputs(test_ppd_installable_text, f);
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

  // T27 — Defaults: OptionDuplexer=False (declared default).  Propose
  //       Duplex=DuplexNoTumble: shadow becomes OptionDuplexer=False
  //       (marked) + Duplex=DuplexNoTumble (proposed); both halves of the
  //       constraint match → active → ppdInstallableConflict() returns 1.
  testBegin("ppdInstallableConflict on PPD #2 with OptionDuplexer=False -> 1");
  ppdMarkDefaults(ppd2);
  testEnd(ppdInstallableConflict(ppd2, "Duplex", "DuplexNoTumble") == 1);

  // T28 — Mark OptionDuplexer=True.  Now the OptionDuplexer half no
  //       longer matches the constraint's "False" requirement → constraint
  //       inactive → ppdInstallableConflict() returns 0.
  testBegin("Mark OptionDuplexer=True; ppdInstallableConflict -> 0");
  ppdMarkOption(ppd2, "OptionDuplexer", "True");
  testEnd(ppdInstallableConflict(ppd2, "Duplex", "DuplexNoTumble") == 0);


  // -------------------------------------------------------------------------
  // Tear down and return success/failure based on the framework flag.
  // -------------------------------------------------------------------------
  ppdClose(ppd2);
  ppdClose(ppd);
  return (testsPassed ? 0 : 1);
}
