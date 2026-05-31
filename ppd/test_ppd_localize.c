//
// PPD localization API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered:
//   ppdLocalizeAttr()      - keyword/spec lookup with locale fallback
//   ppdLocalizeIPPReason() - IPP reason text/URI extraction with guard checks
//
// Design: The PPD is constructed entirely in memory via tmpfile() + ppdOpen()
// so this binary is fully self-contained and imposes no file-system
// requirements on the build or CI environment.
//
// No locale-specific attributes are embedded in the test PPD, which means
// every lookup always falls through to the unlocalized fallback.  This makes
// all 16 assertions deterministic regardless of the test machine's locale.
//

#include <ppd/ppd.h>
#include "test-internal.h"
#include <stdio.h>
#include <string.h>


//
// Minimal self-contained PPD content.
//
// Mandatory PPD-Adobe header fields ensure ppdOpen() always returns a valid
// ppd_file_t *.  Two "cupsTest" attributes exercise ppdLocalizeAttr(), and
// one "cupsIPPReason" attribute (with a plain HTTP URI value) exercises
// ppdLocalizeIPPReason().
//
// Intentionally omitted: any locale-prefixed entries such as
// "fr.cupsIPPReason" or "en_US.cupsTest".  Their absence guarantees that
// every call falls back to ppdFindAttr() and produces locale-independent
// results on every CI runner.
//

static const char test_ppd_text[] =
  "*PPD-Adobe: \"4.3\"\n"
  "*FormatVersion: \"4.3\"\n"
  "*FileVersion: \"1.0\"\n"
  "*LanguageVersion: English\n"
  "*LanguageEncoding: ISOLatin1\n"
  "*PCFileName: \"LOCTEST.PPD\"\n"
  "*Product: \"(Test)\"\n"
  "*ModelName: \"Localize Test Printer\"\n"
  "*ShortNickName: \"LocTest\"\n"
  "*NickName: \"Localize Test Printer\"\n"
  "*PSVersion: \"(3010.000) 0\"\n"
  "*LanguageLevel: \"3\"\n"
  "*ColorDevice: False\n"
  "*DefaultColorSpace: Gray\n"
  "*FileSystem: False\n"
  "*Throughput: \"1\"\n"
  "*LandscapeOrientation: Plus90\n"
  "*TTRasterizer: Type42\n"
  "*DefaultResolution: 600dpi\n"
  "*% --- Attributes for ppdLocalizeAttr() tests ---\n"
  "*cupsTest Foo/I Love Foo: \"\"\n"
  "*cupsTest Bar/I Love Bar: \"\"\n"
  "*% --- Attribute for ppdLocalizeIPPReason() tests ---\n"
  "*cupsIPPReason foo/Foo Reason: \"http://foo/bar.html\"\n";


//
// 'main()' - Run all localization unit tests.
//

int					// O - Exit status (0 = all pass)
main(void)
{
  ppd_file_t   *ppd;			// PPD file handle
  ppd_attr_t   *attr;			// Returned by ppdLocalizeAttr()
  char          buf[PPD_MAX_TEXT];	// Output buffer for ppdLocalizeIPPReason()
  const char   *val;			// Return value from ppdLocalizeIPPReason()
  FILE         *f;			// Temporary FILE for in-memory PPD
  ppd_status_t  err;			// PPD parse error code
  int           line;			// Line number of any parse error


  //
  // --- Setup: open the embedded PPD from a tmpfile ---
  //
  // tmpfile() creates an anonymous, auto-deleted temporary file.
  // We write the PPD text, rewind to the beginning, and hand the FILE*
  // directly to ppdOpen().  The FILE is closed immediately afterwards;
  // libppd has already consumed it by then.
  //

  testBegin("ppdOpen(embedded test PPD)");

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


  //
  // --- ppdLocalizeAttr() tests ---
  //
  // ppdLocalizeAttr(ppd, keyword, spec) first searches for a locale-prefixed
  // attribute (e.g. "en_US.cupsTest Foo").  When that is absent — as it always
  // is in our minimal test PPD — it falls back to ppdFindAttr(ppd, keyword,
  // spec).  All four results below are therefore locale-independent.
  //

  // 1. A known keyword + spec combination returns the matching attribute.
  testBegin("ppdLocalizeAttr(cupsTest, Foo) returns non-NULL");
  attr = ppdLocalizeAttr(ppd, "cupsTest", "Foo");
  testEnd(attr != NULL);

  // 2. The returned attribute carries the correct spec field.
  testBegin("ppdLocalizeAttr(cupsTest, Foo) spec == \"Foo\"");
  testEnd(attr != NULL && !strcmp(attr->spec, "Foo"));

  // 3. The text field holds the human-readable string from the PPD "/..." slot.
  testBegin("ppdLocalizeAttr(cupsTest, Foo) text == \"I Love Foo\"");
  testEnd(attr != NULL && !strcmp(attr->text, "I Love Foo"));

  // 4. A second distinct spec ("Bar") also resolves correctly.
  testBegin("ppdLocalizeAttr(cupsTest, Bar) returns attr with spec Bar");
  attr = ppdLocalizeAttr(ppd, "cupsTest", "Bar");
  testEndMessage(attr != NULL && !strcmp(attr->spec, "Bar"),
                 "spec=\"%s\"", attr ? attr->spec : "(null)");

  // 5. A spec that does not exist in the PPD returns NULL (clean miss).
  testBegin("ppdLocalizeAttr(cupsTest, NoSuch) returns NULL");
  attr = ppdLocalizeAttr(ppd, "cupsTest", "NoSuch");
  testEnd(attr == NULL);

  // 6. A keyword that is entirely absent from the PPD also returns NULL.
  testBegin("ppdLocalizeAttr(nonExistentKeyword, NULL) returns NULL");
  attr = ppdLocalizeAttr(ppd, "nonExistentKeyword", NULL);
  testEnd(attr == NULL);


  //
  // --- ppdLocalizeIPPReason() guard tests ---
  //
  // The function's prologue rejects malformed arguments before touching any
  // PPD data.  Each test below triggers exactly one rejection branch.
  //

  // 7. NULL ppd pointer must be rejected.
  testBegin("ppdLocalizeIPPReason(NULL ppd) returns NULL");
  val = ppdLocalizeIPPReason(NULL, "foo", NULL, buf, PPD_MAX_TEXT);
  testEnd(val == NULL);

  // 8. NULL reason keyword must be rejected.
  testBegin("ppdLocalizeIPPReason(NULL reason) returns NULL");
  val = ppdLocalizeIPPReason(ppd, NULL, NULL, buf, PPD_MAX_TEXT);
  testEnd(val == NULL);

  // 9. NULL output buffer must be rejected.
  testBegin("ppdLocalizeIPPReason(NULL buffer) returns NULL");
  val = ppdLocalizeIPPReason(ppd, "foo", NULL, NULL, PPD_MAX_TEXT);
  testEnd(val == NULL);

  // 10. A buffer smaller than PPD_MAX_TEXT must be rejected.
  //     The spec requires bufsize >= PPD_MAX_TEXT to guarantee space for
  //     the longest possible localized text.
  testBegin("ppdLocalizeIPPReason(bufsize < PPD_MAX_TEXT) returns NULL");
  val = ppdLocalizeIPPReason(ppd, "foo", NULL, buf, (size_t)(PPD_MAX_TEXT - 1));
  testEnd(val == NULL);

  // 11. A non-NULL but empty scheme string ("") must be rejected.
  //     The guard is: scheme && !*scheme → return NULL.
  testBegin("ppdLocalizeIPPReason(empty-string scheme) returns NULL");
  val = ppdLocalizeIPPReason(ppd, "foo", "", buf, PPD_MAX_TEXT);
  testEnd(val == NULL);


  //
  // --- ppdLocalizeIPPReason() functional tests ---
  //
  // Our test PPD defines exactly one cupsIPPReason entry:
  //
  //   *cupsIPPReason foo/Foo Reason: "http://foo/bar.html"
  //
  // Because there are no locale-prefixed variants, the same unlocalized
  // attribute is returned on every machine regardless of LC_ALL / LANG.
  // Results are therefore byte-for-byte identical on every CI runner.
  //

  // 12. scheme=NULL (text mode): returns the human-readable text field.
  //     Implementation: strlcpy(buffer, locattr->text, ...) → "Foo Reason".
  //     The value "http://foo/bar.html" contains no "text:" URI, so the
  //     initial strlcpy result is returned unchanged.
  testBegin("ppdLocalizeIPPReason(foo, NULL) == \"Foo Reason\"");
  val = ppdLocalizeIPPReason(ppd, "foo", NULL, buf, PPD_MAX_TEXT);
  testEndMessage(val != NULL && !strcmp(val, "Foo Reason"),
                 "got \"%s\"", val ? val : "(null)");

  // 13. scheme="text" is the explicit form of scheme=NULL — same code path.
  testBegin("ppdLocalizeIPPReason(foo, \"text\") == \"Foo Reason\"");
  val = ppdLocalizeIPPReason(ppd, "foo", "text", buf, PPD_MAX_TEXT);
  testEndMessage(val != NULL && !strcmp(val, "Foo Reason"),
                 "got \"%s\"", val ? val : "(null)");

  // 14. scheme="http" extracts the HTTP URI embedded in the attribute value.
  //     The value starts with "http://foo/bar.html", which matches immediately.
  testBegin("ppdLocalizeIPPReason(foo, \"http\") starts with \"http://\"");
  val = ppdLocalizeIPPReason(ppd, "foo", "http", buf, PPD_MAX_TEXT);
  testEndMessage(val != NULL && !strncmp(val, "http://", 7),
                 "got \"%s\"", val ? val : "(null)");

  // 15. A URI scheme absent from the attribute value returns NULL.
  //     Our value has only "http://"; no "ftp:" token exists.
  testBegin("ppdLocalizeIPPReason(foo, \"ftp\") returns NULL");
  val = ppdLocalizeIPPReason(ppd, "foo", "ftp", buf, PPD_MAX_TEXT);
  testEnd(val == NULL);

  // 16. A reason keyword with no matching cupsIPPReason attribute and no
  //     standard printer-state-reasons entry in the locale catalog returns
  //     NULL.  The fabricated reason string matches nothing anywhere.
  testBegin("ppdLocalizeIPPReason(totally-unknown-reason) returns NULL");
  val = ppdLocalizeIPPReason(ppd, "totally-unknown-xyz-reason-gsoc2026",
                             NULL, buf, PPD_MAX_TEXT);
  testEnd(val == NULL);


  //
  // --- Cleanup ---
  //

  ppdClose(ppd);

  return (testsPassed ? 0 : 1);
}
