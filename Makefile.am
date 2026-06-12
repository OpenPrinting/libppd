ACLOCAL_AMFLAGS = -I m4

pkgconfdir = $(libdir)/pkgconfig
pkgconf_DATA = \
	libppd.pc

doc_DATA = \
	ABOUT-NLS \
	AUTHORS \
	COPYING \
	CHANGES.md \
	CHANGES-1.x.md \
	INSTALL.md \
	LICENSE \
	NOTICE \
	README.md \
	DEVELOPING.md \
	CONTRIBUTING.md

EXTRA_DIST = \
	$(doc_DATA) \
	autogen.sh \
	config.rpath \
	libppd.pc.in

EXTRA_DIST += \
	ppd/testdriver.c \
	ppd/test-internal.h

# =========
# Utilities
# =========

pkgutilsdir = $(bindir)
pkgutils_PROGRAMS =

# ==========================
# PPD legacy support library
# ==========================
pkgppdincludedir = $(includedir)/ppd
pkgppdinclude_DATA = \
	ppd/ppd.h \
	ppd/ppdc.h \
	ppd/ppd-filter.h

pkgppddefsdir = $(datadir)/ppdc
pkgppddefs_DATA = \
	ppd/epson.h \
	ppd/hp.h \
	ppd/label.h \
	ppd/font.defs \
	ppd/media.defs \
	ppd/raster.defs

lib_LTLIBRARIES = libppd.la

check_PROGRAMS = \
	testppd \
	test_ppd_localize \
	test_ppd_cache \
	test_ppd_ipp \
	test_ppd_mark \
	test_ppd_custom \
	test_ppd_attr \
	test_ppd_page \
	test_ppd_conflicts \
	test_ppd_emit \
	test_ppd_filter \
	test_ppd_generator \
	test_ppd_load_profile
TESTS = \
	testppd \
	test_ppd_localize \
	test_ppd_cache \
	test_ppd_ipp \
	test_ppd_mark \
	test_ppd_custom \
	test_ppd_attr \
	test_ppd_page \
	test_ppd_conflicts \
	test_ppd_emit \
	test_ppd_filter \
	test_ppd_generator \
	test_ppd_load_profile

libppd_la_SOURCES = \
	ppd/ppd-attr.c \
	ppd/ppd.c \
	ppd/ppd-cache.c \
	ppd/ppd-collection.cxx \
	ppd/ppd-conflicts.c \
	ppd/ppd-custom.c \
	ppd/ppd-emit.c \
	ppd/ppd-filter.c \
	ppd/ppd-generator.c \
	ppd/ppd-load-profile.c \
	ppd/ppd-localize.c \
	ppd/ppd-mark.c \
	ppd/ppd-page.c \
	ppd/ppd-ipp.c \
	ppd/ppd-test.c \
	ppd/array.c \
	ppd/array-private.h \
	ppd/debug.c \
	ppd/debug-internal.h \
	ppd/debug-private.h \
	ppd/encode.c \
	ppd/file.c \
	ppd/file-private.h \
	ppd/imagetops-pstops.c \
	ppd/ipp-private.h \
	ppd/libcups2.c \
	ppd/libcups2-private.h \
	ppd/pdftops.c \
	ppd/raster-interpret.c \
	ppd/raster-error.c \
	ppd/raster-private.h \
	ppd/rastertops.c \
	ppd/string.c \
	ppd/snprintf.c \
	ppd/string-private.h \
	ppd/thread.c \
	ppd/thread-private.h \
	ppd/ppdc-array.cxx \
	ppd/ppdc-attr.cxx \
	ppd/ppdc-catalog.cxx \
	ppd/ppdc-choice.cxx \
	ppd/ppdc-constraint.cxx \
	ppd/ppdc-driver.cxx \
	ppd/ppdc-file.cxx \
	ppd/ppdc-filter.cxx \
	ppd/ppdc-font.cxx \
	ppd/ppdc-group.cxx \
	ppd/ppdc-import.cxx \
	ppd/ppdc-mediasize.cxx \
	ppd/ppdc-message.cxx \
	ppd/ppdc-option.cxx \
	ppd/ppdc-private.h \
	ppd/ppdc-profile.cxx \
	ppd/ppdc-shared.cxx \
	ppd/ppdc-source.cxx \
	ppd/ppdc-string.cxx \
	ppd/ppdc-variable.cxx \
	$(pkgppdinclude_DATA) \
	$(pkgppddefs_DATA)
libppd_la_LIBADD = \
	$(LIBCUPSFILTERS_LIBS) \
	$(ZLIB_LIBS) \
	$(CUPS_LIBS)
libppd_la_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(ZLIB_CFLAGS) \
	$(CUPS_CFLAGS)
libppd_la_CXXFLAGS = \
	$(libppd_la_CFLAGS)
libppd_la_LDFLAGS = \
	-no-undefined \
	-version-info 2

testppd_SOURCES = ppd/testppd.c
testppd_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
testppd_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_localize_SOURCES = ppd/test_ppd_localize.c
test_ppd_localize_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_localize_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_cache_SOURCES = ppd/test_ppd_cache.c
test_ppd_cache_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_cache_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_ipp_SOURCES = ppd/test_ppd_ipp.c
test_ppd_ipp_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_ipp_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_mark_SOURCES = ppd/test_ppd_mark.c
test_ppd_mark_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_mark_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_custom_SOURCES = ppd/test_ppd_custom.c
test_ppd_custom_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_custom_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_attr_SOURCES = ppd/test_ppd_attr.c
test_ppd_attr_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_attr_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_page_SOURCES = ppd/test_ppd_page.c
test_ppd_page_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_page_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_conflicts_SOURCES = ppd/test_ppd_conflicts.c
test_ppd_conflicts_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_conflicts_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_emit_SOURCES = ppd/test_ppd_emit.c
test_ppd_emit_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
test_ppd_emit_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

test_ppd_filter_SOURCES = ppd/test_ppd_filter.c
test_ppd_filter_LDADD = \
	libppd.la \
	$(LIBCUPSFILTERS_LIBS) \
	$(CUPS_LIBS)
test_ppd_filter_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(CUPS_CFLAGS)

test_ppd_generator_SOURCES = ppd/test_ppd_generator.c
test_ppd_generator_LDADD = \
	libppd.la \
	$(LIBCUPSFILTERS_LIBS) \
	$(CUPS_LIBS)
test_ppd_generator_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(CUPS_CFLAGS)

test_ppd_load_profile_SOURCES = ppd/test_ppd_load_profile.c
test_ppd_load_profile_LDADD = \
	libppd.la \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS)
test_ppd_load_profile_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS)

EXTRA_DIST += \
	$(pkgppdinclude_DATA) \
	$(pkgppddefs_DATA) \
	ppd/test.ppd \
	ppd/test2.ppd \
	ppd/README.md

# ================
# PPD test utility
# ================

if ENABLE_TESTPPDFILE
pkgutils_PROGRAMS += \
	testppdfile

testppdfile_SOURCES = ppd/testppdfile.c
testppdfile_LDADD = \
	libppd.la \
	$(CUPS_LIBS) \
	$(LIBCUPSFILTERS_LIBS)
testppdfile_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS) \
	$(LIBCUPSFILTERS_CFLAGS)
endif

# ===========================
# ppdc PPD compiler utilities
# ===========================

if ENABLE_PPDC_UTILS
pkgutils_PROGRAMS += \
	ppdc \
	ppdhtml \
	ppdi \
	ppdmerge \
	ppdpo
endif
if ENABLE_GENSTRINGS
pkgutils_PROGRAMS += \
	genstrings
endif

genstrings_SOURCES = \
	ppd/genstrings.cxx
genstrings_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
genstrings_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdc_SOURCES = \
	ppd/ppdc.cxx
ppdc_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdc_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdhtml_SOURCES = \
	ppd/ppdhtml.cxx
ppdhtml_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdhtml_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdi_SOURCES = \
	ppd/ppdi.cxx
ppdi_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdi_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdmerge_SOURCES = \
	ppd/ppdmerge.cxx
ppdmerge_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdmerge_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

ppdpo_SOURCES = \
	ppd/ppdpo.cxx
ppdpo_LDADD = \
	libppd.la \
	$(CUPS_LIBS)
ppdpo_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS)

distclean-local:
	rm -rf *.cache *~

clean-local:
	rm -rf $(CIROOT)

SUBDIRS =

# ============================================================
# Staged-install downstream autopkgtests (DESTDIR staging)
# ============================================================
# Install the just-built tree into an ephemeral root ($(CIROOT)) with
# `make install DESTDIR=`, top up the bits that `make install` does not ship
# but the downstream Debian autopkgtests require, fix up the staged .pc, then
# run the unmodified downstream scripts against that tree.
CIROOT = $(abs_top_builddir)/_ciroot

EXTRA_DIST += \
	ci/autopkgtest/run.sh \
	ci/autopkgtest/debian-tests/control \
	ci/autopkgtest/debian-tests/libppd-2-dev \
	ci/autopkgtest/debian-tests/libppd-2-ppd-handling

# Fix A: check_PROGRAMS are build-only under Automake and are never copied by
# `make install`.  The downstream PPD-handling test executes the test driver
# as an *installed* binary (/usr/bin/testppd), so build it and stage it into
# $(CIROOT)$(bindir).  Uses libtool --mode=install so the binary is relinked
# cleanly against $(libdir) (LD_LIBRARY_PATH then forces the staged library).
STAGED_TEST_BINARIES = testppd

install-test-programs: $(STAGED_TEST_BINARIES)
	$(MKDIR_P) "$(CIROOT)$(bindir)"
	@for p in $(STAGED_TEST_BINARIES); do \
	  echo "  STAGE   $$p -> $(CIROOT)$(bindir)/$$p"; \
	  $(LIBTOOL) --mode=install $(INSTALL_PROGRAM) "$$p" "$(CIROOT)$(bindir)/$$p" || exit 1; \
	done

# Fix C (data seeding): the downstream PPD-handling test copies its PPDs from
# the installed $(datadir)/ppd/testppd directory, which upstream `make install`
# does not ship.  Seed every test PPD the current testppd binary opens
# (test.ppd, test2.ppd, sbo_test.ppd) so the unmodified script finds them.
STAGED_TEST_PPDS = \
	ppd/test.ppd \
	ppd/test2.ppd \
	ppd/sbo_test.ppd

install-test-data:
	$(MKDIR_P) "$(CIROOT)$(datadir)/ppd/testppd"
	@for f in $(STAGED_TEST_PPDS); do \
	  echo "  STAGE   $$f -> $(CIROOT)$(datadir)/ppd/testppd/"; \
	  $(INSTALL_DATA) "$(srcdir)/$$f" "$(CIROOT)$(datadir)/ppd/testppd/" || exit 1; \
	done

# Build everything, DESTDIR-install it, top up the test-only programs/data,
# then rewrite the staged libppd.pc prefix so a downstream consumer resolves
# THIS build (not a system copy) while the system dependency .pc files
# (libcupsfilters, cups) keep resolving normally.
stage-ciroot: all
	rm -rf "$(CIROOT)"
	$(MAKE) $(AM_MAKEFLAGS) install DESTDIR="$(CIROOT)"
	$(MAKE) $(AM_MAKEFLAGS) install-test-programs install-test-data
	@pc="$(CIROOT)$(libdir)/pkgconfig/libppd.pc"; \
	if [ -f "$$pc" ]; then \
	  sed -i.bak -E "s#^(prefix|exec_prefix|libdir|includedir)=(/.*)#\1=$(CIROOT)\2#" "$$pc"; \
	  rm -f "$$pc.bak"; \
	  echo "stage-ciroot: rewrote staged-tree paths in $$pc"; \
	fi

# Common environment for the wrapper.  PPDC_DATADIR / CUPS_DATADIR point ppdc
# at the staged *.defs so `ppdc test.drv` resolves <font.defs> / <media.defs>
# from the staging tree.  LIBPPD_TESTPPD / LIBPPD_PPDDIR point the PPD-handling
# test at the staged binary and data instead of the system /usr paths, so it
# needs no absolute-path redirection (no root, no proot) and runs identically
# on native and QEMU-emulated architectures.
# NB: libppd configures with a split prefix (prefix=/ but exec_prefix=/usr and
# everything installs under /usr), so the staging root the wrapper points at is
# $(CIROOT)$(exec_prefix), not $(CIROOT)$(prefix).
CIROOT_ENV = \
	CIROOT="$(CIROOT)" \
	CIPREFIX="$(exec_prefix)" \
	TOP_BUILDDIR="$(abs_top_builddir)" \
	PPDC_DATADIR="$(CIROOT)$(datadir)/ppdc" \
	CUPS_DATADIR="$(CIROOT)$(datadir)" \
	LIBPPD_TESTPPD="$(CIROOT)$(bindir)/testppd" \
	LIBPPD_PPDDIR="$(CIROOT)$(datadir)/ppd/testppd"

# Full downstream suite in a single wrapper invocation, so the run produces one
# aggregate PASS/FAIL summary across both tests.  Neither test needs privilege
# or path redirection, so this is identical on every architecture.
test-autopkgtest: stage-ciroot
	$(CIROOT_ENV) \
	    $(SHELL) $(srcdir)/ci/autopkgtest/run.sh libppd-2-dev libppd-2-ppd-handling

# Single-test convenience targets (handy for debugging one case in isolation).
test-autopkgtest-dev: stage-ciroot
	$(CIROOT_ENV) \
	    $(SHELL) $(srcdir)/ci/autopkgtest/run.sh libppd-2-dev

test-autopkgtest-ppd: stage-ciroot
	$(CIROOT_ENV) \
	    $(SHELL) $(srcdir)/ci/autopkgtest/run.sh libppd-2-ppd-handling

.PHONY: stage-ciroot install-test-programs install-test-data \
	test-autopkgtest test-autopkgtest-dev test-autopkgtest-ppd
