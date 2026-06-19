#!/bin/sh
# ci/ci-setup.sh
#
# CI helper for building libppd against several CUPS releases on both native
# and QEMU-emulated runners.  The same source compiles against CUPS 2.4.x,
# 2.5.x and 3.x; this script provides each of those CUPS builds (and a matching
# libcupsfilters) and then builds and tests libppd against it.
#
# libppd depends on BOTH libcups and libcupsfilters, so for the source-CUPS
# legs the distro libcupsfilters-dev (built against CUPS 2.4) is the wrong ABI;
# this script builds libcupsfilters from source against the active CUPS instead.
#
# Subcommands:
#   deps                 install build dependencies
#   cups <kind>          provide libcups; <kind> is one of:
#                          system-2x    distro libcups2-dev  (CUPS 2.4.x)
#                          source-2.5.x OpenPrinting/cups@master    (CUPS 2.5.x)
#                          source-3.x   OpenPrinting/libcups@master (libcups3)
#   pdfio                build/install pdfio (required by libcupsfilters)
#   libcupsfilters <kind> provide libcupsfilters matching the active CUPS:
#                          system-2x    distro libcupsfilters-dev
#                          source-*     OpenPrinting/libcupsfilters@master
#   build-libppd         autogen + configure + make + make check
#
# Environment knobs honoured by build-libppd:
#   CUPS_KIND   the <kind> above (controls test XFAILs for source CUPS)
#   EMULATED    "1" when running under QEMU emulation (controls test XFAILs)
#
# Override knobs (optional):
#   LIBCUPSFILTERS_URL  git URL for the source libcupsfilters build
#   LIBCUPSFILTERS_REF  git ref for the source libcupsfilters build
#
# The script runs as root inside emulation containers and via sudo on native
# runners; it detects which automatically.
set -eu

PDFIO_VER=1.6.4
LIBCUPSFILTERS_URL="${LIBCUPSFILTERS_URL:-https://github.com/OpenPrinting/libcupsfilters.git}"
LIBCUPSFILTERS_REF="${LIBCUPSFILTERS_REF:-master}"

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

# Make apt completely non-interactive.  Native GitHub runners ship needrestart,
# whose service-restart prompt otherwise hangs the job forever; the emulated
# containers do not have it, which is why only the native legs stalled.
export DEBIAN_FRONTEND=noninteractive
export NEEDRESTART_MODE=a
export NEEDRESTART_SUSPEND=1

# Source-built CUPS / libcupsfilters install their .pc files under
# $prefix/lib[/<multiarch>]/pkgconfig; make sure pkg-config (and therefore
# libppd's configure) can find them.
ma=$(gcc -dumpmachine 2>/dev/null || echo "")
PKG_CONFIG_PATH="/usr/lib/pkgconfig${ma:+:/usr/lib/$ma/pkgconfig}:/usr/local/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PKG_CONFIG_PATH

apt_install() {
	$SUDO apt-get update --fix-missing -y
	$SUDO apt-get install -y "$@"
}

cmd_deps() {
	# Union of libppd's own build deps and the deps needed to build
	# libcupsfilters and pdfio from source on the source-CUPS legs.
	apt_install \
		build-essential autoconf automake libtool libtool-bin pkg-config \
		gettext autopoint autotools-dev cmake git wget tar make gcc g++ \
		file dbus \
		libavahi-client-dev libssl-dev libpam-dev libusb-1.0-0-dev \
		zlib1g-dev libqpdf-dev libexif-dev liblcms2-dev libfontconfig1-dev \
		libfreetype6-dev libcairo2-dev libjpeg-dev libpng-dev libtiff-dev \
		libjxl-dev libpoppler-dev libpoppler-cpp-dev libdbus-1-dev \
		libopenjp2-7-dev mupdf-tools poppler-utils ghostscript
	# Never let pre-shipped libppd / libcupsfilters shadow the builds under test.
	$SUDO apt-get remove -y libppd-dev libcupsfilters-dev || true
}

# build_autoconf <url> <ref> <submodule-flag> [configure-args...]
build_autoconf() {
	url="$1"; ref="$2"; sub="$3"; shift 3
	echo "ci-setup: building $url @ $ref"
	src="$(mktemp -d)"
	git clone --depth 1 --branch "$ref" $sub "$url" "$src"
	( cd "$src"
	  [ -x ./configure ] || ./autogen.sh
	  ./configure --prefix=/usr "$@" || ./configure --prefix=/usr
	  make -j"$(nproc)"
	  $SUDO make install )
	$SUDO ldconfig || true
}

cmd_cups() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libcups2-dev
			;;
		source-2.5.x)
			# CUPS 2.5 (OpenPrinting/cups master) ships cups.pc and has
			# dropped cups-config; libppd's configure now detects it via
			# pkg-config, so no cups-config shim is needed.
			#
			# Force the multiarch libdir: CUPS's configure otherwise installs
			# libcups into /usr/lib64 on 64-bit hosts, which is not on the
			# default linker search path.  A downstream consumer that links
			# only "-lppd" must be able to find libcups transitively at link
			# time - which only works if libcups sits in a default search dir.
			build_autoconf https://github.com/OpenPrinting/cups.git master "" \
				--disable-systemd ${ma:+--libdir=/usr/lib/$ma}
			;;
		source-3.x)
			build_autoconf https://github.com/OpenPrinting/libcups.git master \
				"--recurse-submodules"
			;;
		*)
			echo "ci-setup: unknown cups kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_pdfio() {
	echo "ci-setup: building pdfio $PDFIO_VER"
	src="$(mktemp -d)"
	( cd "$src"
	  wget -q "https://github.com/michaelrsweet/pdfio/releases/download/v$PDFIO_VER/pdfio-$PDFIO_VER.tar.gz"
	  tar -xzf "pdfio-$PDFIO_VER.tar.gz"
	  cd "pdfio-$PDFIO_VER"
	  ./configure --prefix=/usr --enable-shared
	  make all
	  $SUDO make install )
	$SUDO ldconfig || true
}

cmd_libcupsfilters() {
	kind="$1"
	case "$kind" in
		system-2x)
			apt_install libcupsfilters-dev
			;;
		source-*)
			# Build libcupsfilters against the CUPS already installed above.
			# Its configure auto-detects CUPS via pkg-config (cups3 / cups /
			# cups-config), so the same source matches every CUPS release.
			build_autoconf "$LIBCUPSFILTERS_URL" "$LIBCUPSFILTERS_REF" ""
			;;
		*)
			echo "ci-setup: unknown libcupsfilters kind: $kind" >&2; exit 2 ;;
	esac
}

cmd_build() {
	./autogen.sh
	# --enable-ppdc-utils: the downstream libppd-2-dev autopkgtest needs the
	# staged `ppdc` to compile its test.drv into a PPD.
	./configure --enable-ppdc-utils
	make -j"$(nproc)" V=1

	# Report which CUPS the configure step actually selected.
	echo "ci-setup: configured against:"
	grep -E "libcups:|cups-config:" config.log 2>/dev/null || true

	# Hook for tests that depend on environment quirks of a source-installed
	# or emulated CUPS.  Empty for now; add space-separated test names here if
	# a leg surfaces an environment-only failure.
	xfail=""

	if [ -n "$xfail" ]; then
		make check V=1 VERBOSE=1 XFAIL_TESTS="$xfail" \
			|| { test -f test-suite.log && cat test-suite.log; exit 1; }
	else
		make check V=1 VERBOSE=1 \
			|| { test -f test-suite.log && cat test-suite.log; exit 1; }
	fi
}

case "${1:-}" in
	deps)                 cmd_deps ;;
	cups)                 shift; cmd_cups "$@" ;;
	pdfio)                cmd_pdfio ;;
	libcupsfilters)       shift; cmd_libcupsfilters "$@" ;;
	build-libppd)         cmd_build ;;
	*)
		echo "usage: ci-setup.sh {deps | cups <kind> | pdfio | libcupsfilters <kind> | build-libppd}" >&2
		exit 2 ;;
esac
