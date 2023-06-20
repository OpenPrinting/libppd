# OpenPrinting libppd v2.0rc2 - 2023-06-20

Looking for compile instructions? Read the file "INSTALL"
instead...


## INTRODUCTION

CUPS is a standards-based, open-source printing system used by Apple's
Mac OS® and other UNIX®-like operating systems, especially also
Linux. CUPS uses the Internet Printing Protocol ("IPP") and provides
System V and Berkeley command-line interfaces, a web interface, and a
C API to manage printers and print jobs.

CUPS 1.0 was released in early 2000 and since then and until CUPS 2.x
PPD (PostScript Printer Description) files were used to describe the
properties, features and user-settable options of printers. The
development of PPD files (and also PostScript) was stopped by Adobe
back in 1984, and Michael Sweet, author of CUPS, deprecated PPD files
already ~10 years ago, seeking for a more modern alternative.

Introducing the concept of Printer Applications (emulations of
driverless IPP printers, provides printer properties on
`get-printer-attributes` IPP request) on the OpenPrinting Summit/PWG
Meeting in May 2018, the replacement is there and PPD file support is
going away in CUPS 3.x, to be released end-2023. This also means that
libcups3 will not contain any PPD-file-supporting functions any more.

Currently, ~10000 printer models are supported with PPD-file-based
classic CUPS drivers. Many drivers are even coming from the pre-CUPS
era and got retro-fitted with Foomatic and its PPD file generator.

And these drivers are a huge code base and most of these old printers
are not easily accessible for testing, converting these drivers into
native Printer Applications by rewriting their code would be an
unbearable and error-prone burden. Therefore a minimum-invasive
retro-fitting method is needed, which simply encapsulates the drivers
and PPD files as they are and therefore we still need to be able to
handle PPD files (and also *.drv PPD generator files).

To avoid re-writing support for an obsolete format from scratch, only
for retro-fitting legacy printer drivers we have created this package,
libppd, the legacy support library for PPD files, which is by 95 %
code overtaken from CUPS 2.x:

- All PPD file support functions of libcups (see `ppd/ppd.h`)
- All functions and utilities of CUPS' PPD compiler (see `ppd/ppdc.h`.
  code taken from the `ppdc/` directory of CUPS 2.x source code)
- Most functions of `cups-driverd` of CUPS 2.x (see `ppd/ppd.h`, for
  handling collections of PPD files in a driver package/Printer
  Application).
- Some new code got added for PPD file support in filter functions
  (of licupsfilters) and wrapping filter functions into filter
  executables for CUPS 2.x (see `ppd/ppd-filter.h`).

Currently, libppd is made use of for the following applications:

- Legacy filter executables for CUPS 2.x, the cups-filters package
  (version 2.x or newer). It allows updating to
  cups-filters/libcupsfilters 2.x while still using CUPS 2.x.
- CUPS-driver-retro-fitting Printer Applications based on PAPPL and
  pappl-retrofit. The currently 4 retro-fitting Printer Applications
  in the Snap Store contain **all** the drivers which are available as
  Debian package, for the above-mentioned ~10000 printer models!
- cups-browsed. Unfortunately, there are still many print dialogs
  around which do not support printers for which CUPS auto-generates a
  temporary print queue. Therefore cups-browsed still has to create
  permanent print queues for PPD-supporting CUPS 2.x and for this deal
  with PPD files. Conversion to a Printer Application without any PPD
  file handling is planned.

**NOTE: LIBPPD IS ONLY FOR LEGACY PPD FILE SUPPORT! IT SHOULD NOT BE A
MOTIVATION TO CREATE NEW PPD FILES OR NEW PPD EXTENSIONS!**

As libppd is only for legacy PPD file support we do not plan to add
any new features to it. Bug fixes will happen whenever needed though.

For compiling and using this package libcups of CUPS 2.2.2 or newer
and libcupsfilters, version 2.0.0 or newer is needed. Of libcups no
PPD-supporting functions are used, so porting libppd to use libcups3
should be rather easy (or it already works, not tested yet).
Report bugs to

    https://github.com/OpenPrinting/libppd/issues

See the "COPYING", "LICENCE", and "NOTICE" files for legal
information. The license is the same as for CUPS, for a maximum of
compatibility.

## LINKS

### Classic CUPS

* [How it all began](https://openprinting.github.io/history/)
* [Classic CUPS drivers](https://openprinting.github.io/achievements/#all-free-drivers-to-be-used-with-cups)

### The New Architecture of Printing and Scanning

* [The New Architecture - What is it?](https://openprinting.github.io/current/#the-new-architecture-for-printing-and-scanning)
* [Ubuntu Desktop Team Indaba on YouTube](https://www.youtube.com/watch?v=P22DOu_ahBo)

### Printer Applications

* [All free drivers in a PPD-less world - OR - All free drivers in Snaps](https://openprinting.github.io/achievements/#all-free-drivers-in-a-ppd-less-world---or---all-free-drivers-in-snaps)
* [Current activity on Printer Applications](https://openprinting.github.io/current/#printer-applications)
* [PostScript Printer Application](https://github.com/OpenPrinting/ps-printer-app) ([Snap Store](https://snapcraft.io/ps-printer-app)): Printer Application Snap for PostScript printers which are supported by the manufacturer's PPD files. User can add PPD files if the needed one is not included or outdated.
* [Ghostscript Printer Application](https://github.com/OpenPrinting/ghostscript-printer-app) ([Snap Store](https://snapcraft.io/ghostscript-printer-app)): Printer Application with Ghostscript and many other drivers, for practically all Linux-supported printers which are not PostScript and not supported by HPLIP or Gutenprint.
* [HPLIP Printer Application](https://github.com/OpenPrinting/hplip-printer-app) ([Snap Store](https://snapcraft.io/hplip-printer-app)): HPLIP in a Printer Application Snap. Supports nearly every HP printer ever made. Installing HP's proprietary plugin (needed for a few printers) into the Snap is supported and easily done with the web interface.
* [Gutenprint Printer Application](https://github.com/OpenPrinting/gutenprint-printer-app) ([Snap Store](https://snapcraft.io/gutenprint-printer-app)): High quality output and a lot of knobs to adjust, especially for Epson and Canon inkjets but also for many other printers, in a Printer Application Snap.
* [Legacy Printer Application](https://github.com/OpenPrinting/pappl-retrofit#legacy-printer-application) (not available as Snap): It is a part of the [pappl-retrofit](https://github.com/OpenPrinting/pappl-retrofit) package and it makes drivers classically installed for the system's classically installed CUPS available in a Printer Application and this way for the CUPS Snap. It is especially helpful for drivers which are not (yet) available as Printer Application.
* [PAPPL](https://github.com/michaelrsweet/pappl/): Base infrastructure for all the Printer Applications linked above.
* [PAPPL CUPS driver retro-fit library](https://github.com/OpenPrinting/pappl-retrofit): Retro-fit layer to integrate CUPS drivers consisting of PPD files, CUPS filters, and CUPS backends into Printer Applications.
* [Printer Applications 2020 (PDF)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/printer-applications-may-2020.pdf)
* [Printer Applications 2021 (PDF)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/printer-applications-may-2021.pdf)
* [CUPS 2018 (PDF, pages 28-29)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-plenary-may-18.pdf)
* [CUPS 2019 (PDF, pages 30-35)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-plenary-april-19.pdf)
* [cups-filters 2018 (PDF, page 11)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-filters-ippusbxd-2018.pdf)
* [cups-filters 2019 (PDF, pages 16-17)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-filters-ippusbxd-2019.pdf)
* [cups-filters 2020 (PDF)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-filters-ippusbxd-2020.pdf)
* [cups-filters 2021 (PDF)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-filters-cups-snap-ipp-usb-and-more-2021.pdf)
* [cups-filters 2022 (PDF)](https://ftp.pwg.org/pub/pwg/liaison/openprinting/presentations/cups-filters-cups-snap-ipp-usb-and-more-2022.pdf)

