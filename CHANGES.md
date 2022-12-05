# CHANGES - libppd v2.0b1 - 2022-11-17

## CHANGES IN V2.0b2 (TBA)

- ppdFilterEmitJCL(): Added NULL check for PPD not being supplied.
  Classic CUPS filters created based on filter functions using
  ppdFilterCUPSWrapper() and also filter functions of libppd
  (ppdFilter...()) should also work without PPD file and not crash if
  no PPD file is supplied.

- configure.ac: Added "foreign" to to AM_INIT_AUTOMAKE() call. Makes
  automake not require a file named README.

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
