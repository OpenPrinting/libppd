# OpenPrinting libppd v2.1.0 Installation Guide


## Overview

This guide provides instructions for compiling and installing OpenPrinting libppd from source code. For more details, refer to "README.md" and for a change log, see "CHANGES.md".

### Before You Begin

#### Prerequisites
- ANSI-compliant C and C++ compilers (tested with several versions of GCC)
- make program and POSIX-compliant shell (/bin/sh)
- GNU make recommended (especially for BSD users)

#### Dependencies
- libcups of CUPS 2.2.2 or newer
- libcupsfilters 2.0.0 or newer

### Compiling the GIT Repository Code
- libppd GIT repository does not include a pre-built configure script.
- Run GNU autoconf (2.65 or higher) to create it:
  ```
  ./autogen.sh
  ```

### Configuration
- Use the "configure" script in the main directory:
  ```
  ./configure
  ```
- Default installation path is "/usr".
- For custom installation path, use the "--prefix" option:
  ```
  ./configure --prefix=/some/directory
  ```
- Use `./configure --help` for all configuration options.
- Set environment variables for libraries in non-default locations.

### Building the Software
- Run `make` (or `gmake` for BSD systems) to build the software.

### Installing the Software
- After building, install the software with:
  ```
  make install
  ```
- For BSD systems:
  ```
  gmake install
  ```

### Packaging for Operating System Distributions
- libppd is for retro-fitting legacy CUPS drivers using PPD files.
- Needed if Printer Applications are installed as classic packages (RPM, DEB).
- Not required for Printer Application Snaps or for modern IPP printers only.

## Installing Required Tools

### For Debian/Ubuntu-based Systems
- Install C and C++ compilers, make, autoconf:
  ```
  sudo apt-get install build-essential autoconf
  ```
- Install libcups and libcupsfilters

### For Red Hat/Fedora-based Systems
- Install C and C++ compilers, make, autoconf:
  ```
  sudo dnf install gcc gcc-c++ make autoconf
  ```
- Install libcups and libcupsfilters:

Note: The above commands are for common Linux distributions. For other operating systems or distributions, refer to respective package management instructions.
