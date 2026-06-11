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
# Those are redirected into the staging tree, surgically and reversibly, via
# AUTOPKGTEST_BINDS (Fix C).  Two backends are supported, chosen automatically:
#
#   * mount --bind   - when running as root or with passwordless sudo
#                      (CI native runners).  Real kernel bind mounts: robust
#                      on every architecture, torn down on exit.
#   * proot          - fallback when unprivileged (local dev): no root, no
#                      host changes.  (proot is ptrace-based and unreliable
#                      under QEMU emulation and on some kernels, hence it is
#                      only the fallback.)
#
# Env in:
#   CIROOT             staging root        (default: $PWD/_ciroot)
#   CIPREFIX           configured prefix   (default: /usr)
#   TOP_BUILDDIR       build tree          (default: $PWD)
#   AUTOPKGTEST_BINDS  optional space-separated "host:guest" binds for scripts
#                      that read installed binaries/data via absolute paths.
#                      Only the named paths are overlaid; the rest of the host
#                      /usr (compiler, system libs) stays intact.
#   Any extra exported variables (e.g. PPDC_DATADIR, CUPS_DATADIR) are passed
#   straight through to the test scripts.
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

# ---------------------------------------------------------------------------
# Surgical /usr redirection (Fix C)
# ---------------------------------------------------------------------------
BIND_UNMOUNT=""
BIND_RMDIRS=""
cleanup_binds() {
    # Unmount in reverse and remove any placeholder targets we created.
    for m in $BIND_UNMOUNT; do $BIND_SUDO umount "$m" 2>/dev/null || true; done
    for d in $BIND_RMDIRS;  do $BIND_SUDO rm -rf "$d" 2>/dev/null || true; done
}

if [ -n "${AUTOPKGTEST_BINDS:-}" ] && [ -z "${_REDIR_DONE:-}" ]; then
    # Pick a privilege escalator for mount --bind, if available.
    BIND_SUDO=""
    have_priv=0
    if [ "$(id -u)" = 0 ]; then
        have_priv=1
    elif command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        BIND_SUDO="sudo"
        have_priv=1
    fi

    if [ "$have_priv" = 1 ]; then
        trap cleanup_binds EXIT INT TERM
        for pair in $AUTOPKGTEST_BINDS; do
            src=${pair%%:*}
            dst=${pair#*:}
            if [ -d "$src" ]; then
                if [ ! -e "$dst" ]; then $BIND_SUDO mkdir -p "$dst"; BIND_RMDIRS="$dst $BIND_RMDIRS"; fi
            else
                if [ ! -e "$dst" ]; then
                    $BIND_SUDO mkdir -p "$(dirname "$dst")"
                    $BIND_SUDO touch "$dst"
                    BIND_RMDIRS="$dst $BIND_RMDIRS"
                fi
            fi
            $BIND_SUDO mount --bind "$src" "$dst"
            BIND_UNMOUNT="$dst $BIND_UNMOUNT"
            echo "run.sh: bound $src -> $dst (mount --bind)"
        done
        _REDIR_DONE=1
    elif command -v proot >/dev/null 2>&1; then
        binds=""
        for pair in $AUTOPKGTEST_BINDS; do binds="$binds -b $pair"; done
        _REDIR_DONE=1; export _REDIR_DONE
        echo "run.sh: redirecting via proot (unprivileged fallback)"
        # shellcheck disable=SC2086
        exec proot $binds "$0" "$@"
    else
        echo "run.sh: AUTOPKGTEST_BINDS set but neither root/passwordless-sudo" >&2
        echo "run.sh: (for mount --bind) nor proot is available." >&2
        exit 1
    fi
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
