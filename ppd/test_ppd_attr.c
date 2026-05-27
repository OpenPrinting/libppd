//
// PPD model-specific attribute API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (30 assertions across 5 groups):
//
//   Group 1  (T01-T05)  NULL/argument guards — every public function in
//                       ppd-attr.c opens with a range check.  ppdFindAttr()
//                       and ppdFindNextAttr() bail on `!ppd || !name ||
//                       num_attrs == 0`; ppdNormalizeMakeAndModel() bails on
//                       `!make_and_model || !buffer || bufsize < 1`.  These
//                       run before any PPD is opened so there is no
//                       dependency on PPD content.
//
//   Group 2  (T06-T14)  ppdFindAttr() basic lookup — open the embedded PPD
//                       and resolve attributes by name.  ppd->sorted_attrs is
//                       a cupsArray whose comparator (ppd_compare_attrs) keys
//                       on name only via _ppd_strcasecmp(), so name lookup is
//                       case-insensitive while the stored name preserves the
//                       PPD's casing.  We verify the post-open name guard, an
//                       unknown name, three standard single-instance
//                       attributes (Manufacturer/ModelName/NickName) and
//                       their values, case-insensitive name lookup returning
//                       the same record, casing preservation, and the empty
//                       specifier of a header attribute.
//
//   Group 3  (T15-T18)  ppdFindAttr() with a specifier — when a non-NULL
//                       spec is supplied the function walks the run of
//                       same-named attributes (they are adjacent because the
//                       comparator keys on name) and returns the one whose
//                       ->spec matches via _ppd_strcasecmp().  We use a custom
//                       attribute declared three times (TestRepeatAttr with
//                       specifiers Alpha/Beta/Gamma) to confirm an exact spec
//                       hit, its value, a case-insensitive spec hit, and a
//                       miss for an absent specifier.
//
//   Group 4  (T19-T21)  ppdFindNextAttr() iteration — ppdFindAttr() positions
//                       the array's single internal cursor at the first match
//                       and ppdFindNextAttr() advances it, returning each
//                       successive same-named attribute and NULL once the run
//                       ends (at which point it parks the cursor past the last
//                       element).  We walk all three TestRepeatAttr instances,
//                       confirm a further call still returns NULL, and confirm
//                       a single-instance attribute yields NULL on the first
//                       ppdFindNextAttr().
//
//                       NOTE: ppdFindAttr() and ppdFindNextAttr() share the
//                       same cupsArray cursor on ppd->sorted_attrs.  A
//                       ppdFindAttr() call therefore disturbs an in-progress
//                       walk, so the iteration tests below never interleave a
//                       Find between the initial Find and the Next loop.
//
//   Group 5  (T22-T30)  ppdNormalizeMakeAndModel() — the make-and-model
//                       string cleaner.  It strips surrounding parenthesis,
//                       prepends/normalizes well-known manufacturer names,
//                       trims surrounding whitespace, and returns NULL when
//                       the result is empty.  We exercise the parenthesis,
//                       whitespace-trim, Hewlett-Packard→HP, deskjet→HP,
//                       agfa→AGFA, XPrint→Xerox, and pass-through branches,
//                       confirm the return value aliases the caller's buffer,
//                       and confirm an all-whitespace input yields NULL.
//
// Design: the PPD is built entirely in memory from a single static string
// and loaded via tmpfile() + ppdOpen(), so the binary is fully hermetic — no
// external files are needed at build or CI time.  It declares:
//
//   • Standard header attributes (*Manufacturer, *ModelName, *NickName, …) —
//     these are also stored in ppd->sorted_attrs and so are retrievable with
//     ppdFindAttr(), which is exactly how CUPS itself reads them back.
//   • A valid media option set (PageSize/PageRegion/ImageableArea/
//     PaperDimension) so ppdOpen() accepts the file.
//   • A custom vendor attribute "TestRepeatAttr" declared three times with
//     distinct specifiers (Alpha/Beta/Gamma) — an arbitrary keyword the
//     parser stores verbatim as generic attributes, giving us a clean,
//     special-case-free run of same-named attributes to drive the specifier
//     lookup and ppdFindNextAttr() iteration tests.
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
//     Mandatory metadata so ppdOpen() returns a valid ppd_file_t *.  The
//     *Manufacturer/*ModelName/*NickName lines double as the standard
//     single-instance attributes asserted in Group 2; their declared values
//     are echoed in the expected strings below.
//
//   PageSize / PageRegion / ImageableArea / PaperDimension:
//     A valid, spec-conformant media option set required for a parseable PPD.
//
//   TestRepeatAttr (×3):
//     An arbitrary vendor keyword the parser does not special-case, so each
//     line is stored verbatim as a generic ppd_attr_t (name "TestRepeatAttr",
//     spec = the option word, value = the quoted string).  Declaring it three
//     times with specifiers Alpha/Beta/Gamma creates the run of same-named
//     attributes used by the specifier-lookup and ppdFindNextAttr() tests.
//     They sit at top level (outside any *OpenUI/*CloseUI block) so they are
//     never swallowed as option choices.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"ATTRTEST.PPD\"\n"
  "*Manufacturer: \"Revrag\"\n"
  "*Product: \"(AttrTest)\"\n"
  "*ModelName: \"Revrag AttrTest\"\n"
  "*ShortNickName: \"AttrTest\"\n"
  "*NickName: \"Revrag AttrTest, 1.0\"\n"
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
  // PageRegion (parallel of PageSize — required by the Adobe PPD spec)
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
  // Custom vendor attribute declared three times — a clean, special-case-free
  // run of same-named attributes (specifiers Alpha, Beta, Gamma).
  "*TestRepeatAttr Alpha/Alpha Label: \"patch-alpha\"\n"
  "*TestRepeatAttr Beta/Beta Label: \"patch-beta\"\n"
  "*TestRepeatAttr Gamma/Gamma Label: \"patch-gamma\"\n";


//
// 'main()' - Run all ppd-attr.c unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// PPD file handle
  ppd_attr_t   *attr;			// Returned by ppdFind/FindNextAttr()
  ppd_attr_t   *attr2;			// Second lookup for pointer-identity test
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error
  int           count;			// Iteration counter
  bool          saw_a, saw_b, saw_g;	// Specifier-seen sentinels
  char          buf[256];		// Scratch buffer for normalizer
  char         *ret;			// Return value of normalizer


  // =========================================================================
  // Group 1: NULL/argument guards (T01–T05)
  //
  // ppdFindAttr()/ppdFindNextAttr() range-check `!ppd || !name || num_attrs
  // == 0`; ppdNormalizeMakeAndModel() range-checks `!make_and_model ||
  // !buffer || bufsize < 1`.  These need no PPD content and run first.
  // =========================================================================

  // T01 — ppdFindAttr: `if (!ppd ...) return (NULL);` — a NULL PPD must
  //       short-circuit before touching ppd->sorted_attrs.
  testBegin("ppdFindAttr(NULL ppd, \"ModelName\", NULL) returns NULL");
  testEnd(ppdFindAttr(NULL, "ModelName", NULL) == NULL);

  // T02 — ppdFindNextAttr: same range check — a NULL PPD returns NULL.
  testBegin("ppdFindNextAttr(NULL ppd, \"ModelName\", NULL) returns NULL");
  testEnd(ppdFindNextAttr(NULL, "ModelName", NULL) == NULL);

  // T03 — ppdNormalizeMakeAndModel: a NULL source string returns NULL and,
  //       because the buffer is non-NULL, the function clears buffer[0].
  testBegin("ppdNormalizeMakeAndModel(NULL, buf, size) returns NULL, clears buf");
  buf[0] = 'X';
  ret    = ppdNormalizeMakeAndModel(NULL, buf, sizeof(buf));
  testEnd(ret == NULL && buf[0] == '\0');

  // T04 — ppdNormalizeMakeAndModel: a NULL buffer returns NULL (nothing to
  //       write into).
  testBegin("ppdNormalizeMakeAndModel(\"x\", NULL, size) returns NULL");
  testEnd(ppdNormalizeMakeAndModel("x", NULL, sizeof(buf)) == NULL);

  // T05 — ppdNormalizeMakeAndModel: a zero-length buffer (bufsize < 1)
  //       returns NULL.
  testBegin("ppdNormalizeMakeAndModel(\"x\", buf, 0) returns NULL");
  testEnd(ppdNormalizeMakeAndModel("x", buf, 0) == NULL);


  // =========================================================================
  // Group 2: ppdFindAttr() basic lookup (T06–T14)
  //
  // Open the embedded PPD.  Header lines like *Manufacturer are also recorded
  // in ppd->sorted_attrs, so ppdFindAttr() can read them back.  The array
  // comparator keys on name via _ppd_strcasecmp() → case-insensitive lookup,
  // case-preserving storage.
  // =========================================================================

  testBegin("ppdOpen(embedded attribute test PPD)");
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

  // T07 — Post-open name guard: a NULL name still hits the `!name` range
  //       check and returns NULL even though the PPD now has attributes.
  testBegin("ppdFindAttr(ppd, NULL, NULL) returns NULL");
  testEnd(ppdFindAttr(ppd, NULL, NULL) == NULL);

  // T08 — A name that appears nowhere in the PPD returns NULL.
  testBegin("ppdFindAttr(ppd, \"NoSuchAttr\", NULL) returns NULL");
  testEnd(ppdFindAttr(ppd, "NoSuchAttr", NULL) == NULL);

  // T09 — *ModelName: "Revrag AttrTest" is stored as an attribute; with a
  //       NULL spec the first (only) match is returned and its dequoted value
  //       must equal the declared string.
  testBegin("ppdFindAttr(ppd, \"ModelName\", NULL) value == \"Revrag AttrTest\"");
  attr = ppdFindAttr(ppd, "ModelName", NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "Revrag AttrTest"),
                 "got \"%s\"", (attr && attr->value) ? attr->value : "(null)");

  // T10 — *Manufacturer: "Revrag" → value "Revrag".
  testBegin("ppdFindAttr(ppd, \"Manufacturer\", NULL) value == \"Revrag\"");
  attr = ppdFindAttr(ppd, "Manufacturer", NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "Revrag"),
                 "got \"%s\"", (attr && attr->value) ? attr->value : "(null)");

  // T11 — *NickName: "Revrag AttrTest, 1.0" → value preserved verbatim.
  testBegin("ppdFindAttr(ppd, \"NickName\", NULL) value == \"Revrag AttrTest, 1.0\"");
  attr = ppdFindAttr(ppd, "NickName", NULL);
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "Revrag AttrTest, 1.0"),
                 "got \"%s\"", (attr && attr->value) ? attr->value : "(null)");

  // T12 — Case-insensitive name lookup: "manufacturer" resolves to the *same*
  //       record pointer as "Manufacturer" because the comparator is
  //       _ppd_strcasecmp().
  testBegin("ppdFindAttr(ppd, \"manufacturer\", NULL) finds the same record");
  attr  = ppdFindAttr(ppd, "Manufacturer", NULL);
  attr2 = ppdFindAttr(ppd, "manufacturer", NULL);
  testEndMessage(attr2 != NULL && attr2 == attr, "%p vs %p",
                 (void *)attr2, (void *)attr);

  // T13 — Stored name preserves the PPD's casing (strlcpy() copied it
  //       verbatim; only comparison is case-folded).
  testBegin("ppdFindAttr(ppd, \"manufacturer\", NULL)->name == \"Manufacturer\"");
  attr = ppdFindAttr(ppd, "manufacturer", NULL);
  testEndMessage(attr != NULL && !strcmp(attr->name, "Manufacturer"),
                 "got \"%s\"", attr ? attr->name : "(null)");

  // T14 — *ModelName has no option word before the colon, so its specifier is
  //       the empty string.
  testBegin("ppdFindAttr(ppd, \"ModelName\", NULL)->spec == \"\"");
  attr = ppdFindAttr(ppd, "ModelName", NULL);
  testEndMessage(attr != NULL && attr->spec[0] == '\0',
                 "got \"%s\"", attr ? attr->spec : "(null)");


  // =========================================================================
  // Group 3: ppdFindAttr() with a specifier (T15–T18)
  //
  // TestRepeatAttr is declared three times (Alpha/Beta/Gamma).  All three are
  // adjacent in sorted_attrs (same name → comparator returns 0), so a spec
  // search walks the run and returns the matching ->spec via _ppd_strcasecmp().
  // =========================================================================

  // T15 — With a NULL spec, the first attribute named "TestRepeatAttr" is
  //       returned; we only assert it exists and carries the right name (the
  //       ordering among equal-named records is an array detail, not asserted).
  testBegin("ppdFindAttr(ppd, \"TestRepeatAttr\", NULL) returns a TestRepeatAttr");
  attr = ppdFindAttr(ppd, "TestRepeatAttr", NULL);
  testEnd(attr != NULL && !strcmp(attr->name, "TestRepeatAttr"));

  // T16 — An exact specifier ("Beta") selects that specific instance, whose
  //       value is the string declared on the *TestRepeatAttr Beta line.
  testBegin("ppdFindAttr(ppd, \"TestRepeatAttr\", \"Beta\") -> spec/value match");
  attr = ppdFindAttr(ppd, "TestRepeatAttr", "Beta");
  testEndMessage(attr != NULL && !strcmp(attr->spec, "Beta") &&
                 attr->value != NULL && !strcmp(attr->value, "patch-beta"),
                 "spec=\"%s\" value=\"%s\"",
                 attr ? attr->spec : "(null)",
                 (attr && attr->value) ? attr->value : "(null)");

  // T17 — Specifier matching is case-insensitive (_ppd_strcasecmp), so lower-
  //       case "beta" resolves the same "Beta" instance.
  testBegin("ppdFindAttr(ppd, \"TestRepeatAttr\", \"beta\") matches case-insensitively");
  attr = ppdFindAttr(ppd, "TestRepeatAttr", "beta");
  testEndMessage(attr != NULL && attr->value != NULL &&
                 !strcmp(attr->value, "patch-beta"),
                 "value=\"%s\"", (attr && attr->value) ? attr->value : "(null)");

  // T18 — A specifier that no instance carries exhausts the run → NULL.
  testBegin("ppdFindAttr(ppd, \"TestRepeatAttr\", \"Delta\") returns NULL");
  testEnd(ppdFindAttr(ppd, "TestRepeatAttr", "Delta") == NULL);


  // =========================================================================
  // Group 4: ppdFindNextAttr() iteration (T19–T21)
  //
  // ppdFindAttr() seeds the cursor at the first match; ppdFindNextAttr()
  // walks forward through the same-named run and returns NULL past its end.
  // The walk below performs NO interleaved ppdFindAttr() (which would reset
  // the shared cursor).
  // =========================================================================

  // T19 — Walk every TestRepeatAttr via ppdFindAttr() + ppdFindNextAttr().
  //       The PPD declares exactly three, and all three specifiers (Alpha,
  //       Beta, Gamma) must be observed.  The set is checked rather than the
  //       order, since ordering among equal-named records is not part of the
  //       public contract.
  testBegin("ppdFindNextAttr iterates exactly 3 TestRepeatAttr instances");
  count = 0;
  saw_a = saw_b = saw_g = false;
  for (attr = ppdFindAttr(ppd, "TestRepeatAttr", NULL); attr != NULL;
       attr = ppdFindNextAttr(ppd, "TestRepeatAttr", NULL))
  {
    count++;
    if (!strcmp(attr->spec, "Alpha"))
      saw_a = true;
    else if (!strcmp(attr->spec, "Beta"))
      saw_b = true;
    else if (!strcmp(attr->spec, "Gamma"))
      saw_g = true;
  }
  testEndMessage(count == 3 && saw_a && saw_b && saw_g,
                 "count=%d a=%s b=%s g=%s", count,
                 saw_a ? "y" : "n", saw_b ? "y" : "n", saw_g ? "y" : "n");

  // T20 — The loop above ended with ppdFindNextAttr() returning NULL and the
  //       cursor parked past the last element; a further call still returns
  //       NULL (no crash, no spurious match).
  testBegin("ppdFindNextAttr(ppd, \"TestRepeatAttr\", NULL) stays NULL past end");
  testEnd(ppdFindNextAttr(ppd, "TestRepeatAttr", NULL) == NULL);

  // T21 — For a single-instance attribute, ppdFindAttr() returns it and the
  //       immediately following ppdFindNextAttr() returns NULL (no second
  //       same-named record).
  testBegin("ppdFindNextAttr after single-instance \"ModelName\" returns NULL");
  attr = ppdFindAttr(ppd, "ModelName", NULL);
  testEnd(attr != NULL && ppdFindNextAttr(ppd, "ModelName", NULL) == NULL);


  // =========================================================================
  // Group 5: ppdNormalizeMakeAndModel() transformations (T22–T30)
  //
  // The cleaner rewrites a raw make-and-model string into a tidy one.  Each
  // case exercises one branch; expected outputs are computed directly from
  // the source logic in ppd-attr.c.
  // =========================================================================

  // T22 — A parenthesized string has the surrounding "(...)" stripped:
  //       "(My Printer)" → "My Printer".
  testBegin("ppdNormalizeMakeAndModel(\"(My Printer)\") -> \"My Printer\"");
  ret = ppdNormalizeMakeAndModel("(My Printer)", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "My Printer"), "got \"%s\"", buf);

  // T23 — Leading and trailing whitespace are trimmed: "  Trim Me  " →
  //       "Trim Me".
  testBegin("ppdNormalizeMakeAndModel(\"  Trim Me  \") -> \"Trim Me\"");
  ret = ppdNormalizeMakeAndModel("  Trim Me  ", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "Trim Me"), "got \"%s\"", buf);

  // T24 — "Hewlett-Packard " (16 chars) is collapsed to "HP ":
  //       "Hewlett-Packard LaserJet 4000" → "HP LaserJet 4000".
  testBegin("ppdNormalizeMakeAndModel(\"Hewlett-Packard LaserJet 4000\") -> \"HP LaserJet 4000\"");
  ret = ppdNormalizeMakeAndModel("Hewlett-Packard LaserJet 4000", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "HP LaserJet 4000"), "got \"%s\"", buf);

  // T25 — A model starting with "deskjet" (matched case-insensitively) is
  //       prefixed with "HP ": "DeskJet 3630" → "HP DeskJet 3630".
  testBegin("ppdNormalizeMakeAndModel(\"DeskJet 3630\") -> \"HP DeskJet 3630\"");
  ret = ppdNormalizeMakeAndModel("DeskJet 3630", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "HP DeskJet 3630"), "got \"%s\"", buf);

  // T26 — A make beginning "agfa" is upper-cased in place to "AGFA":
  //       "agfa Accuset 1000" → "AGFA Accuset 1000".
  testBegin("ppdNormalizeMakeAndModel(\"agfa Accuset 1000\") -> \"AGFA Accuset 1000\"");
  ret = ppdNormalizeMakeAndModel("agfa Accuset 1000", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "AGFA Accuset 1000"), "got \"%s\"", buf);

  // T27 — "XPrint " (note the trailing space guard against "Xprinter") is
  //       prefixed with "Xerox ": "XPrint 5000" → "Xerox XPrint 5000".
  testBegin("ppdNormalizeMakeAndModel(\"XPrint 5000\") -> \"Xerox XPrint 5000\"");
  ret = ppdNormalizeMakeAndModel("XPrint 5000", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "Xerox XPrint 5000"), "got \"%s\"", buf);

  // T28 — A string matching no rule is passed through unchanged:
  //       "Generic Foo Printer" → "Generic Foo Printer".
  testBegin("ppdNormalizeMakeAndModel(\"Generic Foo Printer\") passes through");
  ret = ppdNormalizeMakeAndModel("Generic Foo Printer", buf, sizeof(buf));
  testEndMessage(ret != NULL && !strcmp(buf, "Generic Foo Printer"), "got \"%s\"", buf);

  // T29 — On success the function returns the caller's buffer pointer (not a
  //       fresh allocation), so callers can use the return value directly.
  testBegin("ppdNormalizeMakeAndModel returns the caller's buffer on success");
  ret = ppdNormalizeMakeAndModel("Generic Foo Printer", buf, sizeof(buf));
  testEnd(ret == buf);

  // T30 — An input that reduces to an empty string (only whitespace) yields a
  //       NULL return (buffer[0] == '\0' → the final `buffer[0] ? ... : NULL`).
  testBegin("ppdNormalizeMakeAndModel(\"   \") returns NULL (empty result)");
  ret = ppdNormalizeMakeAndModel("   ", buf, sizeof(buf));
  testEnd(ret == NULL);


  // =========================================================================
  // Cleanup
  // =========================================================================

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
