#!/bin/sh
# ci/autopkgtest/run.sh
#
# Universal DESTDIR-staging wrapper for the downstream Debian autopkgtests.
# Points PATH / LD_LIBRARY_PATH / PKG_CONFIG_PATH at the staged install tree
# ($CIROOT) produced by `make stage-ciroot`, then runs the unmodified
# downstream scripts vendored under ci/autopkgtest/debian-tests/.
#
# For libppd the PPD-handling test invokes the installed test binary and test
# data through *absolute* paths (/usr/bin/testppd, /usr/share/ppd/testppd).
# Those are redirected into the staging tree, without root and without
# touching the host /usr, via proot bind mounts requested through
# AUTOPKGTEST_BINDS (Fix C).
#
# Env in:
#   CIROOT             staging root        (default: $PWD/_ciroot)
#   CIPREFIX           configured prefix   (default: /usr)
#   TOP_BUILDDIR       build tree          (default: $PWD)
#   AUTOPKGTEST_BINDS  optional space-separated "host:guest" proot binds for
#                      scripts that read installed binaries/data via absolute
#                      paths.  Each pair is surgically overlaid so the rest of
#                      the host /usr (compiler, system libs) stays intact.
#   Any extra exported variables (e.g. PPDC_DATADIR, CUPS_DATADIR) are passed
#   straight through to the test scripts and survive the proot re-exec.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TESTS_DIR="$SCRIPT_DIR/debian-tests"

: "${CIROOT:=$PWD/_ciroot}"
: "${CIPREFIX:=/usr}"
: "${TOP_BUILDDIR:=$PWD}"

if [ ! -d "$CIROOT" ]; then
    echo "run.sh: staging root not found: $CIROOT (run 'make stage-ciroot' first)" >&2
    exit 1
fi

ROOT="$CIROOT$CIPREFIX"
MULTIARCH=$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null \
            || gcc -dumpmachine 2>/dev/null || echo "")

PATH="$ROOT/bin:$ROOT/sbin:$TOP_BUILDDIR:$TOP_BUILDDIR/.libs:$PATH"
LD_LIBRARY_PATH="$ROOT/lib${MULTIARCH:+:$ROOT/lib/$MULTIARCH}:$TOP_BUILDDIR/.libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
PKG_CONFIG_PATH="$ROOT/lib/pkgconfig${MULTIARCH:+:$ROOT/lib/$MULTIARCH/pkgconfig}:$ROOT/share/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export PATH LD_LIBRARY_PATH PKG_CONFIG_PATH

# Surgical /usr redirection (Fix C): re-exec the whole run under proot with the
# requested binds so unmodified scripts that hardcode /usr/... paths resolve
# into the staging tree.  Only the named paths are overlaid; everything else
# (gcc/g++, system libcupsfilters, the dynamic loader) keeps resolving from the
# real host filesystem.
if [ -n "${AUTOPKGTEST_BINDS:-}" ] && [ -z "${_UNDER_PROOT:-}" ]; then
    if command -v proot >/dev/null 2>&1; then
        binds=""
        for pair in $AUTOPKGTEST_BINDS; do binds="$binds -b $pair"; done
        _UNDER_PROOT=1; export _UNDER_PROOT
        # shellcheck disable=SC2086
        exec proot $binds "$0" "$@"
    fi
    echo "run.sh: AUTOPKGTEST_BINDS set but 'proot' is not installed" >&2
    echo "run.sh: install proot, or run the proot-free target (test-autopkgtest-dev)" >&2
    exit 1
fi

if [ "$#" -eq 0 ]; then
    echo "run.sh: usage: run.sh <test-name> [test-name...]" >&2
    exit 2
fi

rc=0
for name in "$@"; do
    script="$TESTS_DIR/$name"
    if [ ! -f "$script" ]; then
        echo "run.sh: no such test: $script" >&2
        rc=1
        continue
    fi
    chmod +x "$script" 2>/dev/null || true
    workdir=$(mktemp -d)
    echo "=== autopkgtest: $name (CIROOT=$CIROOT, prefix=$CIPREFIX) ==="
    if ( cd "$workdir" && "$script" ); then
        echo "=== PASS: $name ==="
    else
        rc=$?
        echo "=== FAIL: $name (exit $rc) ===" >&2
        rc=1
    fi
    rm -rf "$workdir"
done
exit $rc
