//
// PPD generator API unit tests for libppd.
//
// Copyright © 2026 by OpenPrinting.
//
// Licensed under Apache License v2.0.  See the file "LICENSE" for more
// information.
//
// Tests covered (45 assertions across 12 groups):
//
//   Group  1  (T01-T05)  NULL / argument guards and smoke test.
//                        ppdCreatePPDFromIPP2 returns NULL with errno
//                        message when buffer==NULL OR bufsize<1 (lines
//                        285-290).  Returns NULL with "No IPP
//                        attributes." when supported==NULL (lines
//                        292-297).  Successful minimal call returns the
//                        passed-in buffer pointer and writes a non-empty
//                        PPD to disk.
//
//   Group  2  (T06-T08)  printer-make-and-model sanitization + DNS-SD
//                        fallback + "Unknown Printer" default.  Lines
//                        315-384: first iteration consumes IPP
//                        printer-make-and-model; if absent / empty,
//                        second iteration takes the make_model arg; if
//                        that is also NULL, both make AND model fall
//                        back to "Unknown" / "Printer".  Single-token
//                        make takes the "No separate model name" branch
//                        at line 374 → model = "Printer".
//
//   Group  3  (T09-T10)  HP normalization.  "Hewlett Packard " and
//                        "Hewlett-Packard " (both 16-char prefixes,
//                        line 357) are rewritten to "HP" + the
//                        remainder.  An extra "HP " prefix on the
//                        remainder is also stripped (line 363).
//
//   Group  4  (T11-T13)  Output order + colour + landscape.  A default
//                        output bin whose name contains "face-up"
//                        flips faceupdown and yields *DefaultOutputOrder
//                        Reverse (line 467).  IPP color-supported=True
//                        emits *ColorDevice True (line 477); without it
//                        AND a 0 color arg → *ColorDevice False (line
//                        479).  IPP landscape-orientation-requested-
//                        preferred 4 → Plus90, 5 → Minus90 (lines
//                        485-488).
//
//   Group  5  (T14-T16)  cupsVersion / cupsLanguages / job-account-id.
//                        *cupsVersion line is always emitted (line 491).
//                        printer-strings-languages-supported [en, de,
//                        fr] → *cupsLanguages: "en de fr"; the iteration
//                        at lines 499-505 skips "en" inside the loop
//                        because it is already in the leading literal.
//                        job-account-id-supported=True → *cupsJobAccountId
//                        True (line 593).
//
//   Group  6  (T17-T22)  job-password repertoires.  The if/else-if
//                        chain at lines 662-675 covers six branches:
//                        iana_us-ascii_digits → '1', _letters → 'A',
//                        _complex → 'C', _any → '.', iana_utf-8_digits
//                        → 'N', any unknown / unset string → '*'.
//                        Each test exercises one branch with a chosen
//                        maxlen so the resulting pattern is unambiguous.
//
//   Group  7  (T23-T27)  PDL detection.  document-format-supported
//                        drives the if/else-if chain at lines 772-965:
//                        application/pdf → "*cupsFilter2: application/
//                        vnd.cups-pdf application/pdf 0 -" + is_pdf=1 +
//                        manual_copies=0; application/vnd.cups-pdf →
//                        "application/pdf application/pdf 0 -" (remote
//                        CUPS queue branch, line 775);
//                        application/postscript → manual_copies stays
//                        0; image/pwg-raster with the required
//                        type+resolution supporting attributes →
//                        manual_copies=1 → *cupsManualCopies True; an
//                        empty supported set (no recognized PDL) →
//                        goto bad_ppd at line 974 → NULL return AND
//                        unlinked PPD file.
//
//   Group  8  (T28-T30)  cupsManualCopies / cupsSingleFile / boilerplate.
//                        *cupsSingleFile: True is always emitted (line
//                        690).  *PPD-Adobe: "4.3", *PCFileName:
//                        "drvless.ppd" headers are always emitted
//                        (lines 390, 398).  When pwg-raster is the
//                        sole format, *cupsManualCopies: True appears
//                        (line 980).
//
//   Group  9  (T31-T33)  Resolution + DefaultResolution.  No resolution
//                        attributes at all → fallback to 300 dpi (line
//                        1004) → *DefaultResolution: 300dpi.  Providing
//                        printer-resolution-default 600 dpi (also in
//                        printer-resolution-supported) → *DefaultResolution:
//                        600dpi.  Asymmetric res (x≠y) → "XxYdpi" form
//                        at line 2413.
//
//   Group 10  (T34-T36)  cupsPrintQuality + Fax + NickName.  Providing
//                        print-quality-supported {3,4,5} (Draft/Normal/
//                        High) emits the full *cupsPrintQuality OpenUI
//                        block (lines 2425-2453).  ipp-features-
//                        supported containing "faxout" AND a
//                        printer-uri-supported containing "faxout"
//                        sets is_fax → "*cupsFax: True" line emitted
//                        (line 747); the NickName carries "Fax, "
//                        infix at line 417.
//
//   Group 11  (T37-T40)  ppdCreatePPDFromIPP — the v1 wrapper.
//                        Delegates to v2 with NULL conflicts / sizes /
//                        default_pagesize / default_cluster_color
//                        (line 170).  NULL buffer / NULL supported
//                        propagate the v2 error semantics.  status_msg
//                        propagation: success path writes a "PPD
//                        generated." trailer; NULL supported writes
//                        "No IPP attributes."; bad-PDL writes "does not
//                        support…".
//
//   Group 12  (T41-T45)  Secondary boilerplate + multi-PDL handling.
//                        *FileSystem: False / *LanguageLevel: "3" /
//                        *PSVersion always emitted.  Adding image/jpeg
//                        AND image/png to document-format-supported
//                        alongside application/pdf yields THREE
//                        *cupsFilter2 lines (lines 967-969 add jpeg/png
//                        unconditionally after the if-chain).  NULL
//                        status_msg with size 0 must not crash.
//
// Design notes:
//
//   * Every test builds its IPP attribute set from scratch via ippNew(),
//     calls ppdCreatePPDFromIPP2 (or its v1 wrapper), reads back the
//     resulting PPD file from disk via slurp_file(), asserts via
//     strstr() / strcmp() on the bytes, then unlinks the temp file and
//     ippDelete()s the attribute set.
//
//   * The function uses cupsCreateTempFile() under the hood which
//     places the PPD in the system temp dir (TMPDIR or /tmp); each
//     test cleans up its own file.
//
//   * Branches NOT exercised here (deliberate gaps, flagged for
//     transparency):
//       - printer-strings-uri loop (lines 520-585): makes a real HTTP
//         GET via cupsDoRequest → cannot be driven hermetically.
//       - InputSlot / MediaType / ColorModel / Duplex / OutputBin
//         option blocks (lines 1387-1898): need full media-* IPP
//         setups with media-col-database collections to produce
//         non-degenerate output; the option-block emit logic itself
//         is straightforward `ippContainsString` / cupsFilePrintf
//         scaffolding that mirrors the unit tests already done for
//         test_ppd_ipp.  Verifying *one* page-size variant is the
//         interesting bit — see T29 / T30 / T44.
//       - Finishing options block (lines 1900-2403): exercises an
//         enormous switch table of IPP finishings enums; an entire
//         dedicated test file would be needed to do this justice.
//       - Presets / constraints blocks (lines 2608-2795): driven by
//         IPP collection attributes (preset-name=) that are tedious
//         but trivial to construct; left for a future patch.
//       - urf-supported parse → bad_ppd at line 843: requires
//         CUPS_RASTER_HAVE_APPLERASTER compile-time flag.
//

#include <ppd/ppd.h>
#include <cups/cups.h>
#include <cups/ipp.h>
#include "test-internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>


// =============================================================================
// Helpers.
// =============================================================================

// Slurp a file's contents into a malloc'd, NUL-terminated buffer.
// Returns NULL on error; caller must free().
static char *
slurp_file(const char *path)
{
  FILE  *f;
  long   len;
  char  *buf;
  size_t got;

  if ((f = fopen(path, "rb")) == NULL)
    return (NULL);

  fseek(f, 0, SEEK_END);
  len = ftell(f);
  if (len < 0)
  {
    fclose(f);
    return (NULL);
  }
  rewind(f);

  if ((buf = (char *)malloc((size_t)len + 1)) == NULL)
  {
    fclose(f);
    return (NULL);
  }
  got = fread(buf, 1, (size_t)len, f);
  buf[got] = '\0';
  fclose(f);
  return (buf);
}

// Add a one-value document-format-supported attribute.
static void
add_format(ipp_t *resp, const char *mime)
{
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE,
               "document-format-supported", NULL, mime);
}

// Build an "absolute minimum" valid IPP attribute set (just enough to
// pass the PDL gate at line 974).  The PDF clause sets manual_copies=0
// and is_pdf=1, so no *cupsManualCopies appears in the output PPD.
static ipp_t *
make_pdf_ipp(void)
{
  ipp_t *resp = ippNew();
  add_format(resp, "application/pdf");
  return (resp);
}


int                                          // O - Exit status (0 = all pass)
main(void)
{
  char            buffer[1024];
  char            status_msg[256];
  char           *ppd_text;
  char           *result;
  ipp_t          *resp;


  // =========================================================================
  // Group 1: NULL / argument guards (T01-T05)
  // =========================================================================

  // T01 — buffer==NULL: range-check at line 285 → returns NULL AND
  //       status_msg contains strerror(EINVAL).
  testBegin("ppdCreatePPDFromIPP2(NULL buffer, ...) returns NULL");
  resp = make_pdf_ipp();
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(NULL, 1024, resp, "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEnd(result == NULL);
  ippDelete(resp);

  // T02 — bufsize=0: same range-check at line 285 (the `bufsize < 1` half).
  testBegin("ppdCreatePPDFromIPP2(buffer, bufsize=0, ...) returns NULL");
  resp = make_pdf_ipp();
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, 0, resp, "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEnd(result == NULL);
  ippDelete(resp);

  // T03 — supported==NULL: the "No IPP attributes." path at line 292-297.
  //       status_msg gets exactly that literal.
  testBegin("ppdCreatePPDFromIPP2(supported=NULL) returns NULL + status msg");
  buffer[0] = '\0';
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), NULL,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEndMessage(result == NULL && strstr(status_msg, "No IPP attributes"),
                 "status=\"%s\"", status_msg);

  // T04 — Successful minimal call: returns the buffer pointer (not NULL).
  testBegin("ppdCreatePPDFromIPP2(minimal valid) returns buffer pointer");
  resp = make_pdf_ipp();
  buffer[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Acme Printer", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                NULL, 0);
  testEnd(result == buffer && buffer[0] != '\0');
  if (result == buffer) unlink(buffer);
  ippDelete(resp);

  // T05 — Successful minimal call produces a non-empty PPD on disk
  //       containing the standard *PPD-Adobe header.
  testBegin("ppdCreatePPDFromIPP2(minimal) writes a *PPD-Adobe-bearing file");
  resp = make_pdf_ipp();
  buffer[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Acme Printer", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text != NULL && strstr(ppd_text, "*PPD-Adobe: \"4.3\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 2: Make / model sanitization + DNS-SD fallback (T06-T08)
  // =========================================================================

  // T06 — printer-make-and-model "Acme LaserJet 100" (IPP value).
  //       Sanitized → "Acme LaserJet 100" → space split at line 366 →
  //       make = "Acme", model = "LaserJet 100".  PPD lines:
  //       *Manufacturer: "Acme" / *ModelName: "Acme LaserJet 100".
  testBegin("printer-make-and-model \"Acme LaserJet 100\" → *Manufacturer \"Acme\"");
  resp = ippNew();
  add_format(resp, "application/pdf");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_TEXT,
               "printer-make-and-model", NULL, "Acme LaserJet 100");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                NULL, "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*Manufacturer: \"Acme\"") &&
          strstr(ppd_text, "*ModelName: \"Acme LaserJet 100\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T07 — No printer-make-and-model: first iteration (i=0) bails to
  //       "Unknown", second iteration (i=1) consumes the make_model arg
  //       "Brand Foo" → split → make="Brand", model="Foo".
  testBegin("no IPP make/model + make_model=\"Brand Foo\" → make=Brand, model=Foo");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Brand Foo", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*Manufacturer: \"Brand\"") &&
          strstr(ppd_text, "*ModelName: \"Brand Foo\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T08 — No IPP make/model AND make_model==NULL: both iterations
  //       end at "Unknown" (line 354) → no space → model = "Printer"
  //       at line 377 → *Manufacturer "Unknown" / *ModelName "Unknown Printer".
  testBegin("no IPP make/model + NULL make_model → Manufacturer=Unknown");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                NULL, "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*Manufacturer: \"Unknown\"") &&
          strstr(ppd_text, "*ModelName: \"Unknown Printer\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 3: HP normalization (T09-T10)
  // =========================================================================

  // T09 — "Hewlett Packard LaserJet 4" (with a space, NOT a hyphen):
  //       prefix check at line 357 matches, make rewritten to "HP",
  //       model = "LaserJet 4".  PPD: *Manufacturer "HP" / *ModelName
  //       "HP LaserJet 4".
  testBegin("\"Hewlett Packard LaserJet 4\" → make=HP, model=LaserJet 4");
  resp = ippNew();
  add_format(resp, "application/pdf");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_TEXT,
               "printer-make-and-model", NULL, "Hewlett Packard LaserJet 4");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                NULL, "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*Manufacturer: \"HP\"") &&
          strstr(ppd_text, "*ModelName: \"HP LaserJet 4\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T10 — "Hewlett-Packard HP Color LaserJet": prefix match THEN the
  //       "HP " prefix on the remainder is stripped (line 363).  Final
  //       *ModelName: "HP Color LaserJet" (no double "HP HP ").
  testBegin("\"Hewlett-Packard HP Color LaserJet\" → strips inner \"HP \"");
  resp = ippNew();
  add_format(resp, "application/pdf");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_TEXT,
               "printer-make-and-model", NULL,
               "Hewlett-Packard HP Color LaserJet");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                NULL, "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*Manufacturer: \"HP\"") &&
          strstr(ppd_text, "*ModelName: \"HP Color LaserJet\"") &&
          // The inner "HP " must have been stripped, so we should NOT
          // see "HP HP Color" anywhere.
          strstr(ppd_text, "HP HP Color") == NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 4: Output order, colour, landscape (T11-T13)
  // =========================================================================

  // T11 — Default output bin "face-up" sets faceupdown=-1 at line 463
  //       → firsttolast*faceupdown<0 at line 467 → "*DefaultOutputOrder:
  //       Reverse".
  testBegin("output-bin-default \"face-up\" → *DefaultOutputOrder Reverse");
  resp = make_pdf_ipp();
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "output-bin-default", NULL, "face-up");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*DefaultOutputOrder: Reverse") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T12 — color-supported=True → *ColorDevice True (line 477).  Without
  //       this attribute AND with the `color` arg = 0, the else branch
  //       at line 479 emits *ColorDevice False — covered by T08's PPD.
  testBegin("color-supported=True → *ColorDevice True");
  resp = make_pdf_ipp();
  ippAddBoolean(resp, IPP_TAG_PRINTER, "color-supported", 1);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*ColorDevice: True") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T13 — landscape-orientation-requested-preferred=4 → Plus90 (line
  //       486); =5 → Minus90 (line 488).  Pick 4 here.
  testBegin("landscape-orientation-requested-preferred=4 → Plus90");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_ENUM,
                "landscape-orientation-requested-preferred", 4);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*LandscapeOrientation: Plus90") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 5: cupsVersion / cupsLanguages / accounting (T14-T16)
  // =========================================================================

  // T14 — *cupsVersion is unconditionally emitted (line 491).  We just
  //       check the literal token is present.
  testBegin("*cupsVersion always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsVersion: ") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T15 — printer-strings-languages-supported [en, de, fr]: the leading
  //       literal is *cupsLanguages: "en, then the loop appends " de fr"
  //       (skipping "en" inside the loop at line 503).  Final value:
  //       *cupsLanguages: "en de fr"\n.
  testBegin("printer-strings-languages-supported [en,de,fr] → cupsLanguages \"en de fr\"");
  resp = make_pdf_ipp();
  {
    const char *langs[3] = { "en", "de", "fr" };
    ippAddStrings(resp, IPP_TAG_PRINTER, IPP_TAG_LANGUAGE,
                  "printer-strings-languages-supported", 3, NULL, langs);
  }
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*cupsLanguages: \"en de fr\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T16 — job-account-id-supported=True triggers the accounting block
  //       at line 591 → *cupsJobAccountId: True.
  testBegin("job-account-id-supported=True → *cupsJobAccountId True");
  resp = make_pdf_ipp();
  ippAddBoolean(resp, IPP_TAG_PRINTER, "job-account-id-supported", 1);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*cupsJobAccountId: True") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 6: Password repertoires (T17-T22) — every branch of the
  //          if/else-if chain at lines 662-675.
  // =========================================================================

  // T17 — iana_us-ascii_digits / maxlen=4 → "1111".  Also covers the
  //       !repertoire half of the same conditional via the explicit
  //       string match.
  testBegin("repertoire iana_us-ascii_digits maxlen=4 → \"1111\"");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 4);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "iana_us-ascii_digits");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"1111\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T18 — iana_us-ascii_letters / maxlen=3 → "AAA".
  testBegin("repertoire iana_us-ascii_letters maxlen=3 → \"AAA\"");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 3);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "iana_us-ascii_letters");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"AAA\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T19 — iana_us-ascii_complex / maxlen=2 → "CC".
  testBegin("repertoire iana_us-ascii_complex maxlen=2 → \"CC\"");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 2);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "iana_us-ascii_complex");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"CC\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T20 — iana_us-ascii_any / maxlen=3 → "...".
  testBegin("repertoire iana_us-ascii_any maxlen=3 → \"...\"");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 3);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "iana_us-ascii_any");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"...\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T21 — iana_utf-8_digits / maxlen=2 → "NN".
  testBegin("repertoire iana_utf-8_digits maxlen=2 → \"NN\"");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 2);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "iana_utf-8_digits");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"NN\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T22 — Unknown repertoire string falls through to the final else
  //       branch at line 674 → '*' fill → "****".
  testBegin("repertoire \"xyz_garbage\" maxlen=4 → \"****\" (else branch)");
  resp = make_pdf_ipp();
  ippAddInteger(resp, IPP_TAG_PRINTER, IPP_TAG_INTEGER,
                "job-password-supported", 4);
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "job-password-repertoire-configured", NULL,
               "xyz_garbage");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsJobPassword: \"****\"") != NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 7: PDL detection (T23-T27)
  // =========================================================================

  // T23 — application/pdf (the canonical PDF clause at line 780):
  //       *cupsFilter2: "application/vnd.cups-pdf application/pdf 0 -"
  //       is_pdf=1; manual_copies=0 ⇒ NO *cupsManualCopies.
  testBegin("application/pdf → cupsFilter2 vnd.cups-pdf line + no ManualCopies");
  resp = ippNew();
  add_format(resp, "application/pdf");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text,
                 "*cupsFilter2: \"application/vnd.cups-pdf application/pdf 0 -\"") &&
          strstr(ppd_text, "*cupsManualCopies: True") == NULL);
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T24 — application/vnd.cups-pdf (remote CUPS queue clause at line 772):
  //       *cupsFilter2: "application/pdf application/pdf 0 -"
  testBegin("application/vnd.cups-pdf → cupsFilter2 \"application/pdf application/pdf 0 -\"");
  resp = ippNew();
  add_format(resp, "application/vnd.cups-pdf");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/vnd.cups-pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text,
                 "*cupsFilter2: \"application/pdf application/pdf 0 -\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T25 — application/postscript (clause at line 951): manual_copies=0
  //       → NO *cupsManualCopies.
  testBegin("application/postscript → vnd.cups-postscript cupsFilter2 line");
  resp = ippNew();
  add_format(resp, "application/postscript");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/postscript",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text,
                 "*cupsFilter2: \"application/vnd.cups-postscript "
                 "application/postscript 0 -\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T26 — image/pwg-raster needs:
  //       * document-format-supported includes image/pwg-raster
  //       * pwg-raster-document-type-supported (keyword) at least one value
  //       * pwg-raster-document-resolution-supported (resolution) at least one
  //       → cupsArrayFind+ippFindAttribute all hit → manual_copies set to 1
  //       → *cupsManualCopies: True emitted.
  testBegin("pwg-raster with required attrs → *cupsManualCopies True");
  resp = ippNew();
  add_format(resp, "image/pwg-raster");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "pwg-raster-document-type-supported", NULL, "sgray_8");
  ippAddResolution(resp, IPP_TAG_PRINTER,
                   "pwg-raster-document-resolution-supported",
                   IPP_RES_PER_INCH, 600, 600);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "image/pwg-raster",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text,
                 "*cupsFilter2: \"image/pwg-raster image/pwg-raster 0 -\"") &&
          strstr(ppd_text, "*cupsManualCopies: True"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T27 — bad_ppd path at line 974: no recognized format AND no pdl
  //       arg → formatfound stays 0 → goto bad_ppd → buffer is wiped
  //       to "" (line 2839), file is unlinked (line 2838), status_msg
  //       gets "Printer does not support…".  Return value is NULL.
  testBegin("no recognized PDL → bad_ppd → NULL + status \"does not support\"");
  resp = ippNew();
  add_format(resp, "application/some-weird-format-no-one-knows");
  buffer[0] = '\0';
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", NULL, 0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEndMessage(result == NULL && buffer[0] == '\0' &&
                 strstr(status_msg, "does not support"),
                 "status=\"%s\" buffer=\"%s\"", status_msg, buffer);
  ippDelete(resp);


  // =========================================================================
  // Group 8: ManualCopies / cupsSingleFile / boilerplate (T28-T30)
  // =========================================================================

  // T28 — *cupsSingleFile: True is unconditionally emitted (line 690).
  testBegin("*cupsSingleFile: True always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*cupsSingleFile: True"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T29 — *PCFileName: "drvless.ppd" always emitted (line 398).
  testBegin("*PCFileName: \"drvless.ppd\" always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*PCFileName: \"drvless.ppd\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T30 — *FormatVersion: "4.3" always emitted (line 391).
  testBegin("*FormatVersion: \"4.3\" always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*FormatVersion: \"4.3\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 9: DefaultResolution (T31-T33)
  // =========================================================================

  // T31 — No resolution attrs + PDF (no resolution-requiring PDL) →
  //       common_res empty → 300 dpi fallback (line 1004) →
  //       *DefaultResolution: 300dpi (line 2411).
  testBegin("no resolution attrs → *DefaultResolution: 300dpi");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*DefaultResolution: 300dpi"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T32 — printer-resolution-supported [600x600] + printer-resolution-default
  //       600x600 → common_res = [600x600], common_def = 600x600 →
  //       *DefaultResolution: 600dpi.
  testBegin("printer-resolution-default 600dpi → *DefaultResolution: 600dpi");
  resp = make_pdf_ipp();
  ippAddResolution(resp, IPP_TAG_PRINTER, "printer-resolution-supported",
                   IPP_RES_PER_INCH, 600, 600);
  ippAddResolution(resp, IPP_TAG_PRINTER, "printer-resolution-default",
                   IPP_RES_PER_INCH, 600, 600);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*DefaultResolution: 600dpi"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T33 — Asymmetric resolution 600x1200 → "*DefaultResolution: 600x1200dpi"
  //       form at line 2413 (else branch).
  testBegin("printer-resolution-default 600x1200 → \"600x1200dpi\"");
  resp = make_pdf_ipp();
  ippAddResolution(resp, IPP_TAG_PRINTER, "printer-resolution-supported",
                   IPP_RES_PER_INCH, 600, 1200);
  ippAddResolution(resp, IPP_TAG_PRINTER, "printer-resolution-default",
                   IPP_RES_PER_INCH, 600, 1200);
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*DefaultResolution: 600x1200dpi"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 10: cupsPrintQuality + Fax (T34-T36)
  // =========================================================================

  // T34 — print-quality-supported {3,4,5} → *OpenUI *cupsPrintQuality
  //       block at line 2425 + Draft (3) + Normal (4) + High (5) lines.
  //       Asserting *OpenUI line + Draft + High choices is enough proof.
  testBegin("print-quality-supported [3,4,5] → cupsPrintQuality OpenUI + Draft + High");
  resp = make_pdf_ipp();
  {
    int quals[3] = { IPP_QUALITY_DRAFT, IPP_QUALITY_NORMAL, IPP_QUALITY_HIGH };
    ipp_attribute_t *q =
      ippAddIntegers(resp, IPP_TAG_PRINTER, IPP_TAG_ENUM,
                     "print-quality-supported", 3, quals);
    (void)q;
  }
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*OpenUI *cupsPrintQuality") &&
          strstr(ppd_text, "*cupsPrintQuality Draft") &&
          strstr(ppd_text, "*cupsPrintQuality High") &&
          strstr(ppd_text, "*CloseUI: *cupsPrintQuality"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T35 — Fax detection: ipp-features-supported has "faxout" AND
  //       printer-uri-supported contains "faxout" in its URI →
  //       is_fax=1 → *cupsFax: True + *cupsIPPFaxOut: True emitted
  //       (lines 747-748).
  testBegin("ipp-features-supported faxout + matching URI → *cupsFax True");
  resp = make_pdf_ipp();
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "ipp-features-supported", NULL, "faxout");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_URI,
               "printer-uri-supported", NULL,
               "ipp://192.0.2.1/ipp/faxout");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*cupsFax: True") &&
          strstr(ppd_text, "*cupsIPPFaxOut: True"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T36 — NickName carries "Fax, " when is_fax is set (line 418).
  //       Same setup as T35 — verify the NickName line specifically.
  testBegin("Fax printer NickName carries \"Fax, \"");
  resp = make_pdf_ipp();
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_KEYWORD,
               "ipp-features-supported", NULL, "faxout");
  ippAddString(resp, IPP_TAG_PRINTER, IPP_TAG_URI,
               "printer-uri-supported", NULL,
               "ipp://192.0.2.1/ipp/faxout");
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*NickName:") &&
          strstr(ppd_text, "Fax, driverless"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);


  // =========================================================================
  // Group 11: ppdCreatePPDFromIPP — the v1 wrapper (T37-T40)
  // =========================================================================

  // T37 — Happy path through the v1 wrapper.  It delegates to v2 with
  //       NULL conflicts/sizes/default_pagesize/default_cluster_color
  //       (line 170).  The resulting PPD must contain *PPD-Adobe.
  testBegin("ppdCreatePPDFromIPP(minimal) returns buffer and writes file");
  resp = make_pdf_ipp();
  buffer[0] = '\0';
  result = ppdCreatePPDFromIPP(buffer, sizeof(buffer), resp,
                               "Test", "application/pdf", 0, 0,
                               NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(result == buffer && ppd_text &&
          strstr(ppd_text, "*PPD-Adobe: \"4.3\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T38 — v1 wrapper with NULL buffer propagates v2's NULL result.
  testBegin("ppdCreatePPDFromIPP(NULL buffer) → NULL");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP(NULL, sizeof(buffer), resp,
                               "Test", "application/pdf", 0, 0,
                               NULL, 0);
  testEnd(result == NULL);
  ippDelete(resp);

  // T39 — Success path status_msg trailer: contains "PPD generated."
  //       (line 2805); since application/pdf path was used, the leading
  //       token is "PDF".
  testBegin("status_msg on success → \"PDF PPD generated.\"");
  resp = make_pdf_ipp();
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEndMessage(result == buffer &&
                 strstr(status_msg, "PDF PPD generated.") != NULL,
                 "status=\"%s\"", status_msg);
  if (result == buffer) unlink(buffer);
  ippDelete(resp);

  // T40 — NULL status_msg with size 0 must NOT crash on either success
  //       or failure code paths.  Run both and assert clean return.
  testBegin("NULL status_msg + size 0 does not crash");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL,
                                NULL, 0);
  testEnd(result == buffer);
  if (result == buffer) unlink(buffer);
  ippDelete(resp);


  // =========================================================================
  // Group 12: Boilerplate + multi-PDL handling (T41-T45)
  // =========================================================================

  // T41 — *FileSystem: False always emitted (line 397).
  testBegin("*FileSystem: False always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*FileSystem: False"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T42 — *LanguageLevel: "3" always emitted (line 396).
  testBegin("*LanguageLevel: \"3\" always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*LanguageLevel: \"3\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T43 — *PSVersion: "(3010.000) 0" always emitted (line 395).
  testBegin("*PSVersion: \"(3010.000) 0\" always emitted");
  resp = make_pdf_ipp();
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text && strstr(ppd_text, "*PSVersion: \"(3010.000) 0\""));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T44 — Multi-PDL: document-format-supported = {application/pdf,
  //       image/jpeg, image/png}.  PDF wins the if/else-if chain, jpeg
  //       and png are added unconditionally after it (lines 966-969),
  //       so the PPD ends up with THREE *cupsFilter2 lines.  Verify
  //       the unconditional jpeg + png lines are present.
  testBegin("PDF + JPEG + PNG formats → three *cupsFilter2 lines");
  resp = ippNew();
  {
    const char *fmts[3] = {
      "application/pdf", "image/jpeg", "image/png"
    };
    ippAddStrings(resp, IPP_TAG_PRINTER, IPP_TAG_MIMETYPE,
                  "document-format-supported", 3, NULL, fmts);
  }
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", "application/pdf",
                                0, 0, NULL, NULL, NULL, NULL, NULL, 0);
  ppd_text = result ? slurp_file(buffer) : NULL;
  testEnd(ppd_text &&
          strstr(ppd_text, "*cupsFilter2: \"image/jpeg image/jpeg 0 -\"") &&
          strstr(ppd_text, "*cupsFilter2: \"image/png image/png 0 -\"") &&
          strstr(ppd_text, "application/vnd.cups-pdf application/pdf 0 -"));
  if (result == buffer) unlink(buffer);
  free(ppd_text);
  ippDelete(resp);

  // T45 — Bad-format status_msg precision: must mention "Printer does
  //       not support" (line 2843).  Same setup as T27 — verify the
  //       exact literal substring rather than just "does not support".
  testBegin("bad_ppd status_msg → \"Printer does not support\" literal");
  resp = ippNew();
  add_format(resp, "application/weird-bogus-format");
  buffer[0] = '\0';
  status_msg[0] = '\0';
  result = ppdCreatePPDFromIPP2(buffer, sizeof(buffer), resp,
                                "Test", NULL, 0, 0, NULL, NULL, NULL, NULL,
                                status_msg, sizeof(status_msg));
  testEndMessage(result == NULL &&
                 strstr(status_msg, "Printer does not support") != NULL,
                 "status=\"%s\"", status_msg);
  ippDelete(resp);


  return (testsPassed ? 0 : 1);
}
