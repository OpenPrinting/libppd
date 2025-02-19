# CHANGES - libppd v2.1.1 - 2025-02-19

## CHANGES IN V2.1.1 (19th February 2025)

- pdftops: Use Poppler for a few old Epson laser printers
  This works around documents being printed off-centre, shifted
  towards the top right. Affected are printers using epsoneplijs:
  EPL-5700L, EPL-5800L, EPL-5900L, EPL-6100L, EPL-6200L (Pull request
  #53).

- Fixed bugs discovered by static analyzer OpenScanHub
  Possible buffer overflows, uninitialized memory, format string
  issues and resource leaks, ... (Pull request #54)

- Fix crash bugs in `ppdLoadAttributes()`
  When parsing the "*cupsFilter(2): ..." lines in the PPD file use
  `memmove()` instead of `strcpy()` as the latter does not support
  handling overlapping memory portions and do not move running pointer
  beyond the end of the input string (Pull request #51).


## CHANGES IN V2.1.0 (17th October 2024)

- Prevent PPD generation based on invalid IPP response
  Overtaken from CUPS 2.x: Validate IPP attributes in PPD generator,
  refactor make-and-model code, PPDize preset and template names,
  quote PPD localized strings. Fixes CVE-2024-47175.


## CHANGES IN V2.1b1 (14th August 2024)

- Added support for libcups3 (libcups of CUPS 3.x)
  With these changes libcupsfilters can be built either with libcups2
  (libcups of CUPS 2.x) or libcups3 (libcups of CUPS 3.x). Pull
  request #27.

- Prefer PDF again in PPDs for driverless printers
  PDF works better with finishing, especially combinations of multiple
  copies, collation, and stapling/binding (Issue #42, Pull request
  #44).

- Use 0.5mm as tolerance when comparing page sizes
  For the PWG two page sizes are considered the same when the
  dimensions differ no more than 0.5 mm, libppd used too tight
  tolerances (Issue #29, Pull request #46).

- PPD generator: Check for required attributes when choosing input format
  Check for PCLm and PWG the minimum of attributes which we require
  during PPD generation. (Pull request #39, #40, [Fedora issue](https://bugzilla.redhat.com/show_bug.cgi?id=2263053)).

- `ppdLoadAttributes()`: Improve check whether parameters are integer
  (Pull request #38)

- `ppdLoadAttributes()`: Fix crash when page size could not get determined
  (Issue #31, Pull request #37)

- Fix crash if there is no page size for "Custom"
  (Pull request #35, [CUPS issue #849](https://github.com/OpenPrinting/cups/issues/849))

- Fix crash when incoming `*ptr` is NULL
  (Pull request #28, same as OpenPrinting/cups#831)

- libcups2 compatibility: Use proper CUPS array callback function types
  Fixed CUPS array function call in libcups2 compatibility layer (Pull
  request #33)

- Build system: Fix failure to correctly link to zlib
  Look up zlib properly with pkg-config (Pull request #32)

- Convert `INSTALL` to `INSTALL.md`
  (Pull request #34)


## CHANGES IN V2.0.0 (22th September 2023)

- `ppd_scan_ps()`: Fix CVE-2023-4504
  Added check for end of buffer/string when reading escaped character
  after backslash, return NULL (invalid string) if no character
  follows.

- Promoted the static function "ppd_decode()" of ppd/ppd.c into the
  API function "ppdDecode()".

- `ppdEmitJCLPDF()`: Decode "JCLToPDFInterpreter" value in ppdEmitJCLPDF()
  Fixes "classic" (non-driverless) PDF printing (Issue #24).

- `ppdLoadAttributes()`: Apply `cfIEEE1284NormalizeMakeModel()` to NickName
  Make and model for the printer IPP attributes are extracted from the
  PPD's NickName, which sometimes misses the manufacturer's
  name. Extract it from the PPD's Manufacturer field or derive it from
  the model name if possible. Enhanced alternative for pull request
  #21.

- `Makefile.am`: Fix disabling `testppdfile`
  Missing conditionals made the binary built when disabled (Pull
  request #18).


## CHANGES IN V2.0rc2 (20th June 2023)

- `ppdFilterPSToPS()`: Fixed reverse output order.
  When converting the former `pstops` CUPS filter into the filter
  function, some function calls got wrongly replaced by new ones,
  resulting in no output at all when the input should be re-arranged
  into reverse order. This broke printing with all PostScript printers
  (and proprietary CUPS drivers needing PostScript as input) which do
  reverse-order by default (Issue #20, Ubuntu bug #2022943).

- Fixed resolution handling when converting PPDs to printer IPP
  attributes
  For PWG/Apple Raster or PCLm output resolutions in job options or
  pseudo-PostScript code in the PPD get ignored and instead, the
  lowest resolution of the description of the Raster format used in
  the PPD file gets always used, which reduced output quality
  (Ubuntu bug #2022929).

- All PPD files with "MirrorPrint" option cuased mirrored printout
  If a PPD contains an option "MirrorPrint", the `ppdFilterLoadPPD()`
  sent the option `mirror=true` to the filter functions, regardless of
  the actual setting of "MirrorPrint" (which is usually "False" by
  default), making all jobs coming out with mirrored pages (Ubuntu bug
  #2018538).

- PPD file generator: Put `*cupsSingleFile: True` into generated PPD
  as some driverless IPP printers do not support multi-file jobs (CUPS
  issue #643).

- Add CUPS PPD attributes `*cupsLanguages: ...` and `*cupsStringsURI
  ...` to generated PPDs so that CUPS loads printer-specific option
  names and translations from the printer and uses them without need
  of static translations in the PPD file.

- CUPS renames the PPD option choice name "Custom" to "_Custom" when a
  fixed choice is named as such, to distinguish from CUPS' facility
  for custom option values. We do now the same when loading PPD files.

- Prevent duplicate PPD->IPP media-type name mappings, now we do not
  have dropping of some media types in the Printer Applications any
  more.

- When not specifying a media source and the page size is small
  (5x7" or smaller) do not request the photo tray but `auto` instead.

- Do not override color settings from print dialog ("ColorModel") with
  `print-color-mode` setting.

- Make `ppdFilterPSToPS()` recognize `%%PageRequirements:` DSC
  comment.

- Correctly display "Xprinter" instead of Xerox for Xprinter devices

- Fix the `job-pages-per-set` value (used to apply finishings correctly)
  for duplex and N-up printing.

- `ppdFilterLoadPPD()`: Actually create sample Raster header also for
  Apple/PWG Raster

- Make the `testppd` build test program also work if it is started
  from an environment with non-English locale.

- Minor bug fixes, silencing warnings (especially of clang), fixing
  typos in comments, coding style, ..., and also some fixes for memory
  leaks.


## CHANGES IN V2.0rc1 (11th April 2023)

- PPD file generator: Set default color mode when printer attrs say "auto"
  Actual printer default color mode did not get set and the often "Gray"
  gots set for color printers. Now we always choose the "best" mode.

      https://bugs.launchpad.net/bugs/2014976

- ppdLoadAttributes(): Find default page size also by dimensions
  Some PPDs can contain the same page size twice with different names,
  whereas the PWG cache created from the PPD contains each size only
  once. This could make the default size not being found when the
  PPD is converted to IPP attributes. Now we search also by size and
  not only by name.

      https://bugs.launchpad.net/bugs/2013131

- PPD generator for driverless printers: Add "*LandscapeOrientation:"
  according to the landscape-orientation-requested-preferred printer
  IPP attribute.

- ppdFilterLoadPPD(): Corrected PPD attribute name so that for printers
  which receive PWG Raster a sample Raster header gets created.

- Make the testppdfile utility getting built and installed by default

- Improved formatting of reports generated by ppdTest()


## CHANGES IN V2.0b4 (22nd February 2023)

- Transfer CUPS' `cupstestppd` utility to `ppdTest()` library function
  The valuable tool got forgotten in the first place, now it is available
  as `ppdTest()` library function and `testppdfile` command line utility.
  The command line utility only gets installed with `./configure`
  called with `--enable-testppdfile` argument.

- In auto-generated PPDs do not set RGB default on mono printers
  When a PPD for a driverless printer is generated by the
  `ppdCreatePPDFromIPP()` function and the get-printer-attributes IPP
  response gives "print-color-mode-default=auto" the PPD's default
  setting for "ColorModel" is always "RGB", even on monochrome
  printers, which makes printing fail on most devices.
  Now we ignore the "print-color-mode-default" if set to "auto".

  See https://github.com/OpenPrinting/cups/issues/614

- ppdLoadAttributes(): Added NULL check for missing PPD PageSize default
  Some PPDs, even "everywhere" PPDs generated by CUPS for a driverless
  IPP printer do not have a valid default value for the default page
  size, like "Unknown". Added a NULL check to avoid a crash by such
  PPD files.

- Coverity check done by Zdenek Dohnal for the inclusion of libppd
  in Fedora and Red Hat. Zdenek has fixed all the issues: Missing free(),
  potential string overflows, ... Thanks a lot!

- `testppd`: String got freed too early
  In this test program run by `make check` a string was used after
  already gotten freed. Discovered via a compiler warning, but program
  could have actually crashed.

- `configure.ac`: Change deprecated AC_PROG_LIBTOOL for LT_INIT (#8)


## CHANGES IN V2.0b3 (31st January 2023)

- COPYING, NOTICE: Simplification for autotools-generated files
  autotools-generated files can be included under the license of the
  upstream code, and FSF copyright added to upstream copyright
  list. Simplified COPYING appropriately.

- Makefile.am: Include LICENSE in distribution tarball


## CHANGES IN V2.0b2 (8th January 2023)

- PPD file generator for driverless printing with CUPS: Support more
  than 2 resolutions in Apple Raster/AirPrint. The `urf-supported` IPP
  attribute was only parsed correctly when its `RS` part had only 1 or
  2 and not more resolutions specified. This commit corrects now for
  an arbitrary amount of resolutions, taking the lowest for "draft",
  the highest for "high" and one in the middle for "normal" print
  quality (PR #3).

- Update cfCatalogLoad() calls for API change in libcupsfilters. In
  libcupsfilters we have added language/translation support to the
  `cfCatalog...()` API functions via
  OpenPrinting/libcupsfilters#2. This changes the `cfCatalogLoad()`
  calls in libppd (both in the PPD generator for driverless
  printing). This commit updates them. For a quick solution we supply
  NULL as language for now, resembling the old behavior. We look into
  language support in the PPD generator later.

- ppdFilterEmitJCL(): Added NULL check for PPD not being supplied.
  Classic CUPS filters created based on filter functions using
  ppdFilterCUPSWrapper() and also filter functions of libppd
  (ppdFilter...()) should also work without PPD file and not crash if
  no PPD file is supplied.

- Make build of "genstrings" optional "genstrings" is only a
  development tool for the PPD compiler, not a user tool, therefore we
  make its build optional. It can be built via "make genstrings" or the
  "./configure" option "--enable-genstrings".

- Makefile.am: Include NOTICE in distribution tarball

- configure.ac: Added "foreign" to to AM_INIT_AUTOMAKE() call. Makes
  automake not require a file named README.

- Cleaned up .gitignore

- Tons of fixes in the source code documentation: README.md, INSTALL,
  DEVELOPING.md, CONTRIBUTING.md, COPYING, NOTICE, ... Adapted to the
  libppd component, added links.


## CHANGES IN V2.0b1 (17th November 2022)

- Introduced the new libppd library overtaking all the PPD handling
  functions from libcups, CUPS' ppdc utility (PPD compiler using *.drv
  files), and the PPD file collection handling functionality from
  cups-driverd, as they are deprecated there and will get removed in
  CUPS 3.x. This form of conservation is mainly intended for
  converting classic printer drivers which use PPDs into Printer
  Applications without completely rewriting them.

- Added functions `ppdLoadAttributes()`, `ppdFilterLoadPPD()`,
  `ppdFilterLoadPPDFile()`, `ppdFilterFreePPD()`, and
  `ppdFilterFreePPDFile()` to convert all PPD file options and
  settings relevant for the filter functions in libcupsfilters into
  printer IPP attributes and options. Also create a named ("libppd")
  extension in the filter data structure, for filter functions
  explicitly supporting PPD files.

- Added `ppdFilterCUPSWrapper()` convenience function to easily create
  a classic CUPS filter from a filter function.

- Converted the `...tops` CUPS filters into the filter functions
  `ppdFilterPDFToPS()`, and `ppdFilterRasterToPS()`. As PostScript as
  print output format is as deprecated as PPD files and all PostScript
  printers use PPD files, we move PostScript output generation to
  libppd, too.

- Converted CUPS' `pstops` filter into the `ppdFilterPSToPS()` filter function.

- Introduced `ppdFilterImageToPS()` filter function for Printer
  Applications to print on PostScript printers. It is used to print
  images (IPP standard requires at least JPEG to be accepted) without
  need of a PDF renderer (`cfFilterImageToPDF()` ->
  `cfFilterPDFToPS()`) and without need to convert to Raster
  (`cfFilterImageToRaster()` -> `cfFilterRasterToPS()`).

- Created wrappers to add PPD-defined JCL code to jobs for native PDF
  printers, `ppdFilterImageToPDF()` and `ppdFilterPDFToPDF()` filter
  functions with the underlying `ppdFilterEmitJCL()` function.

- Created `ppdFilterUniversal()` wrapper function for
  `cfFilterUniversal()` to add PPD file support, especially for PPD
  files which define driver filters.

- `ppdFilterExternalCUPS()` wrapper function for `cfFilterExternal()`
  to add PPD file support.

- Auto-pre-fill the presets with most suitable options from the PPD
  files with new `ppdCacheAssignPresets()` function (Only if not
  filled via "`APPrinterPreset`" in the PPD). This way we can use the
  IPP options `print-color-mode`, `print-quality`, and
  `print-content-optimize` in a PPD/CUPS-driver-retro-fitting Printer
  Application with most PPDs.

- In `ppdCacheCreateWithPPD()` also support presets for
  `print-content-optimize`, not only for `print-color-mode` and
  `print-quality`.

- In the `ppdCacheGetPageSize()` function do not only check the fit of
  the margins with the page size matching the job's page size (like
  "A4") but also the margins of the variants (like "A4.Borderless"),
  even if the variant's size dimensions differ from the base size (and
  the physical paper size), for example for overspray in borderless
  printing mode (HPLIP does this). Also select a borderless variant
  only if the job requests borderless (all margins zero).

- Move the functions `ppdPwgUnppdizeName()`, `ppdPwgPpdizeName()`, and
  `ppdPwgPpdizeResolution()` into the public API.

- In the `ppdPwgUnppdizeName()` allow to supply NULL as the list of
  characters to replace by dashes. Then all non-alpha-numeric
  characters get replaced, to result in IPP-conforming keyword
  strings.

- In the `ppdPwgUnppdizeName()` function support negative numbers

- `ppdFilterPSToPS()`: Introduced new `input-page-ranges` attribute
  (Issue #365, Pull request #444, #445).

- Changed `ColorModel` option in the PPDs from the PPD generator to
  mirror the `print-color-mode` IPP attribute instead of providing all
  color space/depth combos for manual selection. Color space and depth
  are now auto-selected by the `urf-supported` and
  `pwg-raster-document-type-supported` printer IPP attributes and the
  settings of `print-color-mode` and `print-quality`. This is now
  implemented in the `cfFilterGhostscript()` filter function both for
  use of the auto-generated PPD file for driverless iPP printers and
  use without PPD, based on IPP attributes.  For this the new library
  functions `cupsRasterPrepareHeader()` to create a header for Raster
  output and `cupsRasterSetColorSpace()` to auto-select color space
  and depth were created.

- Clean-up of human-readable string handling in the PPD generator.

- Removed support for asymmetric image resolutions ("ppi=XXXxYYY") in
  `cfFilterImageToPS()` as CUPS does not support this (Issue #347,
  Pull request #361, OpenPrinting CUPS issue #115).

- Removed versioning.h and the macros defined in this file (Issue #334).

- Removed `ppdCreateFromIPPCUPS()`, we have a better one in
  libppd. Also removed corresponding test in `testppd`.

- Build system, README: Require CUPS 2.2.2+.

- Build system: Remove '-D_PPD_DEPRECATED=""' from the compiling
  command lines of the source files which use libcups. The flag is not
  supported any more for longer times already and all the PPD-related
  functions deprecated by CUPS have moved into libppd now.

- Added support for Sharp-proprietary `ARDuplex` PPD option name for
  double-sided printing.

- Build system: Add files in `.gitignore` that are generated by
  `autogen.sh`, `configure`, and `make` (Pull request #336).

- Fixed PPD memory leak caused by "emulators" field not freed
  (OpenPrinting CUPS issue #124).

- Make "True" in boolean options case-insensitive (OpenPrinting CUPS
  pull request #106).
