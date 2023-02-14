# Process this file with autoconf to create configure.

AC_PREREQ([2.65])

# ====================
# Version informations
# ====================
AC_INIT([libppd], [2.0b3], [https://github.com/OpenPrinting/libppd/issues], [libppd], [https://github.com/OpenPrinting/libppd/])
libppd_version="AC_PACKAGE_VERSION"
libppd_version_major="`echo AC_PACKAGE_VERSION | awk -F. '{print $1}'`"
libppd_version_major="`echo AC_PACKAGE_VERSION | awk -F. '{printf("%d\n",$2);}'`"

# =============
# Automake init
# =============
AC_CONFIG_MACRO_DIR([m4])
m4_include([m4/ac_define_dir.m4])
m4_include([m4/ax_compare_version.m4])
m4_include([m4/basic-directories.m4])
AM_INIT_AUTOMAKE([1.11 gnu dist-xz dist-bzip2 subdir-objects foreign])
AM_SILENT_RULES([yes])
AC_LANG([C++])
AC_CONFIG_HEADERS([config.h])
# Extra defines for the config.h
AH_BOTTOM([
#ifdef HAVE_LONG_LONG
#  define CUPS_LLFMT   "%lld"
#  define CUPS_LLCAST  (long long)
#else
#  define CUPS_LLFMT   "%ld"
#  define CUPS_LLCAST  (long)
#endif /* HAVE_LONG_LONG */

#ifdef HAVE_ARC4RANDOM
#  define CUPS_RAND() arc4random()
#  define CUPS_SRAND(v) arc4random_stir()
#elif defined(HAVE_RANDOM)
#  define CUPS_RAND() random()
#  define CUPS_SRAND(v) srandom(v)
#elif defined(HAVE_LRAND48)
#  define CUPS_RAND() lrand48()
#  define CUPS_SRAND(v) srand48(v)
#else
#  define CUPS_RAND() rand()
#  define CUPS_SRAND(v) srand(v)
#endif /* HAVE_ARC4RANDOM */
])

# ===========================
# Find required base packages
# ===========================
AC_PROG_CC
AC_PROG_CXX
AX_CXX_COMPILE_STDCXX([11],[noext],[mandatory])
AM_PROG_CC_C_O
AM_ICONV
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET
LT_INIT
PKG_PROG_PKG_CONFIG([0.20])

AM_GNU_GETTEXT_VERSION([0.18.3])
AM_GNU_GETTEXT([external])

# ================================
# Find CUPS internals (no pc file)
# ================================
AC_ARG_WITH([cups-config],
	[AS_HELP_STRING([--with-cups-config=path], [Specify path to cups-config executable.])],
	[with_cups_config="$withval"],
	[with_cups_config=system]
)

AS_IF([test "x$with_cups_config" != "xsystem"], [
	CUPSCONFIG=$with_cups_config
], [
	AC_PATH_TOOL(CUPSCONFIG, [cups-config])
	AS_IF([test -z "$CUPSCONFIG"], [
		AC_MSG_ERROR([Required cups-config is missing. Please install CUPS developer packages.])
	])
])
CUPS_CFLAGS=`$CUPSCONFIG --cflags`
CUPS_LIBS=`$CUPSCONFIG --image --libs`
CUPS_VERSION=`$CUPSCONFIG --version`
AC_SUBST(CUPS_CFLAGS)
AC_SUBST(CUPS_LIBS)

CUPS_DATADIR="`$CUPSCONFIG --datadir`"
AC_DEFINE_UNQUOTED(CUPS_DATADIR, "$CUPS_DATADIR", [CUPS datadir])
AC_SUBST(CUPS_DATADIR)

CUPS_SERVERROOT="`$CUPSCONFIG --serverroot`"
AC_DEFINE_UNQUOTED(CUPS_SERVERROOT, "$CUPS_SERVERROOT", [CUPS serverroot])
AC_SUBST(CUPS_SERVERROOT)

CUPS_FONTPATH="$CUPS_DATADIR/fonts"
AC_DEFINE_UNQUOTED(CUPS_FONTPATH, "$CUPS_FONTPATH", [Path to CUPS fonts dir])
AC_SUBST(CUPS_FONTPATH)

CUPS_SERVERBIN="`$CUPSCONFIG --serverbin`"
AC_DEFINE_UNQUOTED(CUPS_SERVERBIN, "$CUPS_SERVERBIN", [Path to CUPS binaries dir])
AC_SUBST(CUPS_SERVERBIN)

# Transient run-time state dir of CUPS
CUPS_STATEDIR=""
AC_ARG_WITH(cups-rundir, [  --with-cups-rundir           set transient run-time state directory of CUPS],CUPS_STATEDIR="$withval",[
        case "$uname" in
                Darwin*)
                        # Darwin (OS X)
                        CUPS_STATEDIR="$CUPS_SERVERROOT"
                        ;;
                *)
                        # All others
                        CUPS_STATEDIR="$localstatedir/run/cups"
                        ;;
        esac])
AC_DEFINE_UNQUOTED(CUPS_STATEDIR, "$CUPS_STATEDIR", [Transient run-time state dir of CUPS])
AC_SUBST(CUPS_STATEDIR)

# ========================
# Check for libcupsfilters
# ========================
PKG_CHECK_MODULES([LIBCUPSFILTERS], [libcupsfilters])

# =======================================================
# Check for whether we want to install the ppdc utilities
# =======================================================
AC_ARG_ENABLE([ppdc-utils], [AS_HELP_STRING([--enable-ppdc-utils], [enable ppdc utilities, to build PPD files from driver information files (*.drv).])],
        [enable_ppdc_utils="$enableval"],
        [enable_ppdc_utils="no"])
AM_CONDITIONAL([ENABLE_PPDC_UTILS],
[test "x$enable_ppdc_utils" != "xno"])

PPDC_DATADIR="$datadir/ppdc"
AC_DEFINE_UNQUOTED(PPDC_DATADIR, "$PPDC_DATADIR", [ppdc include dir])
AC_SUBST(PPDC_DATADIR)

# ===============================================
# Check for whether we want to install genstrings
# ===============================================
AC_ARG_ENABLE([genstrings], [AS_HELP_STRING([--enable-genstrings], [enable genstrings, GNU gettext message generator for the libppd PPD Compiler.])],
        [enable_genstrings="$enableval"],
        [enable_genstrings="no"])
AM_CONDITIONAL([ENABLE_GENSTRINGS],
[test "x$enable_genstrings" != "xno"])

# ======================
# Check system functions
# ======================
AC_CHECK_FUNCS(sigaction)
AC_CHECK_FUNCS(waitpid wait3)
AC_CHECK_FUNCS(strtoll)
AC_CHECK_FUNCS(open_memstream)
AC_CHECK_FUNCS(getline,[],AC_SUBST([GETLINE],['bannertopdf-getline.$(OBJEXT)']))
AC_CHECK_FUNCS(strcasestr,[],AC_SUBST([STRCASESTR],['pdftops-strcasestr.$(OBJEXT)']))
AC_SEARCH_LIBS(pow, m)
dnl Checks for string functions.
AC_CHECK_FUNCS(strdup strlcat strlcpy)
if test "$host_os_name" = "hp-ux" -a "$host_os_version" = "1020"; then
        echo Forcing snprintf emulation for HP-UX.
else
        AC_CHECK_FUNCS(snprintf vsnprintf)
fi

# ========================
# Check for system headers
# ========================
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([sys/stat.h])
AC_CHECK_HEADERS([sys/types.h])
AC_CHECK_HEADERS([unistd.h])
AC_CHECK_HEADERS([zlib.h])
AC_CHECK_HEADERS([endian.h])
AC_CHECK_HEADERS([dirent.h])
AC_CHECK_HEADERS([sys/ioctl.h])
AC_CHECK_HEADER(string.h,AC_DEFINE(HAVE_STRING_H))
AC_CHECK_HEADER(strings.h,AC_DEFINE(HAVE_STRINGS_H))

# ===================================
# Check for large files and long long
# ===================================
AC_SYS_LARGEFILE
LARGEFILE=""
AS_IF([test x"$enable_largefile" != "xno"], [
	LARGEFILE="-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE"
	AS_IF([test x"$ac_cv_sys_large_files" = "x1"], [LARGEFILE="$LARGEFILE -D_LARGE_FILES"])
	AS_IF([test x"$ac_cv_sys_file_offset_bits" = "x64"], [LARGEFILE="$LARGEFILE -D_FILE_OFFSET_BITS=64"])
])
AC_SUBST(LARGEFILE)

AC_CHECK_TYPE(long long, [long_long_found=yes], [long_long_found=no])
AS_IF([test x"$long_long_found" = "xyes"], [
	AC_DEFINE([HAVE_LONG_LONG], [], [Platform supports long long type])
])

# =====================================================
# Check for Ghostscript and PDF-to-PostScript converter
# =====================================================
AC_ARG_ENABLE([ghostscript],
	[AS_HELP_STRING([--disable-ghostscript], [Disable filters using Ghostscript.])],
	[enable_ghostscript="$enableval"],
	[enable_ghostscript=yes]
)
AC_ARG_ENABLE([pdftops],
	[AS_HELP_STRING([--disable-pdftops], [Disable filters using Poppler's pdftops.])],
	[enable_pdftops="$enableval"],
	[enable_pdftops=yes]
)
AC_ARG_WITH([pdftops],
	[AS_HELP_STRING([--with-pdftops=value], [Set which pdftops to use (gs,pdftops,pdftocairo,acroread,mupdf,hybrid).])],
	[with_pdftops="$withval"],
	[with_pdftops=hybrid]
)
AS_CASE([x$with_pdftops],
        [xgs|xpdftops|xpdftocairo|xacroread|xmupdf|xhybrid], [],
        [AC_MSG_ERROR([Unknown value of with-pdftops provided: $with_pdftops])]
)
AC_ARG_WITH([gs-path],
	[AS_HELP_STRING([--with-gs-path=value], [Set path to ghostcript binary (default: system).])],
	[with_gs_path="$withval"],
	[with_gs_path=system]
)
AC_ARG_WITH([pdftops-path],
        [AS_HELP_STRING([--with-pdftops-path=value], [Set path to pdftops/ghostscript binary (default: system).])],
        [with_pdftops_path="$withval"],
        [with_pdftops_path=system]
)
AC_ARG_WITH([pdftocairo-path],
        [AS_HELP_STRING([--with-pdftocairo-path=value], [Set path to pdftocairo binary (default: system).])],
        [with_pdftocairo_path="$withval"],
        [with_pdftocairo_path=system]
)
AC_ARG_WITH([acroread-path],
        [AS_HELP_STRING([--with-acroread-path=value], [Set path to acroread binary (default: system).])],
        [with_acroread_path="$withval"],
        [with_acroread_path=system]
)
AC_ARG_WITH([ippfind-path],
        [AS_HELP_STRING([--with-ippfind-path=value], [Set path to ippfind binary (default: system).])],
        [with_ippfind_path="$withval"],
        [with_ippfind_path=system]
)
AC_ARG_WITH([pdftops-maxres],
	[AS_HELP_STRING([--with-pdftops-maxres=value], [Set maximum image rendering resolution for pdftops filter (0, 75, 150, 300, 600, 1200, 2400, 4800, 90, 180, 360, 720, 1440, 2880, 5760, unlimited). Default: 1440])],
	[with_pdftops_maxres="$withval"],
	[with_pdftops_maxres=1440]
)
AS_CASE([x$with_pdftops_maxres],
	[x0|x75|x150|x300|x600|x1200|x2400|x4800|x90|x180|x360|x720|x1440|x2880|x5760], [CUPS_PDFTOPS_MAXRES=$with_pdftops_maxres],
	[xunlimited], [CUPS_PDFTOPS_MAXRES=0],
	[AC_MSG_ERROR([Unknown value of with-pdftops-maxres provided: $with_pdftops])]
)
AC_ARG_ENABLE([gs-ps2write],
	[AS_HELP_STRING([--disable-gs-ps2write], [Ghostscript doesn't support ps2write device.])],
	[enable_gs_ps2write="$enableval"],
	[enable_gs_ps2write=yes]
)

CUPS_PDFTOPS_RENDERER=""

CUPS_GHOSTSCRIPT=""
AS_IF([test "x$enable_ghostscript" != "xyes"], [
       with_gs_path=""
], [
	AS_IF([test "x$with_gs_path" != "xsystem"], [
		CUPS_GHOSTSCRIPT="$with_gs_path"
	], [
		AS_IF([test "x$cross_compiling" = "xyes"], [
			CUPS_GHOSTSCRIPT="gs"
		], [
			AC_CHECK_PROG(CUPS_GHOSTSCRIPT, gs, gs)
		])
	])
	AS_IF([test "x$CUPS_GHOSTSCRIPT" = "x"], [
		AC_MSG_ERROR([Required gs binary is missing. Please install ghostscript package.])
	])
	AC_DEFINE([HAVE_GHOSTSCRIPT], [], [Define that we provide ghostscript binary])
	AS_IF([test x"$with_pdftops" = xgs -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=GS])

	AS_IF([test "x$cross_compiling" != "xyes"], [
		AC_MSG_CHECKING(whether gs supports the ps2write device)
		AS_IF([`$CUPS_GHOSTSCRIPT -h 2>&1 | grep -q ps2write`], [
			AC_MSG_RESULT([yes])
		], [
			AC_MSG_RESULT([no])
			enable_gs_ps2write=no
		])
	])
	AS_IF([test x"$enable_gs_ps2write" = "xyes"], [
		AC_DEFINE([HAVE_GHOSTSCRIPT_PS2WRITE], [], [gs supports ps2write])
	])
])
AM_CONDITIONAL(ENABLE_GHOSTSCRIPT, test "x$enable_ghostscript" = xyes)
AC_SUBST(CUPS_GHOSTSCRIPT)

AS_IF([test "x$enable_pdftops" != "xyes"], [
       with_pdftops_path=""
], [
	AS_IF([test "x$with_pdftops_path" != "xsystem"], [
		CUPS_PDFTOPS="$with_pdftops_path"
	], [
		AS_IF([test "x$cross_compiling" = "xyes"], [
			CUPS_PDFTOPS="/usr/bin/pdftops"
		], [
			AC_CHECK_PROG(CUPS_PDFTOPS, pdftops, /usr/bin/pdftops)
		])
		AS_IF([test "x$CUPS_PDFTOPS" = "x"], [
			AC_MSG_ERROR([Required pdftops is missing. Please install the pdftops utility of Poppler.])
		])
	])
	AS_IF([test "x$CUPS_PDFTOPS" != "x"], [
		AC_DEFINE([HAVE_POPPLER_PDFTOPS], [], [Define that we provide poppler pdftops.])
		AS_IF([test x"$with_pdftops" = xpdftops -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=PDFTOPS])

		AS_IF([test "x$cross_compiling" != "xyes"], [
			AC_MSG_CHECKING([whether pdftops supports -origpagesizes])
			AS_IF([`$CUPS_PDFTOPS -h 2>&1 | grep -q -- -origpagesizes`], [
				AC_MSG_RESULT([yes])
				AC_DEFINE([HAVE_POPPLER_PDFTOPS_WITH_ORIGPAGESIZES], [] , [pdftops supports -origpagesizes.])
			], [
				AC_MSG_RESULT([no])
			])
			AC_MSG_CHECKING([whether pdftops supports -r])
			AS_IF([`$CUPS_PDFTOPS -h 2>&1 | grep -q -- '-r '`], [
				AC_MSG_RESULT([yes])
				AC_DEFINE([HAVE_POPPLER_PDFTOPS_WITH_RESOLUTION], [] , [pdftops supports -r argument.])
			], [
				AC_MSG_RESULT([no])
			])
		])
	])
])

# ================
# Check for mutool
# ================
AC_ARG_ENABLE([mutool],
	[AS_HELP_STRING([--disable-mutool], [Disable filters using mutool.])],
	[enable_mutool="$enableval"],
	[enable_mutool=yes]
)

AC_ARG_WITH([mutool-path],
	[AS_HELP_STRING([--with-mutool-path=value], [Set path to mutool binary (default: system).])],
	[with_mutool_path="$withval"],
	[with_mutool_path=system]
)

CUPS_MUTOOL=""
AS_IF([test "x$enable_mutool" != "xyes"], [
	with_mutool_path=""
], [
	AS_IF([test "x$with_mutool_path" != "xsystem"], [
		CUPS_MUTOOL="$with_mutool_path"
	], [
		AS_IF([test "x$cross_compiling" = "xyes"], [
			CUPS_MUTOOL="mutool"
		], [
			AC_CHECK_PROG(CUPS_MUTOOL, mutool, mutool)
		])
	])
	AS_IF([test "x$CUPS_MUTOOL" = "x"], [
		AC_MSG_ERROR([Required mutool binary is missing. Please install mutool.])
	])
	AC_DEFINE([HAVE_MUTOOL], [], [Define that we provide mutool binary])
	AS_IF([test x"$with_pdftops" = xmupdf -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=MUPDF])
])
AM_CONDITIONAL(ENABLE_MUTOOL, test "x$enable_mutool" = xyes)
AC_SUBST(CUPS_MUTOOL)

AC_ARG_ENABLE([pdftocairo],
	[AS_HELP_STRING([--disable-pdftocairo], [Disable filters using pdftocairo.])],
	[enable_pdftocairo="$enableval"],
	[enable_pdftocairo=yes]
)

# ====================
# Check for pdftocairo
# ====================
CUPS_PDFTOCAIRO=""
AS_IF([test "x$enable_pdftocairo" != "xyes"], [
	with_pdftocairo_path=""
], [
	AS_IF([test "x$with_pdftocairo_path" != "xsystem"], [
		CUPS_PDFTOCAIRO="$with_pdftocairo_path"
	], [
		CUPS_PDFTOCAIRO="pdftocairo"
	])
	AS_IF([test "x$CUPS_PDFTOCAIRO" != "x"], [
		AC_DEFINE([HAVE_PDFTOCAIRO], [], [Define that we provide pdftocairo binary])
		AS_IF([test x"$with_pdftops" = xpdftocairo -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=PDFTOCAIRO])
	], [
		AC_MSG_ERROR([Required pdftocairo is missing. Please install Poppler developer packages.])
	])
])

# ==================
# Check for acroread
# ==================
AC_ARG_ENABLE([acroread],
	[AS_HELP_STRING([--disable-acroread], [Disable filters using acroread.])],
	[enable_acroread="$enableval"],
	[enable_acroread=yes]
)

CUPS_ACROREAD=""
AS_IF([test "x$enable_acroread" != "xyes"], [
	with_acroread_path=""
], [
	AS_IF([test "x$with_acroread_path" != "xsystem"], [
		CUPS_ACROREAD="$with_acroread_path"
	], [
		CUPS_ACROREAD="acroread"
	])
	AS_IF([test "x$CUPS_ACROREAD" != "x"], [
		AC_DEFINE([HAVE_ACROREAD], [], [Define that we provide acroread binary])
		AS_IF([test x"$with_pdftops" = xacroread -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=ACROREAD])
	])
])

# =============================================================
# Check for hybrid PDF-to-PostScript (Ghostscript plus Poppler)
# =============================================================
AS_IF([test "x$CUPS_GHOSTSCRIPT" != "x" -a "x$CUPS_PDFTOPS" != "x"], [
	AS_IF([test x"$with_pdftops" = xhybrid -o x"$CUPS_PDFTOPS_RENDERER" = x], [CUPS_PDFTOPS_RENDERER=HYBRID])
])


AC_DEFINE_UNQUOTED([CUPS_GHOSTSCRIPT], "$CUPS_GHOSTSCRIPT", [gs binary to use])
AC_DEFINE_UNQUOTED([CUPS_MUTOOL],"$CUPS_MUTOOL",[mutool binary to use])
AC_DEFINE_UNQUOTED([CUPS_POPPLER_PDFTOPS], "$CUPS_PDFTOPS", [pdftops binary to use.])
AC_DEFINE_UNQUOTED([CUPS_POPPLER_PDFTOCAIRO], "$CUPS_PDFTOCAIRO", [pdftocairo binary to use.])
AC_DEFINE_UNQUOTED([CUPS_ACROREAD], "$CUPS_ACROREAD", [acroread binary to use.])
AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_MAX_RESOLUTION], [$CUPS_PDFTOPS_MAXRES], [max resolution used for pdftops when converting images])
AC_DEFINE_UNQUOTED([CUPS_PDFTOPS_RENDERER], [$CUPS_PDFTOPS_RENDERER], [Define default renderer])

# ================
# Check for CFLAGS
# ================
AC_ARG_ENABLE([werror],
	[AS_HELP_STRING([--enable-werror], [Treat all warnings as errors, useful for development.])],
	[enable_werror="$enableval"],
	[enable_werror=no]
)
AS_IF([test x"$enable_werror" = "xyes"], [
	CFLAGS="$CFLAGS -Werror"
])
AS_IF([test x"$GCC" = "xyes"], [
	# Be tough with warnings and produce less careless code
	CFLAGS="$CFLAGS -Wall -std=gnu11"
	CXXFLAGS="$CXXFLAGS -Wall " # -Weffc++" # TODO: enable when it does not print 1MB of warnings
])
CFLAGS="$CFLAGS -D_GNU_SOURCE"
CXXFLAGS="$CXXFLAGS -D_GNU_SOURCE"

# ============================
# Debugging options for libppd
# ============================
AC_ARG_ENABLE(debug_guards, [  --enable-debug-guards   build libppd with memory allocat
ion guards])
AC_ARG_ENABLE(debug_printfs, [  --enable-debug-printfs  build libppd with CUPS_DEBUG_LOG support])

dnl Debug printfs can slow things down, so provide a separate option for that
if test x$enable_debug_printfs = xyes; then
        CFLAGS="$CFLAGS -DDEBUG"
        CXXFLAGS="$CXXFLAGS -DDEBUG"
fi

dnl Debug guards use an extra 4 bytes for some structures like strings in the
dnl string pool, so provide a separate option for that
if test x$enable_debug_guards = xyes; then
        CFLAGS="$CFLAGS -DDEBUG_GUARDS"
        CXXFLAGS="$CXXFLAGS -DDEBUG_GUARDS"
fi

# =====================
# Prepare all .in files
# =====================
AC_CONFIG_FILES([
	libppd.pc
	Makefile
])
AC_OUTPUT

# ==============================================
# Display final informations about configuration
# ==============================================
AC_MSG_NOTICE([
==============================================================================
Environment settings:
	CFLAGS:                                    ${CFLAGS}
	CXXFLAGS:                                  ${CXXFLAGS}
	LDFLAGS:                                   ${LDFLAGS}
Build configuration:
	cups-config:     ${with_cups_config}
	ghostscript:     ${enable_ghostscript}
	gs-path:         ${with_gs_path}
	gs ps2write:     ${enable_gs_ps2write}
	mutool:          ${enable_mutool}
	mutool-path:     ${with_mutool_path}
	pdftocairo:      ${enable_pdftocairo}
	pdftocairo-path: ${with_pdftocairo_path}
	acroread:        ${enable_acroread}
	acroread-path:   ${with_acroread_path}
	pdftops:         ${CUPS_PDFTOPS_RENDERER}
	Poppler's pdftops: ${enable_pdftops}
	pdftops-path:    ${with_pdftops_path}
	ppdc utilities:  ${enable_ppdc_utils}
	genstrings:      ${enable_genstrings}
	werror:          ${enable_werror}
==============================================================================
])
