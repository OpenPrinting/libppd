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
	INSTALL \
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
	ppd/testdriver.c

# =========
# Utilities
# =========

pkgutilsdir = $(bindir)
pkgutils_PROGRAMS = testppdfile

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
	testppd
TESTS = \
	testppd

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
	ppd/language.c \
	ppd/language-private.h \
	ppd/pdftops.c \
	ppd/raster-interpret.c \
	ppd/raster-error.c \
	ppd/raster-private.h \
	ppd/rastertops.c \
	ppd/string.c \
	ppd/snprintf.c \
	ppd/string-private.h \
	ppd/testppdfile.c \
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
	$(CUPS_LIBS)
libppd_la_CFLAGS = \
	$(LIBCUPSFILTERS_CFLAGS) \
	$(CUPS_CFLAGS)
libppd_la_LDFLAGS = \
	-no-undefined \
	-version-info 2

testppd_SOURCES = ppd/testppd.c
testppd_LDADD = libppd.la \
		-lcups

testppdfile_SOURCES = ppd/testppdfile.c
testppdfile_LDADD = libppd.la \
		$(CUPS_LIBS) \
		$(CUPS-FILTERS_LIBS)
testppdfile_CFLAGS = \
	-I$(srcdir)/ppd/ \
	$(CUPS_CFLAGS) \
	$(CUPS-FILTERS_CFLAGS)

EXTRA_DIST += \
	$(pkgppdinclude_DATA) \
	$(pkgppddefs_DATA) \
	ppd/test.ppd \
	ppd/test2.ppd \
	ppd/README.md

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

SUBDIRS =
